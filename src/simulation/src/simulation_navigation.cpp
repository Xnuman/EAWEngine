// Navigation: route requests, the per-tick search budget, shared formation routes, and group
// cohesion.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::NavigationSystem::run(const View& view)
{
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    std::ranges::fill(locomotor.arriving, std::uint8_t{0});
    process_path_queue(view);
    measure_remaining_routes(route, placement);
    measure_arrival_progress(view);
    update_group_cohesion(view);
    plan_space_avoidance(view);
    plan_ground_avoidance(view);
}

void SimulationWorld::NavigationSystem::plan_route_now(const View& view, const std::size_t index, const Vector3& target)
{
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& ground = view.get<GroundState>();
    const auto& position = placement.positions[index];
    const auto planned = pathfinder_.find(ground.map, {position.x, position.y}, {target.x, target.y}, footing_of(view, index));
    if (planned.status == PathStatus::unavailable)
    {
        route.paths[index].clear();
        route.move_targets[index] = target;
        return;
    }
    ++path_searches;
    path_expansions += planned.expanded;
    if (planned.status == PathStatus::partial)
    {
        ++partial_paths;
    }
    set_route(view, index, planned.waypoints, target.z);
}

void SimulationWorld::NavigationSystem::set_route(const View& view, const std::size_t index, const std::vector<PathPoint>& waypoints, const float height)
{
    auto& route = view.get<RouteColumns>();
    auto& path = route.paths[index];
    path.clear();
    route.detoured[index] = 0;
    if (waypoints.empty())
    {
        route.move_targets[index].reset();
        return;
    }
    const auto destination = waypoints.back();
    route.move_targets[index] = Vector3{destination.x, destination.y, height};
    for (auto waypoint = waypoints.rbegin() + 1; waypoint != waypoints.rend(); ++waypoint)
    {
        path.push_back({waypoint->x, waypoint->y, 0.0F});
    }
}

// Requests are planned in the order they were made until the tick's node budget is spent (at least
// one per tick), so a large order spreads over a few ticks instead of stalling one. Members whose
// request was superseded or who died are skipped.
void SimulationWorld::NavigationSystem::process_path_queue(const View& view)
{
    auto& route = view.get<RouteColumns>();
    auto& path_queue = view.get<PathQueue>();
    std::uint64_t spent = 0;
    bool first = true;
    while (!path_queue.requests.empty() && (first || spent < path_budget_per_tick))
    {
        auto& request = path_queue.requests.front();
        const auto before = path_expansions;
        bool finished = true;
        if (request.frame != 0)
        {
            plan_frame_route(view, request);
        }
        else if (request.members.size() == 1)
        {
            if (const auto dense = dense_index(view, request.members.front()); dense && route.pending[*dense] == request.serials.front())
            {
                route.pending[*dense] = 0;
                plan_route_now(view, *dense, request.goals.front());
            }
        }
        else
        {
            finished = plan_shared_route(view, request, spent, first);
        }
        first = false;
        spent += path_expansions - before;
        if (!finished)
        {
            // The rest of the group waits for the next tick's budget.
            break;
        }
        path_queue.requests.pop_front();
    }
}

