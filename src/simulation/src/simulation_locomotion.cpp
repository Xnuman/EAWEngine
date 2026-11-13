// Locomotion: how units physically move toward what the behavior layer asks for (speed limits,
// turn rates, ground following, flight) and separation.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

float SimulationWorld::travel_speed(const std::size_t index) const noexcept
{
    const auto limit = speed_limits_[index];
    const auto own = keeps(index, HardpointProfile::Kind::engine) ? max_speeds_[index] : max_speeds_[index] * 0.25F;
    return limit > 0.0F ? std::min(own, limit) : own;
}

void SimulationWorld::run_movement()
{
    const auto dt = static_cast<float>(tick_seconds_);
    process_path_queue();
    measure_remaining_routes();
    update_group_cohesion();
    plan_space_avoidance();
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        // A suspended locomotor holds the entity where it is; its orders wait.
        if (locomotor_suspended_[index] != 0)
        {
            continue;
        }
        if (min_speeds_[index] > 0.0F)
        {
            fly(index, dt);
            continue;
        }
        auto& target = move_targets_[index];
        if (!target || group_waiting_[index] != 0 || path_pending_[index] != 0)
        {
            drive_speeds_[index] = 0.0F;
        }
        if (!target)
        {
            speed_limits_[index] = 0.0F;
            group_ids_[index] = 0;
            // Standing: turn in place toward the behavior layer's point, if any.
            if (const auto& face = intents_[index].point)
            {
                const auto dx = face->x - positions_[index].x;
                const auto dy = face->y - positions_[index].y;
                if (dx != 0.0F || dy != 0.0F)
                {
                    (void)steer(index, std::atan2(dy, dx), dt);
                }
            }
            continue;
        }
        if (group_waiting_[index] != 0 || path_pending_[index] != 0)
        {
            continue;
        }
        auto& position = positions_[index];
        if (follows_ground_[index] != 0 && ground_.valid())
        {
            move_over_ground(index, dt);
            continue;
        }
        if (index < detours_.size() && detours_[index])
        {
            // Around a unit in the way: steer at the detour point; arrival still counts at the target.
            if (std::hypot(target->x - position.x, target->y - position.y) <= std::max(travel_speed(index) * dt, arrival_distance))
            {
                position = *target;
                target.reset();
                continue;
            }
            const auto& detour = *detours_[index];
            const Vector3 aside{detour.x - position.x, detour.y - position.y, detour.z - position.z};
            const auto length = std::sqrt(aside.x * aside.x + aside.y * aside.y + aside.z * aside.z);
            const auto factor = aside.x != 0.0F || aside.y != 0.0F ? steer(index, std::atan2(aside.y, aside.x), dt) : 1.0F;
            const auto left = std::hypot(target->x - position.x, target->y - position.y);
            const auto step = std::min(drive(index, wanted_speed(index, factor, left), dt) * dt, length);
            if (length > 0.0F)
            {
                const auto& heading = headings_[index];
                const auto moved = moves_along_heading(index) && left > settle_distance(index) ? keep_apart(index, {std::cos(heading) * step, std::sin(heading) * step, aside.z * step / length})
                                                                                               : keep_apart(index, {aside.x * step / length, aside.y * step / length, aside.z * step / length});
                position = {position.x + moved.x, position.y + moved.y, position.z + moved.z};
            }
            continue;
        }
        const Vector3 delta{target->x - position.x, target->y - position.y, target->z - position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto speed_factor = delta.x != 0.0F || delta.y != 0.0F ? steer(index, std::atan2(delta.y, delta.x), dt) : 1.0F;
        const auto step_length = drive(index, wanted_speed(index, speed_factor, distance), dt) * dt;
        const auto scale = std::min(1.0F, step_length / std::max(distance, arrival_distance));
        const auto& heading = headings_[index];
        const Vector3 wanted = moves_along_heading(index) && distance > settle_distance(index) ? Vector3{std::cos(heading) * step_length, std::sin(heading) * step_length, delta.z * scale}
                                                                                               : Vector3{delta.x * scale, delta.y * scale, delta.z * scale};
        const auto moved = keep_apart(index, wanted);
        if (distance <= std::max(step_length, arrival_distance) && moved.x == wanted.x && moved.y == wanted.y)
        {
            position = *target;
            target.reset();
            continue;
        }
        position = {position.x + moved.x, position.y + moved.y, position.z + moved.z};
    }
}

