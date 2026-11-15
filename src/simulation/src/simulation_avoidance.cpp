// Avoidance (docs/pathfinding-design.md, 4.4): before movement, navigation asks allies in a moving
// ground unit's way to give way and picks each moving ground unit's velocity for the tick by ORCA,
// both from the state at the start of the tick, so the plans of a tick do not depend on the order
// units are planned in; movement follows them and never ends a step inside another unit (keep_apart,
// also what keeps space ships apart). Nothing pushes a ground unit. A squad avoids as one agent, the
// circle of its extent; its soldiers hold their places in it (the owner, 2026-11-15: they keep their
// arrangement while walking).

#include "simulation_internal.hpp"

#include "simulation/orca.hpp"

#include <algorithm>
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
                avoidance.crush_proxies[index] = static_cast<std::uint32_t>(*first);
            }
        }
        else if (members.find(slot, &SquadMemberColumns::states) == nullptr && ground_body(view, index))
        {
            avoidance.radii[index] = placement.radii[index];
        }
    }
}

void SimulationWorld::NavigationSystem::measure_arrival_progress(const View& view)
{
    auto& route = view.get<RouteColumns>();
    for (std::size_t index = 0; index < route.move_targets.size(); ++index)
    {
        // Not on a last leg, or following a frame's moving slot (its distance never shrinks there).
        if (!route.move_targets[index] || !route.paths[index].empty() || in_moving_frame(view, index))
        {
            route.closest_approaches[index] = std::numeric_limits<float>::max();
            route.stalled_ticks[index] = 0;
        }
        else if (route.remaining[index] < route.closest_approaches[index] - arrival_progress)
        {
            route.closest_approaches[index] = route.remaining[index];
            route.stalled_ticks[index] = 0;
        }
        else
        {
            ++route.stalled_ticks[index];
        }
    }
}

