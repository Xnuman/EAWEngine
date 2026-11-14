// Navigation: route requests, the per-tick search budget, shared formation routes, and group
// cohesion.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::plan_move(const std::size_t index, const Vector3& target)
{
    // A walk played as clips given a new destination (not a route planned again to the same one)
    // looks at its first way to it before it turns (steer).
    if (auto* walk = walk_clips_.find(slot_of(index), &WalkClipColumns::plans);
        walk != nullptr && (walk->clip == WalkClip::starting || walk->clip == WalkClip::moving) &&
        (!route_.move_targets[index] || std::hypot(route_.move_targets[index]->x - target.x, route_.move_targets[index]->y - target.y) > arrival_distance))
    {
        walk->new_way = true;
    }
    route_.loiter_points[index].reset();
    route_.paths[index].clear();
    route_.pending[index] = 0;
    if (ground_mover_.follows_ground[index] == 0 || ground_mover_.passable_types[index] == 0 || ground_.map.passability.size() != ground_.map.heights.size() || !ground_.map.valid())
    {
        route_.move_targets[index] = target;
        return;
    }
    // The unit holds still (its target set, so it counts as moving) until the queue plans it.
    route_.move_targets[index] = target;
    const auto serial = path_queue_.next_serial++;
    if (path_queue_.next_serial == 0)
    {
        path_queue_.next_serial = 1;
    }
    route_.pending[index] = serial;
    path_queue_.requests.push_back({{identity_.ids[index]}, {target}, {serial}, target});
}

void SimulationWorld::plan_route_now(const std::size_t index, const Vector3& target)
{
    const auto& position = placement_.positions[index];
    const auto planned = pathfinder_.find(ground_.map, {position.x, position.y}, {target.x, target.y}, ground_mover_.passable_types[index]);
    if (planned.status == PathStatus::unavailable)
    {
        route_.paths[index].clear();
        route_.move_targets[index] = target;
        return;
    }
    ++path_searches_;
    path_expansions_ += planned.expanded;
    if (planned.status == PathStatus::partial)
    {
        ++partial_paths_;
    }
    set_route(index, planned.waypoints, target.z);
}

void SimulationWorld::set_route(const std::size_t index, const std::vector<PathPoint>& waypoints, const float height)
{
    auto& path = route_.paths[index];
    path.clear();
    if (waypoints.empty())
    {
        route_.move_targets[index].reset();
        return;
    }
    const auto destination = waypoints.back();
    route_.move_targets[index] = Vector3{destination.x, destination.y, height};
    for (auto waypoint = waypoints.rbegin() + 1; waypoint != waypoints.rend(); ++waypoint)
    {
        path.push_back({waypoint->x, waypoint->y, 0.0F});
    }
}

// Requests are planned in the order they were made until the tick's node budget is spent (at least
// one per tick), so a large order spreads over a few ticks instead of stalling one. Members whose
// request was superseded or who died are skipped.
void SimulationWorld::process_path_queue()
{
    std::uint64_t spent = 0;
    bool first = true;
    while (!path_queue_.requests.empty() && (first || spent < path_budget_per_tick))
    {
        auto& request = path_queue_.requests.front();
        const auto before = path_expansions_;
        bool finished = true;
        if (request.members.size() == 1)
        {
            if (const auto dense = dense_index(request.members.front()); dense && route_.pending[*dense] == request.serials.front())
            {
                route_.pending[*dense] = 0;
                plan_route_now(*dense, request.goals.front());
            }
        }
        else
        {
            finished = plan_shared_route(request, spent, first);
        }
        first = false;
        spent += path_expansions_ - before;
        if (!finished)
        {
            // The rest of the group waits for the next tick's budget.
            break;
        }
        path_queue_.requests.pop_front();
    }
}

