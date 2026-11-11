// Behavior layer: orders from commands and what each unit wants to do with them each tick.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::apply(const SimCommand& command)
{
    ++commands_applied_;
    if ((command.kind == SimCommandKind::move || command.kind == SimCommandKind::formation_move) && !finite(command.target))
    {
        ++stale_references_;
        return;
    }
    if ((command.kind == SimCommandKind::attack_move || command.kind == SimCommandKind::guard) && !finite(command.target))
    {
        ++stale_references_;
        return;
    }
    if (command.kind == SimCommandKind::formation_move)
    {
        apply_formation_move(command);
        return;
    }
    if (command.kind == SimCommandKind::attack_move || command.kind == SimCommandKind::guard)
    {
        apply_standing_order(command);
        return;
    }
    if (command.kind == SimCommandKind::spawn)
    {
        for (const auto& entity : command.spawns)
        {
            if (!spawn(entity))
            {
                ++rejected_spawns_;
            }
        }
        return;
    }
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        if (command.kind == SimCommandKind::move || command.kind == SimCommandKind::stop || command.kind == SimCommandKind::attack)
        {
            command_serials_[*dense] = command.serial;
            standing_orders_[*dense].kind = StandingOrder::none;
            standing_orders_[*dense].engaging = false;
        }
        switch (command.kind)
        {
        case SimCommandKind::formation_move:
        case SimCommandKind::spawn:
        case SimCommandKind::attack_move:
        case SimCommandKind::guard:
            break;
        case SimCommandKind::set_invulnerable:
            combat_[*dense].invulnerable = command.flag;
            break;
        case SimCommandKind::set_cannot_be_killed:
            combat_[*dense].cannot_be_killed = command.flag;
            break;
        case SimCommandKind::suspend_locomotor:
            locomotor_suspended_[*dense] = command.flag ? 1U : 0U;
            break;
        case SimCommandKind::damage:
            // Damage only entities that can die (structures without health stay); removal is the
            // combat layer's, as for shots.
            if (combat_[*dense].profile.health > 0.0F && std::isfinite(command.amount))
            {
                (void)wound(*dense, command.amount);
            }
            break;
        case SimCommandKind::teleport:
            if (!finite(command.target) || (command.heading && !std::isfinite(*command.heading)))
            {
                ++stale_references_;
                break;
            }
            teleport(*dense, command.target, command.heading);
            break;
        case SimCommandKind::move:
            combat_[*dense].target = {};
            combat_[*dense].ordered = false;
            speed_limits_[*dense] = 0.0F;
            group_ids_[*dense] = 0;
            if (max_speeds_[*dense] > 0.0F)
            {
                plan_move(*dense, command.target);
            }
            break;
        case SimCommandKind::stop:
            speed_limits_[*dense] = 0.0F;
            group_ids_[*dense] = 0;
            move_targets_[*dense].reset();
            paths_[*dense].clear();
            combat_[*dense].target = {};
            combat_[*dense].ordered = false;
            break;
        case SimCommandKind::attack:
        {
            const auto victim = dense_index(command.target_entity);
            auto& combat = combat_[*dense];
            if (!victim || !damageable(*victim))
            {
                ++stale_references_;
                break;
            }
            if (!combat.profile.weapons.empty() && *victim != *dense && hostility_.hostile(owners_[*dense], owners_[*victim]))
            {
                combat.target = command.target_entity;
                combat.ordered = true;
                speed_limits_[*dense] = 0.0F;
                group_ids_[*dense] = 0;
                combat.chase_timer = 0;
            }
            break;
        }
        case SimCommandKind::destroy:
            destroy(*dense);
            break;
        case SimCommandKind::set_stance:
            if (command.stance != Stance::aggressive && command.stance != Stance::hold_position && command.stance != Stance::hold_fire)
            {
                ++stale_references_;
                break;
            }
            stances_[*dense] = command.stance;
            if (command.stance == Stance::hold_fire && !combat_[*dense].ordered)
            {
                combat_[*dense].target = {};
            }
            break;
        }
    }
}