bool SimulationWorld::NavigationSystem::ground_steers(const View& view, const std::size_t index) noexcept
{
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& route = view.get<RouteColumns>();
    const auto& radii = view.get<Avoidance>().radii;
    return index < radii.size() && radii[index] > 0.0F && route.move_targets[index] && !route.give_ways[index] && !waits(view, index) && route.pending[index] == 0 &&
           locomotor.max_speeds[index] > 0.0F && locomotor.suspended[index] == 0;
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

void SimulationWorld::NavigationSystem::plan_ground_avoidance(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& motion = view.get<MotionColumns>();
    const auto& ground = view.get<GroundState>();
    auto& route = view.get<RouteColumns>();
    auto& avoidance = view.get<Avoidance>();
    route.avoid_velocities.assign(identity.ids.size(), std::nullopt);
    if (!ground.map.valid())
    {
        return;
    }
    measure_agents(view);
    const auto& radii = avoidance.radii;
    const auto steers = [&](const std::size_t index)
    {
        return ground_steers(view, index);
    };
    avoidance.largest_radius = radii.empty() ? 0.0F : *std::ranges::max_element(radii);
    if (avoidance.largest_radius <= 0.0F)
    {
        return;
    }
    // By avoidance radius, so a query reaches only as far as the asking agent's own reach, however
    // large other agents are (as contact's grid).
    avoidance.grid.build(placement.positions, radii, avoidance_cell_size);
    plan_giving_way(view);
    const auto step = static_cast<float>(view.get<Clock>().tick_seconds);
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (!steers(index))
        {
            continue;
        }
        const Vector2 here{placement.positions[index].x, placement.positions[index].y};
        const auto heading = heading_toward_waypoint(view, index);
        if (heading == Vector2{})
        {
            continue;
        }
        const auto radius = radii[index];
        const auto speed = travel_speed(view, index);
        // On its last leg it means to stop at its goal: no faster than reaches the goal within the
        // horizon, so a goal beside a standing unit is not taken for a collision beyond it (a formation
        // member stood still ten units short of its slot for that).
        auto wanted = speed;
        if (route.paths[index].empty())
        {
            const auto& goal = *route.move_targets[index];
            wanted = std::min(speed, distance_xy(goal, placement.positions[index]) / avoidance_horizon_seconds);
        }
        const auto preferred = heading * wanted;
        // Its nearest ground units, nearest first (ties by dense index); not what it crushes.
        query_.clear();
        avoidance.grid.query_overlapping(placement.positions[index], radius + std::min(speed * avoidance_horizon_seconds, radius * avoidance_reach_radii), query_);
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
        lines_.clear();
        for (const auto& [distance_squared, other] : nearest_)
        {
            const auto offset = Vector2{placement.positions[other].x, placement.positions[other].y} - here;
            // Two moving allies share the avoidance, each from its current velocity (ORCA's rule, so
            // both correct alike). A standing unit or a unit of another owner does not make way
            // (enemies never give way; giving way by standing allies is its own rule): this unit takes
            // the whole avoidance, from the velocity it prefers, so it goes around instead of slowing
            // in front of it (the constraint at the current velocity of a unit starting from rest only
            // slows it down).
            auto share = 1.0F;
            auto own = preferred;
            if (steers(other) && identity.owners[other] == identity.owners[index])
            {
                const auto mine = radius * radius;
                const auto theirs = radii[other] * radii[other];
                share = theirs / (mine + theirs);
                own = motion.velocities[index];
            }
            lines_.push_back(orca_line(offset, own - motion.velocities[other], radius + radii[other] - contact_tolerance, avoidance_horizon_seconds, step, share, own));
        }
        if (lines_.empty())
        {
            continue;
        }
        // A unit that cannot go its way prefers to turn a little to its right, so two meeting head-on
        // pass each other (as space ships keep right) instead of stopping nose to nose, where the
        // allowed velocity nearest straight ahead is standing still.
        auto chosen = orca_velocity(lines_, speed, preferred);
        if (chosen != preferred)
        {
            const auto turn_cos = std::cos(avoidance_keep_right);
            const auto turn_sin = std::sin(avoidance_keep_right);
            chosen = orca_velocity(lines_, speed, {preferred.x * turn_cos + preferred.y * turn_sin, preferred.y * turn_cos - preferred.x * turn_sin});
        }
        if (length(chosen - preferred) > avoidance_tolerance * speed)
        {
            route.avoid_velocities[index] = chosen;
        }
    }
}

