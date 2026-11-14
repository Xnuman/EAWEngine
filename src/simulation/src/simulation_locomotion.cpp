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
    measure_remaining_routes();
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

// Overlapping ground units are pushed apart, half each by default. Displacements are summed for
// every pair first and applied afterwards, so the result does not depend on which unit of a pair
// is visited first. Entities that cannot move are never pushed (they push the other unit the whole
// way); idle units yield more than moving ones, so a moving unit shoulders through instead of
// stopping. A push never moves a unit onto a cell its class may not enter, and it is limited per
// tick so packed groups spread out over a few ticks instead of jumping.
bool SimulationWorld::can_crush(const std::size_t crusher, const std::size_t victim) const
{
    const auto kind = crush_profile_of(crusher).crusher;
    const auto& squash = crush_profile_of(victim);
    const bool may = squash.squashable || (kind == Crusher::supercrusher && squash.squashable_by_supercrusher);
    const auto& combat = combat_.states[victim];
    return kind != Crusher::none && victim != crusher && may && combat.profile.health > 0.0F && combat.health > 0.0F && rules_.hostility.hostile(identity_.owners[crusher], identity_.owners[victim]);
}

void SimulationWorld::run_crushing()
{
    if (!ground_.map.valid())
    {
        return;
    }
    // shortcut: every moving crusher scans every entity; use the spatial grid when battles get big.
    for (std::size_t crusher = 0; crusher < identity_.ids.size(); ++crusher)
    {
        const auto kind = crush_profile_of(crusher).crusher;
        const auto& from = motion_.starts[crusher];
        const auto& at = placement_.positions[crusher];
        if (kind == Crusher::none || ground_mover_.follows_ground[crusher] == 0 || placement_.radii[crusher] <= 0.0F || (from.x == at.x && from.y == at.y))
        {
            continue;
        }
        for (std::size_t victim = 0; victim < identity_.ids.size(); ++victim)
        {
            if (!can_crush(crusher, victim))
            {
                continue;
            }
            if (std::hypot(placement_.positions[victim].x - at.x, placement_.positions[victim].y - at.y) < placement_.radii[crusher] && wound(victim, combat_.states[victim].health) > 0.0F)
            {
                ++crushed_;
                combat_.states[victim].crushed = true;
            }
        }
    }
}

void SimulationWorld::run_separation()
{
    separated_pairs_ = 0;
    // On land, ground units separate on the surface. Space ships do not push each other (they avoid
    // contact instead: plan_space_avoidance); only flyers separate there.
    const bool land = ground_.map.valid();
    // Flyers separate only from flyers of their owner (observed in the original: squadron mates push
    // each other apart), sideways so they keep their plane; overlap counts in 3D, so flyers passing
    // above or below each other do not push.
    const auto flyer = [&](const std::size_t index)
    {
        return placement_.radii[index] > 0.0F && flight_.min_speeds[index] > 0.0F;
    };
    const auto takes_part = [&](const std::size_t index)
    {
        return flyer(index) || (placement_.radii[index] > 0.0F && flight_.min_speeds[index] <= 0.0F && land && ground_mover_.follows_ground[index] != 0);
    };
    separation_push_.assign(identity_.ids.size(), Vector3{});
    float largest_radius = 0.0F;
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (takes_part(index))
        {
            largest_radius = std::max(largest_radius, placement_.radii[index]);
        }
    }
    grid_.build(placement_.positions, std::clamp(2.0F * largest_radius, separation_min_cell_size, grid_cell_size));
    // Remaining route length of every moving unit: of two moving units, the one further along
    // (less route left) has priority and yields much less, so units queue through a narrow
    // passage instead of shoving each other back and forth (Age of Empires II's priority rule).
    measure_remaining_routes();
    const auto weight = [&](const std::size_t index, const std::size_t other)
    {
        if (locomotor_.max_speeds[index] <= 0.0F)
        {
            return 0.0F;
        }
        // Only units of one owner make way for each other. An idle unit of another owner (an enemy
        // or a neutral) stands its ground against a moving one, which is held back instead; two
        // foreign units that both move or both stand split the push evenly.
        if (identity_.owners[index] != identity_.owners[other])
        {
            return !route_.move_targets[index] && route_.move_targets[other] ? 0.0F : 1.0F;
        }
        if (!route_.move_targets[index])
        {
            return 2.0F;
        }
        if (route_.move_targets[other] && locomotor_.max_speeds[other] > 0.0F && route_.remaining[index] != route_.remaining[other])
        {
            return route_.remaining[index] < route_.remaining[other] ? 0.25F : 1.75F;
        }
        return 1.0F;
    };
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (!takes_part(index))
        {
            continue;
        }
        query_.clear();
        grid_.query(placement_.positions[index], placement_.radii[index] + largest_radius, query_);
        // Each pair once, from its lower dense index, and only units that touch: whether they do comes
        // from positions and footprints alone (most neighbours a query finds do not), before anything
        // else about the other unit is read (separation is memory-bound: a crowd of 2,500 makes about
        // 125,000 candidate pairs a tick). The few left go in index order, so the pushes add up the
        // same whatever the grid's cells.
        const bool flying = flyer(index);
        const auto& here = placement_.positions[index];
        const auto own_radius = placement_.radii[index];
        std::erase_if(query_,
                      [&](const std::uint32_t other)
                      {
                          const auto reach = own_radius + placement_.radii[other];
                          const auto gap_x = placement_.positions[other].x - here.x;
                          const auto gap_y = placement_.positions[other].y - here.y;
                          return other <= index || gap_x * gap_x + gap_y * gap_y >= reach * reach;
                      });
        std::ranges::sort(query_);
        for (const auto other : query_)
        {
            if (!takes_part(other))
            {
                continue;
            }
            if (flying != flyer(other) || (flying && identity_.owners[index] != identity_.owners[other]))
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
            const auto dx = placement_.positions[other].x - placement_.positions[index].x;
            const auto dy = placement_.positions[other].y - placement_.positions[index].y;
            const auto dz = flying ? placement_.positions[other].z - placement_.positions[index].z : 0.0F;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            const auto overlap = placement_.radii[index] + placement_.radii[other] - std::sqrt(distance * distance + dz * dz);
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
                const auto angle = static_cast<float>((identity_.ids[index].index * 2654435761U + identity_.ids[other].index) % 6283U) / 1000.0F;
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
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        auto push = separation_push_[index];
        const auto length = std::sqrt(push.x * push.x + push.y * push.y);
        if (length <= 0.0F)
        {
            continue;
        }
        // At most half a radius, or the unit's own speed, per tick.
        const auto limit = std::max(placement_.radii[index] * 0.5F, locomotor_.max_speeds[index] * dt);
        if (length > limit)
        {
            push.x *= limit / length;
            push.y *= limit / length;
        }
        auto& position = placement_.positions[index];
        const auto next_x = position.x + push.x;
        const auto next_y = position.y + push.y;
        if (const auto allowed = ground_mover_.passable_types[index]; allowed != 0)
        {
            const auto there = ground_.map.passability_at(next_x, next_y);
            if (there && (*there & allowed) == 0)
            {
                continue;
            }
        }
        position = {next_x, next_y, land && !flyer(index) ? ground_.map.surface_height(next_x, next_y) : position.z};
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