void SimulationWorld::move_over_ground(const std::size_t index, const float dt)
{
    auto& target = move_targets_[index];
    auto& position = positions_[index];
    auto& path = paths_[index];
    const auto allowed = passable_types_[index];
    // Ground units move in the horizontal plane; their height always comes from the surface. A
    // tick's movement carries on past reached waypoints, so corners do not slow a unit down.
    auto remaining = 0.0F;
    bool driven = false;
    // An intermediate waypoint is passed once the one after it is in straight reach. A crowd keeps
    // pushing units away from a shared corner waypoint, so requiring each unit to touch it left
    // groups circling there (found by the path budget test).
    if (allowed != 0)
    {
        while (!path.empty())
        {
            const auto& after = path.size() > 1 ? path[path.size() - 2] : *target;
            if (!ground_line_passable(ground_, {position.x, position.y}, {after.x, after.y}, allowed))
            {
                break;
            }
            path.pop_back();
        }
    }
    while (target)
    {
        const auto waypoint = path.empty() ? *target : path.back();
        const auto delta_x = waypoint.x - position.x;
        const auto delta_y = waypoint.y - position.y;
        const auto distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
        auto facing = 1.0F;
        if ((delta_x != 0.0F || delta_y != 0.0F) && distance > arrival_distance)
        {
            facing = steer(index, std::atan2(delta_y, delta_x), dt);
        }
        if (!driven)
        {
            // shortcut: brakes for the destination only, so a heavy unit slides a little past a
            // sharp corner while it stops to turn; look ahead to the corner if that shows.
            remaining = drive(index, wanted_speed(index, facing, remaining_route_[index]), dt) * dt;
            driven = true;
        }
        else
        {
            remaining *= facing;
        }
        const bool arrives = distance <= std::max(remaining, arrival_distance);
        const auto scale = arrives || distance <= 0.0F ? 1.0F : remaining / distance;
        const auto next_x = position.x + delta_x * scale;
        const auto next_y = position.y + delta_y * scale;
        if (allowed != 0)
        {
            const auto here = ground_.passability_at(position.x, position.y);
            const auto there = ground_.passability_at(next_x, next_y);
            const bool leaving_blocked = here && (*here & allowed) == 0;
            if (there && (*there & allowed) == 0 && !leaving_blocked)
            {
                // The planned line no longer fits: separation pushed the unit off it, or the ground
                // changed. Plan again from here (plan_move drops a goal that is no longer reachable).
                const auto goal = *target;
                ++blocked_moves_;
                plan_move(index, goal);
                return;
            }
        }
        position = {next_x, next_y, ground_.surface_height(next_x, next_y)};
        if (!arrives)
        {
            return;
        }
        if (path.empty())
        {
            target.reset();
            return;
        }
        path.pop_back();
        remaining -= distance;
        // A unit that turns at a finite rate faces each new segment over the next ticks.
        if (remaining <= 0.0F || turn_rates_[index] > 0.0F)
        {
            return;
        }
    }
}

float SimulationWorld::steer(const std::size_t index, const float desired, const float dt)
{
    auto& heading = headings_[index];
    auto difference = wrap_angle(desired - heading);
    if (turn_rates_[index] <= 0.0F)
    {
        heading = wrap_angle(desired);
        return 1.0F;
    }
    const auto& movement = movements_[index];
    const bool slow = move_targets_[index] && movement.turn_speed_fraction > 0.0F && drive_speeds_[index] < movement.turn_speed_fraction * travel_speed(index);
    const auto standing = !move_targets_[index] && movement.turn_in_place_rate > 0.0F;
    const auto rate = slow ? movement.stopped_turn_rate : standing ? movement.turn_in_place_rate : turn_rates_[index];
    if (rate <= 0.0F)
    {
        return std::max(0.0F, std::cos(difference));
    }
    const auto step = rate * dt;
    if (std::fabs(difference) <= step)
    {
        heading = wrap_angle(desired);
        difference = 0.0F;
    }
    else
    {
        heading = wrap_angle(heading + (difference > 0.0F ? step : -step));
        difference = wrap_angle(desired - heading);
    }
    return std::max(0.0F, std::cos(difference));
}