void SimulationWorld::NavigationSystem::plan_giving_way(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& motion = view.get<MotionColumns>();
    const auto& ground = view.get<GroundState>();
    auto& route = view.get<RouteColumns>();
    auto& avoidance = view.get<Avoidance>();
    const auto& radii = avoidance.radii;
    const auto limit = ticks_for(view, give_way_seconds);
    // Ending: the passer has passed (it is beyond the unit along its way), is gone or stopped, or took
    // too long. A standing unit then walks back; a moving one goes on along its route.
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        auto& way = route.give_ways[index];
        if (!way || way->returning)
        {
            continue;
        }
        ++way->ticks;
        const auto passer = dense_index(view, way->passer);
        bool done = !passer || !ground_steers(view, *passer) || way->ticks > limit;
        if (!done)
        {
            const auto heading = heading_toward_waypoint(view, *passer);
            const auto behind = Vector2{placement.positions[index].x - placement.positions[*passer].x, placement.positions[index].y - placement.positions[*passer].y};
            done = dot(behind, heading) < -(radii[index] + radii[*passer]);
        }
        if (!done)
        {
            continue;
        }
        if (route.move_targets[index])
        {
            way.reset();
        }
        else
        {
            way->returning = true;
        }
    }
    // Asking: an ally in a moving unit's corridor (its footprint swept ahead) steps sideways out of
    // it, to the side of the mover's line it stands on (right when dead ahead), just far enough (the
    // owner's drawing, 2026-11-15); the other side when that side has no room. A moving ally gives way
    // when it is slower by nature (its top speed, not this tick's: in a queue every unit is slow for a
    // while), when it is all but at its destination (less route left than the two footprints), which
    // then counts as standing there, or when it is behind the mover in the flow (more route left, ties
    // by dense index; the priority Age of Empires II gives the unit further along, so a queue's leader
    // never stands aside for those behind it, and of two units wedged at a passage's mouth the one
    // nearer goes through first).
    requests_.clear();
    for (std::size_t passer = 0; passer < identity.ids.size(); ++passer)
    {
        // Only a unit held up asks (slower last tick than give_way_held_fraction of its speed): one
        // that flows past its allies disturbs no one, and one catching up with slower allies is held
        // up by avoidance first.
        const auto speed = travel_speed(view, passer);
        if (!ground_steers(view, passer) || length(motion.velocities[passer]) >= speed * give_way_held_fraction)
        {
            continue;
        }
        const auto heading = heading_toward_waypoint(view, passer);
        if (heading == Vector2{})
        {
            continue;
        }
        const auto own_radius = radii[passer];
        const auto ahead = std::min(speed * give_way_lookahead_seconds, own_radius * avoidance_reach_radii);
        // The unit in its way: the nearest ground unit in its corridor (ties by dense index). Only that
        // one is asked; the ones beyond it are not what holds the mover up.
        query_.clear();
        avoidance.grid.query_overlapping(placement.positions[passer], own_radius + ahead, query_);
        std::optional<std::uint32_t> blocker;
        auto nearest = std::numeric_limits<float>::max();
        for (const auto other : query_)
        {
            if (other == passer || radii[other] <= 0.0F)
            {
                continue;
            }
            const auto offset = Vector2{placement.positions[other].x - placement.positions[passer].x, placement.positions[other].y - placement.positions[passer].y};
            const auto along = dot(offset, heading);
            const auto reach = own_radius + radii[other];
            if (along > 0.0F && along <= reach + ahead && std::fabs(cross(heading, offset)) < reach && along < nearest)
            {
                nearest = along;
                blocker = other;
            }
        }
        if (blocker)
        {
            const auto yielder = *blocker;
            if (identity.owners[yielder] != identity.owners[passer] || (route.give_ways[yielder] && !route.give_ways[yielder]->returning) || locomotor.max_speeds[yielder] <= 0.0F ||
                locomotor.suspended[yielder] != 0)
            {
                continue;
            }
            // Members of one formation on its way keep their slots; they do not ask each other aside
            // (they did, over and over, and the frame held for the members giving way). Once it has
            // arrived they sort into their final slots and may.
            const auto& groups = view.get<ComponentPool<GroupColumns>>();
            const auto passer_group = groups.find(slot_of(view, passer));
            const auto yielder_group = groups.find(slot_of(view, yielder));
            if (passer_group && yielder_group && groups.columns.ids[*passer_group] == groups.columns.ids[*yielder_group] && in_moving_frame(view, passer))
            {
                continue;
            }
            const auto offset = Vector2{placement.positions[yielder].x - placement.positions[passer].x, placement.positions[yielder].y - placement.positions[passer].y};
            const auto reach = own_radius + radii[yielder];
            // A standing unit on its destination does not make way: the mover stops at it (goal_taken).
            // Units merely near the destination do (a later formation member reaches its slot past
            // members already in theirs).
            const auto& goal = *route.move_targets[passer];
            if (route.paths[passer].empty() && !ground_steers(view, yielder) && std::hypot(goal.x - placement.positions[yielder].x, goal.y - placement.positions[yielder].y) < reach)
            {
                continue;
            }
            const auto aside = cross(heading, offset);
            const auto& yielder_goal = route.move_targets[yielder];
            const bool arriving =
                yielder_goal && route.paths[yielder].empty() && std::hypot(yielder_goal->x - placement.positions[yielder].x, yielder_goal->y - placement.positions[yielder].y) < reach;
            if (ground_steers(view, yielder) && travel_speed(view, yielder) >= speed * give_way_slower_fraction && !arriving &&
                (route.remaining[yielder] < route.remaining[passer] || (route.remaining[yielder] == route.remaining[passer] && yielder < passer)))
            {
                continue;
            }
            const Vector2 left{-heading.y, heading.x};
            const auto clear = own_radius + radii[yielder] * (1.0F + give_way_margin);
            const auto own_side = aside > 0.0F ? 1.0F : -1.0F;
            for (const auto side : {own_side, -own_side})
            {
                const auto shift = side == own_side ? clear - std::fabs(aside) : clear + std::fabs(aside);
                const auto x = placement.positions[yielder].x + left.x * side * shift;
                const auto y = placement.positions[yielder].y + left.y * side * shift;
                if (stands_at(view, yielder, x, y) && room_at(view, yielder, passer, x, y))
                {
                    requests_.push_back({static_cast<std::uint32_t>(yielder), static_cast<std::uint32_t>(passer), {x, y, ground.map.surface_height(x, y)}});
                    break;
                }
            }
        }
    }
    // One passer per unit, the lowest, so the outcome does not depend on the order of the scan.
    std::ranges::sort(requests_,
                      [](const GiveWayRequest& a, const GiveWayRequest& b)
                      {
                          return a.yielder != b.yielder ? a.yielder < b.yielder : a.passer < b.passer;
                      });
    for (std::size_t request = 0; request < requests_.size(); ++request)
    {
        const auto& asked = requests_[request];
        if (request > 0 && requests_[request - 1].yielder == asked.yielder)
        {
            continue;
        }
        auto& way = route.give_ways[asked.yielder];
        const auto back = way ? way->back : placement.positions[asked.yielder];
        way = GiveWay{.passer = identity.ids[asked.passer], .aside = asked.aside, .back = back};
    }
}

