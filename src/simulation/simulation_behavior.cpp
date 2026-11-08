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
    if (command.kind == SimCommandKind::formation_move)
    {
        apply_formation_move(command);
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
        switch (command.kind)
        {
        case SimCommandKind::formation_move:
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
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        intents_[index] = {};
        if (min_speeds_[index] > 0.0F)
        {
            plan_flight(index, dt);
        }
        else if (combat_[index].ordered)
        {
            pursue_ordered_target(index);
        }
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
// where to steer. They keep to the plane they cruise in (observed in the original: fighters fly
// in one plane most of the time), so every point is taken at their cruise height. Attack runs: inside a third of its weapon range of the target a flyer holds its
// heading, so it passes the target, flies on, and turns back once outside. An aggressive flyer
// runs at any target it has; otherwise only at an ordered one. Without a target it flies to its
// destination, arriving within a few ticks of flight of it, and circles where it arrived or idles.
// Engine choices; the original's fighter locomotor is not documented.
void SimulationWorld::plan_flight(const std::size_t index, const float dt)
{
    const auto& position = positions_[index];
    auto& intent = intents_[index];
    const auto& combat = combat_[index];
    if (combat.ordered || stances_[index] == Stance::aggressive)
    {
        if (const auto victim = attack_victim(index))
        {
            const auto pass = std::max(combat.profile.range() / 3.0F, (radii_[index] + radii_[*victim]) * 2.0F);
            intent.point = SimVector3{positions_[*victim].x, positions_[*victim].y, cruise_heights_[index]};
            intent.hold_heading = distance_xy(position, positions_[*victim]) < pass;
            return;
        }
    }
    auto& target = move_targets_[index];
    if (target)
    {
        intent.point = SimVector3{target->x, target->y, cruise_heights_[index]};
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
    intent.point = SimVector3{loiter_points_[index]->x, loiter_points_[index]->y, cruise_heights_[index]};
}

} // namespace eaw