float SimulationWorld::wanted_speed(const std::size_t index, const float facing, const float distance) const noexcept
{
    const auto& movement = movements_[index];
    auto factor = facing;
    // Turning in place is for starting from rest (Close_Enough_Angle_For_Move_Start); a unit on the
    // move turns while it walks (the owner's recording: the AT-AT swings 90 degrees mid-walk).
    if (movement.turn_in_place_angle > 0.0F && drive_speeds_[index] <= 0.0F && facing < std::cos(movement.turn_in_place_angle))
    {
        factor = 0.0F;
    }
    else if (movement.turn_speed_fraction > 0.0F)
    {
        factor = std::max(factor, movement.turn_speed_fraction);
    }
    auto wanted = travel_speed(index) * factor;
    if (movement.deceleration > 0.0F)
    {
        // The speed it can still brake from to stop at the destination.
        wanted = std::min(wanted, std::sqrt(2.0F * movement.deceleration * std::max(distance, 0.0F)));
    }
    return wanted;
}

float SimulationWorld::drive(const std::size_t index, const float wanted, const float dt) noexcept
{
    const auto& movement = movements_[index];
    auto& speed = drive_speeds_[index];
    const auto rate = wanted > speed ? movement.acceleration : movement.deceleration;
    if (rate <= 0.0F)
    {
        speed = wanted;
    }
    else
    {
        speed = wanted > speed ? std::min(wanted, speed + rate * dt) : std::max(wanted, speed - rate * dt);
    }
    return speed;
}

bool SimulationWorld::moves_along_heading(const std::size_t index) const noexcept
{
    const auto& movement = movements_[index];
    return follows_ground_[index] == 0 && turn_rates_[index] > 0.0F && movement.turn_speed_fraction > 0.0F && movement.stopped_turn_rate <= 0.0F;
}

// Within twice its slowest turn radius it slides straight to the destination at braking speed:
// circling at that radius, a mover always comes this close, so it cannot orbit its destination.
float SimulationWorld::settle_distance(const std::size_t index) const noexcept
{
    const auto turn_radius = movements_[index].turn_speed_fraction * travel_speed(index) / turn_rates_[index];
    return std::max(2.0F * turn_radius, arrival_distance);
}

// Overlapping ground units are pushed apart, half each by default. Displacements are summed for
// every pair first and applied afterwards, so the result does not depend on which unit of a pair
// is visited first. Entities that cannot move are never pushed (they push the other unit the whole
// way); idle units yield more than moving ones, so a moving unit shoulders through instead of
// stopping. A push never moves a unit onto a cell its class may not enter, and it is limited per
// tick so packed groups spread out over a few ticks instead of jumping.
bool SimulationWorld::can_crush(const std::size_t crusher, const std::size_t victim) const
{
    const auto kind = crushes_[crusher].crusher;
    const auto& squash = crushes_[victim];
    const bool may = squash.squashable || (kind == Crusher::supercrusher && squash.squashable_by_supercrusher);
    const auto& combat = combat_[victim];
    return kind != Crusher::none && victim != crusher && may && combat.profile.health > 0.0F && combat.health > 0.0F && hostility_.hostile(owners_[crusher], owners_[victim]);
}

void SimulationWorld::run_crushing()
{
    if (!ground_.valid())
    {
        return;
    }
    // shortcut: every moving crusher scans every entity; use the spatial grid when battles get big.
    for (std::size_t crusher = 0; crusher < ids_.size(); ++crusher)
    {
        const auto kind = crushes_[crusher].crusher;
        const auto& from = tick_start_positions_[crusher];
        const auto& at = positions_[crusher];
        if (kind == Crusher::none || follows_ground_[crusher] == 0 || radii_[crusher] <= 0.0F || (from.x == at.x && from.y == at.y))
        {
            continue;
        }
        for (std::size_t victim = 0; victim < ids_.size(); ++victim)
        {
            if (!can_crush(crusher, victim))
            {
                continue;
            }
            if (std::hypot(positions_[victim].x - at.x, positions_[victim].y - at.y) < radii_[crusher] && wound(victim, combat_[victim].health) > 0.0F)
            {
                ++crushed_;
                combat_[victim].crushed = true;
            }
        }
    }
}

