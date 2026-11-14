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
    const auto limit = locomotor_.speed_limits[index];
    const auto own = keeps(index, HardpointProfile::Kind::engine) ? locomotor_.max_speeds[index] : locomotor_.max_speeds[index] * 0.25F;
    return limit > 0.0F ? std::min(own, limit) : own;
}

void SimulationWorld::run_navigation()
{
    std::ranges::fill(locomotor_.arriving, std::uint8_t{0});
    process_path_queue();
    measure_remaining_routes(route_, placement_);
    update_group_cohesion();
    plan_space_avoidance();
}

void SimulationWorld::run_movement()
{
    const auto dt = static_cast<float>(tick_seconds_);
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        // A suspended locomotor holds the entity where it is; its orders wait.
        if (locomotor_.suspended[index] != 0)
        {
            continue;
        }
        if (flight_.min_speeds[index] > 0.0F)
        {
            fly(index, dt);
            continue;
        }
        // A walk played as clips that stops, or walks on to its stop, goes straight ahead.
        if (locomotor_.movements[index].walk_stop_parts > 0 && walk_straight(index, dt))
        {
            continue;
        }
        auto& target = route_.move_targets[index];
        if (!target || group_.waiting[index] != 0 || route_.pending[index] != 0)
        {
            locomotor_.drive_speeds[index] = 0.0F;
        }
        if (!target)
        {
            locomotor_.speed_limits[index] = 0.0F;
            group_.ids[index] = 0;
            // Standing: turn in place toward the behavior layer's point, if any.
            if (const auto& face = steering_.intents[index].point)
            {
                const auto dx = face->x - placement_.positions[index].x;
                const auto dy = face->y - placement_.positions[index].y;
                if (dx != 0.0F || dy != 0.0F)
                {
                    (void)steer(index, std::atan2(dy, dx), dt);
                }
            }
            continue;
        }
        if (group_.waiting[index] != 0 || route_.pending[index] != 0)
        {
            continue;
        }
        auto& position = placement_.positions[index];
        if (ground_mover_.follows_ground[index] != 0 && ground_.map.valid())
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
            const auto step = std::min(move_speed(index, factor, left, dt) * dt, length);
            if (length > 0.0F)
            {
                const auto& heading = placement_.headings[index];
                const auto moved = moves_along_heading(index) && left > settle_distance(index) ? keep_apart(index, {std::cos(heading) * step, std::sin(heading) * step, aside.z * step / length})
                                                                                               : keep_apart(index, {aside.x * step / length, aside.y * step / length, aside.z * step / length});
                position = {position.x + moved.x, position.y + moved.y, position.z + moved.z};
            }
            continue;
        }
        const Vector3 delta{target->x - position.x, target->y - position.y, target->z - position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto speed_factor = delta.x != 0.0F || delta.y != 0.0F ? steer(index, std::atan2(delta.y, delta.x), dt) : 1.0F;
        const auto step_length = move_speed(index, speed_factor, distance, dt) * dt;
        const auto scale = std::min(1.0F, step_length / std::max(distance, arrival_distance));
        const auto& heading = placement_.headings[index];
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
    auto& target = route_.move_targets[index];
    auto& position = placement_.positions[index];
    auto& path = route_.paths[index];
    const auto allowed = ground_mover_.passable_types[index];
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
            if (!ground_line_passable(ground_.map, {position.x, position.y}, {after.x, after.y}, allowed))
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
            remaining = move_speed(index, facing, route_.remaining[index], dt) * dt;
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
            const auto here = ground_.map.passability_at(position.x, position.y);
            const auto there = ground_.map.passability_at(next_x, next_y);
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
        position = {next_x, next_y, ground_.map.surface_height(next_x, next_y)};
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
        if (remaining <= 0.0F || locomotor_.turn_rates[index] > 0.0F)
        {
            return;
        }
    }
}