// One search from the member nearest the group's centroid (the commander) to the group target, then
// per member a short leg onto it (a straight line when one fits) and one from its end to the
// member's slot. A member whose leg cannot be found gets a full search of its own. Members are
// planned in order until the tick's budget is spent; the request keeps its shared path and resumes
// with the next member in a later tick.
bool SimulationWorld::NavigationSystem::plan_shared_route(const View& view, PathRequest& request, std::uint64_t spent_before, const bool first_in_tick)
{
    auto& placement = view.get<PlacementColumns>();
    auto& ground_mover = view.get<GroundMoverColumns>();
    auto& route = view.get<RouteColumns>();
    auto& ground = view.get<GroundState>();
    const auto live = [&](const std::size_t member) -> std::optional<std::size_t>
    {
        const auto dense = dense_index(view, request.members[member]);
        if (dense && route.pending[*dense] == request.serials[member])
        {
            return dense;
        }
        return std::nullopt;
    };
    const auto call_start = path_expansions;
    const auto spent = [&]
    {
        return spent_before + (path_expansions - call_start);
    };
    bool planned_any = false;
    if (!request.shared_ready)
    {
        request.shared_ready = true;
        // The commander: the live member nearest the live members' centroid (ties to the lower id).
        std::vector<std::size_t> alive;
        for (std::size_t member = 0; member < request.members.size(); ++member)
        {
            if (const auto dense = live(member))
            {
                alive.push_back(*dense);
            }
        }
        if (alive.empty())
        {
            return true;
        }
        const std::optional<std::size_t> commander = nearest_to_centroid(placement, alive);
        request.mask = ground_mover.passable_types[*commander];
        request.shared_start = {placement.positions[*commander].x, placement.positions[*commander].y};
        // The shared way fits the request's largest member (members share a movement class).
        auto footing = footing_of(view, *commander);
        for (const auto id : request.members)
        {
            if (const auto member = dense_index(view, id))
            {
                footing.radius = std::max(footing.radius, placement.radii[*member]);
            }
        }
        const auto shared = pathfinder_.find(ground.map, request.shared_start, {request.group_target.x, request.group_target.y}, footing);
        ++path_searches;
        path_expansions += shared.expanded;
        if (shared.status == PathStatus::partial)
        {
            ++partial_paths;
        }
        request.shared_waypoints = shared.waypoints;
        if (!request.shared_waypoints.empty())
        {
            ++shared_routes;
        }
    }
    for (; request.next_member < request.members.size(); ++request.next_member)
    {
        // The budget is spent and this tick already planned something: resume next tick.
        if (spent() >= path_budget_per_tick && (planned_any || !first_in_tick))
        {
            return false;
        }
        const auto dense = live(request.next_member);
        if (!dense)
        {
            continue;
        }
        route.pending[*dense] = 0;
        plan_shared_member(view, request, *dense, request.goals[request.next_member]);
        planned_any = true;
    }
    return true;
}

void SimulationWorld::NavigationSystem::plan_shared_member(const View& view, const PathRequest& request, const std::size_t index, const Vector3& goal)
{
    auto& placement = view.get<PlacementColumns>();
    auto& ground = view.get<GroundState>();
    if (request.shared_waypoints.empty())
    {
        plan_route_now(view, index, goal);
        return;
    }
    const auto& shared_end = request.shared_waypoints.back();

    const PathPoint position{placement.positions[index].x, placement.positions[index].y};
    const auto footing = footing_of(view, index);
    std::vector<PathPoint> path_points;
    // Onto the shared path: straight to its first waypoint, or a short search to the commander.
    if (!ground_line_passable(ground.map, position, request.shared_waypoints.front(), footing))
    {
        const auto onto = local_pathfinder_.find(ground.map, position, request.shared_start, footing);
        path_expansions += onto.expanded;
        if (onto.status != PathStatus::reached)
        {
            plan_route_now(view, index, goal);
            return;
        }
        path_points = onto.waypoints;
    }
    path_points.insert(path_points.end(), request.shared_waypoints.begin(), request.shared_waypoints.end());
    // Off it to the member's own slot.
    const PathPoint slot{goal.x, goal.y};
    if (slot != shared_end)
    {
        if (ground_line_passable(ground.map, shared_end, slot, footing))
        {
            path_points.push_back(slot);
        }
        else
        {
            const auto off = local_pathfinder_.find(ground.map, shared_end, slot, footing);
            path_expansions += off.expanded;
            path_points.insert(path_points.end(), off.waypoints.begin(), off.waypoints.end());
        }
    }
    // Straighten the composed path_points from the member's position: keep a waypoint only where a
    // straight line to the following one would cross ground the class may not enter. In open
    // ground members head straight for their slots instead of converging on the shared path.
    std::vector<PathPoint> straightened;
    auto anchor = position;
    for (std::size_t next = 0; next < path_points.size();)
    {
        auto furthest = next;
        while (furthest + 1 < path_points.size() && ground_line_passable(ground.map, anchor, path_points[furthest + 1], footing))
        {
            ++furthest;
        }
        straightened.push_back(path_points[furthest]);
        anchor = path_points[furthest];
        next = furthest + 1;
    }
    set_route(view, index, straightened, goal.z);
}