void SimulationWorld::run_separation()
{
    separated_pairs_ = 0;
    // On land, ground units separate on the surface. Space ships do not push each other (they avoid
    // contact instead: plan_space_avoidance); only flyers separate there.
    const bool land = ground_.valid();
    // Flyers separate only from flyers of their owner (observed in the original: squadron mates push
    // each other apart), sideways so they keep their plane; overlap counts in 3D, so flyers passing
    // above or below each other do not push.
    const auto flyer = [&](const std::size_t index)
    {
        return radii_[index] > 0.0F && min_speeds_[index] > 0.0F;
    };
    const auto takes_part = [&](const std::size_t index)
    {
        return flyer(index) || (radii_[index] > 0.0F && min_speeds_[index] <= 0.0F && land && follows_ground_[index] != 0);
    };
    grid_.build(positions_, grid_cell_size);
    separation_push_.assign(ids_.size(), Vector3{});
    float largest_radius = 0.0F;
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (takes_part(index))
        {
            largest_radius = std::max(largest_radius, radii_[index]);
        }
    }
    // Remaining route length of every moving unit: of two moving units, the one further along
    // (less route left) has priority and yields much less, so units queue through a narrow
    // passage instead of shoving each other back and forth (Age of Empires II's priority rule).
    measure_remaining_routes();
    const auto weight = [&](const std::size_t index, const std::size_t other)
    {
        if (max_speeds_[index] <= 0.0F)
        {
            return 0.0F;
        }
        // Only units of one owner make way for each other. An idle unit of another owner (an enemy
        // or a neutral) stands its ground against a moving one, which is held back instead; two
        // foreign units that both move or both stand split the push evenly.
        if (owners_[index] != owners_[other])
        {
            return !move_targets_[index] && move_targets_[other] ? 0.0F : 1.0F;
        }
        if (!move_targets_[index])
        {
            return 2.0F;
        }
        if (move_targets_[other] && max_speeds_[other] > 0.0F && remaining_route_[index] != remaining_route_[other])
        {
            return remaining_route_[index] < remaining_route_[other] ? 0.25F : 1.75F;
        }
        return 1.0F;
    };
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (!takes_part(index))
        {
            continue;
        }
        query_.clear();
        grid_.query(positions_[index], radii_[index] + largest_radius, query_);
        for (const auto other : query_)
        {
            // Each pair once, from its lower dense index.
            if (other <= index || !takes_part(other))
            {
                continue;
            }
            const bool flying = flyer(index);
            if (flying != flyer(other) || (flying && owners_[index] != owners_[other]))
            {
                continue;
            }
            // A crusher and what it can crush do not push each other: a walker walks over standing
            // infantry (and crushes it) instead of being held back by it, the owner's report; pushing
            // the infantry aside instead would keep a slow walker from ever reaching it. Infantry may
            // stand at a still walker's feet, out of its guns' reach, as in the original.
            if (!flying && (can_crush(index, other) || can_crush(other, index)))
            {
                continue;
            }
            const auto dx = positions_[other].x - positions_[index].x;
            const auto dy = positions_[other].y - positions_[index].y;
            const auto dz = flying ? positions_[other].z - positions_[index].z : 0.0F;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            const auto overlap = radii_[index] + radii_[other] - std::sqrt(distance * distance + dz * dz);
            const auto first_weight = flying ? 1.0F : weight(index, other);
            const auto second_weight = flying ? 1.0F : weight(other, index);
            // A hundredth of a world unit of overlap is tolerated, so units at rest stop pushing.
            if (overlap <= 0.01F || first_weight + second_weight <= 0.0F)
            {
                continue;
            }
            // Coincident units part along a direction fixed by their ids.
            float nx = 0.0F;
            float ny = 0.0F;
            if (distance > 1.0e-4F)
            {
                nx = dx / distance;
                ny = dy / distance;
            }
            else
            {
                const auto angle = static_cast<float>((ids_[index].index * 2654435761U + ids_[other].index) % 6283U) / 1000.0F;
                nx = std::cos(angle);
                ny = std::sin(angle);
            }
            const auto total = first_weight + second_weight;
            const auto first_share = overlap * first_weight / total;
            const auto second_share = overlap * second_weight / total;
            separation_push_[index].x -= nx * first_share;
            separation_push_[index].y -= ny * first_share;
            separation_push_[other].x += nx * second_share;
            separation_push_[other].y += ny * second_share;
            ++separated_pairs_;
        }
    }
    const auto dt = static_cast<float>(tick_seconds_);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        auto push = separation_push_[index];
        const auto length = std::sqrt(push.x * push.x + push.y * push.y);
        if (length <= 0.0F)
        {
            continue;
        }
        // At most half a radius, or the unit's own speed, per tick.
        const auto limit = std::max(radii_[index] * 0.5F, max_speeds_[index] * dt);
        if (length > limit)
        {
            push.x *= limit / length;
            push.y *= limit / length;
        }
        auto& position = positions_[index];
        const auto next_x = position.x + push.x;
        const auto next_y = position.y + push.y;
        if (const auto allowed = passable_types_[index]; allowed != 0)
        {
            const auto there = ground_.passability_at(next_x, next_y);
            if (there && (*there & allowed) == 0)
            {
                continue;
            }
        }
        position = {next_x, next_y, land && !flyer(index) ? ground_.surface_height(next_x, next_y) : position.z};
    }
}

