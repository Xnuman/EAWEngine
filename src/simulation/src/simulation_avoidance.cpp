// Units and other units (movement-redesign branch: avoidance, giving way, detours, and the arrival
// rules among others are removed until the new design): navigation measures each ground unit's
// footprint into a grid, and movement never ends a step inside another unit (keep_apart, also what
// keeps space ships apart). A squad counts as one unit, the rectangle bounding its soldiers.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"
#include "core/profile.hpp"
#include "jobs/parallel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::NavigationSystem::measure_agents(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& ground_mover = view.get<GroundMoverColumns>();
    const auto& squads = view.get<ComponentPool<SquadColumns>>();
    const auto& members = view.get<ComponentPool<SquadMemberColumns>>();
    auto& avoidance = view.get<Avoidance>();
    avoidance.radii.assign(identity.ids.size(), 0.0F);
    avoidance.bodies.assign(identity.ids.size(), FootprintRect{});
    avoidance.crush_proxies.resize(identity.ids.size());
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        avoidance.crush_proxies[index] = static_cast<std::uint32_t>(index);
        const auto slot = slot_of(view, index);
        if (const auto* squad = squads.find(slot, &SquadColumns::states))
        {
            const auto first = squad->members.empty() ? std::nullopt : dense_index(view, squad->members.front());
            if (first && ground_mover.follows_ground[index] != 0 && !flies(view, index))
            {
                avoidance.radii[index] = squad->extent;
                // The rectangle bounding its soldiers (the owner's squads, pathfinding-design.md 4.7).
                avoidance.bodies[index] = squad->body;
                avoidance.crush_proxies[index] = static_cast<std::uint32_t>(*first);
            }
        }
        else if (members.find(slot, &SquadMemberColumns::states) == nullptr && ground_body(view, index))
        {
            avoidance.radii[index] = placement.radii[index];
            avoidance.bodies[index] = placement.bodies[index];
        }
    }
    avoidance.reaches.resize(identity.ids.size());
    avoidance.facings.resize(identity.ids.size());
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        avoidance.reaches[index] = avoidance.radii[index] > 0.0F ? footprint_reach(avoidance.bodies[index], avoidance.radii[index]) : 0.0F;
        avoidance.facings[index] = avoidance.bodies[index].half_width > 0.0F ? facing_of(placement.headings[index]) : Vector2{1.0F, 0.0F};
    }
}

bool SimulationWorld::NavigationSystem::ground_steers(const View& view, const std::size_t index) noexcept
{
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& route = view.get<RouteColumns>();
    const auto& radii = view.get<Avoidance>().radii;
    return index < radii.size() && radii[index] > 0.0F && route.move_targets[index] && !waits(view, index) && route.pending[index] == 0 && locomotor.max_speeds[index] > 0.0F &&
           locomotor.suspended[index] == 0;
}

Vector2 SimulationWorld::NavigationSystem::heading_toward_waypoint(const View& view, const std::size_t index) noexcept
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& route = view.get<RouteColumns>();
    const auto& path = route.paths[index];
    const auto& aim = path.empty() ? *route.move_targets[index] : path.back();
    const auto to = Vector2{aim.x - placement.positions[index].x, aim.y - placement.positions[index].y};
    return length(to) > arrival_distance ? normalize_or(to, {}) : Vector2{};
}

void SimulationWorld::NavigationSystem::measure_ground_units(const View& view)
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

Vector3 SimulationWorld::MovementSystem::keep_apart(const View& view, const std::size_t index, Vector3 step)
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
