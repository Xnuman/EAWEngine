// Navigation: route requests, the per-tick search budget, shared formation routes, and group
// cohesion.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::plan_move(const std::size_t index, const Vector3& target)
{
    loiter_points_[index].reset();
    paths_[index].clear();
    path_pending_[index] = 0;
    if (follows_ground_[index] == 0 || passable_types_[index] == 0 || ground_.passability.size() != ground_.heights.size() || !ground_.valid())
    {
        move_targets_[index] = target;
        return;
    }
    // The unit holds still (its target set, so it counts as moving) until the queue plans it.
    move_targets_[index] = target;
    const auto serial = next_path_serial_++;
    if (next_path_serial_ == 0)
    {
        next_path_serial_ = 1;
    }
    path_pending_[index] = serial;
    path_queue_.push_back({{ids_[index]}, {target}, {serial}, target});
}

void SimulationWorld::plan_route_now(const std::size_t index, const Vector3& target)
{
    const auto& position = positions_[index];
    const auto planned = pathfinder_.find(ground_, {position.x, position.y}, {target.x, target.y}, passable_types_[index]);
    if (planned.status == PathStatus::unavailable)
    {
        paths_[index].clear();
        move_targets_[index] = target;
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
    auto& path = paths_[index];
    path.clear();
    if (waypoints.empty())
    {
        move_targets_[index].reset();
        return;
    }
    const auto destination = waypoints.back();
    move_targets_[index] = Vector3{destination.x, destination.y, height};
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
    while (!path_queue_.empty() && (first || spent < path_budget_per_tick))
    {
        auto& request = path_queue_.front();
        const auto before = path_expansions_;
        bool finished = true;
        if (request.members.size() == 1)
        {
            if (const auto dense = dense_index(request.members.front()); dense && path_pending_[*dense] == request.serials.front())
            {
                path_pending_[*dense] = 0;
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
        path_queue_.pop_front();
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
        if (dense && path_pending_[*dense] == request.serials[member])
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
                cx += positions_[*dense].x;
                cy += positions_[*dense].y;
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
            const auto distance = std::hypot(static_cast<double>(positions_[*dense].x) - cx, static_cast<double>(positions_[*dense].y) - cy);
            if (!commander || distance < std::hypot(static_cast<double>(positions_[*commander].x) - cx, static_cast<double>(positions_[*commander].y) - cy))
            {
                commander = dense;
            }
        }
        request.mask = passable_types_[*commander];
        request.shared_start = {positions_[*commander].x, positions_[*commander].y};
        const auto shared = pathfinder_.find(ground_, request.shared_start, {request.group_target.x, request.group_target.y}, request.mask);
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
        path_pending_[*dense] = 0;
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

    const PathPoint position{positions_[index].x, positions_[index].y};
    std::vector<PathPoint> route;
    // Onto the shared path: straight to its first waypoint, or a short search to the commander.
    if (!ground_line_passable(ground_, position, request.shared_waypoints.front(), request.mask))
    {
        const auto onto = local_pathfinder_.find(ground_, position, request.shared_start, request.mask);
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
        if (ground_line_passable(ground_, shared_end, slot, request.mask))
        {
            route.push_back(slot);
        }
        else
        {
            const auto off = local_pathfinder_.find(ground_, shared_end, slot, request.mask);
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
        while (furthest + 1 < route.size() && ground_line_passable(ground_, anchor, route[furthest + 1], request.mask))
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
    detours_.assign(ids_.size(), std::nullopt);
    if (ground_.valid())
    {
        return;
    }
    const auto ship = [&](const std::size_t index)
    {
        return radii_[index] > 0.0F && min_speeds_[index] <= 0.0F;
    };
    const auto steering = [&](const std::size_t index)
    {
        return ship(index) && move_targets_[index] && group_waiting_[index] == 0 && path_pending_[index] == 0 && max_speeds_[index] > 0.0F;
    };
    float largest_radius = 0.0F;
    bool any = false;
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (ship(index))
        {
            largest_radius = std::max(largest_radius, radii_[index]);
            any = any || steering(index);
        }
    }
    if (!any)
    {
        return;
    }
    grid_.build(positions_, grid_cell_size);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (!steering(index))
        {
            continue;
        }
        const auto& here = positions_[index];
        const auto& target = *move_targets_[index];
        const float to_x = target.x - here.x;
        const float to_y = target.y - here.y;
        const float distance = std::sqrt(to_x * to_x + to_y * to_y);
        if (distance <= arrival_distance)
        {
            continue;
        }
        const float along_x = to_x / distance;
        const float along_y = to_y / distance;
        const float radius = radii_[index];
        const float lookahead = std::min(distance, std::max(travel_speed(index) * avoid_lookahead_seconds, radius * avoid_min_lookahead_radii));
        query_.clear();
        grid_.query(here, lookahead + radius + largest_radius, query_);
        // The nearest unit ahead whose widened footprint crosses the course; ties to the lower id.
        std::optional<std::size_t> nearest;
        float nearest_ahead = 0.0F;
        float nearest_side = 0.0F;
        for (const auto other : query_)
        {
            if (other == index || !ship(other) || radii_[other] < radius * avoid_small_fraction || std::abs(positions_[other].z - here.z) > radius + radii_[other])
            {
                continue;
            }
            const float offset_x = positions_[other].x - here.x;
            const float offset_y = positions_[other].y - here.y;
            const float ahead = offset_x * along_x + offset_y * along_y;
            // Positive: the other unit lies left of the course.
            const float side = along_x * offset_y - along_y * offset_x;
            const float clearance = radius + radii_[other] + radius * avoid_margin_fraction;
            if (ahead <= 0.0F || ahead > lookahead + radii_[other] || std::abs(side) >= clearance)
            {
                continue;
            }
            const auto lower_id = !nearest || ids_[other].index < ids_[*nearest].index || (ids_[other].index == ids_[*nearest].index && ids_[other].generation < ids_[*nearest].generation);
            if (!nearest || ahead < nearest_ahead || (ahead == nearest_ahead && lower_id))
            {
                nearest = other;
                nearest_ahead = ahead;
                nearest_side = side;
            }
        }
        if (!nearest)
        {
            continue;
        }
        const auto& blocker = positions_[*nearest];
        const float reach = radius + radii_[*nearest] + radius * avoid_margin_fraction;
        if (std::hypot(target.x - blocker.x, target.y - blocker.y) < reach)
        {
            // The destination lies at the unit in the way (a move onto another unit): the destination
            // becomes the point at its clearance on the ship's side, as close as it gets, so the
            // locomotor arrives there exactly.
            const float away_x = here.x - blocker.x;
            const float away_y = here.y - blocker.y;
            const float away = std::hypot(away_x, away_y);
            if (away > 0.0F)
            {
                move_targets_[index] = Vector3{blocker.x + away_x / away * reach, blocker.y + away_y / away * reach, target.z};
            }
            continue;
        }
        // Pass on the side away from it (to the right when it is dead ahead, so two ships meeting
        // head-on both keep right), at its clearance: beside it while the ship is still a clearance
        // or more away (aiming past it would cut the corner), moving ahead of it as the ship comes
        // alongside (a point right beside it would hold the ship there); on the way to the target's
        // height.
        const float clearance = radius + radii_[*nearest] + radius * avoid_margin_fraction;
        const float pass = nearest_side < 0.0F ? -1.0F : 1.0F;
        const auto& obstacle = positions_[*nearest];
        const float forward = clearance - std::min(nearest_ahead, clearance);
        const float height = here.z + (target.z - here.z) * std::min(1.0F, (nearest_ahead + forward) / distance);
        detours_[index] = Vector3{obstacle.x + along_y * clearance * pass + along_x * forward, obstacle.y - along_x * clearance * pass + along_y * forward, height};
    }
}

void SimulationWorld::apply_formation_move(const SimCommand& command)
{
    std::vector<std::size_t> movers;
    std::vector<FormationMember> members;
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        if (max_speeds_[*dense] <= 0.0F || std::ranges::find(movers, *dense) != movers.end())
        {
            continue;
        }
        movers.push_back(*dense);
        command_serials_[*dense] = command.serial;
        standing_orders_[*dense].kind = StandingOrder::none;
        standing_orders_[*dense].engaging = false;
        members.push_back({id, positions_[*dense], radii_[*dense], formation_spacings_[*dense], formation_orders_[*dense]});
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
    float group_speed = max_speeds_[movers.front()];
    for (const auto index : movers)
    {
        group_speed = std::min(group_speed, max_speeds_[index]);
    }
    // Ground members that search share one request per movement class; the rest plan as usual.
    std::vector<PathRequest> shared;
    for (std::size_t member = 0; member < movers.size(); ++member)
    {
        const auto index = movers[member];
        combat_[index].target = {};
        combat_[index].ordered = false;
        speed_limits_[index] = movers.size() > 1 ? group_speed : 0.0F;
        group_ids_[index] = group;
        group_best_remaining_[index] = std::numeric_limits<float>::max();
        group_stall_ticks_[index] = 0;
        auto slot = slots[member].position;
        // Flyers keep their altitude; ground units take theirs from the surface anyway.
        if (follows_ground_[index] == 0)
        {
            slot.z = positions_[index].z;
        }
        plan_move(index, slot);
        if (path_pending_[index] == 0 || movers.size() < 2)
        {
            continue;
        }
        // Move the member's queued request into its class's shared request.
        const auto serial = path_pending_[index];
        path_queue_.pop_back();
        auto found = std::ranges::find_if(shared,
                                          [&](const PathRequest& request)
                                          {
                                              return passable_types_[*dense_index(request.members.front())] == passable_types_[index];
                                          });
        if (found == shared.end())
        {
            shared.push_back({{}, {}, {}, command.target});
            found = shared.end() - 1;
        }
        found->members.push_back(ids_[index]);
        found->goals.push_back(slot);
        found->serials.push_back(serial);
    }
    for (auto& request : shared)
    {
        path_queue_.push_back(std::move(request));
    }
}

void SimulationWorld::measure_remaining_routes()
{
    remaining_route_.assign(ids_.size(), 0.0F);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (const auto& target = move_targets_[index])
        {
            auto from = positions_[index];
            float length = 0.0F;
            for (auto waypoint = paths_[index].rbegin(); waypoint != paths_[index].rend(); ++waypoint)
            {
                length += std::hypot(waypoint->x - from.x, waypoint->y - from.y);
                from = *waypoint;
            }
            remaining_route_[index] = length + std::hypot(target->x - from.x, target->y - from.y);
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
    group_waiting_.assign(ids_.size(), 0);
    // Furthest remaining route and largest footprint per group, in first-seen order.
    struct GroupState
    {
        std::uint32_t id{};
        float furthest{};
        float largest_radius{};
    };
    std::vector<GroupState> groups;
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (group_ids_[index] == 0 || !move_targets_[index])
        {
            continue;
        }
        auto found = std::ranges::find(groups, group_ids_[index], &GroupState::id);
        if (found == groups.end())
        {
            groups.push_back({group_ids_[index], 0.0F, 0.0F});
            found = groups.end() - 1;
        }
        found->furthest = std::max(found->furthest, remaining_route_[index]);
        found->largest_radius = std::max(found->largest_radius, radii_[index]);
    }
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (group_ids_[index] == 0 || !move_targets_[index])
        {
            continue;
        }
        const auto& group = *std::ranges::find(groups, group_ids_[index], &GroupState::id);
        const auto slack = cohesion_slack + 4.0F * group.largest_radius;
        if (remaining_route_[index] + slack < group.furthest)
        {
            group_waiting_[index] = 1;
            ++waiting_members_;
            continue;
        }
        // Progress is measured against the best remaining route reached so far.
        if (remaining_route_[index] + 0.5F < group_best_remaining_[index])
        {
            group_best_remaining_[index] = remaining_route_[index];
            group_stall_ticks_[index] = 0;
        }
        else if (++group_stall_ticks_[index] >= cohesion_stall_ticks)
        {
            group_ids_[index] = 0;
            ++released_members_;
        }
    }
}

} // namespace eaw
