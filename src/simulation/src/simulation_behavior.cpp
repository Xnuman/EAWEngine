// Behavior layer: orders from commands and what each unit wants to do with them each tick.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::apply(const SimCommand& command)
{
    ++commands_applied_;
    if ((command.kind == SimCommandKind::move || command.kind == SimCommandKind::formation_move) && !is_finite(command.target))
    {
        ++stale_references_;
        return;
    }
    if ((command.kind == SimCommandKind::attack_move || command.kind == SimCommandKind::guard) && !is_finite(command.target))
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
            orders_.command_serials[*dense] = command.serial;
            orders_.standing[*dense].kind = StandingOrder::none;
            orders_.standing[*dense].engaging = false;
        }
        switch (command.kind)
        {
        case SimCommandKind::formation_move:
        case SimCommandKind::spawn:
        case SimCommandKind::attack_move:
        case SimCommandKind::guard:
            break;
        case SimCommandKind::set_invulnerable:
            combat_.states[*dense].invulnerable = command.flag;
            break;
        case SimCommandKind::set_cannot_be_killed:
            combat_.states[*dense].cannot_be_killed = command.flag;
            break;
        case SimCommandKind::suspend_locomotor:
            locomotor_.suspended[*dense] = command.flag ? 1U : 0U;
            break;
        case SimCommandKind::damage:
            // Damage only entities that can die (structures without health stay); removal is the
            // combat layer's, as for shots.
            if (combat_.states[*dense].profile.health > 0.0F && std::isfinite(command.amount))
            {
                (void)wound(combat_.states[*dense], command.amount);
            }
            break;
        case SimCommandKind::teleport:
            if (!is_finite(command.target) || (command.heading && !std::isfinite(*command.heading)))
            {
                ++stale_references_;
                break;
            }
            teleport(*dense, command.target, command.heading);
            break;
        case SimCommandKind::move:
            combat_.states[*dense].target = {};
            combat_.states[*dense].ordered = false;
            locomotor_.speed_limits[*dense] = 0.0F;
            group_.ids[*dense] = 0;
            if (locomotor_.max_speeds[*dense] > 0.0F)
            {
                plan_move(*dense, command.target);
            }
            break;
        case SimCommandKind::stop:
            locomotor_.speed_limits[*dense] = 0.0F;
            group_.ids[*dense] = 0;
            route_.move_targets[*dense].reset();
            route_.paths[*dense].clear();
            combat_.states[*dense].target = {};
            combat_.states[*dense].ordered = false;
            // A ground unit that brakes (MovementProfile::deceleration) coasts to a stop straight ahead
            // over its braking distance instead of halting at once (a walk played as clips walks on to
            // its stride's next part and plays its stop there: walk_straight).
            if (const auto speed = locomotor_.drive_speeds[*dense]; ground_mover_.follows_ground[*dense] != 0 && locomotor_.movements[*dense].deceleration > 0.0F &&
                                                                    locomotor_.movements[*dense].walk_stop_parts == 0 && speed > 0.0F && locomotor_.max_speeds[*dense] > 0.0F)
            {
                const auto distance = speed * speed / (2.0F * locomotor_.movements[*dense].deceleration);
                const auto& here = placement_.positions[*dense];
                plan_move(*dense, {here.x + std::cos(placement_.headings[*dense]) * distance, here.y + std::sin(placement_.headings[*dense]) * distance, here.z});
            }
            break;
        case SimCommandKind::attack:
        {
            const auto victim = dense_index(command.target_entity);
            auto& combat = combat_.states[*dense];
            if (!victim || !damageable(*victim))
            {
                ++stale_references_;
                break;
            }
            if (!combat.profile.weapons.empty() && *victim != *dense && rules_.hostility.hostile(identity_.owners[*dense], identity_.owners[*victim]))
            {
                combat.target = command.target_entity;
                combat.ordered = true;
                locomotor_.speed_limits[*dense] = 0.0F;
                group_.ids[*dense] = 0;
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
            combat_.stances[*dense] = command.stance;
            if (command.stance == Stance::hold_fire && !combat_.states[*dense].ordered)
            {
                combat_.states[*dense].target = {};
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
    const bool scans = std::ranges::any_of(orders_.standing,
                                           [](const StandingOrderState& order)
                                           {
                                               return order.kind != StandingOrder::none && !order.engaging && order.scan_timer == 0;
                                           });
    if (scans)
    {
        grid_.build(placement_.positions, grid_cell_size);
    }
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        steering_.intents[index] = {};
        run_standing_order(index);
        if (flight_.min_speeds[index] > 0.0F)
        {
            plan_flight(index, dt);
        }
        else
        {
            if (combat_.states[index].ordered)
            {
                pursue_ordered_target(index);
            }
            // Standing body-aimers turn to face their target (ordered or picked on their own), and so
            // do units whose limited turret cannot reach it (unless their turret does not turn them).
            if (!route_.move_targets[index])
            {
                if (const auto victim = attack_victim(index))
                {
                    const auto& turret = combat_.states[index].profile.turret;
                    const auto& to = placement_.positions[*victim];
                    const bool beyond_turret =
                        turret && turret->turns_body && turret->yaw_limit < pi &&
                        std::fabs(wrap_angle(std::atan2(to.y - placement_.positions[index].y, to.x - placement_.positions[index].x) - placement_.headings[index])) > turret->yaw_limit;
                    if (combat_.aims_with_body[index] != 0 || beyond_turret)
                    {
                        steering_.intents[index].point = to;
                    }
                    else if (combat_.states[index].ordered && ground_mover_.follows_ground[index] == 0 && !ground_.map.valid())
                    {
                        // An ordered space ship turns in place toward its target's nearest live
                        // hardpoint; an idle one keeps its heading (recorded in the FoC debug build:
                        // EaW-Revised S-22 to S-27, docs/compatibility-research.md).
                        steering_.intents[index].point = aim_point(*victim, aim_hardpoint(*victim, placement_.positions[index]));
                    }
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
        auto& order = orders_.standing[*dense];
        auto& combat = combat_.states[*dense];
        orders_.command_serials[*dense] = command.serial;
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
            if (route_.move_targets[*dense])
            {
                order.point = *route_.move_targets[*dense];
            }
            continue;
        }
        locomotor_.speed_limits[*dense] = 0.0F;
        group_.ids[*dense] = 0;
        if (const auto anchor = command.target_entity.valid() ? dense_index(command.target_entity) : std::optional<std::size_t>(); anchor)
        {
            order.point = placement_.positions[*anchor];
        }
        if (locomotor_.max_speeds[*dense] > 0.0F)
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
    if (ground_mover_.follows_ground[index] != 0 && ground_.map.valid())
    {
        position.z = ground_.map.surface_height(position.x, position.y);
    }
    if (auto* footprint = footprint_.find(slot_of(index), &FootprintColumns::shapes))
    {
        // The footprint keeps its place relative to the entity: its offset turns with the heading.
        const float turn = heading ? *heading - placement_.headings[index] : 0.0F;
        const float offset_x = footprint->center.x - placement_.positions[index].x;
        const float offset_y = footprint->center.y - placement_.positions[index].y;
        footprint->center.x = position.x + offset_x * std::cos(turn) - offset_y * std::sin(turn);
        footprint->center.y = position.y + offset_x * std::sin(turn) + offset_y * std::cos(turn);
        footprint->heading += turn;
    }
    placement_.positions[index] = position;
    if (heading)
    {
        placement_.headings[index] = *heading;
    }
    route_.move_targets[index].reset();
    route_.paths[index].clear();
    route_.pending[index] = 0;
    locomotor_.speed_limits[index] = 0.0F;
    group_.ids[index] = 0;
    route_.loiter_points[index].reset();
    stamp_footprint(index, true);
}

void SimulationWorld::disengage(const std::size_t index)
{
    combat_.states[index].target = {};
    combat_.states[index].ordered = false;
    orders_.standing[index].engaging = false;
}

// Attack-move and guard (engine semantics; the original's are unmeasured). Both scan for hostiles
// every retarget_interval ticks (staggered by entity index) and engage the nearest one as an
// ordered attack, which the pursuit chases; they break off when the victim is gone or the chase
// leaves its leash, then head for the destination (attack-move) or back to the anchor (guard). An
// attack-move ends once it arrives with no engagement; a guard ends when its object dies.
void SimulationWorld::run_standing_order(const std::size_t index)
{
    auto& order = orders_.standing[index];
    if (order.kind == StandingOrder::none)
    {
        return;
    }
    if (order.entity.valid())
    {
        if (const auto followed = dense_index(order.entity))
        {
            order.point = placement_.positions[*followed];
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
    auto& combat = combat_.states[index];
    const float range = combat.profile.range();
    const bool guard = order.kind == StandingOrder::guard;
    const float guard_radius = std::max(range, minimum_guard_radius);
    const bool mobile = locomotor_.max_speeds[index] > 0.0F;
    if (order.engaging)
    {
        const auto victim = combat.ordered ? attack_victim(index) : std::nullopt;
        const auto& leash_center = guard ? order.point : order.engage_origin;
        const float leash = guard && combat.profile.guard_chase_range > 0.0F ? combat.profile.guard_chase_range : std::max(range * leash_range_factor, guard ? guard_radius : 0.0F);
        if (victim && distance_xy(placement_.positions[*victim], leash_center) <= leash)
        {
            return;
        }
        disengage(index);
        // Resume: back on the way, or back to the anchor when out of the guard's reach.
        if (mobile && (!guard || distance_xy(placement_.positions[index], order.point) > guard_radius * 0.5F))
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
            const auto& center = guard ? order.point : placement_.positions[index];
            const float response = combat.profile.attack_move_response_range;
            const float reach = guard ? guard_radius : (response > 0.0F ? response : range * acquire_range_factor);
            const auto best = choose_target(index, center, reach,
                                            [&](const std::size_t candidate)
                                            {
                                                return engages(index, candidate);
                                            });
            if (best)
            {
                combat.target = identity_.ids[*best];
                combat.ordered = true;
                combat.chase_timer = 0;
                locomotor_.speed_limits[index] = 0.0F;
                group_.ids[index] = 0;
                order.engaging = true;
                order.engage_origin = placement_.positions[index];
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
        if (distance_xy(placement_.positions[index], order.point) > guard_radius * 0.5F && (!route_.move_targets[index] || distance_xy(*route_.move_targets[index], order.point) > follow_tolerance) &&
            order.scan_timer == 0)
        {
            plan_move(index, order.point);
        }
        return;
    }
    if (order.entity.valid() && (!route_.move_targets[index] || distance_xy(*route_.move_targets[index], order.point) > follow_tolerance) && order.scan_timer == 0)
    {
        plan_move(index, order.point);
        return;
    }
    // Arrived with nothing to fight (at the point, or within reach of the object): the attack-move
    // is done.
    if (!route_.move_targets[index] && route_.pending[index] == 0 && (!order.entity.valid() || distance_xy(placement_.positions[index], order.point) <= std::max(follow_tolerance, range)))
    {
        order.kind = StandingOrder::none;
    }
}

std::optional<std::size_t> SimulationWorld::attack_victim(const std::size_t index) const noexcept
{
    const auto& combat = combat_.states[index];
    if (!combat.target.valid())
    {
        return std::nullopt;
    }
    const auto victim = dense_index(combat.target);
    if (!victim || !damageable(*victim) || !rules_.hostility.hostile(identity_.owners[index], identity_.owners[*victim]))
    {
        return std::nullopt;
    }
    return victim;
}

// Chase an ordered target until it is in range, then hold position (combat fires). The chase
// replans every chase_interval ticks while the target moves.
void SimulationWorld::pursue_ordered_target(const std::size_t index)
{
    auto& combat = combat_.states[index];
    const auto victim = attack_victim(index);
    if (!victim)
    {
        // Combat drops the order this tick.
        return;
    }
    if (distance_xy(placement_.positions[index], placement_.positions[*victim]) > combat.profile.range())
    {
        if (locomotor_.max_speeds[index] > 0.0F && (combat.chase_timer == 0 || !route_.move_targets[index]))
        {
            // Flyers keep their altitude; ground units take theirs from the surface anyway.
            plan_move(index,
                      {placement_.positions[*victim].x, placement_.positions[*victim].y, ground_mover_.follows_ground[index] != 0 ? placement_.positions[*victim].z : placement_.positions[index].z});
            combat.chase_timer = chase_interval;
        }
    }
    else if (route_.move_targets[index])
    {
        route_.move_targets[index].reset();
        route_.paths[index].clear();
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
    const auto& position = placement_.positions[index];
    auto& intent = steering_.intents[index];
    if (flight_maneuvers_.get(slot_of(index), &FlightManeuverColumns::looping) != 0)
    {
        intent.loop = true;
        return;
    }
    const auto& combat = combat_.states[index];
    const auto plane = flight_.cruise_heights[index];
    const auto choose = [&]
    {
        if (combat.ordered || combat_.stances[index] == Stance::aggressive)
        {
            if (const auto victim = attack_victim(index))
            {
                const auto& there = placement_.positions[*victim];
                if (flight_.min_speeds[*victim] > 0.0F)
                {
                    // Each flyer weaves on its own phase (from its slot), so a fight spreads vertically.
                    const auto seconds = static_cast<float>(static_cast<double>(tick_) * tick_seconds_);
                    const auto phase = static_cast<float>(identity_.ids[index].index % 16U) * (pi / 8.0F);
                    const auto weave = dogfight_weave * std::sin(seconds * (two_pi / dogfight_weave_seconds) + phase);
                    intent.point = Vector3{there.x, there.y, std::clamp(there.z + weave, plane - dogfight_band, plane + dogfight_band)};
                    return;
                }
                const auto pass = std::max(combat.profile.range() / 3.0F, (placement_.radii[index] + placement_.radii[*victim]) * 2.0F);
                intent.point = Vector3{there.x, there.y, plane};
                intent.hold_heading = distance_xy(position, there) < pass;
                return;
            }
        }
        auto& target = route_.move_targets[index];
        if (target)
        {
            intent.point = Vector3{target->x, target->y, plane};
            if (distance_xy(position, *target) <= travel_speed(index) * dt * 3.0F)
            {
                route_.loiter_points[index] = *target;
                target.reset();
                locomotor_.speed_limits[index] = 0.0F;
                group_.ids[index] = 0;
            }
            return;
        }
        if (!route_.loiter_points[index])
        {
            route_.loiter_points[index] = position;
        }
        intent.point = Vector3{route_.loiter_points[index]->x, route_.loiter_points[index]->y, plane};
    };
    choose();
    if (intent.hold_heading || !intent.point || locomotor_.turn_rates[index] <= 0.0F)
    {
        return;
    }
    const auto dx = intent.point->x - position.x;
    const auto dy = intent.point->y - position.y;
    const auto turn_circle = travel_speed(index) / locomotor_.turn_rates[index];
    if (std::hypot(dx, dy) > 4.0F * turn_circle && std::fabs(wrap_angle(std::atan2(dy, dx) - placement_.headings[index])) > flight_reversal_angle)
    {
        intent.loop = true;
    }
}

} // namespace eaw