float SimulationWorld::steer(const std::size_t index, const float desired, const float dt)
{
    auto& heading = placement_.headings[index];
    auto difference = wrap_angle(desired - heading);
    if (locomotor_.turn_rates[index] <= 0.0F)
    {
        heading = wrap_angle(desired);
        return 1.0F;
    }
    const auto& movement = locomotor_.movements[index];
    // A turn in place played as clips (MovementProfile::turn_begin_seconds) follows its plan.
    if (turn_as_clips(index, difference, dt))
    {
        return std::max(0.0F, std::cos(wrap_angle(desired - heading)));
    }
    // A walk played as clips given a new destination more than its turn-in-place angle off holds its
    // heading, stops at its stride's next part, and turns in place with its clips (walk_speed); a
    // route's corners it turns while it walks (the owner's recording of the original: an AT-AT swings
    // 90 degrees and back while walking).
    auto* walk = walk_clips_.find(slot_of(index), &WalkClipColumns::plans);
    if (walk != nullptr && walk->new_way && walk->clip == WalkClip::moving)
    {
        walk->new_way = false;
        walk->turn_back = movement.turn_begin_seconds[0] > 0.0F && movement.turn_in_place_angle > 0.0F && std::fabs(difference) > movement.turn_in_place_angle;
    }
    if (walk != nullptr && walk->turn_back)
    {
        return std::max(0.0F, std::cos(difference));
    }
    const bool slow = route_.move_targets[index] && movement.turn_speed_fraction > 0.0F && locomotor_.drive_speeds[index] < movement.turn_speed_fraction * travel_speed(index);
    const auto standing = !route_.move_targets[index] && movement.turn_in_place_rate > 0.0F;
    const auto rate = slow ? movement.stopped_turn_rate : standing ? movement.turn_in_place_rate : locomotor_.turn_rates[index];
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

float SimulationWorld::wanted_speed(const std::size_t index, const float facing, const float distance) noexcept
{
    const auto& movement = locomotor_.movements[index];
    auto factor = facing;
    // Turning in place is for starting from rest (Close_Enough_Angle_For_Move_Start); a unit on the
    // move turns while it walks (the owner's recording: the AT-AT swings 90 degrees mid-walk).
    if (turning_as_clips(index) || (movement.turn_in_place_angle > 0.0F && locomotor_.drive_speeds[index] <= 0.0F && facing < std::cos(movement.turn_in_place_angle)))
    {
        factor = 0.0F;
    }
    else if (movement.turn_speed_fraction > 0.0F)
    {
        factor = std::max(factor, movement.turn_speed_fraction);
    }
    auto wanted = travel_speed(index) * factor;
    // A walk played as clips plans its stop by its stride instead (walk_speed).
    if (movement.deceleration > 0.0F && movement.walk_stop_parts == 0)
    {
        // The speed it can still brake from to stop at the destination.
        const auto braking = std::sqrt(2.0F * movement.deceleration * std::max(distance, 0.0F));
        if (braking < wanted)
        {
            wanted = braking;
            locomotor_.arriving[index] = 1;
        }
    }
    return wanted;
}

float SimulationWorld::drive(const std::size_t index, const float wanted, const float dt) noexcept
{
    const auto& movement = locomotor_.movements[index];
    auto& speed = locomotor_.drive_speeds[index];
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
    const auto& movement = locomotor_.movements[index];
    return ground_mover_.follows_ground[index] == 0 && locomotor_.turn_rates[index] > 0.0F && movement.turn_speed_fraction > 0.0F && movement.stopped_turn_rate <= 0.0F;
}

// Within twice its slowest turn radius it slides straight to the destination at braking speed:
// circling at that radius, a mover always comes this close, so it cannot orbit its destination.
float SimulationWorld::settle_distance(const std::size_t index) const noexcept
{
    const auto turn_radius = locomotor_.movements[index].turn_speed_fraction * travel_speed(index) / locomotor_.turn_rates[index];
    return std::max(2.0F * turn_radius, arrival_distance);
}

// Flight: along the nose at between the minimum and maximum speed, turning toward the behavior
// layer's point at the turn rate (slowing toward the minimum while facing away) and pitching toward
// its height at the same rate, or holding heading and pitch when asked to. A half-loop pitches the
// nose up at the turn rate through the vertical until the flyer is inverted and heading back; it
// then flies level the other way (heading reversed) with a half roll left to unwind, which turns
// back to upright at twice the turn rate.
void SimulationWorld::fly(const std::size_t index, const float dt)
{
    const auto& intent = steering_.intents[index];
    auto& position = placement_.positions[index];
    auto& pitch = placement_.pitches[index];
    const auto slot = slot_of(index);
    auto& roll = flight_maneuvers_.get(slot, &FlightManeuverColumns::rolls);
    auto& looping = flight_maneuvers_.get(slot, &FlightManeuverColumns::looping);
    const auto heading_before = placement_.headings[index];
    const auto rate = locomotor_.turn_rates[index] > 0.0F ? locomotor_.turn_rates[index] : flight_max_pitch * 2.0F / std::max(dt, 1.0e-6F);
    auto factor = 1.0F;
    if (intent.loop && looping == 0)
    {
        looping = 1;
    }
    if (looping != 0)
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
    const auto speed = std::clamp(top * factor, std::min(flight_.min_speeds[index], top), top);
    const auto heading = placement_.headings[index];
    const auto level = speed * std::cos(pitch) * dt;
    position = {position.x + std::cos(heading) * level, position.y + std::sin(heading) * level, position.z + std::sin(pitch) * speed * dt};
    flight_maneuvers_.get(slot, &FlightManeuverColumns::yaw_rates) = dt > 0.0F ? wrap_angle(heading - heading_before) / dt : 0.0F;
    if (looping != 0 && pitch >= pi)
    {
        // Inverted at the top, heading back: the same attitude as level flight the other way rolled
        // upside down.
        looping = 0;
        placement_.headings[index] = wrap_angle(heading + pi);
        pitch = 0.0F;
        roll = pi;
    }
}

} // namespace eaw