// Each tick, before movement: every unit turns its order, stance, and target into what it asks of
// navigation (a move request) or of its locomotor (a steering intent). Combat later picks targets
// and fires; it never moves a unit.
void SimulationWorld::run_behavior()
{
    const auto dt = static_cast<float>(tick_seconds_);
    // Standing orders scan with the spatial grid; built here only in ticks where one scans (the
    // grid of the last tick's combat names entities by dense indices that spawns and removals since
    // then have shifted).
    const bool scans = std::ranges::any_of(standing_orders_,
                                           [](const StandingOrderState& order)
                                           {
                                               return order.kind != StandingOrder::none && !order.engaging && order.scan_timer == 0;
                                           });
    if (scans)
    {
        grid_.build(positions_, grid_cell_size);
    }
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        intents_[index] = {};
        run_standing_order(index);
        if (min_speeds_[index] > 0.0F)
        {
            plan_flight(index, dt);
        }
        else
        {
            if (combat_[index].ordered)
            {
                pursue_ordered_target(index);
            }
            // Standing body-aimers turn to face their target (ordered or picked on their own).
            if (aims_with_body_[index] != 0 && !move_targets_[index])
            {
                if (const auto victim = attack_victim(index))
                {
                    intents_[index].point = positions_[*victim];
                }
            }
        }
    }
}

void SimulationWorld::apply_standing_order(const SimCommand& command)
{
    const bool attack_move = command.kind == SimCommandKind::attack_move;
    // An attack-move to a point moves the group as one formation; each member then keeps its own
    // slot as destination. To an object, or a guard, each entity heads for the object or point.
    if (attack_move && !command.target_entity.valid())
    {
        SimCommand move = command;
        move.kind = SimCommandKind::formation_move;
        apply_formation_move(move);
    }
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        auto& order = standing_orders_[*dense];
        auto& combat = combat_[*dense];
        command_serials_[*dense] = command.serial;
        combat.target = {};
        combat.ordered = false;
        order.kind = attack_move ? StandingOrder::attack_move : StandingOrder::guard;
        order.entity = command.target_entity;
        order.point = command.target;
        order.engaging = false;
        if (attack_move && !command.target_entity.valid())
        {
            // The member's formation slot, where the formation move sent it (immobile members keep
            // the ordered point).
            if (move_targets_[*dense])
            {
                order.point = *move_targets_[*dense];
            }
            continue;
        }
        speed_limits_[*dense] = 0.0F;
        group_ids_[*dense] = 0;
        if (const auto anchor = command.target_entity.valid() ? dense_index(command.target_entity) : std::optional<std::size_t>(); anchor)
        {
            order.point = positions_[*anchor];
        }
        if (max_speeds_[*dense] > 0.0F)
        {
            plan_move(*dense, order.point);
        }
    }
}

void SimulationWorld::teleport(const std::size_t index, const Vector3& point, const std::optional<float> heading)
{
    // A placed obstacle's footprint moves with it.
    stamp_footprint(index, false);
    auto position = point;
    if (follows_ground_[index] != 0 && ground_.valid())
    {
        position.z = ground_.surface_height(position.x, position.y);
    }
    if (auto& footprint = footprints_[index])
    {
        // The footprint keeps its place relative to the entity: its offset turns with the heading.
        const float turn = heading ? *heading - headings_[index] : 0.0F;
        const float offset_x = footprint->center.x - positions_[index].x;
        const float offset_y = footprint->center.y - positions_[index].y;
        footprint->center.x = position.x + offset_x * std::cos(turn) - offset_y * std::sin(turn);
        footprint->center.y = position.y + offset_x * std::sin(turn) + offset_y * std::cos(turn);
        footprint->heading += turn;
    }
    positions_[index] = position;
    if (heading)
    {
        headings_[index] = *heading;
    }
    move_targets_[index].reset();
    paths_[index].clear();
    path_pending_[index] = 0;
    speed_limits_[index] = 0.0F;
    group_ids_[index] = 0;
    loiter_points_[index].reset();
    stamp_footprint(index, true);
}

void SimulationWorld::disengage(const std::size_t index)
{
    combat_[index].target = {};
    combat_[index].ordered = false;
    standing_orders_[index].engaging = false;
}