// Flight: along the nose at between the minimum and maximum speed, turning toward the behavior
// layer's point at the turn rate (slowing toward the minimum while facing away) and pitching toward
// its height at the same rate, or holding heading and pitch when asked to. A half-loop pitches the
// nose up at the turn rate through the vertical until the flyer is inverted and heading back; it
// then flies level the other way (heading reversed) with a half roll left to unwind, which turns
// back to upright at twice the turn rate.
void SimulationWorld::fly(const std::size_t index, const float dt)
{
    const auto& intent = intents_[index];
    auto& position = positions_[index];
    auto& pitch = pitches_[index];
    auto& roll = rolls_[index];
    const auto heading_before = headings_[index];
    const auto rate = turn_rates_[index] > 0.0F ? turn_rates_[index] : flight_max_pitch * 2.0F / std::max(dt, 1.0e-6F);
    auto factor = 1.0F;
    if (intent.loop && looping_[index] == 0)
    {
        looping_[index] = 1;
    }
    if (looping_[index] != 0)
    {
        pitch = std::min(pitch + rate * dt, pi);
    }
    else
    {
        auto desired_pitch = intent.hold_heading ? pitch : 0.0F;
        if (intent.point && !intent.hold_heading)
        {
            const auto dx = intent.point->x - position.x;
            const auto dy = intent.point->y - position.y;
            if (dx != 0.0F || dy != 0.0F)
            {
                factor = steer(index, std::atan2(dy, dx), dt);
            }
            // The height is reached within a few turn circles, not over the whole way, so a flyer
            // returns to its plane soon after leaving it.
            const auto climb_distance = std::max(4.0F * travel_speed(index) / rate, 1.0F);
            desired_pitch = std::clamp(std::atan2(intent.point->z - position.z, std::clamp(std::hypot(dx, dy), 1.0F, climb_distance)), -flight_max_pitch, flight_max_pitch);
        }
        pitch += std::clamp(desired_pitch - pitch, -rate * dt, rate * dt);
        // Unwind a half-loop's roll.
        const auto unwind = 2.0F * rate * dt;
        roll = std::fabs(roll) <= unwind ? 0.0F : roll - std::copysign(unwind, roll);
    }
    const auto top = travel_speed(index);
    const auto speed = std::clamp(top * factor, std::min(min_speeds_[index], top), top);
    const auto heading = headings_[index];
    const auto level = speed * std::cos(pitch) * dt;
    position = {position.x + std::cos(heading) * level, position.y + std::sin(heading) * level, position.z + std::sin(pitch) * speed * dt};
    yaw_rates_[index] = dt > 0.0F ? wrap_angle(heading - heading_before) / dt : 0.0F;
    if (looping_[index] != 0 && pitch >= pi)
    {
        // Inverted at the top, heading back: the same attitude as level flight the other way rolled
        // upside down.
        looping_[index] = 0;
        headings_[index] = wrap_angle(heading + pi);
        pitch = 0.0F;
        roll = pi;
    }
}

} // namespace eaw
