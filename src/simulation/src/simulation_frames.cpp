// Formation frames (docs/pathfinding-design.md, 4.3): a ground formation moves as one body. Its frame,
// a point and a facing, follows one route at the slowest member's speed; each member walks to its
// slot in the frame (as a squad's soldiers do around their team), so the group keeps its shape on the
// way and arrives in it, instead of gathering on one path and spreading at the end. The frame waits
// for members far behind their slots; where the ground is narrower than the formation it draws its
// slots in toward its line. An order splits a selection into frames by distance.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

using namespace simulation_detail;

std::size_t SimulationWorld::nearest_to_centroid(const PlacementColumns& placement, const std::span<const std::size_t> members)
{
    EAW_ASSERT_MSG(!members.empty(), "a group has members");
    double cx = 0.0;
    double cy = 0.0;
    for (const auto member : members)
    {
        cx += placement.positions[member].x;
        cy += placement.positions[member].y;
    }
    cx /= static_cast<double>(members.size());
    cy /= static_cast<double>(members.size());
    auto nearest = members.front();
    auto best = std::numeric_limits<double>::max();
    for (const auto member : members)
    {
        const auto distance = std::hypot(static_cast<double>(placement.positions[member].x) - cx, static_cast<double>(placement.positions[member].y) - cy);
        if (distance < best)
        {
            best = distance;
            nearest = member;
        }
    }
    return nearest;
}

void SimulationWorld::form_frames(const SimCommand& command, const std::span<const std::size_t> movers, const std::span<const FormationSlot> slots, const float facing)
{
    // Parts by distance (single linkage), in member order.
    constexpr auto unassigned = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> part(movers.size(), unassigned);
    std::uint32_t parts = 0;
    std::vector<std::size_t> pending;
    for (std::size_t first = 0; first < movers.size(); ++first)
    {
        if (part[first] != unassigned)
        {
            continue;
        }
        part[first] = parts;
        pending.assign(1, first);
        while (!pending.empty())
        {
            const auto member = pending.back();
            pending.pop_back();
            for (std::size_t other = 0; other < movers.size(); ++other)
            {
                if (part[other] == unassigned && distance_xy(placement_.positions[movers[member]], placement_.positions[movers[other]]) <= frame_split_distance)
                {
                    part[other] = parts;
                    pending.push_back(other);
                }
            }
        }
        ++parts;
    }
    const auto forward_x = std::cos(facing);
    const auto forward_y = std::sin(facing);
    std::vector<std::size_t> members;
    for (std::uint32_t current = 0; current < parts; ++current)
    {
        members.clear();
        for (std::size_t member = 0; member < movers.size(); ++member)
        {
            if (part[member] == current)
            {
                members.push_back(member);
            }
        }
        if (members.size() == 1)
        {
            // Alone in its part: straight to its slot.
            const auto index = movers[members.front()];
            leave_group(*this, index);
            locomotor_.speed_limits[index] = 0.0F;
            plan_move(*this, index, slots[members.front()].position);
            continue;
        }
        const auto group = next_group_id_++;
        if (next_group_id_ == 0)
        {
            next_group_id_ = 1;
        }
        std::vector<std::size_t> dense;
        auto speed = std::numeric_limits<float>::max();
        for (const auto member : members)
        {
            dense.push_back(movers[member]);
            speed = std::min(speed, locomotor_.max_speeds[movers[member]]);
        }
        const auto start = placement_.positions[nearest_to_centroid(placement_, dense)];
        const auto heading = command.target.x != start.x || command.target.y != start.y ? std::atan2(command.target.y - start.y, command.target.x - start.x) : facing;
        frames_.list.push_back({.group = group, .position = start, .facing = heading, .target = command.target, .target_facing = facing, .speed = speed * frame_pace});
        PathRequest request{.group_target = command.target, .frame = group};
        for (const auto member : members)
        {
            const auto index = movers[member];
            leave_group(*this, index);
            locomotor_.speed_limits[index] = speed;
            route_.move_targets[index].reset();
            route_.paths[index].clear();
            route_.pending[index] = 0;
            const auto row = group_.add(identity_.ids[index].index);
            group_.columns.ids[row] = group;
            group_.columns.best_remaining[row] = std::numeric_limits<float>::max();
            // The slot's offset from the ordered point, forward and right of the formation's facing.
            const auto dx = slots[member].position.x - command.target.x;
            const auto dy = slots[member].position.y - command.target.y;
            group_.columns.ahead[row] = dx * forward_x + dy * forward_y;
            group_.columns.aside[row] = dx * forward_y - dy * forward_x;
            // Where it stands in the frame at the start, along the frame's first heading.
            const auto sx = placement_.positions[index].x - start.x;
            const auto sy = placement_.positions[index].y - start.y;
            group_.columns.start_ahead[row] = sx * std::cos(heading) + sy * std::sin(heading);
            group_.columns.start_aside[row] = sx * std::sin(heading) - sy * std::cos(heading);
            group_.columns.sent[row].reset();
            request.members.push_back(identity_.ids[index]);
        }
        path_queue_.requests.push_back(std::move(request));
    }
}