// Attack-move and guard (engine semantics; the original's are unmeasured). Both scan for hostiles
// every retarget_interval ticks (staggered by entity index) and engage the nearest one as an
// ordered attack, which the pursuit chases; they break off when the victim is gone or the chase
// leaves its leash, then head for the destination (attack-move) or back to the anchor (guard). An
// attack-move ends once it arrives with no engagement; a guard ends when its object dies.
void SimulationWorld::run_standing_order(const std::size_t index)
{
    auto& order = standing_orders_[index];
    if (order.kind == StandingOrder::none)
    {
        return;
    }
    if (order.entity.valid())
    {
        if (const auto followed = dense_index(order.entity))
        {
            order.point = positions_[*followed];
        }
        else
        {
            // The destination or anchor is gone: the order ends, a fight it started included.
            if (order.engaging)
            {
                disengage(index);
            }
            order.kind = StandingOrder::none;
            return;
        }
    }
    auto& combat = combat_[index];
    const float range = combat.profile.range();
    const bool guard = order.kind == StandingOrder::guard;
    const float guard_radius = std::max(range, minimum_guard_radius);
    const bool mobile = max_speeds_[index] > 0.0F;
    if (order.engaging)
    {
        const auto victim = combat.ordered ? attack_victim(index) : std::nullopt;
        const auto& leash_center = guard ? order.point : order.engage_origin;
        const float leash = guard && combat.profile.guard_chase_range > 0.0F ? combat.profile.guard_chase_range : std::max(range * leash_range_factor, guard ? guard_radius : 0.0F);
        if (victim && distance_xy(positions_[*victim], leash_center) <= leash)
        {
            return;
        }
        disengage(index);
        // Resume: back on the way, or back to the anchor when out of the guard's reach.
        if (mobile && (!guard || distance_xy(positions_[index], order.point) > guard_radius * 0.5F))
        {
            plan_move(index, order.point);
        }
        return;
    }
    if (!combat.profile.weapons.empty())
    {
        if (order.scan_timer > 0)
        {
            --order.scan_timer;
        }
        else
        {
            order.scan_timer = retarget_interval;
            const auto& center = guard ? order.point : positions_[index];
            const float response = combat.profile.attack_move_response_range;
            const float reach = guard ? guard_radius : (response > 0.0F ? response : range * acquire_range_factor);
            const auto best = choose_target(index, center, reach,
                                            [&](const std::size_t candidate)
                                            {
                                                return engages(index, candidate);
                                            });
            if (best)
            {
                combat.target = ids_[*best];
                combat.ordered = true;
                combat.chase_timer = 0;
                speed_limits_[index] = 0.0F;
                group_ids_[index] = 0;
                order.engaging = true;
                order.engage_origin = positions_[index];
                return;
            }
        }
    }
    if (!mobile)
    {
        return;
    }
    if (guard)
    {
        // Back toward the anchor once out of its reach; a moving anchor is followed.
        if (distance_xy(positions_[index], order.point) > guard_radius * 0.5F && (!move_targets_[index] || distance_xy(*move_targets_[index], order.point) > follow_tolerance) && order.scan_timer == 0)
        {
            plan_move(index, order.point);
        }
        return;
    }
    if (order.entity.valid() && (!move_targets_[index] || distance_xy(*move_targets_[index], order.point) > follow_tolerance) && order.scan_timer == 0)
    {
        plan_move(index, order.point);
        return;
    }
    // Arrived with nothing to fight (at the point, or within reach of the object): the attack-move
    // is done.
    if (!move_targets_[index] && path_pending_[index] == 0 && (!order.entity.valid() || distance_xy(positions_[index], order.point) <= std::max(follow_tolerance, range)))
    {
        order.kind = StandingOrder::none;
    }
}

std::optional<std::size_t> SimulationWorld::attack_victim(const std::size_t index) const noexcept
{
    const auto& combat = combat_[index];
    if (!combat.target.valid())
    {
        return std::nullopt;
    }
    const auto victim = dense_index(combat.target);
    if (!victim || !damageable(*victim) || !hostility_.hostile(owners_[index], owners_[*victim]))
    {
        return std::nullopt;
    }
    return victim;
}