// One search from the member nearest the group's centroid (the commander) to the group target, then
// per member a short leg onto it (a straight line when one fits) and one from its end to the
// member's slot. A member whose leg cannot be found gets a full search of its own. Members are
// planned in order until the tick's budget is spent; the request keeps its shared path and resumes
// with the next member in a later tick.
bool SimulationWorld::plan_shared_route(PathRequest& request, std::uint64_t spent_before, const bool first_in_tick)
{
    const auto live = [&](const std::size_t member) -> std::optional<std::size_t>
    {
        const auto dense = dense_index(request.members[member]);
        if (dense && route_.pending[*dense] == request.serials[member])
        {
            return dense;
        }
        return std::nullopt;
    };
    const auto call_start = path_expansions_;
    const auto spent = [&]
    {
        return spent_before + (path_expansions_ - call_start);
    };
    bool planned_any = false;
    if (!request.shared_ready)
    {
        request.shared_ready = true;
        // The commander: the live member nearest the live members' centroid (ties to the lower id).
        double cx = 0.0;
        double cy = 0.0;
        std::size_t count = 0;
        for (std::size_t member = 0; member < request.members.size(); ++member)
        {
            if (const auto dense = live(member))
            {
                cx += placement_.positions[*dense].x;
                cy += placement_.positions[*dense].y;
                ++count;
            }
        }
        if (count == 0)
        {
            return true;
        }
        cx /= static_cast<double>(count);
        cy /= static_cast<double>(count);
        std::optional<std::size_t> commander;
        for (std::size_t member = 0; member < request.members.size(); ++member)
        {
            const auto dense = live(member);
            if (!dense)
            {
                continue;
            }
            const auto distance = std::hypot(static_cast<double>(placement_.positions[*dense].x) - cx, static_cast<double>(placement_.positions[*dense].y) - cy);
            if (!commander || distance < std::hypot(static_cast<double>(placement_.positions[*commander].x) - cx, static_cast<double>(placement_.positions[*commander].y) - cy))
            {
                commander = dense;
            }
        }
        request.mask = ground_mover_.passable_types[*commander];
        request.shared_start = {placement_.positions[*commander].x, placement_.positions[*commander].y};
        const auto shared = pathfinder_.find(ground_.map, request.shared_start, {request.group_target.x, request.group_target.y}, request.mask);
        ++path_searches_;
        path_expansions_ += shared.expanded;
        if (shared.status == PathStatus::partial)
        {
            ++partial_paths_;
        }
        request.shared_waypoints = shared.waypoints;
        if (!request.shared_waypoints.empty())
        {
            ++shared_routes_;
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
        route_.pending[*dense] = 0;
        plan_shared_member(request, *dense, request.goals[request.next_member]);
        planned_any = true;
    }
    return true;
}

void SimulationWorld::plan_shared_member(const PathRequest& request, const std::size_t index, const Vector3& goal)
{
    if (request.shared_waypoints.empty())
    {
        plan_route_now(index, goal);
        return;
    }
    const auto& shared_end = request.shared_waypoints.back();

    const PathPoint position{placement_.positions[index].x, placement_.positions[index].y};
    std::vector<PathPoint> route;
    // Onto the shared path: straight to its first waypoint, or a short search to the commander.
    if (!ground_line_passable(ground_.map, position, request.shared_waypoints.front(), request.mask))
    {
        const auto onto = local_pathfinder_.find(ground_.map, position, request.shared_start, request.mask);
        path_expansions_ += onto.expanded;
        if (onto.status != PathStatus::reached)
        {
            plan_route_now(index, goal);
            return;
        }
        route = onto.waypoints;
    }
    route.insert(route.end(), request.shared_waypoints.begin(), request.shared_waypoints.end());
    // Off it to the member's own slot.
    const PathPoint slot{goal.x, goal.y};
    if (slot != shared_end)
    {
        if (ground_line_passable(ground_.map, shared_end, slot, request.mask))
        {
            route.push_back(slot);
        }
        else
        {
            const auto off = local_pathfinder_.find(ground_.map, shared_end, slot, request.mask);
            path_expansions_ += off.expanded;
            route.insert(route.end(), off.waypoints.begin(), off.waypoints.end());
        }
    }
    // Straighten the composed route from the member's position: keep a waypoint only where a
    // straight line to the following one would cross ground the class may not enter. In open
    // ground members head straight for their slots instead of converging on the shared path.
    std::vector<PathPoint> straightened;
    auto anchor = position;
    for (std::size_t next = 0; next < route.size();)
    {
        auto furthest = next;
        while (furthest + 1 < route.size() && ground_line_passable(ground_.map, anchor, route[furthest + 1], request.mask))
        {
            ++furthest;
        }
        straightened.push_back(route[furthest]);
        anchor = route[furthest];
        next = furthest + 1;
    }
    set_route(index, straightened, goal.z);
}

void SimulationWorld::plan_space_avoidance()
{
    route_.detours.assign(identity_.ids.size(), std::nullopt);
    avoidance_.largest_ship_radius = 0.0F;
    if (ground_.map.valid())
    {
        return;
    }
    const auto ship = [&](const std::size_t index)
    {
        return placement_.radii[index] > 0.0F && flight_.min_speeds[index] <= 0.0F;
    };
    const auto steering = [&](const std::size_t index)
    {
        return ship(index) && route_.move_targets[index] && group_.waiting[index] == 0 && route_.pending[index] == 0 && locomotor_.max_speeds[index] > 0.0F && locomotor_.suspended[index] == 0;
    };
    float largest_radius = 0.0F;
    bool any = false;
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (ship(index))
        {
            largest_radius = std::max(largest_radius, placement_.radii[index]);
            any = any || steering(index);
        }
    }
    if (!any)
    {
        return;
    }
    avoidance_.largest_ship_radius = largest_radius;
    // How fast a ship makes way on a course: a ship that turns slows by the cosine of how far its
    // heading is off the course, as locomotion moves it (steer).
    const auto way = [&](const std::size_t index, const float course)
    {
        return locomotor_.turn_rates[index] > 0.0F ? std::max(0.0F, std::cos(wrap_angle(course - placement_.headings[index]))) : 1.0F;
    };
    // Where a ship goes in the plane, as the others see it: toward its target at its travel speed,
    // slowed while it turns; still when it has no target or is held. From state alone, so the plans
    // of a tick do not depend on the order ships are planned in.
    const auto velocity = [&](const std::size_t index) -> Vector2
    {
        if (!steering(index))
        {
            return {};
        }
        const auto& toward = *route_.move_targets[index];
        const float x = toward.x - placement_.positions[index].x;
        const float y = toward.y - placement_.positions[index].y;
        const float length = std::hypot(x, y);
        if (length <= arrival_distance)
        {
            return {};
        }
        const float speed = travel_speed(index) * way(index, std::atan2(y, x));
        return {x / length * speed, y / length * speed};
    };
    std::vector<Vector2> velocities(identity_.ids.size());
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        velocities[index] = ship(index) ? velocity(index) : Vector2{};
    }
    avoidance_.grid.build(placement_.positions, grid_cell_size);
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (!steering(index))
        {
            continue;
        }
        const auto& here = placement_.positions[index];
        const float radius = placement_.radii[index];
        const float speed = travel_speed(index);
        const float horizon = std::max(avoid_lookahead_seconds, radius * avoid_min_lookahead_radii / std::max(speed, 1.0e-3F));
        query_.clear();
        avoidance_.grid.query(here, speed * horizon * 2.0F + radius + largest_radius, query_);
        // The ships this one keeps clear of: at its height, not much smaller.
        neighbours_.clear();
        for (const auto other : query_)
        {
            if (other != index && ship(other) && placement_.radii[other] >= radius * avoid_small_fraction && std::abs(placement_.positions[other].z - here.z) <= radius + placement_.radii[other])
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
            return radius + placement_.radii[other] + radius * avoid_margin_fraction;
        };

        // Where a neighbour will be: a still ship where it is; a moving one at its target when this
        // ship yields to it (a lower entity id goes first).
        const auto yields_to = [&](const std::size_t other)
        {
            return identity_.ids[other].index != identity_.ids[index].index ? identity_.ids[other].index < identity_.ids[index].index
                                                                            : identity_.ids[other].generation < identity_.ids[index].generation;
        };
        const auto resting_place = [&](const std::size_t other) -> std::optional<Vector3>
        {
            if (!steering(other))
            {
                return placement_.positions[other];
            }
            return yields_to(other) ? std::optional<Vector3>(*route_.move_targets[other]) : std::nullopt;
        };
        // A destination inside the clearance of such a place moves to the nearest free point of
        // that clearance circle, starting on this ship's side, so ships sent to one point stop side
        // by side.
        auto& target = *route_.move_targets[index];
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
                const float px = placement_.positions[other].x - here.x;
                const float py = placement_.positions[other].y - here.y;
                const float wx = velocities[other].x - course.x;
                const float wy = velocities[other].y - course.y;
                const float reach = margin ? clearance(other) : radius + placement_.radii[other];
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
        const float first_side = wrap_angle(placement_.headings[index] - straight) > 0.0F ? 1.0F : -1.0F;
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
        route_.detours[index] = Vector3{here.x + std::cos(best_angle) * ahead, here.y + std::sin(best_angle) * ahead, height};
    }
}

