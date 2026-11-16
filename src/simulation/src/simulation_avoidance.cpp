// Avoidance (docs/pathfinding-design.md, 4.4): before movement, navigation asks allies in a moving
// ground unit's way to give way and picks each moving ground unit's velocity for the tick by ORCA,
// both from the state at the start of the tick, so the plans of a tick do not depend on the order
// units are planned in; movement follows them and never ends a step inside another unit (keep_apart,
// also what keeps space ships apart). Nothing pushes a ground unit. A squad avoids as one agent, the
// circle of its extent; its soldiers hold their places in it (the owner, 2026-11-15: they keep their
// arrangement while walking).

#include "simulation_internal.hpp"

#include "math/scalar.hpp"
#include "core/profile.hpp"
#include "jobs/parallel.hpp"
#include "simulation/orca.hpp"

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
    {
        EAW_PROFILE_ZONE("giving way");
        plan_giving_way(view);
    }
    {
        EAW_PROFILE_ZONE("detours");
        plan_detours(view);
    }
    EAW_PROFILE_ZONE("ground avoidance");
    const auto step = static_cast<float>(view.get<Clock>().tick_seconds);
    // Each unit's velocity from the state at the start of the tick into its own entry: units are
    // planned in chunks on the job system, each chunk with its own scratch, and the result does not
    // depend on how they are split (docs/ecs-design.md, 7.5; check-battle compares a serial run).
    const auto plan = [&](const std::size_t index, AvoidanceScratch& scratch)
    {
        if (!steers(index))
        {
            return;
        }
        const Vector2 here{placement.positions[index].x, placement.positions[index].y};
        const auto heading = heading_toward_waypoint(view, index);
        if (heading == Vector2{})
        {
            return;
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
        auto& query = scratch.query;
        auto& nearest = scratch.nearest;
        auto& lines = scratch.lines;
        query.clear();
        avoidance.grid.query_overlapping(placement.positions[index], radius + std::min(speed * avoidance_horizon_seconds, radius * avoidance_reach_radii), query);
        nearest.clear();
        for (const auto other : query)
        {
            if (other == index || radii[other] <= 0.0F || can_crush(view, index, avoidance.crush_proxies[other]))
            {
                continue;
            }
            const auto offset = Vector2{placement.positions[other].x, placement.positions[other].y} - here;
            nearest.emplace_back(dot(offset, offset), other);
        }
        std::ranges::sort(nearest);
        nearest.resize(std::min(nearest.size(), avoidance_neighbours));
        lines.clear();
        for (const auto& [distance_squared, other] : nearest)
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
            lines.push_back(orca_line(offset, own - motion.velocities[other], radius + radii[other] - contact_tolerance, avoidance_horizon_seconds, step, share, own));
        }
        if (lines.empty())
        {
            return;
        }
        // A unit that cannot go its way prefers to turn a little to its right, so two meeting head-on
        // pass each other (as space ships keep right) instead of stopping nose to nose, where the
        // allowed velocity nearest straight ahead is standing still.
        auto chosen = orca_velocity(lines, speed, preferred);
        if (chosen != preferred)
        {
            const auto turn_cos = std::cos(avoidance_keep_right);
            const auto turn_sin = std::sin(avoidance_keep_right);
            chosen = orca_velocity(lines, speed, {preferred.x * turn_cos + preferred.y * turn_sin, preferred.y * turn_cos - preferred.x * turn_sin});
        }
        if (length(chosen - preferred) > avoidance_tolerance * speed)
        {
            route.avoid_velocities[index] = chosen;
        }
    };
    // The units that steer, in dense order, split into chunks: spawned units of one type are
    // neighbours in that order, so chunks of the whole table left nearly all the work to one.
    steering_.clear();
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (steers(index))
        {
            steering_.push_back(static_cast<std::uint32_t>(index));
        }
    }
    if (jobs_ == nullptr)
    {
        avoidance_scratch_.resize(std::max<std::size_t>(avoidance_scratch_.size(), 1));
        for (const auto index : steering_)
        {
            plan(index, avoidance_scratch_.front());
        }
        return;
    }
    avoidance_scratch_.resize(std::max(avoidance_scratch_.size(), chunk_count(steering_.size(), avoidance_grain)));
    parallel_for(*jobs_, steering_.size(), avoidance_grain,
                 [&](const std::size_t begin, const std::size_t end, const std::size_t chunk)
                 {
                     for (std::size_t unit = begin; unit < end; ++unit)
                     {
                         plan(steering_[unit], avoidance_scratch_[chunk]);
                     }
                 });
}