void SimulationWorld::NavigationSystem::plan_space_avoidance(const View& view)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& ground = view.get<GroundState>();
    auto& avoidance = view.get<Avoidance>();
    route.detours.assign(identity.ids.size(), std::nullopt);
    avoidance.largest_radius = 0.0F;
    if (ground.map.valid())
    {
        return;
    }
    const auto ship = [&](const std::size_t index)
    {
        return placement.radii[index] > 0.0F && !flies(view, index);
    };
    const auto steers = [&](const std::size_t index)
    {
        return ship(index) && route.move_targets[index] && !waits(view, index) && route.pending[index] == 0 && locomotor.max_speeds[index] > 0.0F && locomotor.suspended[index] == 0;
    };
    float largest_radius = 0.0F;
    bool any = false;
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (ship(index))
        {
            largest_radius = std::max(largest_radius, placement.radii[index]);
            any = any || steers(index);
        }
    }
    if (!any)
    {
        return;
    }
    avoidance.largest_radius = largest_radius;
    // How fast a ship makes way on a course: a ship that turns slows by the cosine of how far its
    // heading is off the course, as locomotion moves it (steer).
    const auto way = [&](const std::size_t index, const float course)
    {
        return locomotor.turn_rates[index] > 0.0F ? std::max(0.0F, std::cos(wrap_angle(course - placement.headings[index]))) : 1.0F;
    };
    // Where a ship goes in the plane, as the others see it: toward its target at its travel speed,
    // slowed while it turns; still when it has no target or is held. From state alone, so the plans
    // of a tick do not depend on the order ships are planned in.
    const auto velocity = [&](const std::size_t index) -> Vector2
    {
        if (!steers(index))
        {
            return {};
        }
        const auto& toward = *route.move_targets[index];
        const float x = toward.x - placement.positions[index].x;
        const float y = toward.y - placement.positions[index].y;
        const float length = std::hypot(x, y);
        if (length <= arrival_distance)
        {
            return {};
        }
        const float speed = travel_speed(view, index) * way(index, std::atan2(y, x));
        return {x / length * speed, y / length * speed};
    };
    std::vector<Vector2> velocities(identity.ids.size());
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        velocities[index] = ship(index) ? velocity(index) : Vector2{};
    }
    avoidance.grid.build(placement.positions, grid_cell_size);
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (!steers(index))
        {
            continue;
        }
        const auto& here = placement.positions[index];
        const float radius = placement.radii[index];
        const float speed = travel_speed(view, index);
        const float horizon = std::max(avoid_lookahead_seconds, radius * avoid_min_lookahead_radii / std::max(speed, 1.0e-3F));
        query_.clear();
        avoidance.grid.query(here, speed * horizon * 2.0F + radius + largest_radius, query_);
        // The ships this one keeps clear of: at its height, not much smaller.
        neighbours_.clear();
        for (const auto other : query_)
        {
            if (other != index && ship(other) && placement.radii[other] >= radius * avoid_small_fraction && std::abs(placement.positions[other].z - here.z) <= radius + placement.radii[other])
            {
                neighbours_.push_back(other);
            }
        }
        if (neighbours_.empty())
        {
            continue;
        }
        const auto clearance = [&](const std::size_t other)
        {
            return radius + placement.radii[other] + radius * avoid_margin_fraction;
        };

        // Where a neighbour will be: a still ship where it is; a moving one at its target when this
        // ship yields to it (a lower entity id goes first).
        const auto yields_to = [&](const std::size_t other)
        {
            return identity.ids[other].index != identity.ids[index].index ? identity.ids[other].index < identity.ids[index].index : identity.ids[other].generation < identity.ids[index].generation;
        };
        const auto resting_place = [&](const std::size_t other) -> std::optional<Vector3>
        {
            if (!steers(other))
            {
                return placement.positions[other];
            }
            return yields_to(other) ? std::optional<Vector3>(*route.move_targets[other]) : std::nullopt;
        };
        // A destination inside the clearance of such a place moves to the nearest free point of
        // that clearance circle, starting on this ship's side, so ships sent to one point stop side
        // by side.
        auto& target = *route.move_targets[index];
        for (const auto other : neighbours_)
        {
            const auto taken = resting_place(other);
            if (!taken || std::hypot(target.x - taken->x, target.y - taken->y) >= clearance(other))
            {
                continue;
            }
            const float away = std::atan2(here.y - taken->y, here.x - taken->x);
            const float reach = clearance(other) * (1.0F + avoid_arrival_slack);
            for (std::uint32_t step = 0; step < avoid_arrival_candidates; ++step)
            {
                // 0, +1, -1, +2, -2, ... steps around the circle from this ship's side.
                const auto turn = static_cast<float>((step + 1U) / 2U) * (step % 2U == 1U ? 1.0F : -1.0F);
                const float angle = away + turn * two_pi / static_cast<float>(avoid_arrival_candidates);
                const Vector3 point{taken->x + std::cos(angle) * reach, taken->y + std::sin(angle) * reach, target.z};
                const bool free = std::ranges::none_of(neighbours_,
                                                       [&](const std::size_t blocker)
                                                       {
                                                           const auto place = resting_place(blocker);
                                                           return place && std::hypot(point.x - place->x, point.y - place->y) < clearance(blocker);
                                                       });
                if (free)
                {
                    target = point;
                    break;
                }
            }
            break;
        }

        const float to_x = target.x - here.x;
        const float to_y = target.y - here.y;
        const float distance = std::hypot(to_x, to_y);
        if (distance <= arrival_distance)
        {
            continue;
        }
        // A course's outlook: when it first touches a neighbour's clearance before the ship arrives
        // or the horizon ends (that limit when it never does; 0 when it is already inside one), and
        // how fast it closes on the neighbours already inside (approach speed, summed). The ship
        // stops at its target, so what lies beyond does not count.
        struct Outlook
        {
            float time{};
            float closing{};
        };
        const auto outlook = [&](const Vector2& course, const float limit, const bool margin)
        {
            Outlook result{limit, 0.0F};
            for (const auto other : neighbours_)
            {
                const float px = placement.positions[other].x - here.x;
                const float py = placement.positions[other].y - here.y;
                const float wx = velocities[other].x - course.x;
                const float wy = velocities[other].y - course.y;
                const float reach = margin ? clearance(other) : radius + placement.radii[other];
                const float c = px * px + py * py - reach * reach;
                const float b = px * wx + py * wy;
                if (c < 0.0F)
                {
                    // Inside: fine while the gap opens.
                    if (b < 0.0F)
                    {
                        result.time = 0.0F;
                        result.closing += -b / std::max(std::hypot(px, py), 1.0e-3F);
                    }
                    continue;
                }
                const float a = wx * wx + wy * wy;
                const float discriminant = b * b - a * c;
                if (a <= 0.0F || b >= 0.0F || discriminant < 0.0F)
                {
                    continue;
                }
                result.time = std::min(result.time, (-b - std::sqrt(discriminant)) / a);
            }
            return result;
        };
        // Courses from the straight one outward (ships meeting head-on both keep right): the first
        // clear of every clearance until arrival or the horizon
        // wins; else the first clear of contact (both radii: the margin is for comfort, and a ship
        // may pass a neighbour closer to reach a packed slot); else the course whose contact comes
        // latest, or among courses already in contact the one closing slowest. Courses are judged
        // at full speed: slowing while it turns lasts only until the ship faces the course.
        const float straight = std::atan2(to_y, to_x);
        const float limit = std::min(horizon, distance / std::max(speed, 1.0e-3F));
        // At each angle off the straight course, the side the ship is already turned toward comes
        // first, so a ship going around a blockage keeps going around it instead of swinging back
        // and forth; a ship facing its course takes the right first.
        const float first_side = wrap_angle(placement.headings[index] - straight) > 0.0F ? 1.0F : -1.0F;
        const auto course_angle = [&](const std::uint32_t step)
        {
            const auto turn = static_cast<float>((step + 1U) / 2U) * (step % 2U == 1U ? first_side : -first_side);
            return straight + turn * avoid_course_step;
        };
        std::optional<float> chosen;
        float best_angle = straight;
        Outlook best{-1.0F, 0.0F};
        for (const bool margin : {true, false})
        {
            for (std::uint32_t step = 0; step < avoid_course_candidates && !chosen; ++step)
            {
                const float angle = course_angle(step);
                const auto candidate = outlook({std::cos(angle) * speed, std::sin(angle) * speed}, limit, margin);
                if (candidate.time >= limit)
                {
                    chosen = angle;
                    break;
                }
                const bool better = candidate.time > best.time || (candidate.time == 0.0F && best.time == 0.0F && candidate.closing < best.closing);
                if (!margin && better)
                {
                    best = candidate;
                    best_angle = angle;
                }
            }
            if (chosen)
            {
                best_angle = *chosen;
                break;
            }
        }
        if (best_angle == straight)
        {
            continue;
        }
        // Steer along the chosen course, a horizon's travel ahead (never beyond the target's
        // distance), on the way to the target's height.
        const float ahead = std::min(distance, speed * horizon);
        const float height = here.z + (target.z - here.z) * std::min(1.0F, ahead / distance);
        route.detours[index] = Vector3{here.x + std::cos(best_angle) * ahead, here.y + std::sin(best_angle) * ahead, height};
    }
}

