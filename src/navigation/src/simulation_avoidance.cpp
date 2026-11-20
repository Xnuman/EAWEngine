// Units and other units (movement-redesign branch: avoidance, giving way, detours, and the arrival
// rules among others are removed until the new design): navigation measures each ground unit's
// footprint into a grid, and movement never ends a step inside another unit (keep_apart, also what
// keeps space ships apart). A squad counts as one unit, the rectangle bounding its soldiers.

#include "math/scalar.hpp"
#include "core/profile.hpp"
#include "jobs/parallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace eaw {



bool NavigationSystem::ground_steers(const View& view, const std::size_t index) noexcept
{
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& route = view.get<RouteColumns>();
    const auto& radii = view.get<Avoidance>().radii;
    return index < radii.size() && radii[index] > 0.0F && route.move_targets[index] && !waits(view, index) && route.pending[index] == 0 && locomotor.max_speeds[index] > 0.0F &&
           locomotor.suspended[index] == 0;
}

Vector2 NavigationSystem::heading_toward_waypoint(const View& view, const std::size_t index) noexcept
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& route = view.get<RouteColumns>();
    const auto& path = route.paths[index];
    const auto& aim = path.empty() ? *route.move_targets[index] : path.back();
    const auto to = Vector2{aim.x - placement.positions[index].x, aim.y - placement.positions[index].y};
    return length(to) > arrival_distance ? normalize_or(to, {}) : Vector2{};
}

void NavigationSystem::measure_ground_units(const View& view)
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& ground = view.get<GroundState>();
    auto& avoidance = view.get<Avoidance>();
    if (!ground.map.valid())
    {
        return;
    }
    measure_agents(view);
    const auto& radii = avoidance.radii;
    avoidance.largest_radius = radii.empty() ? 0.0F : *std::ranges::max_element(radii);
    if (avoidance.largest_radius <= 0.0F)
    {
        return;
    }
    // By how far each unit reaches (its rectangle or circle), so a query reaches only as far as the
    // asking unit's own reach, however large other units are (as contact's grid).
    avoidance.grid.build(placement.positions, avoidance.reaches, avoidance_cell_size);
}

// Serial, in dense order, from the state at the start of the tick and the last tick's choices.
void NavigationSystem::plan_ground_avoidance(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& motion = view.get<MotionColumns>();
    auto& route = view.get<RouteColumns>();
    auto& avoidance = view.get<Avoidance>();
    route.avoid_velocities.resize(identity.ids.size());
    if (!view.get<GroundState>().map.valid() || avoidance.largest_radius <= 0.0F)
    {
        std::ranges::fill(route.avoid_velocities, std::nullopt);
        return;
    }
    const auto& radii = avoidance.radii;
    const auto step = static_cast<float>(view.get<Clock>().tick_seconds);
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        const auto last = std::exchange(route.avoid_velocities[index], std::nullopt);
        if (!ground_steers(view, index))
        {
            continue;
        }
        const auto heading = heading_toward_waypoint(view, index);
        if (heading == Vector2{})
        {
            continue;
        }
        const Vector2 here{placement.positions[index].x, placement.positions[index].y};
        const auto radius = radii[index];
        const auto speed = travel_speed(view, index);
        // On its last leg it means to stop at its goal: no faster than reaches it within the horizon.
        auto wanted = speed;
        if (route.paths[index].empty())
        {
            wanted = std::min(speed, distance_xy(*route.move_targets[index], placement.positions[index]) / avoidance_horizon_seconds);
        }
        const auto preferred = heading * wanted;
        // Its nearest ground units, nearest first (ties by dense index); not what it crushes.
        query_.clear();
        avoidance.grid.query_overlapping(placement.positions[index], avoidance.reaches[index] + std::min(speed * avoidance_horizon_seconds, radius * avoidance_reach_radii), query_);
        nearest_.clear();
        for (const auto other : query_)
        {
            if (other == index || radii[other] <= 0.0F || can_crush(view, index, avoidance.crush_proxies[other]))
            {
                continue;
            }
            const auto offset = Vector2{placement.positions[other].x, placement.positions[other].y} - here;
            nearest_.emplace_back(dot(offset, offset), other);
        }
        std::ranges::sort(nearest_);
        nearest_.resize(std::min(nearest_.size(), avoidance_neighbours));
        orca_lines_.clear();
        for (const auto& [distance_squared, other] : nearest_)
        {
            const auto offset = Vector2{placement.positions[other].x, placement.positions[other].y} - here;
            // A standing unit or one of another owner does not make way: this unit takes the whole
            // avoidance, from the velocity it prefers. Two moving allies share it from their current
            // velocities: in a formation by priority (the higher takes less: the owner's "who waits
            // for whom" made continuous), else by footprint area (the larger gives way less).
            auto share = 1.0F;
            auto own = preferred;
            if (ground_steers(view, other) && identity.owners[other] == identity.owners[index])
            {
                const auto mine = static_cast<float>(route.priorities[index]);
                const auto theirs = static_cast<float>(route.priorities[other]);
                const bool ranked = route.orders[index] != 0 && route.orders[index] == route.orders[other] && mine > 0.0F && theirs > 0.0F;
                share = ranked ? mine / (mine + theirs) : radii[other] * radii[other] / (radius * radius + radii[other] * radii[other]);
                own = motion.velocities[index];
            }
            const auto room = agent_room(view, index, placement.positions[index], other, placement.positions[other]);
            orca_lines_.push_back(orca_line(offset, own - motion.velocities[other], room - contact_tolerance, avoidance_horizon_seconds, step, share, own));
        }
        if (orca_lines_.empty())
        {
            continue;
        }
        auto chosen = orca_velocity(orca_lines_, speed, preferred);
        if (chosen != preferred)
        {
            const auto turn_cos = std::cos(avoidance_keep_right);
            const auto turn_sin = std::sin(avoidance_keep_right);
            chosen = orca_velocity(orca_lines_, speed, {preferred.x * turn_cos + preferred.y * turn_sin, preferred.y * turn_cos - preferred.x * turn_sin});
        }
        // Smoothed against the last tick's choice (or the way it went without one).
        const auto before = last ? *last : preferred;
        chosen = before + (chosen - before) * avoidance_smoothing;
        // Slower than a crawl: it stands still rather than turning toward a velocity whose direction
        // is noise (near its goal, among others, those directions flipped every tick).
        if (length(chosen) < avoidance_crawl_fraction * wanted)
        {
            chosen = {};
        }
        if (length(chosen - preferred) > avoidance_tolerance * speed)
        {
            route.avoid_velocities[index] = chosen;
        }
    }
}