void SimulationWorld::NavigationSystem::plan_frame_route(const View& view, const PathRequest& request)
{
    auto& ground = view.get<GroundState>();
    auto& frames = view.get<FormationFrames>();
    const auto frame = std::ranges::find(frames.list, request.frame, &FormationFrame::group);
    if (frame == frames.list.end())
    {
        return;
    }
    frame->planned = true;
    frame->route.clear();
    // The route fits the group's largest footprint: a vehicle's, or a squad's largest soldier's (not
    // the squad's whole circle: planned for that, narrow ways looked closed and the route ended short,
    // the owner's report of groups stopping early).
    auto& placement = view.get<PlacementColumns>();
    std::optional<std::size_t> largest;
    float radius = 0.0F;
    for (const auto id : request.members)
    {
        const auto member = dense_index(view, id);
        if (!member)
        {
            continue;
        }
        auto own = placement.radii[*member];
        if (const auto* squad = view.get<ComponentPool<SquadColumns>>().find(slot_of(view, *member), &SquadColumns::states))
        {
            for (const auto soldier_id : squad->members)
            {
                if (const auto soldier = dense_index(view, soldier_id))
                {
                    own = std::max(own, placement.radii[*soldier]);
                }
            }
        }
        if (!largest || own > radius)
        {
            largest = member;
            radius = own;
        }
    }
    if (!largest)
    {
        return;
    }
    auto footing = footing_of(view, *largest);
    footing.radius = std::max(footing.radius, radius);
    const auto found = pathfinder_.find(navigation_data_, ground.map, {frame->position.x, frame->position.y}, {frame->target.x, frame->target.y}, footing);
    ++path_searches;
    path_expansions += found.expanded;
    if (found.status == PathStatus::partial)
    {
        ++partial_paths;
    }
    if (found.waypoints.empty())
    {
        frame->route.push_back(frame->target);
        frame->forming = std::min(frame_forming_distance, distance_xy(frame->position, frame->target));
        return;
    }
    ++shared_routes;
    // A goal out of reach: the frame ends where the route does.
    const auto& end = found.waypoints.back();
    frame->target = {end.x, end.y, frame->target.z};
    float length = 0.0F;
    PathPoint from{frame->position.x, frame->position.y};
    for (const auto& waypoint : found.waypoints)
    {
        length += std::hypot(waypoint.x - from.x, waypoint.y - from.y);
        from = waypoint;
    }
    frame->forming = std::min(frame_forming_distance, length);
    for (auto waypoint = found.waypoints.rbegin(); waypoint != found.waypoints.rend(); ++waypoint)
    {
        frame->route.push_back({waypoint->x, waypoint->y, frame->target.z});
    }
}