bool SimulationWorld::NavigationSystem::room_at(const View& view, const std::size_t yielder, const std::size_t passer, const float x, const float y)
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& radii = view.get<Avoidance>().radii;
    const auto& avoidance = view.get<Avoidance>();
    const auto radius = radii[yielder];
    room_query_.clear();
    avoidance.grid.query_overlapping({x, y, 0.0F}, radius, room_query_);
    return std::ranges::none_of(room_query_,
                                [&](const std::uint32_t other)
                                {
                                    return other != yielder && other != passer && radii[other] > 0.0F &&
                                           std::hypot(placement.positions[other].x - x, placement.positions[other].y - y) < radius + radii[other];
                                });
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
    query_.clear();
    // On land the grid is built by footprint (plan_ground_avoidance); in space by center.
    if (land)
    {
        avoidance.grid.query_overlapping(here, radius + std::hypot(step.x, step.y), query_);
    }
    else
    {
        avoidance.grid.query(here, radius + avoidance.largest_radius + std::hypot(step.x, step.y), query_);
    }
    // Whether a step ends inside a unit it moves toward (deeper than it was). On land: ground units,
    // not what this one crushes; in space: ships at its height, not much smaller.
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
        if (std::hypot(here.x + candidate.x - there.x, here.y + candidate.y - there.y) >= radius + other_radius)
        {
            return false;
        }
        to_x = there.x - here.x;
        to_y = there.y - here.y;
        apart = std::hypot(to_x, to_y);
        return apart > 0.0F && candidate.x * to_x + candidate.y * to_y > 0.0F;
    };
    // Twice, so a unit wedged between two slides clear of both where it can.
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

bool SimulationWorld::MovementSystem::avoid_step(const View& view, const std::size_t index, const Vector2 velocity, const float dt)
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