bool MovementSystem::avoid_step(const View& view, const std::size_t index, const Vector2 velocity, const float dt)
{
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& ground = view.get<GroundState>();
    auto& position = placement.positions[index];
    const auto wanted = length(velocity);
    if (wanted <= 0.0F)
    {
        // Nowhere to go this tick: it slows to a stop where it is.
        (void)drive(view, index, 0.0F, dt);
        return true;
    }
    // Whether the longest step this way fits the ground, before the unit turns or speeds up.
    const auto longest = std::min(travel_speed(view, index), wanted) * dt / wanted;
    if (!stands_at(view, index, position.x + velocity.x * longest, position.y + velocity.y * longest) && stands_at(view, index, position.x, position.y))
    {
        return false;
    }
    const auto facing = steer(view, index, std::atan2(velocity.y, velocity.x), dt);
    const auto distance = std::min(move_speed(view, index, facing, route.remaining[index], dt), wanted) * dt;
    const auto moved = keep_apart(view, index, {velocity.x / wanted * distance, velocity.y / wanted * distance, 0.0F});
    const auto next_x = position.x + moved.x;
    const auto next_y = position.y + moved.y;
    if (stands_at(view, index, next_x, next_y) || !stands_at(view, index, position.x, position.y))
    {
        position = {next_x, next_y, ground.map.surface_height(next_x, next_y)};
    }
    return true;
}