void SimulationWorld::BehaviorSystem::take_final_slots(const View& view, const FormationFrame& frame, const std::span<const std::pair<std::size_t, std::size_t>> members)
{
    auto& placement = view.get<PlacementColumns>();
    auto& group = view.get<ComponentPool<GroupColumns>>();
    const Vector2 forward{std::cos(frame.target_facing), std::sin(frame.target_facing)};
    const Vector2 right{forward.y, -forward.x};
    const auto offset_of = [&](const std::size_t member)
    {
        const auto& at = placement.positions[members[member].first];
        return Vector2{at.x - frame.target.x, at.y - frame.target.y};
    };
    // A slot's rank: its offset ahead, to a hundredth of a world unit.
    constexpr float same_rank = 0.01F;
    std::vector<std::uint8_t> done(members.size(), 0);
    std::vector<std::size_t> same;
    std::vector<std::pair<float, float>> offsets;
    for (std::size_t first = 0; first < members.size(); ++first)
    {
        if (done[first] != 0)
        {
            continue;
        }
        const auto radius = unit_radius(view, members[first].first);
        same.clear();
        offsets.clear();
        for (auto member = first; member < members.size(); ++member)
        {
            if (done[member] == 0 && unit_radius(view, members[member].first) == radius)
            {
                done[member] = 1;
                same.push_back(member);
                offsets.emplace_back(group.columns.ahead[members[member].second], group.columns.aside[members[member].second]);
            }
        }
        std::ranges::stable_sort(same,
                                 [&](const std::size_t a, const std::size_t b)
                                 {
                                     return dot(offset_of(a), forward) > dot(offset_of(b), forward);
                                 });
        std::ranges::stable_sort(offsets,
                                 [](const auto& a, const auto& b)
                                 {
                                     return a.first > b.first;
                                 });
        for (std::size_t begin = 0; begin < offsets.size();)
        {
            auto end = begin + 1;
            while (end < offsets.size() && std::fabs(offsets[end].first - offsets[begin].first) < same_rank)
            {
                ++end;
            }
            std::stable_sort(same.begin() + static_cast<std::ptrdiff_t>(begin), same.begin() + static_cast<std::ptrdiff_t>(end),
                             [&](const std::size_t a, const std::size_t b)
                             {
                                 return dot(offset_of(a), right) < dot(offset_of(b), right);
                             });
            std::stable_sort(offsets.begin() + static_cast<std::ptrdiff_t>(begin), offsets.begin() + static_cast<std::ptrdiff_t>(end),
                             [](const auto& a, const auto& b)
                             {
                                 return a.second < b.second;
                             });
            begin = end;
        }
        for (std::size_t taken = 0; taken < same.size(); ++taken)
        {
            const auto row = members[same[taken]].second;
            group.columns.ahead[row] = offsets[taken].first;
            group.columns.aside[row] = offsets[taken].second;
            group.columns.sent[row].reset();
        }
    }
}