void SimulationWorld::NavigationSystem::plan_giving_way(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& motion = view.get<MotionColumns>();
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
            way->ticks = 0;
        }
    }
    // Asking: an ally in a moving unit's corridor (its footprint swept ahead) steps out of it, just
    // far enough, preferring the side of the mover's line it stands on (the owner's drawing,
    // 2026-11-15), in any direction where the way is free; a packed line opens a lane (make_way). A moving ally gives way
    // when it is slower by nature (its top speed, not this tick's: in a queue every unit is slow for a
    // while), when it is all but at its destination (less route left than the two footprints), which
    // then counts as standing there, or when it is behind the mover in the flow (more route left, ties
    // by dense index; the priority Age of Empires II gives the unit further along, so a queue's leader
    // never stands aside for those behind it, and of two units wedged at a passage's mouth the one
    // nearer goes through first).
    requests_.clear();
    for (std::size_t passer = 0; passer < identity.ids.size(); ++passer)
    {
        if (route.ask_waits[passer] > 0)
        {
            --route.ask_waits[passer];
            continue;
        }
        // A unit held up (slower last tick than give_way_held_fraction of its speed) asks the ally in
        // its way; one still flowing asks only a standing ally there, so a line it walks at opens
        // instead of the mover sliding around it (the owner, 2026-11-15: lines open). Moving allies
        // it catches up with hold it up by avoidance first.
        const auto speed = travel_speed(view, passer);
        if (!ground_steers(view, passer))
        {
            continue;
        }
        const bool held = length(motion.velocities[passer]) < speed * give_way_held_fraction;
        const auto heading = heading_toward_waypoint(view, passer);
        // A unit that went straight at its speed last tick had nothing in its way.
        if (heading == Vector2{} || (!held && dot(motion.velocities[passer], heading) >= speed * give_way_unhindered_fraction))
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
        if (blocker && (held || (!ground_steers(view, *blocker) && !route.move_targets[*blocker])))
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
            const auto reach = own_radius + radii[yielder];
            // A standing unit on its destination does not make way: the mover stops at it (goal_taken).
            // Units merely near the destination do (a later formation member reaches its slot past
            // members already in theirs).
            const auto& goal = *route.move_targets[passer];
            if (route.paths[passer].empty() && !ground_steers(view, yielder) && std::hypot(goal.x - placement.positions[yielder].x, goal.y - placement.positions[yielder].y) < reach)
            {
                continue;
            }
            const auto& yielder_goal = route.move_targets[yielder];
            const bool arriving =
                yielder_goal && route.paths[yielder].empty() && std::hypot(yielder_goal->x - placement.positions[yielder].x, yielder_goal->y - placement.positions[yielder].y) < reach;
            if (ground_steers(view, yielder) && travel_speed(view, yielder) >= speed * give_way_slower_fraction && !arriving &&
                (route.remaining[yielder] < route.remaining[passer] || (route.remaining[yielder] == route.remaining[passer] && yielder < passer)))
            {
                continue;
            }
            if (!make_way(view, yielder, passer, heading))
            {
                route.ask_waits[passer] = ticks_for(view, give_way_retry_seconds);
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

void SimulationWorld::NavigationSystem::plan_detours(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& motion = view.get<MotionColumns>();
    const auto& ground = view.get<GroundState>();
    auto& route = view.get<RouteColumns>();
    auto& avoidance = view.get<Avoidance>();
    const auto& radii = avoidance.radii;
    const auto limit = ticks_for(view, detour_after_seconds);
    const auto& map = ground.map;
    if (obstacle_cells_.size() != map.passability.size())
    {
        obstacle_cells_.assign(map.passability.size(), 0);
    }
    std::uint32_t planned = 0;
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        // Held up: moving, slower than the held share of its speed (a formation member follows its
        // frame instead).
        if (!ground_steers(view, index) || in_moving_frame(view, index) || length(motion.velocities[index]) >= travel_speed(view, index) * give_way_held_fraction)
        {
            route.blocked_ticks[index] = 0;
            continue;
        }
        if (route.blocked_ticks[index] < limit)
        {
            ++route.blocked_ticks[index];
        }
        // At most detours_per_tick a tick (whole groups are held up at once); the rest on the next.
        if (route.blocked_ticks[index] < limit || planned >= detours_per_tick)
        {
            continue;
        }
        route.blocked_ticks[index] = 0;
        // The units near it block the cells within their summed radii (not those under the unit
        // itself, where it stands): standing units, and with `held` also the movers held up there
        // themselves. Only a unit held by one of them beside it detours; units flowing past are
        // avoidance's. Counting only standing units, a knot of held movers never detoured (the
        // owner's Kamino recording, 2026-11-16: seven tanks held for good between a pillar and a
        // wall). The cells marked are cleared again after each search (only they, not the map).
        const auto& here = placement.positions[index];
        const auto own = radii[index];
        const auto& goal = *route.move_targets[index];
        const auto goal_cell = map.cell_at(goal.x, goal.y);
        query_.clear();
        avoidance.grid.query_overlapping(here, own + detour_window, query_);
        struct Marked
        {
            bool beside{};
            bool movers{};
        };
        const auto mark = [&](const bool held)
        {
            Marked marked;
            for (const auto other : query_)
            {
                const bool held_mover = held && route.move_targets[other] && length(motion.velocities[other]) < travel_speed(view, other) * give_way_held_fraction;
                if (other == index || radii[other] <= 0.0F || (route.move_targets[other] && !held_mover))
                {
                    continue;
                }
                marked.movers = marked.movers || held_mover;
                const auto& there = placement.positions[other];
                const auto reach = own + radii[other];
                marked.beside = marked.beside || distance_xy(there, here) <= reach * detour_beside;
                const auto first_x = static_cast<long>(std::floor((there.x - reach) / map.spacing));
                const auto last_x = static_cast<long>(std::ceil((there.x + reach) / map.spacing));
                const auto first_y = static_cast<long>(std::floor((there.y - reach) / map.spacing));
                const auto last_y = static_cast<long>(std::ceil((there.y + reach) / map.spacing));
                for (auto y = std::max(0L, first_y); y <= std::min(static_cast<long>(map.height) - 1, last_y); ++y)
                {
                    for (auto x = std::max(0L, first_x); x <= std::min(static_cast<long>(map.width) - 1, last_x); ++x)
                    {
                        const Vector3 center{static_cast<float>(x) * map.spacing, static_cast<float>(y) * map.spacing, 0.0F};
                        if (distance_xy(center, there) < reach && distance_xy(center, here) > own)
                        {
                            const auto cell = static_cast<std::uint32_t>(y) * map.width + static_cast<std::uint32_t>(x);
                            if (obstacle_cells_[cell] == 0)
                            {
                                obstacle_cells_[cell] = 1;
                                marked_cells_.push_back(cell);
                            }
                        }
                    }
                }
            }
            return marked;
        };
        const auto unmark = [&]
        {
            for (const auto cell : marked_cells_)
            {
                obstacle_cells_[cell] = 0;
            }
            marked_cells_.clear();
        };
        // A search around the cells marked (then cleared); whether it leads nowhere from here.
        const auto search = [&](PathResult& found)
        {
            ++planned;
            detour_pathfinder_.set_obstacles(obstacle_cells_);
            found = detour_pathfinder_.find(navigation_data_, map, {here.x, here.y}, {goal.x, goal.y}, footing_of(view, index));
            detour_pathfinder_.set_obstacles({});
            unmark();
            ++path_searches;
            path_expansions += found.expanded;
            return found.waypoints.empty() || (found.status != PathStatus::reached && std::hypot(found.waypoints.back().x - here.x, found.waypoints.back().y - here.y) <= std::max(own, map.spacing));
        };
        // Where a unit stops for good, the closest point it can reach: where standing units close
        // its way. Held movers leave in time: closed by them, it keeps its order and plans again
        // later (stopping there, three tanks of the Kamino recording ended 600 units short).
        const auto standing_close_way = [&]
        {
            if (!mark(false).beside)
            {
                unmark();
                return false;
            }
            PathResult found;
            return search(found);
        };
        const auto stop_here = [&]
        {
            route.last_goals[index] = goal;
            route.move_targets[index].reset();
            route.paths[index].clear();
            route.detoured[index] = detour_none;
        };
        // A goal among the standing units is taken: no way leads closer through them, and where it
        // stops is the arrival rules' (goal_taken), which a detour would keep from ever applying; so
        // is a crowd at the goal, whose held movers it would go around and around (the budget test).
        mark(false);
        const bool goal_taken_by_standing = goal_cell && obstacle_cells_[*goal_cell] != 0;
        unmark();
        if (goal_taken_by_standing)
        {
            continue;
        }
        // Around held movers only while the unit has detours around them left.
        const auto marked = mark(route.mover_detours[index] < mover_detour_limit);
        // Held up again before passing a waypoint of its detour (planning the same detour again, a
        // unit in contact with the standing units around it never stopped).
        if (route.detoured[index] == detour_fresh && !marked.movers)
        {
            unmark();
            stop_here();
            continue;
        }
        if (!marked.beside)
        {
            unmark();
            continue;
        }
        PathResult found;
        if (search(found))
        {
            if (!marked.movers || standing_close_way())
            {
                stop_here();
            }
            continue;
        }
        // Around them, then on to its goal as before (the goal stays the order's).
        if (marked.movers)
        {
            ++route.mover_detours[index];
        }
        auto& path = route.paths[index];
        path.clear();
        // A detour that reaches the goal ends on it: the goal stays the move target, not a waypoint
        // (as set_route does). Kept as a waypoint (its cell's center, a little off the goal), a unit
        // held short of it in a crowd never came to its last leg, where the arrival rules apply,
        // and stood "moving" for good (the budget test).
        const auto first = found.status == PathStatus::reached ? found.waypoints.rbegin() + 1 : found.waypoints.rbegin();
        for (auto waypoint = first; waypoint != found.waypoints.rend(); ++waypoint)
        {
            path.push_back({waypoint->x, waypoint->y, 0.0F});
        }
        route.detoured[index] = path.empty() ? detour_none : detour_fresh;
    }
}

void SimulationWorld::NavigationSystem::blockers_at(const View& view, const std::size_t unit, const std::size_t passer, const Vector2 at)
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& avoidance = view.get<Avoidance>();
    const auto& radii = avoidance.radii;
    const auto radius = radii[unit];
    blockers_.clear();
    room_query_.clear();
    avoidance.grid.query_overlapping({at.x, at.y, 0.0F}, radius, room_query_);
    for (const auto other : room_query_)
    {
        const bool chained = std::ranges::any_of(chain_,
                                                 [&](const GiveWayRequest& asked)
                                                 {
                                                     return asked.yielder == other;
                                                 });
        if (other != unit && other != passer && radii[other] > 0.0F && !chained && std::hypot(placement.positions[other].x - at.x, placement.positions[other].y - at.y) < radius + radii[other])
        {
            blockers_.push_back(other);
        }
    }
    // The places units of this chain step to are taken too.
    for (const auto& asked : chain_)
    {
        if (asked.yielder != unit && std::hypot(asked.aside.x - at.x, asked.aside.y - at.y) < radius + radii[asked.yielder])
        {
            blockers_.push_back(asked.yielder);
        }
    }
}