Vector3 SimulationWorld::keep_apart(const std::size_t index, Vector3 step)
{
    const float radius = placement_.radii[index];
    if (avoidance_.largest_ship_radius <= 0.0F || radius <= 0.0F || flight_.min_speeds[index] > 0.0F || ground_.map.valid())
    {
        return step;
    }
    const auto& here = placement_.positions[index];
    query_.clear();
    avoidance_.grid.query(here, radius + avoidance_.largest_ship_radius + std::hypot(step.x, step.y), query_);
    // Whether a step ends inside a ship it moves toward (deeper than it was).
    const auto deeper = [&](const std::uint32_t other, const Vector3& candidate, float& to_x, float& to_y, float& apart)
    {
        const auto& there = placement_.positions[other];
        if (other == index || placement_.radii[other] <= 0.0F || flight_.min_speeds[other] > 0.0F || placement_.radii[other] < radius * avoid_small_fraction ||
            std::abs(there.z - here.z) > radius + placement_.radii[other])
        {
            return false;
        }
        if (std::hypot(here.x + candidate.x - there.x, here.y + candidate.y - there.y) >= radius + placement_.radii[other])
        {
            return false;
        }
        to_x = there.x - here.x;
        to_y = there.y - here.y;
        apart = std::hypot(to_x, to_y);
        return apart > 0.0F && candidate.x * to_x + candidate.y * to_y > 0.0F;
    };
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

void SimulationWorld::apply_formation_move(const SimCommand& command)
{
    std::vector<std::size_t> movers;
    std::vector<std::size_t> solos;
    std::vector<FormationMember> members;
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(id);
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
        if (formation_.solos[*dense] != 0)
        {
            solos.push_back(*dense);
            continue;
        }
        movers.push_back(*dense);
        // Ships in space keep slots wide enough for a ship to pass between two that have stopped.
        const bool ship_in_space = !ground_.map.valid() && placement_.radii[*dense] > 0.0F && flight_.min_speeds[*dense] <= 0.0F;
        const auto spacing = ship_in_space ? std::max(formation_.spacings[*dense], space_formation_spacing) : formation_.spacings[*dense];
        members.push_back({id, placement_.positions[*dense], placement_.radii[*dense], spacing, formation_.orders[*dense], formation_.teams[*dense]});
    }
    for (const auto index : solos)
    {
        combat_.states[index].target = {};
        combat_.states[index].ordered = false;
        locomotor_.speed_limits[index] = 0.0F;
        group_.ids[index] = 0;
        auto goal = command.target;
        if (ground_mover_.follows_ground[index] == 0)
        {
            goal.z = placement_.positions[index].z;
        }
        plan_move(index, goal);
    }
    if (movers.empty())
    {
        return;
    }
    const auto slots = formation_slots(members, command.target, formation_facing(members, command.target));
    const auto group = movers.size() > 1 ? next_group_id_++ : 0U;
    if (next_group_id_ == 0)
    {
        next_group_id_ = 1;
    }
    float group_speed = locomotor_.max_speeds[movers.front()];
    for (const auto index : movers)
    {
        group_speed = std::min(group_speed, locomotor_.max_speeds[index]);
    }
    // Ground members that search share one request per movement class; the rest plan as usual.
    std::vector<PathRequest> shared;
    for (std::size_t member = 0; member < movers.size(); ++member)
    {
        const auto index = movers[member];
        combat_.states[index].target = {};
        combat_.states[index].ordered = false;
        locomotor_.speed_limits[index] = movers.size() > 1 ? group_speed : 0.0F;
        group_.ids[index] = group;
        group_.best_remaining[index] = std::numeric_limits<float>::max();
        group_.stall_ticks[index] = 0;
        auto slot = slots[member].position;
        // Flyers keep their altitude; ground units take theirs from the surface anyway.
        if (ground_mover_.follows_ground[index] == 0)
        {
            slot.z = placement_.positions[index].z;
        }
        plan_move(index, slot);
        if (route_.pending[index] == 0 || movers.size() < 2)
        {
            continue;
        }
        // Move the member's queued request into its class's shared request.
        const auto serial = route_.pending[index];
        path_queue_.requests.pop_back();
        auto found = std::ranges::find_if(shared,
                                          [&](const PathRequest& request)
                                          {
                                              return ground_mover_.passable_types[*dense_index(request.members.front())] == ground_mover_.passable_types[index];
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
// detour re-forms. A member that makes no progress for cohesion_stall_ticks while not waiting is
// released from its group (it keeps its order), so one stuck unit cannot hold the others forever.
void SimulationWorld::update_group_cohesion()
{
    waiting_members_ = 0;
    group_.waiting.assign(identity_.ids.size(), 0);
    // Furthest remaining route and largest footprint per group, in first-seen order.
    struct GroupState
    {
        std::uint32_t id{};
        float furthest{};
        float largest_radius{};
    };
    std::vector<GroupState> groups;
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (group_.ids[index] == 0 || !route_.move_targets[index])
        {
            continue;
        }
        auto found = std::ranges::find(groups, group_.ids[index], &GroupState::id);
        if (found == groups.end())
        {
            groups.push_back({group_.ids[index], 0.0F, 0.0F});
            found = groups.end() - 1;
        }
        found->furthest = std::max(found->furthest, route_.remaining[index]);
        found->largest_radius = std::max(found->largest_radius, placement_.radii[index]);
    }
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (group_.ids[index] == 0 || !route_.move_targets[index])
        {
            continue;
        }
        const auto& group = *std::ranges::find(groups, group_.ids[index], &GroupState::id);
        const auto slack = cohesion_slack + 4.0F * group.largest_radius;
        if (route_.remaining[index] + slack < group.furthest)
        {
            group_.waiting[index] = 1;
            ++waiting_members_;
            continue;
        }
        // Progress is measured against the best remaining route reached so far.
        if (route_.remaining[index] + 0.5F < group_.best_remaining[index])
        {
            group_.best_remaining[index] = route_.remaining[index];
            group_.stall_ticks[index] = 0;
        }
        else if (++group_.stall_ticks[index] >= cohesion_stall_ticks)
        {
            group_.ids[index] = 0;
            ++released_members_;
        }
    }
}

} // namespace eaw