void SimulationWorld::apply_formation_move(const SimCommand& command)
{
    std::vector<std::size_t> movers;
    std::vector<std::size_t> solos;
    std::vector<FormationMember> members;
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(*this, id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        if (locomotor_.max_speeds[*dense] <= 0.0F || std::ranges::find(movers, *dense) != movers.end() || std::ranges::find(solos, *dense) != solos.end())
        {
            continue;
        }
        orders_.command_serials[*dense] = command.serial;
        orders_.standing[*dense].kind = StandingOrder::none;
        orders_.standing[*dense].engaging = false;
        const auto& recipe = recipes_.list[identity_.recipes[*dense]];
        if (recipe.formation_solo)
        {
            solos.push_back(*dense);
            continue;
        }
        movers.push_back(*dense);
        // Ships in space keep slots wide enough for a ship to pass between two that have stopped.
        const bool ship_in_space = !ground_.map.valid() && placement_.radii[*dense] > 0.0F && !flies(*this, *dense);
        const auto spacing = ship_in_space ? std::max(recipe.formation_spacing, space_formation_spacing) : recipe.formation_spacing;
        const auto* team = formation_.find(slot_of(*this, *dense), &FormationColumns::teams);
        const auto radius = unit_radius(*this, *dense);
        members.push_back({id, placement_.positions[*dense], radius, spacing, recipe.formation_order, team != nullptr ? *team : FormationTeam{}});
    }
    for (const auto index : solos)
    {
        drop_target(*this, index);
        locomotor_.speed_limits[index] = 0.0F;
        leave_group(*this, index);
        auto goal = command.target;
        if (ground_mover_.follows_ground[index] == 0)
        {
            goal.z = placement_.positions[index].z;
        }
        plan_move(*this, index, goal);
    }
    if (movers.empty())
    {
        return;
    }
    const auto facing = formation_facing(members, command.target);
    auto slots = formation_slots(members, command.target, facing);
    place_block(slots, movers);
    if (command.kind == SimCommandKind::group_move)
    {
        // A group move (the default; the owner, 2026-11-15): each member from where it stands to its
        // own slot on a route of its own, all at the slowest member's speed; no shared route, no
        // frame, no waiting.
        auto group_speed = std::numeric_limits<float>::max();
        for (const auto index : movers)
        {
            group_speed = std::min(group_speed, locomotor_.max_speeds[index]);
        }
        for (std::size_t member = 0; member < movers.size(); ++member)
        {
            const auto index = movers[member];
            drop_target(*this, index);
            leave_group(*this, index);
            locomotor_.speed_limits[index] = movers.size() > 1 ? group_speed : 0.0F;
            auto slot = slots[member].position;
            if (ground_mover_.follows_ground[index] == 0)
            {
                slot.z = placement_.positions[index].z;
            }
            plan_move(*this, index, slot);
        }
        return;
    }
    // On land, ground members of a group move as frames; the rest (and ships) plan routes per member.
    std::vector<std::size_t> framed;
    std::vector<FormationSlot> framed_slots;
    std::vector<std::size_t> routed;
    for (std::size_t member = 0; member < movers.size(); ++member)
    {
        if (movers.size() > 1 && ground_.map.valid() && ground_mover_.follows_ground[movers[member]] != 0)
        {
            framed.push_back(movers[member]);
            framed_slots.push_back(slots[member]);
            drop_target(*this, movers[member]);
        }
        else
        {
            routed.push_back(member);
        }
    }
    if (!framed.empty())
    {
        form_frames(command, framed, framed_slots, facing);
    }
    if (routed.empty())
    {
        return;
    }
    const auto group = routed.size() > 1 ? next_group_id_++ : 0U;
    if (next_group_id_ == 0)
    {
        next_group_id_ = 1;
    }
    float group_speed = locomotor_.max_speeds[movers[routed.front()]];
    for (const auto member : routed)
    {
        group_speed = std::min(group_speed, locomotor_.max_speeds[movers[member]]);
    }
    // Members that search share one request per movement class; the rest plan as usual.
    std::vector<PathRequest> shared;
    for (const auto member : routed)
    {
        const auto index = movers[member];
        drop_target(*this, index);
        locomotor_.speed_limits[index] = routed.size() > 1 ? group_speed : 0.0F;
        // A lone mover has no group; the members of a new one start afresh.
        leave_group(*this, index);
        if (group != 0)
        {
            const auto row = group_.add(identity_.ids[index].index);
            group_.columns.ids[row] = group;
            group_.columns.best_remaining[row] = std::numeric_limits<float>::max();
        }
        auto slot = slots[member].position;
        // Flyers keep their altitude; ground units take theirs from the surface anyway.
        if (ground_mover_.follows_ground[index] == 0)
        {
            slot.z = placement_.positions[index].z;
        }
        plan_move(*this, index, slot);
        if (route_.pending[index] == 0 || routed.size() < 2)
        {
            continue;
        }
        // Move the member's queued request into its class's shared request.
        const auto serial = route_.pending[index];
        path_queue_.requests.pop_back();
        auto found = std::ranges::find_if(shared,
                                          [&](const PathRequest& request)
                                          {
                                              return ground_mover_.passable_types[*dense_index(*this, request.members.front())] == ground_mover_.passable_types[index];
                                          });
        if (found == shared.end())
        {
            shared.push_back({{}, {}, {}, command.target});
            found = shared.end() - 1;
        }
        found->members.push_back(identity_.ids[index]);
        found->goals.push_back(slot);
        found->serials.push_back(serial);
    }
    for (auto& request : shared)
    {
        path_queue_.requests.push_back(std::move(request));
    }
}