bool SimulationWorld::MovementSystem::goal_taken(const View& view, const std::size_t index)
{
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& avoidance = view.get<Avoidance>();
    const auto& radii = avoidance.radii;
    const auto radius = index < radii.size() ? radii[index] : 0.0F;
    if (!route.paths[index].empty() || avoidance.largest_radius <= 0.0F || radius <= 0.0F)
    {
        return false;
    }
    // A member of a formation on its way follows a moving slot: its frame decides where it stops.
    if (in_moving_frame(view, index))
    {
        return false;
    }
    const auto& goal = *route.move_targets[index];
    const auto& here = placement.positions[index];
    const auto own_distance = std::hypot(goal.x - here.x, goal.y - here.y);
    // It has come no closer for a while: a unit still working its way through a crowd is not stopped.
    const bool stalled = route.stalled_ticks[index] >= ticks_for(view, arrival_stall_seconds);
    // Held within its footprint's radius of its goal (by the units around it): arrived.
    if (stalled && own_distance <= radius * arrival_reach_share)
    {
        return true;
    }
    const auto owner = view.get<IdentityColumns>().owners[index];
    query_.clear();
    avoidance.grid.query_overlapping(here, radius * 5.0F + avoidance.largest_radius * taken_goal_margin, query_);
    return std::ranges::any_of(query_,
                               [&](const std::uint32_t other)
                               {
                                   if (other == index || radii[other] <= 0.0F)
                                   {
                                       return false;
                                   }
                                   const auto& there = placement.positions[other];
                                   const auto reach = radius + radii[other];
                                   const auto from_goal = std::hypot(goal.x - there.x, goal.y - there.y);
                                   const auto apart = std::hypot(here.x - there.x, here.y - there.y);
                                   const bool standing = !route.move_targets[other];
                                   // Its goal under a standing unit it touches.
                                   if (standing && from_goal < reach && apart <= reach * (1.0F + taken_goal_margin))
                                   {
                                       return true;
                                   }
                                   // Stalled at a crowd of its allies that stopped at the end of the same order, or at
                                   // destinations near its own (within taken_goal_crowd of their summed radii: units sent
                                   // to one point, or to neighbouring slots), between it and
                                   // its goal, within two of its own footprints' widths of them (a crowd grows ring by ring,
                                   // rings a little apart where avoidance kept its margins:
                                   // each ring stops at the ring that stopped before it). An ally that stopped elsewhere is
                                   // no crowd at this goal: a unit held behind it waits for it to give way (the owner's
                                   // report: companies sent through a standing squad stopped at it). A jam of moving units
                                   // is no crowd either.
                                   const auto& stopped_at = route.last_goals[other];
                                   const auto& serials = view.get<OrderColumns>().command_serials;
                                   const bool same_order = serials[other] == serials[index];
                                   return stalled && standing && stopped_at && (same_order || distance_xy(*stopped_at, goal) < reach * taken_goal_crowd) &&
                                          view.get<IdentityColumns>().owners[other] == owner && from_goal < own_distance && apart <= reach + 4.0F * radius;
                               });
}

void SimulationWorld::MovementSystem::give_way_step(const View& view, const std::size_t index, const float dt)
{
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& ground = view.get<GroundState>();
    auto& way = route.give_ways[index];
    auto& position = placement.positions[index];
    const auto& goal = way->returning ? way->back : way->aside;
    const Vector2 to{goal.x - position.x, goal.y - position.y};
    const auto distance = length(to);
    if (distance <= arrival_distance)
    {
        (void)drive(view, index, 0.0F, dt);
        if (way->returning)
        {
            way.reset();
        }
        return;
    }
    const auto facing = steer(view, index, std::atan2(to.y, to.x), dt);
    const auto travel = std::min(move_speed(view, index, facing, distance, dt) * dt, distance);
    const auto moved = keep_apart(view, index, {to.x / distance * travel, to.y / distance * travel, 0.0F});
    if (moved.x == 0.0F && moved.y == 0.0F)
    {
        // Units stand in its way aside (it cannot make room) or back (its spot is taken): it stays
        // where it is instead of standing frozen in the crowd until giving way times out.
        way.reset();
        return;
    }
    const auto next_x = position.x + moved.x;
    const auto next_y = position.y + moved.y;
    if (!stands_at(view, index, next_x, next_y) && stands_at(view, index, position.x, position.y))
    {
        return;
    }
    position = {next_x, next_y, ground.map.surface_height(next_x, next_y)};
}

} // namespace eaw