Vector3 MovementSystem::keep_apart(const View& view, const std::size_t index, Vector3 step)
{
    auto& placement = view.get<PlacementColumns>();
    auto& ground = view.get<GroundState>();
    auto& avoidance = view.get<Avoidance>();
    const bool land = ground.map.valid();
    // On land the agents' avoidance radii (a squad's extent; 0 for its soldiers), in space footprints.
    const auto radius_of = [&](const std::size_t entity)
    {
        return land ? (entity < avoidance.radii.size() ? avoidance.radii[entity] : 0.0F) : placement.radii[entity];
    };
    const float radius = radius_of(index);
    if (avoidance.largest_radius <= 0.0F || radius <= 0.0F || flies(view, index))
    {
        return step;
    }
    const auto& here = placement.positions[index];
    const auto& route = view.get<RouteColumns>();
    // On land, a unit of a formation keeps a margin from a higher-priority unit of its order that is
    // still on its way: it holds before contact (priority_wait_margin).
    const auto margin_to = [&](const std::uint32_t other)
    {
        const auto order = route.orders[index];
        const auto priority = route.priorities[index];
        const bool yields =
            land && order != 0 && priority != 0 && route.orders[other] == order && route.priorities[other] != 0 && route.priorities[other] < priority && route.move_targets[other].has_value();
        return yields ? radius * priority_wait_margin : 0.0F;
    };
    query_.clear();
    // On land the grid is built by footprint (plan_ground_avoidance); in space by center.
    if (land)
    {
        avoidance.grid.query_overlapping(here, avoidance.reaches[index] + std::hypot(step.x, step.y) + radius * priority_wait_margin, query_);
    }
    else
    {
        avoidance.grid.query(here, radius + avoidance.largest_radius + std::hypot(step.x, step.y), query_);
    }
    // Whether a step ends inside a unit it moves toward (deeper than it was), or on land within the
    // unit's margin of it. On land: ground units, not what this one crushes; in space: ships at its
    // height, not much smaller.
    const auto deeper = [&](const std::uint32_t other, const Vector3& candidate, float& to_x, float& to_y, float& apart)
    {
        const auto& there = placement.positions[other];
        const auto other_radius = radius_of(other);
        if (other == index || other_radius <= 0.0F || flies(view, other))
        {
            return false;
        }
        if (land ? can_crush(view, index, avoidance.crush_proxies[other]) : other_radius < radius * avoid_small_fraction || std::abs(there.z - here.z) > radius + other_radius)
        {
            return false;
        }
        const Vector3 next{here.x + candidate.x, here.y + candidate.y, here.z};
        const auto next_x = next.x - there.x;
        const auto next_y = next.y - there.y;
        const auto apart_squared = next_x * next_x + next_y * next_y;
        const auto margin = margin_to(other);
        // Farther apart than both reach: clear, without measuring their extents (squared, no root).
        const auto most = land ? avoidance.reaches[index] + avoidance.reaches[other] + margin : radius + other_radius;
        if (apart_squared >= most * most)
        {
            return false;
        }
        const auto room = land ? agent_room(view, index, next, other, there) + margin : radius + other_radius;
        if (apart_squared >= room * room)
        {
            return false;
        }
        to_x = there.x - here.x;
        to_y = there.y - here.y;
        apart = std::hypot(to_x, to_y);
        if (land)
        {
            // Deeper means more overlap than before the step: a unit already inside another's room (a
            // rectangle that turned swings its corners out) may still move along it or away from it.
            // The tolerance (world units) keeps rounding from counting as deeper; an engine choice.
            constexpr float deeper_tolerance = 1.0e-3F;
            const auto overlap_before = std::max(0.0F, agent_room(view, index, here, other, there) + margin - apart);
            return room - std::sqrt(apart_squared) > overlap_before + deeper_tolerance;
        }
        return apart > 0.0F && candidate.x * to_x + candidate.y * to_y > 0.0F;
    };
    if (land)
    {
        // The new movement of a single unit, step 7: a ground unit stops at a unit in its way instead
        // of sliding along it; navigation decides in the next tick whether it waits or goes around.
        for (const auto other : query_)
        {
            float to_x = 0.0F;
            float to_y = 0.0F;
            float apart = 0.0F;
            if (deeper(other, step, to_x, to_y, apart))
            {
                view.get<RouteColumns>().blockers[index] = view.get<IdentityColumns>().ids[other];
                return {0.0F, 0.0F, step.z};
            }
        }
        return step;
    }
    // Twice, so a ship wedged between two slides clear of both where it can.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto other : query_)
        {
            float to_x = 0.0F;
            float to_y = 0.0F;
            float apart = 0.0F;
            if (deeper(other, step, to_x, to_y, apart))
            {
                const float along = (step.x * to_x + step.y * to_y) / apart;
                step.x -= to_x / apart * along;
                step.y -= to_y / apart * along;
            }
        }
    }
    // Wedged in where no slide is free: it holds its place this tick.
    for (const auto other : query_)
    {
        float to_x = 0.0F;
        float to_y = 0.0F;
        float apart = 0.0F;
        if (deeper(other, step, to_x, to_y, apart))
        {
            return {0.0F, 0.0F, step.z};
        }
    }
    return step;
}

} // namespace eaw