bool SimulationWorld::NavigationSystem::way_clear(const View& view, const std::size_t unit, const Vector2 from, const Vector2 to)
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& avoidance = view.get<Avoidance>();
    const auto& radii = avoidance.radii;
    const auto step = to - from;
    const auto span = length(step);
    if (span <= 0.0F)
    {
        return true;
    }
    const auto middle = from + step * 0.5F;
    room_query_.clear();
    avoidance.grid.query_overlapping({middle.x, middle.y, 0.0F}, span * 0.5F + radii[unit], room_query_);
    for (const auto other : room_query_)
    {
        // The units of the chain step on ahead of it; those at its new place are the chain's to move.
        const bool moving_on = std::ranges::any_of(chain_,
                                                   [&](const GiveWayRequest& asked)
                                                   {
                                                       return asked.yielder == other;
                                                   }) ||
                               std::ranges::find(blockers_, other) != blockers_.end();
        if (other == unit || radii[other] <= 0.0F || moving_on)
        {
            continue;
        }
        const Vector2 there{placement.positions[other].x, placement.positions[other].y};
        const auto reach = radii[unit] + radii[other];
        const auto along = std::clamp(dot(there - from, step) / (span * span), 0.0F, 1.0F);
        const auto nearest = length(there - (from + step * along));
        // Not nearer than it already is (units packed side by side may slide past each other).
        if (nearest < std::min(reach, length(there - from)) - give_way_way_tolerance)
        {
            return false;
        }
    }
    return true;
}