void SimulationWorld::BehaviorSystem::follow_frames(const View& view, const float dt)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& group = view.get<ComponentPool<GroupColumns>>();
    auto& frames = view.get<FormationFrames>();
    auto& ground = view.get<GroundState>();
    auto& list = frames.list;
    if (list.empty())
    {
        return;
    }
    // Each frame's members in dense order: (dense index, group row).
    frame_members_.resize(list.size());
    for (auto& members : frame_members_)
    {
        members.clear();
    }
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (const auto row = group.find(slot_of(view, index)))
        {
            const auto at = static_cast<std::size_t>(std::ranges::find(list, group.columns.ids[*row], &FormationFrame::group) - list.begin());
            if (at < list.size())
            {
                frame_members_[at].emplace_back(index, *row);
            }
        }
    }
    const auto radius_of = [&](const std::size_t index)
    {
        return unit_radius(view, index);
    };
    const auto place = [](const Vector3& center, const float turn, const float ahead, const float aside)
    {
        return Vector3{center.x + std::cos(turn) * ahead + std::sin(turn) * aside, center.y + std::sin(turn) * ahead - std::cos(turn) * aside, center.z};
    };
    const auto wait_limit = ticks_for(view, frame_wait_seconds);
    const auto hold_limit = ticks_for(view, frame_hold_seconds);
    releases_.clear();
    std::size_t kept = 0;
    for (std::size_t at = 0; at < list.size(); ++at)
    {
        auto& frame = list[at];
        const auto& members = frame_members_[at];
        if (members.empty())
        {
            // Every member left (arrived, released, or gone): the frame is done.
            continue;
        }
        // The slots: at the ordered point once arrived; on the way around the frame, and where the
        // ground is narrower than the formation, in as many files as fit, members in the order they
        // stand front to back and left to right (Age of Empires II, Company of Heroes). The ground counts at the
        // frame and at every member, so the formation stays in files until its whole body is through.
        slots_.clear();
        const auto& center = frame.arrived ? frame.target : frame.position;
        const auto turn = frame.arrived ? frame.target_facing : frame.facing;
        float half_width = 0.0F;
        float largest = 0.0F;
        std::optional<float> room;
        for (const auto& [index, row] : members)
        {
            half_width = std::max(half_width, std::fabs(group.columns.aside[row]) + radius_of(index));
            largest = std::max(largest, radius_of(index));
            const auto footing = footing_of(view, index);
            if (frame.arrived || footing.clearance == nullptr)
            {
                continue;
            }
            for (const auto& point : {center, placement.positions[index]})
            {
                const auto cell = ground.map.cell_at(point.x, point.y);
                if (cell && *cell < footing.clearance->cells.size())
                {
                    room = std::min(room.value_or(footing.clearance->cells[*cell]), footing.clearance->cells[*cell]);
                }
            }
        }
        if (room && half_width > *room)
        {
            const auto pitch = 2.0F * largest * frame_file_spacing;
            const auto files = 1U + static_cast<std::uint32_t>(std::max(0.0F, 2.0F * (*room - largest)) / pitch);
            order_.resize(members.size());
            for (std::size_t member = 0; member < members.size(); ++member)
            {
                order_[member] = member;
            }
            // By where the members are (furthest along the frame's heading first, then left to right):
            // ordered by their formation slots, a member near the front of a forming formation was sent
            // to the back of the file through the others.
            const Vector2 forward{std::cos(turn), std::sin(turn)};
            const auto along = [&](const std::size_t member)
            {
                const auto& at = placement.positions[members[member].first];
                return Vector2{at.x - center.x, at.y - center.y};
            };
            std::ranges::sort(order_,
                              [&](const std::size_t a, const std::size_t b)
                              {
                                  const auto ahead_a = dot(along(a), forward);
                                  const auto ahead_b = dot(along(b), forward);
                                  if (ahead_a != ahead_b)
                                  {
                                      return ahead_a > ahead_b;
                                  }
                                  return cross(forward, along(a)) > cross(forward, along(b));
                              });
            slots_.resize(members.size());
            for (std::size_t rank = 0; rank < order_.size(); ++rank)
            {
                const auto file = static_cast<float>(rank % files) - static_cast<float>(files - 1U) * 0.5F;
                const auto behind = static_cast<float>(rank / files);
                slots_[order_[rank]] = place(center, turn, -behind * pitch, file * pitch);
            }
        }
        else
        {
            for (const auto& [index, row] : members)
            {
                // Blending from where the member stood (before the route is planned, it holds there).
                const auto formed = frame.arrived ? 1.0F : !frame.planned ? 0.0F : frame.forming > 0.0F ? std::min(1.0F, frame.travelled / frame.forming) : 1.0F;
                const auto ahead = group.columns.start_ahead[row] + (group.columns.ahead[row] - group.columns.start_ahead[row]) * formed;
                const auto aside = group.columns.start_aside[row] + (group.columns.aside[row] - group.columns.start_aside[row]) * formed;
                slots_.push_back(place(center, turn, ahead, aside));
            }
        }
        // Laggers: far behind their slots. The frame holds for them while they close in; one that has
        // come no closer for the wait limit leaves the group and goes on alone to its slot at the end
        // (after every frame: leaving the group moves pool rows).
        bool holding = false;
        releasing_.assign(members.size(), 0);
        for (std::size_t member = 0; member < members.size(); ++member)
        {
            const auto [index, row] = members[member];
            const auto away = distance_xy(slots_[member], placement.positions[index]);
            if (frame.arrived || !frame.planned || away <= frame_leash + 2.0F * radius_of(index))
            {
                group.columns.best_remaining[row] = std::numeric_limits<float>::max();
                group.columns.stall_ticks[row] = 0;
                continue;
            }
            if (away + arrival_progress < group.columns.best_remaining[row])
            {
                group.columns.best_remaining[row] = away;
                group.columns.stall_ticks[row] = 0;
            }
            else if (++group.columns.stall_ticks[row] >= wait_limit)
            {
                releases_.emplace_back(index, place(frame.target, frame.target_facing, group.columns.ahead[row], group.columns.aside[row]));
                releasing_[member] = 1;
                continue;
            }
            holding = true;
        }
        // Advance: turn toward the route and move along it while facing the way, unless holding.
        // It holds at most the hold limit at a stretch: in a crowd, laggers inch closer for a long time.
        frame.wait_ticks = holding ? frame.wait_ticks + 1U : 0U;
        if (frame.planned && !frame.arrived && (!holding || frame.wait_ticks > hold_limit))
        {
            auto left = frame.speed * dt;
            bool turned = false;
            while (left > 0.0F && !frame.route.empty())
            {
                const auto& next = frame.route.back();
                const auto to_x = next.x - frame.position.x;
                const auto to_y = next.y - frame.position.y;
                const auto distance = std::hypot(to_x, to_y);
                if (distance <= arrival_distance)
                {
                    frame.route.pop_back();
                    continue;
                }
                const auto desired = std::atan2(to_y, to_x);
                if (!turned)
                {
                    const auto difference = wrap_angle(desired - frame.facing);
                    const auto step = frame_turn_rate * dt;
                    frame.facing = std::fabs(difference) <= step ? desired : wrap_angle(frame.facing + std::copysign(step, difference));
                    turned = true;
                }
                if (std::fabs(wrap_angle(desired - frame.facing)) > frame_turn_hold)
                {
                    break;
                }
                const auto travel = std::min(left, distance);
                frame.position = {frame.position.x + to_x / distance * travel, frame.position.y + to_y / distance * travel, frame.position.z};
                frame.travelled += travel;
                left -= travel;
                if (travel >= distance)
                {
                    frame.route.pop_back();
                }
            }
            frame.arrived = frame.route.empty();
            if (frame.arrived)
            {
                take_final_slots(view, frame, members);
            }
        }
        // Members to their slots (the slots of this tick's frame; around the start while the route is
        // planned: the group forms up).
        for (std::size_t member = 0; member < members.size(); ++member)
        {
            const auto [index, row] = members[member];
            if (releasing_[member] != 0)
            {
                continue;
            }
            auto slot = slots_[member];
            if (!stands_at(view, index, slot.x, slot.y))
            {
                // The nearest point around it where the member can stand (rings of its footprint, six
                // points more each), else the frame's own place.
                const auto step = std::max(2.0F * radius_of(index), 1.0F);
                std::optional<Vector3> found;
                for (std::uint32_t ring = 1; ring <= frame_slot_rings && !found; ++ring)
                {
                    const auto points = 6U * ring;
                    for (std::uint32_t point = 0; point < points; ++point)
                    {
                        const auto angle = two_pi * static_cast<float>(point) / static_cast<float>(points);
                        const Vector3 candidate{slot.x + std::cos(angle) * step * static_cast<float>(ring), slot.y + std::sin(angle) * step * static_cast<float>(ring), slot.z};
                        if (stands_at(view, index, candidate.x, candidate.y))
                        {
                            found = candidate;
                            break;
                        }
                    }
                }
                slot = found.value_or(center);
            }
            // At the end: a member that stopped after it was sent to its final slot (there, or at the unit
            // standing on it) is done and leaves the group; sent again, it stopped and restarted every tick.
            const auto& last_sent = group.columns.sent[row];
            if (frame.arrived && !route.move_targets[index] && route.pending[index] == 0 && last_sent && distance_xy(*last_sent, slot) <= slot_follow_tolerance)
            {
                continue;
            }
            const auto sent_before = group.columns.sent[row];
            follow_slot(view, index, slot, group.columns.sent[row]);
            // Sent where a straight line does not fit (a rock, a cliff's edge): by way of the frame, on the
            // group's route; a search (movement plans one when the step does not fit) only where that
            // way does not fit either.
            const auto footing = footing_of(view, index);
            const PathPoint from{placement.positions[index].x, placement.positions[index].y};
            if (group.columns.sent[row] != sent_before && !frame.arrived && !ground_line_passable(ground.map, from, {slot.x, slot.y}, footing) &&
                ground_line_passable(ground.map, from, {center.x, center.y}, footing) && ground_line_passable(ground.map, {center.x, center.y}, {slot.x, slot.y}, footing))
            {
                route.paths[index].assign(1, center);
            }
            // On the way a member keeps its slot as its target even at it, so it stays in its group.
            if (!frame.arrived && !route.move_targets[index] && route.pending[index] == 0)
            {
                route.move_targets[index] = slot;
            }
        }
        if (kept != at)
        {
            list[kept] = std::move(frame);
        }
        ++kept;
    }
    list.resize(kept);
    for (const auto& [index, goal] : releases_)
    {
        leave_group(view, index);
        locomotor.speed_limits[index] = 0.0F;
        plan_move(view, index, goal);
        ++released_members;
    }
}

} // namespace eaw