void SimulationWorld::measure_remaining_routes(RouteColumns& route, const PlacementColumns& placement)
{
    route.remaining.assign(route.move_targets.size(), 0.0F);
    for (std::size_t index = 0; index < route.move_targets.size(); ++index)
    {
        if (const auto& target = route.move_targets[index])
        {
            auto from = placement.positions[index];
            float length = 0.0F;
            for (auto waypoint = route.paths[index].rbegin(); waypoint != route.paths[index].rend(); ++waypoint)
            {
                length += std::hypot(waypoint->x - from.x, waypoint->y - from.y);
                from = *waypoint;
            }
            route.remaining[index] = length + std::hypot(target->x - from.x, target->y - from.y);
        }
    }
}

// A formation advances together, as in Age of Empires II: a moving member more than the slack
// ahead of its group's furthest moving member waits, so a group torn apart by a narrow passage or a
// detour re-forms. A member that makes no progress for cohesion_stall_seconds while not waiting is
// released from its group (it keeps its order), so one stuck unit cannot hold the others forever.
void SimulationWorld::NavigationSystem::update_group_cohesion(const View& view)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& group = view.get<ComponentPool<GroupColumns>>();
    const auto& frames = view.get<FormationFrames>().list;
    waiting_members = 0;
    std::ranges::fill(group.columns.waiting, std::uint8_t{0});
    // A group with a frame keeps together through its frame (simulation_frames.cpp); its members wait
    // while the frame holds for members behind.
    const auto framed = [&](const std::uint32_t id)
    {
        return std::ranges::find(frames, id, &FormationFrame::group) != frames.end();
    };
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (const auto row = group.find(slot_of(view, index)))
        {
            const auto frame = std::ranges::find(frames, group.columns.ids[*row], &FormationFrame::group);
            waiting_members += frame != frames.end() && frame->wait_ticks > 0 ? 1U : 0U;
        }
    }
    // Furthest remaining route and largest footprint per progress, in first-seen order.
    struct GroupState
    {
        std::uint32_t id{};
        float furthest{};
        float largest_radius{};
    };
    std::vector<GroupState> groups;
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        const auto row = group.find(slot_of(view, index));
        if (!row || !route.move_targets[index] || framed(group.columns.ids[*row]))
        {
            continue;
        }
        const auto id = group.columns.ids[*row];
        auto found = std::ranges::find(groups, id, &GroupState::id);
        if (found == groups.end())
        {
            groups.push_back({id, 0.0F, 0.0F});
            found = groups.end() - 1;
        }
        found->furthest = std::max(found->furthest, route.remaining[index]);
        found->largest_radius = std::max(found->largest_radius, placement.radii[index]);
    }
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        // Rows are looked up per entity: a release swaps the pool's last row into its place.
        const auto row = group.find(slot_of(view, index));
        if (!row || !route.move_targets[index] || framed(group.columns.ids[*row]))
        {
            continue;
        }
        const auto& progress = *std::ranges::find(groups, group.columns.ids[*row], &GroupState::id);
        const auto slack = cohesion_slack + 4.0F * progress.largest_radius;
        if (route.remaining[index] + slack < progress.furthest)
        {
            group.columns.waiting[*row] = 1;
            ++waiting_members;
            continue;
        }
        // Progress is measured against the best remaining route reached so far.
        if (route.remaining[index] + 0.5F < group.columns.best_remaining[*row])
        {
            group.columns.best_remaining[*row] = route.remaining[index];
            group.columns.stall_ticks[*row] = 0;
        }
        else if (++group.columns.stall_ticks[*row] >= ticks_for(view, cohesion_stall_seconds))
        {
            leave_group(view, index);
            ++released_members;
        }
    }
}

} // namespace eaw