bool SimulationWorld::NavigationSystem::may_shift(const View& view, const std::size_t unit, const std::size_t passer) noexcept
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& route = view.get<RouteColumns>();
    return unit != passer && view.get<Avoidance>().radii[unit] > 0.0F && identity.owners[unit] == identity.owners[passer] && !route.move_targets[unit] &&
           (!route.give_ways[unit] || route.give_ways[unit]->returning) && locomotor.max_speeds[unit] > 0.0F && locomotor.suspended[unit] == 0 &&
           std::ranges::none_of(chain_,
                                [&](const GiveWayRequest& asked)
                                {
                                    return asked.yielder == unit;
                                });
}

bool SimulationWorld::NavigationSystem::make_way(const View& view, const std::size_t yielder, const std::size_t passer, const Vector2 heading)
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& ground = view.get<GroundState>();
    const auto& radii = view.get<Avoidance>().radii;
    const Vector2 here{placement.positions[yielder].x, placement.positions[yielder].y};
    const Vector2 from{placement.positions[passer].x, placement.positions[passer].y};
    const auto clear = radii[passer] + radii[yielder] * (1.0F + give_way_margin);
    const auto aside = cross(heading, here - from);
    // Candidates: the shortest step in each of give_way_directions directions that takes the unit out
    // of the passer's corridor, shortest first, then toward its own side of the line (the owner's
    // drawing, 2026-11-15, and after it: any direction where the ground is free, sideways, diagonally,
    // or back).
    struct Candidate
    {
        float shift{};
        bool own_side{};
        std::uint32_t direction{};
        Vector2 toward;
    };
    std::array<Candidate, give_way_directions> candidates{};
    std::size_t count = 0;
    const auto facing = std::atan2(heading.y, heading.x);
    for (std::uint32_t direction = 0; direction < give_way_directions; ++direction)
    {
        const auto angle = facing + two_pi * static_cast<float>(direction) / static_cast<float>(give_way_directions);
        const Vector2 toward{std::cos(angle), std::sin(angle)};
        const auto lateral = cross(heading, toward);
        if (std::fabs(lateral) < give_way_least_lateral)
        {
            continue;
        }
        // The step s at which |aside + lateral s| reaches the clearance.
        const auto side = lateral > 0.0F ? 1.0F : -1.0F;
        const auto shift = (side * clear - aside) / lateral;
        if (shift <= 0.0F || shift > clear * give_way_max_shift)
        {
            continue;
        }
        candidates[count++] = {shift, (side > 0.0F) == (aside > 0.0F), direction, toward};
    }
    std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(count),
              [](const Candidate& a, const Candidate& b)
              {
                  if (a.shift != b.shift)
                  {
                      return a.shift < b.shift;
                  }
                  return a.own_side != b.own_side ? a.own_side : a.direction < b.direction;
              });
    // A free place first; else a place a single standing ally holds, which steps on the same way to
    // make room (a line opens a lane, give_way_chain deep). Each candidate is checked once, the cheap
    // checks first: an ask is made for every unit in a mover's way each tick, and most fail in a
    // crowd.
    const auto at = [&](const std::size_t candidate)
    {
        return here + candidates[candidate].toward * candidates[candidate].shift;
    };
    const auto start_chain = [&](const Vector2 to)
    {
        chain_.clear();
        chain_.push_back({static_cast<std::uint32_t>(yielder), static_cast<std::uint32_t>(passer), {to.x, to.y, ground.map.surface_height(to.x, to.y)}});
    };
    std::array<std::uint8_t, give_way_directions> held_by_one{};
    chain_.clear();
    for (std::size_t candidate = 0; candidate < count; ++candidate)
    {
        const auto to = at(candidate);
        blockers_at(view, yielder, passer, to);
        if (blockers_.size() > 1 || !stands_at(view, yielder, to.x, to.y) || !ground_line_passable(ground.map, {here.x, here.y}, {to.x, to.y}, footing_of(view, yielder)) ||
            !way_clear(view, yielder, here, to))
        {
            continue;
        }
        if (blockers_.empty())
        {
            start_chain(to);
            requests_.insert(requests_.end(), chain_.begin(), chain_.end());
            chain_.clear();
            return true;
        }
        held_by_one[candidate] = 1;
    }
    for (std::size_t candidate = 0; candidate < count; ++candidate)
    {
        if (held_by_one[candidate] == 0)
        {
            continue;
        }
        start_chain(at(candidate));
        blockers_at(view, yielder, passer, at(candidate));
        if (shift_on(view, passer, candidates[candidate].toward, 1U))
        {
            requests_.insert(requests_.end(), chain_.begin(), chain_.end());
            chain_.clear();
            return true;
        }
    }
    chain_.clear();
    return false;
}