// Chase an ordered target until it is in range, then hold position (combat fires). The chase
// replans every chase_interval ticks while the target moves.
void SimulationWorld::pursue_ordered_target(const std::size_t index)
{
    auto& combat = combat_[index];
    const auto victim = attack_victim(index);
    if (!victim)
    {
        // Combat drops the order this tick.
        return;
    }
    if (distance_xy(positions_[index], positions_[*victim]) > combat.profile.range())
    {
        if (max_speeds_[index] > 0.0F && (combat.chase_timer == 0 || !move_targets_[index]))
        {
            // Flyers keep their altitude; ground units take theirs from the surface anyway.
            plan_move(index, {positions_[*victim].x, positions_[*victim].y, follows_ground_[index] != 0 ? positions_[*victim].z : positions_[index].z});
            combat.chase_timer = chase_interval;
        }
    }
    else if (move_targets_[index])
    {
        move_targets_[index].reset();
        paths_[index].clear();
    }
    if (combat.chase_timer > 0)
    {
        --combat.chase_timer;
    }
}

// Flyers never stop (their locomotor keeps them at their minimum speed), so behavior only says
// where to steer. Observed in the original (docs/compatibility-research.md, "Fighter flight"):
// fighters fly in one plane most of the time, dogfight above and below it against other flyers,
// and reverse direction through a vertical half-loop. Here:
// - against a flyer (any class with a minimum speed: fighters, bombers, mod units), a dogfight:
//   steer at the target in 3D, weaving above and below it, within a band around the plane;
// - against anything else, attack runs in the plane: inside a third of its weapon range of the
//   target a flyer holds its heading, passes, and turns back once outside;
// - without a target, to the destination (arriving within a few ticks of flight of it), then
//   circling where it arrived or idles, in the plane;
// - a goal far behind starts a half-loop, which the locomotor finishes.
// An aggressive flyer engages any target it has; otherwise only an ordered one. The numbers are
// engine choices.
void SimulationWorld::plan_flight(const std::size_t index, const float dt)
{
    const auto& position = positions_[index];
    auto& intent = intents_[index];
    if (looping_[index] != 0)
    {
        intent.loop = true;
        return;
    }
    const auto& combat = combat_[index];
    const auto plane = cruise_heights_[index];
    const auto choose = [&]
    {
        if (combat.ordered || stances_[index] == Stance::aggressive)
        {
            if (const auto victim = attack_victim(index))
            {
                const auto& there = positions_[*victim];
                if (min_speeds_[*victim] > 0.0F)
                {
                    // Each flyer weaves on its own phase (from its slot), so a fight spreads vertically.
                    const auto seconds = static_cast<float>(static_cast<double>(tick_) * tick_seconds_);
                    const auto phase = static_cast<float>(ids_[index].index % 16U) * (std::numbers::pi_v<float> / 8.0F);
                    const auto weave = dogfight_weave * std::sin(seconds * (2.0F * std::numbers::pi_v<float> / dogfight_weave_seconds) + phase);
                    intent.point = Vector3{there.x, there.y, std::clamp(there.z + weave, plane - dogfight_band, plane + dogfight_band)};
                    return;
                }
                const auto pass = std::max(combat.profile.range() / 3.0F, (radii_[index] + radii_[*victim]) * 2.0F);
                intent.point = Vector3{there.x, there.y, plane};
                intent.hold_heading = distance_xy(position, there) < pass;
                return;
            }
        }
        auto& target = move_targets_[index];
        if (target)
        {
            intent.point = Vector3{target->x, target->y, plane};
            if (distance_xy(position, *target) <= travel_speed(index) * dt * 3.0F)
            {
                loiter_points_[index] = *target;
                target.reset();
                speed_limits_[index] = 0.0F;
                group_ids_[index] = 0;
            }
            return;
        }
        if (!loiter_points_[index])
        {
            loiter_points_[index] = position;
        }
        intent.point = Vector3{loiter_points_[index]->x, loiter_points_[index]->y, plane};
    };
    choose();
    if (intent.hold_heading || !intent.point || turn_rates_[index] <= 0.0F)
    {
        return;
    }
    const auto dx = intent.point->x - position.x;
    const auto dy = intent.point->y - position.y;
    const auto turn_circle = travel_speed(index) / turn_rates_[index];
    if (std::hypot(dx, dy) > 4.0F * turn_circle && std::fabs(wrap_angle(std::atan2(dy, dx) - headings_[index])) > flight_reversal_angle)
    {
        intent.loop = true;
    }
}

} // namespace eaw