bool SimulationWorld::NavigationSystem::shift_on(const View& view, const std::size_t passer, const Vector2 toward, const std::uint32_t depth)
{
    const auto& placement = view.get<PlacementColumns>();
    const auto& ground = view.get<GroundState>();
    const auto& radii = view.get<Avoidance>().radii;
    // The one ally in the way of the last place of the chain; more than one, or one that cannot move,
    // and the chain fails.
    if (blockers_.size() != 1 || depth > give_way_chain || !may_shift(view, blockers_.front(), passer))
    {
        return false;
    }
    const auto unit = static_cast<std::size_t>(blockers_.front());
    const auto& pusher = chain_.back();
    const Vector2 there{placement.positions[unit].x, placement.positions[unit].y};
    const Vector2 taken{pusher.aside.x, pusher.aside.y};
    // Far enough along `toward` to clear the place the unit before it steps to.
    const auto reach = (radii[unit] + radii[pusher.yielder]) * (1.0F + give_way_margin);
    const auto offset = there - taken;
    const auto along = dot(offset, toward);
    const auto discriminant = along * along - (dot(offset, offset) - reach * reach);
    const auto shift = discriminant > 0.0F ? -along + std::sqrt(discriminant) : 0.0F;
    if (shift <= 0.0F)
    {
        return false;
    }
    const auto to = there + toward * shift;
    if (!stands_at(view, unit, to.x, to.y) || !ground_line_passable(ground.map, {there.x, there.y}, {to.x, to.y}, footing_of(view, unit)))
    {
        return false;
    }
    chain_.push_back({static_cast<std::uint32_t>(unit), static_cast<std::uint32_t>(passer), {to.x, to.y, ground.map.surface_height(to.x, to.y)}});
    blockers_at(view, unit, passer, to);
    return way_clear(view, unit, there, to) && (blockers_.empty() || shift_on(view, passer, toward, depth + 1U));
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
    // Held within its footprint's diameter of its goal (by the units around it): arrived.
    if (stalled && own_distance <= radius * arrival_reach_share)
    {
        return true;
    }
    const auto owner = view.get<IdentityColumns>().owners[index];
    query_.clear();
    avoidance.grid.query_overlapping(here, radius * 5.0F + avoidance.largest_radius * taken_goal_margin, query_);
    // Standing: no move of its own, and not giving way (a unit stepping aside is no crowd).
    const auto standing_at = [&](const std::size_t other)
    {
        return !route.move_targets[other] && !route.give_ways[other];
    };
    // A crowd is made of allies that stopped short of their own goals (held off by those in front):
    // in a group move each member has a slot of its own, and group-mates that reached theirs are no
    // crowd at its goal; a unit held behind them waits for them to give way (the owner's report:
    // units stopped while others had not finished giving way).
    const auto stopped_short = [&](const std::size_t other)
    {
        const auto& stopped_at = route.last_goals[other];
        return stopped_at && distance_xy(placement.positions[other], *stopped_at) > radii[other];
    };
    // Allies that reached their own goals count too where this unit's own goal is taken (a standing
    // unit on it): the inner ring of a crowd at shared points; or once it has come no closer for the
    // give-up time (its goal is a hole among standing allies too narrow to reach, and no one made way):
    // where it stands is the closest point it can reach.
    const auto& placed = placement.positions;
    const bool given_up = route.stalled_ticks[index] >= ticks_for(view, arrival_give_up_seconds);
    goal_query_.clear();
    avoidance.grid.query_overlapping(goal, radius, goal_query_);
    const bool goal_occupied = std::ranges::any_of(goal_query_,
                                                   [&](const std::uint32_t other)
                                                   {
                                                       return other != index && radii[other] > 0.0F && standing_at(other) && distance_xy(placed[other], goal) < radius + radii[other];
                                                   });
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
                                   const bool standing = standing_at(other);
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
                                   // Giving up counts only beside allies that stopped near its own goal (not anywhere along a
                                   // queue at a passage, where a long wait is normal).
                                   const bool near_goal = stopped_at && distance_xy(*stopped_at, goal) < reach * taken_goal_crowd;
                                   return stalled && standing && (stopped_short(other) || goal_occupied || (given_up && near_goal)) && (same_order || near_goal) &&
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
        // Units stand in its way aside: it cannot make room, and stays where it is instead of standing
        // frozen in the crowd until giving way times out. On its way back it waits for the way to
        // clear (the unit it made way for often still stands in it), at most the giving-way limit;
        // giving up at once, units that had given way stayed aside (the owner's report).
        if (way->returning && ++way->ticks <= ticks_for(view, give_way_seconds))
        {
            (void)drive(view, index, 0.0F, dt);
            return;
        }
        if (!way->returning && distance_xy(position, way->back) > arrival_distance)
        {
            // Held on its way aside, away from its place: it walks back (forgetting the place, it was
            // asked again from where it stood and drifted).
            way->returning = true;
            way->ticks = 0;
            return;
        }
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
