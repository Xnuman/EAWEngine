#include "eaw/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace eaw {
namespace {

constexpr std::uint32_t no_dense = std::numeric_limits<std::uint32_t>::max();
// Arrival tolerance in world units.
constexpr float arrival_distance = 0.01F;
// Broad-phase cell size in world units (a little above typical weapon ranges).
constexpr float grid_cell_size = 256.0F;
// Ticks between automatic target searches of an idle weapon, and between replans of an ordered
// chase whose target is out of range. Engine choices, not measured from the original.
constexpr std::uint32_t retarget_interval = 10;
constexpr std::uint32_t chase_interval = 30;
// A formation member waits while it is more than this much route ahead of its group's furthest
// member (plus two footprint diameters), and leaves its group after this many ticks without
// progress while not waiting. Engine choices.
constexpr float cohesion_slack = 30.0F;
constexpr std::uint32_t cohesion_stall_ticks = 90;
// Path search nodes expanded per tick at most (about 5 ms in a Release build), always at least one
// request. An engine choice; requests beyond it wait for the next tick in order.
constexpr std::uint64_t path_budget_per_tick = 30000;

bool finite(const SimVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_amount(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

bool valid_profile(const CombatProfile& profile) noexcept
{
    if (!valid_amount(profile.health) || !valid_amount(profile.shield) || !valid_amount(profile.shield_regen_per_second) || !std::isfinite(profile.aim_height))
    {
        return false;
    }
    if (const auto& weapon = profile.weapon)
    {
        return valid_amount(weapon->range) && valid_amount(weapon->recharge_seconds) && valid_amount(weapon->pulse_delay_seconds) && valid_amount(weapon->damage) &&
               valid_amount(weapon->projectile_speed) && valid_amount(weapon->max_flight_distance);
    }
    return true;
}

float distance_xy(const SimVector3& from, const SimVector3& to) noexcept
{
    return std::hypot(to.x - from.x, to.y - from.y);
}

std::uint64_t fnv1a(std::uint64_t hash, const void* data, const std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

template <class T> std::uint64_t hash_value(const std::uint64_t hash, const T& value) noexcept
{
    return fnv1a(hash, &value, sizeof(value));
}

float wrap_angle(float angle) noexcept
{
    constexpr auto two_pi = 2.0F * std::numbers::pi_v<float>;
    angle = std::fmod(angle, two_pi);
    if (angle > std::numbers::pi_v<float>)
    {
        angle -= two_pi;
    }
    else if (angle < -std::numbers::pi_v<float>)
    {
        angle += two_pi;
    }
    return angle;
}

} // namespace

FixedStepClock::FixedStepClock(const double ticks_per_second, const std::uint32_t max_ticks_per_advance) : tick_seconds_(1.0 / ticks_per_second), max_ticks_(max_ticks_per_advance)
{
    EAW_ASSERT_MSG(ticks_per_second > 0.0 && std::isfinite(ticks_per_second) && max_ticks_per_advance != 0, "fixed-step clock needs a positive tick rate and tick budget");
}

std::uint32_t FixedStepClock::advance(const double seconds)
{
    if (std::isfinite(seconds) && seconds > 0.0)
    {
        accumulator_ += seconds;
    }
    std::uint32_t ticks = 0;
    while (accumulator_ >= tick_seconds_ && ticks < max_ticks_)
    {
        accumulator_ -= tick_seconds_;
        ++ticks;
    }
    if (accumulator_ >= tick_seconds_)
    {
        const auto excess = static_cast<std::uint64_t>(accumulator_ / tick_seconds_);
        dropped_ += excess;
        accumulator_ -= static_cast<double>(excess) * tick_seconds_;
    }
    return ticks;
}

float FixedStepClock::interpolation() const noexcept
{
    return static_cast<float>(std::clamp(accumulator_ / tick_seconds_, 0.0, 1.0));
}

double FixedStepClock::tick_seconds() const noexcept
{
    return tick_seconds_;
}

std::uint64_t FixedStepClock::dropped_ticks() const noexcept
{
    return dropped_;
}

SimulationWorld::SimulationWorld(const double tick_seconds) : tick_seconds_(tick_seconds)
{
    EAW_ASSERT_MSG(tick_seconds > 0.0 && std::isfinite(tick_seconds), "simulation tick length must be positive");
}

Result<EntityId> SimulationWorld::spawn(const EntitySpawn& spawn)
{
    if (!finite(spawn.position) || !std::isfinite(spawn.heading) || !std::isfinite(spawn.max_speed) || spawn.max_speed < 0.0F)
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a non-finite position, heading, or speed");
    }
    if (!valid_amount(spawn.radius) || !valid_amount(spawn.turn_rate) || !valid_amount(spawn.formation_spacing))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite radius, turn rate, or formation spacing");
    }
    if (const auto& footprint = spawn.footprint;
        footprint && (!finite(footprint->center) || !valid_amount(footprint->half_length) || !valid_amount(footprint->half_width) || !std::isfinite(footprint->heading)))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite footprint");
    }
    if (!valid_profile(spawn.combat))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite combat value");
    }
    if (ids_.size() >= no_dense)
    {
        return make_error("simulation entity storage is full");
    }
    std::uint32_t slot{};
    if (!free_slots_.empty())
    {
        slot = free_slots_.back();
        free_slots_.pop_back();
    }
    else
    {
        slot = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1);
        slot_dense_.push_back(no_dense);
    }
    const EntityId id{slot, generations_[slot]};
    slot_dense_[slot] = static_cast<std::uint32_t>(ids_.size());
    ids_.push_back(id);
    positions_.push_back(spawn.position);
    headings_.push_back(wrap_angle(spawn.heading));
    max_speeds_.push_back(spawn.max_speed);
    move_targets_.emplace_back();
    owners_.push_back(spawn.owner);
    visuals_.push_back(spawn.visual);
    selectable_.push_back(spawn.selectable ? 1U : 0U);
    type_names_.push_back(spawn.type_name);
    follows_ground_.push_back(spawn.follows_ground ? 1U : 0U);
    passable_types_.push_back(spawn.passable_types);
    paths_.emplace_back();
    combat_.push_back({.profile = spawn.combat, .health = spawn.combat.health, .shield = spawn.combat.shield});
    radii_.push_back(spawn.radius);
    turn_rates_.push_back(spawn.turn_rate);
    formation_orders_.push_back(spawn.formation_order);
    formation_spacings_.push_back(spawn.formation_spacing);
    speed_limits_.push_back(0.0F);
    group_ids_.push_back(0);
    group_best_remaining_.push_back(0.0F);
    group_stall_ticks_.push_back(0);
    group_waiting_.push_back(0);
    footprints_.push_back(spawn.footprint);
    path_pending_.push_back(0);
    footprint_stamped_.push_back(0);
    stamp_footprint(ids_.size() - 1U, true);
    if (spawn.follows_ground && ground_.valid())
    {
        positions_.back().z = ground_.surface_height(spawn.position.x, spawn.position.y);
    }
    return id;
}

void SimulationWorld::submit(SimCommand command)
{
    pending_.push_back(std::move(command));
}

void SimulationWorld::step()
{
    // Commands first, in submission order, so the tick sees a consistent intent; then systems.
    auto commands = std::move(pending_);
    pending_.clear();
    for (const auto& command : commands)
    {
        apply(command);
    }
    run_movement();
    run_separation();
    run_combat();
    ++tick_;
    publish();
}

bool SimulationWorld::alive(const EntityId id) const noexcept
{
    return dense_index(id).has_value();
}

const SimSnapshot& SimulationWorld::snapshot() const noexcept
{
    return snapshot_;
}

SimulationWorld::Stats SimulationWorld::stats() const noexcept
{
    return {.tick = tick_,
            .entities = ids_.size(),
            .commands_applied = commands_applied_,
            .stale_references = stale_references_,
            .blocked_moves = blocked_moves_,
            .path_searches = path_searches_,
            .partial_paths = partial_paths_,
            .path_expansions = path_expansions_,
            .queued_paths = path_queue_.size(),
            .shared_routes = shared_routes_,
            .shots_fired = shots_fired_,
            .hits = hits_,
            .kills = kills_,
            .projectiles = projectiles_.size(),
            .separated_pairs = separated_pairs_,
            .waiting_members = waiting_members_,
            .released_members = released_members_};
}

std::uint64_t SimulationWorld::state_hash() const noexcept
{
    auto hash = hash_value(14695981039346656037ULL, tick_);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        hash = hash_value(hash, ids_[index]);
        hash = hash_value(hash, positions_[index]);
        hash = hash_value(hash, headings_[index]);
        hash = hash_value(hash, max_speeds_[index]);
        const auto& target = move_targets_[index];
        hash = hash_value(hash, target.has_value());
        if (target)
        {
            hash = hash_value(hash, *target);
        }
        hash = hash_value(hash, owners_[index]);
        hash = hash_value(hash, visuals_[index]);
        hash = hash_value(hash, selectable_[index]);
        hash = hash_value(hash, follows_ground_[index]);
        hash = hash_value(hash, passable_types_[index]);
        hash = hash_value(hash, paths_[index].size());
        for (const auto& waypoint : paths_[index])
        {
            hash = hash_value(hash, waypoint);
        }
        // Field by field: the combat records have padding, whose bytes are unspecified.
        const auto& combat = combat_[index];
        hash = hash_value(hash, combat.health);
        hash = hash_value(hash, combat.shield);
        hash = hash_value(hash, combat.target);
        hash = hash_value(hash, combat.ordered);
        hash = hash_value(hash, combat.cooldown);
        hash = hash_value(hash, combat.pulses_left);
        hash = hash_value(hash, combat.pulse_timer);
        hash = hash_value(hash, combat.retarget_timer);
        hash = hash_value(hash, combat.chase_timer);
        hash = hash_value(hash, radii_[index]);
        hash = hash_value(hash, turn_rates_[index]);
        hash = hash_value(hash, formation_orders_[index]);
        hash = hash_value(hash, formation_spacings_[index]);
        hash = hash_value(hash, speed_limits_[index]);
        hash = hash_value(hash, group_ids_[index]);
        hash = hash_value(hash, group_best_remaining_[index]);
        hash = hash_value(hash, group_stall_ticks_[index]);
        hash = hash_value(hash, path_pending_[index]);
    }
    for (const auto& request : path_queue_)
    {
        for (std::size_t member = 0; member < request.members.size(); ++member)
        {
            hash = hash_value(hash, request.members[member]);
            hash = hash_value(hash, request.goals[member]);
            hash = hash_value(hash, request.serials[member]);
        }
        hash = hash_value(hash, request.group_target);
    }
    for (const auto& projectile : projectiles_)
    {
        hash = hash_value(hash, projectile.position);
        hash = hash_value(hash, projectile.aim);
        hash = hash_value(hash, projectile.target);
        hash = hash_value(hash, projectile.owner);
        hash = hash_value(hash, projectile.damage);
        hash = hash_value(hash, projectile.flown);
    }
    return hash;
}

std::optional<std::size_t> SimulationWorld::dense_index(const EntityId id) const noexcept
{
    if (!id.valid() || id.index >= generations_.size() || generations_[id.index] != id.generation || slot_dense_[id.index] == no_dense)
    {
        return std::nullopt;
    }
    return slot_dense_[id.index];
}

void SimulationWorld::apply(const SimCommand& command)
{
    ++commands_applied_;
    if ((command.kind == SimCommandKind::move || command.kind == SimCommandKind::formation_move) && !finite(command.target))
    {
        ++stale_references_;
        return;
    }
    if (command.kind == SimCommandKind::formation_move)
    {
        apply_formation_move(command);
        return;
    }
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        switch (command.kind)
        {
        case SimCommandKind::formation_move:
            break;
        case SimCommandKind::move:
            combat_[*dense].target = {};
            combat_[*dense].ordered = false;
            speed_limits_[*dense] = 0.0F;
            group_ids_[*dense] = 0;
            if (max_speeds_[*dense] > 0.0F)
            {
                plan_move(*dense, command.target);
            }
            break;
        case SimCommandKind::stop:
            speed_limits_[*dense] = 0.0F;
            group_ids_[*dense] = 0;
            move_targets_[*dense].reset();
            paths_[*dense].clear();
            combat_[*dense].target = {};
            combat_[*dense].ordered = false;
            break;
        case SimCommandKind::attack:
        {
            const auto victim = dense_index(command.target_entity);
            auto& combat = combat_[*dense];
            if (!victim || !damageable(*victim))
            {
                ++stale_references_;
                break;
            }
            if (combat.profile.weapon && *victim != *dense && hostility_.hostile(owners_[*dense], owners_[*victim]))
            {
                combat.target = command.target_entity;
                combat.ordered = true;
                speed_limits_[*dense] = 0.0F;
                group_ids_[*dense] = 0;
                combat.chase_timer = 0;
            }
            break;
        }
        case SimCommandKind::destroy:
            destroy(*dense);
            break;
        }
    }
}

void SimulationWorld::plan_move(const std::size_t index, const SimVector3& target)
{
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

void SimulationWorld::plan_route_now(const std::size_t index, const SimVector3& target)
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
    move_targets_[index] = SimVector3{destination.x, destination.y, height};
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

void SimulationWorld::plan_shared_member(const PathRequest& request, const std::size_t index, const SimVector3& goal)
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

float SimulationWorld::travel_speed(const std::size_t index) const noexcept
{
    const auto limit = speed_limits_[index];
    return limit > 0.0F ? std::min(max_speeds_[index], limit) : max_speeds_[index];
}

void SimulationWorld::destroy(const std::size_t dense)
{
    stamp_footprint(dense, false);
    const auto slot = ids_[dense].index;
    const auto last = ids_.size() - 1U;
    if (dense != last)
    {
        // Swap-remove keeps storage dense; the order change depends only on the command history.
        ids_[dense] = ids_[last];
        positions_[dense] = positions_[last];
        headings_[dense] = headings_[last];
        max_speeds_[dense] = max_speeds_[last];
        move_targets_[dense] = move_targets_[last];
        owners_[dense] = owners_[last];
        visuals_[dense] = visuals_[last];
        selectable_[dense] = selectable_[last];
        type_names_[dense] = std::move(type_names_[last]);
        follows_ground_[dense] = follows_ground_[last];
        passable_types_[dense] = passable_types_[last];
        paths_[dense] = std::move(paths_[last]);
        combat_[dense] = combat_[last];
        radii_[dense] = radii_[last];
        turn_rates_[dense] = turn_rates_[last];
        formation_orders_[dense] = formation_orders_[last];
        formation_spacings_[dense] = formation_spacings_[last];
        speed_limits_[dense] = speed_limits_[last];
        group_ids_[dense] = group_ids_[last];
        group_best_remaining_[dense] = group_best_remaining_[last];
        group_stall_ticks_[dense] = group_stall_ticks_[last];
        group_waiting_[dense] = group_waiting_[last];
        footprints_[dense] = footprints_[last];
        path_pending_[dense] = path_pending_[last];
        footprint_stamped_[dense] = footprint_stamped_[last];
        slot_dense_[ids_[dense].index] = static_cast<std::uint32_t>(dense);
    }
    ids_.pop_back();
    positions_.pop_back();
    headings_.pop_back();
    max_speeds_.pop_back();
    move_targets_.pop_back();
    owners_.pop_back();
    visuals_.pop_back();
    selectable_.pop_back();
    type_names_.pop_back();
    follows_ground_.pop_back();
    passable_types_.pop_back();
    paths_.pop_back();
    combat_.pop_back();
    radii_.pop_back();
    turn_rates_.pop_back();
    formation_orders_.pop_back();
    formation_spacings_.pop_back();
    speed_limits_.pop_back();
    group_ids_.pop_back();
    group_best_remaining_.pop_back();
    group_stall_ticks_.pop_back();
    group_waiting_.pop_back();
    footprints_.pop_back();
    path_pending_.pop_back();
    footprint_stamped_.pop_back();
    slot_dense_[slot] = no_dense;
    // A slot whose generation would wrap is retired instead of reused.
    if (generations_[slot] != std::numeric_limits<std::uint32_t>::max())
    {
        ++generations_[slot];
        free_slots_.push_back(slot);
    }
}

void SimulationWorld::run_movement()
{
    const auto dt = static_cast<float>(tick_seconds_);
    process_path_queue();
    measure_remaining_routes();
    update_group_cohesion();
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        auto& target = move_targets_[index];
        if (!target)
        {
            speed_limits_[index] = 0.0F;
            group_ids_[index] = 0;
            continue;
        }
        if (group_waiting_[index] != 0 || path_pending_[index] != 0)
        {
            continue;
        }
        auto& position = positions_[index];
        if (follows_ground_[index] != 0 && ground_.valid())
        {
            move_over_ground(index, dt);
            continue;
        }
        const SimVector3 delta{target->x - position.x, target->y - position.y, target->z - position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto speed_factor = delta.x != 0.0F || delta.y != 0.0F ? steer(index, std::atan2(delta.y, delta.x), dt) : 1.0F;
        const auto step_length = travel_speed(index) * dt * speed_factor;
        if (distance <= std::max(step_length, arrival_distance))
        {
            position = *target;
            target.reset();
            continue;
        }
        const auto scale = step_length / distance;
        position = {position.x + delta.x * scale, position.y + delta.y * scale, position.z + delta.z * scale};
    }
}

void SimulationWorld::move_over_ground(const std::size_t index, const float dt)
{
    auto& target = move_targets_[index];
    auto& position = positions_[index];
    auto& path = paths_[index];
    const auto allowed = passable_types_[index];
    // Ground units move in the horizontal plane; their height always comes from the surface. A
    // tick's movement carries on past reached waypoints, so corners do not slow a unit down.
    auto remaining = travel_speed(index) * dt;
    // An intermediate waypoint is passed once the one after it is in straight reach. A crowd keeps
    // pushing units away from a shared corner waypoint, so requiring each unit to touch it left
    // groups circling there (found by the path budget test).
    if (allowed != 0)
    {
        while (!path.empty())
        {
            const auto& after = path.size() > 1 ? path[path.size() - 2] : *target;
            if (!ground_line_passable(ground_, {position.x, position.y}, {after.x, after.y}, allowed))
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
        if ((delta_x != 0.0F || delta_y != 0.0F) && distance > arrival_distance)
        {
            remaining *= steer(index, std::atan2(delta_y, delta_x), dt);
        }
        const bool arrives = distance <= std::max(remaining, arrival_distance);
        const auto scale = arrives || distance <= 0.0F ? 1.0F : remaining / distance;
        const auto next_x = position.x + delta_x * scale;
        const auto next_y = position.y + delta_y * scale;
        if (allowed != 0)
        {
            const auto here = ground_.passability_at(position.x, position.y);
            const auto there = ground_.passability_at(next_x, next_y);
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
        position = {next_x, next_y, ground_.surface_height(next_x, next_y)};
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
        if (remaining <= 0.0F || turn_rates_[index] > 0.0F)
        {
            return;
        }
    }
}

float SimulationWorld::steer(const std::size_t index, const float desired, const float dt)
{
    auto& heading = headings_[index];
    const auto rate = turn_rates_[index];
    auto difference = wrap_angle(desired - heading);
    if (rate <= 0.0F)
    {
        heading = wrap_angle(desired);
        return 1.0F;
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

// Overlapping ground units are pushed apart, half each by default. Displacements are summed for
// every pair first and applied afterwards, so the result does not depend on which unit of a pair
// is visited first. Entities that cannot move are never pushed (they push the other unit the whole
// way); idle units yield more than moving ones, so a moving unit shoulders through instead of
// stopping. A push never moves a unit onto a cell its class may not enter, and it is limited per
// tick so packed groups spread out over a few ticks instead of jumping.
void SimulationWorld::run_separation()
{
    separated_pairs_ = 0;
    if (!ground_.valid())
    {
        return;
    }
    grid_.build(positions_, grid_cell_size);
    separation_push_.assign(ids_.size(), SimVector3{});
    float largest_radius = 0.0F;
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (follows_ground_[index] != 0)
        {
            largest_radius = std::max(largest_radius, radii_[index]);
        }
    }
    // Remaining route length of every moving unit: of two moving units, the one further along
    // (less route left) has priority and yields much less, so units queue through a narrow
    // passage instead of shoving each other back and forth (Age of Empires II's priority rule).
    measure_remaining_routes();
    const auto weight = [&](const std::size_t index, const std::size_t other)
    {
        if (max_speeds_[index] <= 0.0F)
        {
            return 0.0F;
        }
        // Only units of one owner make way for each other. An idle unit of another owner (an enemy
        // or a neutral) stands its ground against a moving one, which is held back instead; two
        // foreign units that both move or both stand split the push evenly.
        if (owners_[index] != owners_[other])
        {
            return !move_targets_[index] && move_targets_[other] ? 0.0F : 1.0F;
        }
        if (!move_targets_[index])
        {
            return 2.0F;
        }
        if (move_targets_[other] && max_speeds_[other] > 0.0F && remaining_route_[index] != remaining_route_[other])
        {
            return remaining_route_[index] < remaining_route_[other] ? 0.25F : 1.75F;
        }
        return 1.0F;
    };
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (follows_ground_[index] == 0 || radii_[index] <= 0.0F)
        {
            continue;
        }
        query_.clear();
        grid_.query(positions_[index], radii_[index] + largest_radius, query_);
        for (const auto other : query_)
        {
            // Each pair once, from its lower dense index.
            if (other <= index || follows_ground_[other] == 0 || radii_[other] <= 0.0F)
            {
                continue;
            }
            const auto dx = positions_[other].x - positions_[index].x;
            const auto dy = positions_[other].y - positions_[index].y;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            const auto overlap = radii_[index] + radii_[other] - distance;
            const auto first_weight = weight(index, other);
            const auto second_weight = weight(other, index);
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
                const auto angle = static_cast<float>((ids_[index].index * 2654435761U + ids_[other].index) % 6283U) / 1000.0F;
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
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        auto push = separation_push_[index];
        const auto length = std::sqrt(push.x * push.x + push.y * push.y);
        if (length <= 0.0F)
        {
            continue;
        }
        // At most half a radius, or the unit's own speed, per tick.
        const auto limit = std::max(radii_[index] * 0.5F, max_speeds_[index] * dt);
        if (length > limit)
        {
            push.x *= limit / length;
            push.y *= limit / length;
        }
        auto& position = positions_[index];
        const auto next_x = position.x + push.x;
        const auto next_y = position.y + push.y;
        if (const auto allowed = passable_types_[index]; allowed != 0)
        {
            const auto there = ground_.passability_at(next_x, next_y);
            if (there && (*there & allowed) == 0)
            {
                continue;
            }
        }
        position = {next_x, next_y, ground_.surface_height(next_x, next_y)};
    }
}

std::vector<SimVector3> SimulationWorld::route(const EntityId id) const
{
    std::vector<SimVector3> points;
    const auto dense = dense_index(id);
    if (!dense || !move_targets_[*dense])
    {
        return points;
    }
    points.assign(paths_[*dense].rbegin(), paths_[*dense].rend());
    points.push_back(*move_targets_[*dense]);
    return points;
}

void SimulationWorld::set_damage_table(DamageTable table)
{
    damage_table_ = std::move(table);
}

void SimulationWorld::set_hostility(const Hostility& hostility) noexcept
{
    hostility_ = hostility;
}

const Hostility& SimulationWorld::hostility() const noexcept
{
    return hostility_;
}

std::uint32_t SimulationWorld::ticks_for(const float seconds) const noexcept
{
    const auto ticks = std::lround(static_cast<double>(seconds) / tick_seconds_);
    return static_cast<std::uint32_t>(std::clamp<long>(ticks, 0L, 1L << 30));
}

bool SimulationWorld::damageable(const std::size_t index) const noexcept
{
    return combat_[index].profile.health > 0.0F && combat_[index].health > 0.0F;
}

// Order within a tick: shields recharge, then every armed entity (in dense order) updates its
// target and fires, then shots fly and hit, then the dead are removed. Entities are only removed
// at the end, so dense indices stay valid throughout.
void SimulationWorld::run_combat()
{
    const auto dt = static_cast<float>(tick_seconds_);
    grid_.build(positions_, grid_cell_size);
    for (auto& combat : combat_)
    {
        if (combat.shield < combat.profile.shield && combat.health > 0.0F)
        {
            combat.shield = std::min(combat.profile.shield, combat.shield + combat.profile.shield_regen_per_second * dt);
        }
    }
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (combat_[index].profile.weapon)
        {
            update_target(index);
            run_weapon(index);
        }
    }
    run_projectiles();
    remove_dead();
}

void SimulationWorld::update_target(const std::size_t index)
{
    auto& combat = combat_[index];
    const auto& weapon = *combat.profile.weapon;
    if (combat.target.valid())
    {
        const auto victim = dense_index(combat.target);
        bool keep = victim && damageable(*victim) && hostility_.hostile(owners_[index], owners_[*victim]);
        if (keep && !combat.ordered)
        {
            keep = distance_xy(positions_[index], positions_[*victim]) <= weapon.range;
        }
        if (!keep)
        {
            combat.target = {};
            combat.ordered = false;
        }
    }
    if (combat.ordered)
    {
        // Chase an ordered target until it is in range, then hold position and fire.
        const auto victim = *dense_index(combat.target);
        if (distance_xy(positions_[index], positions_[victim]) > weapon.range)
        {
            if (max_speeds_[index] > 0.0F && (combat.chase_timer == 0 || !move_targets_[index]))
            {
                // Flyers keep their altitude; ground units take theirs from the surface anyway.
                plan_move(index, {positions_[victim].x, positions_[victim].y, follows_ground_[index] != 0 ? positions_[victim].z : positions_[index].z});
                combat.chase_timer = chase_interval;
            }
        }
        else if (move_targets_[index])
        {
            move_targets_[index].reset();
            paths_[index].clear();
        }
        if (combat.chase_timer > 0)
        {
            --combat.chase_timer;
        }
        return;
    }
    if (combat.target.valid())
    {
        return;
    }
    if (combat.retarget_timer > 0)
    {
        --combat.retarget_timer;
        return;
    }
    combat.retarget_timer = retarget_interval;
    // The nearest damageable hostile in range; equal distances go to the lower entity id.
    query_.clear();
    grid_.query(positions_[index], weapon.range, query_);
    std::optional<std::size_t> best;
    float best_distance = 0.0F;
    for (const auto candidate : query_)
    {
        if (candidate == index || !damageable(candidate) || !hostility_.hostile(owners_[index], owners_[candidate]))
        {
            continue;
        }
        const auto distance = distance_xy(positions_[index], positions_[candidate]);
        const auto lower_id = [&](const std::size_t a, const std::size_t b)
        {
            return ids_[a].index != ids_[b].index ? ids_[a].index < ids_[b].index : ids_[a].generation < ids_[b].generation;
        };
        if (!best || distance < best_distance || (distance == best_distance && lower_id(candidate, *best)))
        {
            best = candidate;
            best_distance = distance;
        }
    }
    if (best)
    {
        combat.target = ids_[*best];
    }
}

void SimulationWorld::run_weapon(const std::size_t index)
{
    auto& combat = combat_[index];
    const auto& weapon = *combat.profile.weapon;
    if (combat.cooldown > 0)
    {
        --combat.cooldown;
    }
    if (combat.pulse_timer > 0)
    {
        --combat.pulse_timer;
    }
    const auto victim = combat.target.valid() ? dense_index(combat.target) : std::nullopt;
    if (!victim)
    {
        combat.pulses_left = 0;
        return;
    }
    if (combat.pulses_left == 0)
    {
        if (combat.cooldown != 0 || distance_xy(positions_[index], positions_[*victim]) > weapon.range)
        {
            return;
        }
        combat.pulses_left = std::max(1U, weapon.pulse_count);
        combat.pulse_timer = 0;
        combat.cooldown = std::max(1U, ticks_for(weapon.recharge_seconds));
    }
    // A burst keeps firing at its target, in range or not, until its shots are spent.
    while (combat.pulses_left > 0 && combat.pulse_timer == 0)
    {
        launch(index, *victim);
        --combat.pulses_left;
        combat.pulse_timer = ticks_for(weapon.pulse_delay_seconds);
    }
}

void SimulationWorld::launch(const std::size_t shooter, const std::size_t victim)
{
    const auto& weapon = *combat_[shooter].profile.weapon;
    ++shots_fired_;
    if (weapon.projectile_speed <= 0.0F)
    {
        ++hits_;
        apply_damage(victim, weapon.damage, weapon.damage_type, weapon.damages_shield, weapon.damages_health);
        return;
    }
    const auto& from = positions_[shooter];
    const auto& to = positions_[victim];
    Projectile projectile;
    projectile.position = {from.x, from.y, from.z + combat_[shooter].profile.aim_height};
    projectile.aim = {to.x, to.y, to.z + combat_[victim].profile.aim_height};
    projectile.target = ids_[victim];
    projectile.owner = owners_[shooter];
    projectile.damage = weapon.damage;
    projectile.damage_type = weapon.damage_type;
    projectile.damages_shield = weapon.damages_shield;
    projectile.damages_health = weapon.damages_health;
    projectile.speed = weapon.projectile_speed;
    projectile.max_flight = weapon.max_flight_distance;
    projectiles_.push_back(projectile);
}

// Provisional semantics: shields absorb a shot first (scaled by the shield armor type); what the
// shield cannot absorb, converted back to raw damage, reaches the hull scaled by the armor type.
void SimulationWorld::apply_damage(const std::size_t victim, float damage, const std::uint16_t damage_type, const bool damages_shield, const bool damages_health)
{
    auto& combat = combat_[victim];
    if (damages_shield && combat.shield > 0.0F)
    {
        const auto modifier = damage_table_.modifier(damage_type, combat.profile.shield_armor_type);
        const auto dealt = damage * modifier;
        if (dealt <= combat.shield)
        {
            combat.shield -= dealt;
            return;
        }
        damage = modifier > 0.0F ? (dealt - combat.shield) / modifier : 0.0F;
        combat.shield = 0.0F;
    }
    if (damages_health)
    {
        combat.health -= damage * damage_table_.modifier(damage_type, combat.profile.armor_type);
    }
}

void SimulationWorld::run_projectiles()
{
    const auto dt = static_cast<float>(tick_seconds_);
    std::size_t kept = 0;
    for (std::size_t index = 0; index < projectiles_.size(); ++index)
    {
        auto projectile = projectiles_[index];
        // Shots home on their target's current aim point; a dead target's shot flies on to the
        // last point and is lost there.
        const auto victim = dense_index(projectile.target);
        if (victim)
        {
            const auto& to = positions_[*victim];
            projectile.aim = {to.x, to.y, to.z + combat_[*victim].profile.aim_height};
        }
        const SimVector3 delta{projectile.aim.x - projectile.position.x, projectile.aim.y - projectile.position.y, projectile.aim.z - projectile.position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto step_length = projectile.speed * dt;
        if (distance > 0.0F)
        {
            projectile.direction = {delta.x / distance, delta.y / distance, delta.z / distance};
        }
        if (distance <= step_length)
        {
            if (victim && damageable(*victim))
            {
                ++hits_;
                apply_damage(*victim, projectile.damage, projectile.damage_type, projectile.damages_shield, projectile.damages_health);
            }
            continue;
        }
        const auto scale = step_length / distance;
        projectile.position = {projectile.position.x + delta.x * scale, projectile.position.y + delta.y * scale, projectile.position.z + delta.z * scale};
        projectile.flown += step_length;
        if (projectile.max_flight > 0.0F && projectile.flown > projectile.max_flight)
        {
            continue;
        }
        projectiles_[kept++] = projectile;
    }
    projectiles_.resize(kept);
}

void SimulationWorld::remove_dead()
{
    // Back to front: swap-removal only moves already-visited entities.
    for (auto index = ids_.size(); index-- > 0;)
    {
        if (combat_[index].profile.health > 0.0F && combat_[index].health <= 0.0F)
        {
            destroy(index);
            ++kills_;
        }
    }
}

void SimulationWorld::set_ground(GroundMap ground)
{
    ground_ = std::move(ground);
    local_pathfinder_.reset();
    ground_passability_ = ground_.passability;
    footprint_cover_.assign(ground_passability_.size(), 0);
    std::ranges::fill(footprint_stamped_, std::uint8_t{0});
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        stamp_footprint(index, true);
    }
    pathfinder_.reset();
}

// Cells whose sample point lies inside the footprint rectangle take its passability type while any
// footprint covers them; the last footprint removed restores the ground's own value. The cached
// region labels are dropped, so paths see the change.
void SimulationWorld::stamp_footprint(const std::size_t index, const bool add)
{
    const auto& footprint = footprints_[index];
    if (!footprint || !ground_.valid() || ground_.passability.size() != ground_.heights.size() || footprint_cover_.size() != ground_.passability.size() || (footprint_stamped_[index] != 0) == add)
    {
        return;
    }
    const auto cos_heading = std::cos(footprint->heading);
    const auto sin_heading = std::sin(footprint->heading);
    const auto reach = std::hypot(footprint->half_length, footprint->half_width);
    const auto spacing = ground_.spacing;
    const auto first_column = std::max(0L, static_cast<long>(std::floor((footprint->center.x - reach) / spacing)));
    const auto last_column = std::min(static_cast<long>(ground_.width) - 1, static_cast<long>(std::ceil((footprint->center.x + reach) / spacing)));
    const auto first_row = std::max(0L, static_cast<long>(std::floor((footprint->center.y - reach) / spacing)));
    const auto last_row = std::min(static_cast<long>(ground_.height) - 1, static_cast<long>(std::ceil((footprint->center.y + reach) / spacing)));
    for (auto row = first_row; row <= last_row; ++row)
    {
        for (auto column = first_column; column <= last_column; ++column)
        {
            const auto dx = static_cast<float>(column) * spacing - footprint->center.x;
            const auto dy = static_cast<float>(row) * spacing - footprint->center.y;
            const auto along = dx * cos_heading + dy * sin_heading;
            const auto across = -dx * sin_heading + dy * cos_heading;
            if (std::fabs(along) > footprint->half_length || std::fabs(across) > footprint->half_width)
            {
                continue;
            }
            const auto cell = static_cast<std::size_t>(row) * ground_.width + static_cast<std::size_t>(column);
            if (add)
            {
                ++footprint_cover_[cell];
                ground_.passability[cell] = footprint->passability;
            }
            else if (footprint_cover_[cell] > 0 && --footprint_cover_[cell] == 0)
            {
                ground_.passability[cell] = ground_passability_[cell];
            }
        }
    }
    footprint_stamped_[index] = add ? 1U : 0U;
    pathfinder_.reset();
    local_pathfinder_.reset();
}

const GroundMap& SimulationWorld::ground() const noexcept
{
    return ground_;
}

void SimulationWorld::publish()
{
    snapshot_.tick = tick_;
    snapshot_.entities.clear();
    snapshot_.entities.reserve(ids_.size());
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        const auto& combat = combat_[index];
        snapshot_.entities.push_back({ids_[index], positions_[index], headings_[index], visuals_[index], owners_[index], move_targets_[index].has_value(), selectable_[index] != 0, max_speeds_[index],
                                      follows_ground_[index] != 0, combat.health, combat.profile.health, combat.shield, combat.profile.shield, combat.target, radii_[index]});
    }
    snapshot_.projectiles.clear();
    snapshot_.projectiles.reserve(projectiles_.size());
    for (const auto& projectile : projectiles_)
    {
        snapshot_.projectiles.push_back({projectile.position, projectile.direction, projectile.owner});
    }
}

std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, const float alpha)
{
    const auto t = std::clamp(std::isfinite(alpha) ? alpha : 1.0F, 0.0F, 1.0F);
    std::unordered_map<std::uint64_t, const EntitySnapshot*> before;
    before.reserve(previous.entities.size());
    for (const auto& entity : previous.entities)
    {
        before.emplace((static_cast<std::uint64_t>(entity.id.generation) << 32U) | entity.id.index, &entity);
    }
    std::vector<EntitySnapshot> result;
    result.reserve(current.entities.size());
    for (const auto& entity : current.entities)
    {
        auto blended = entity;
        if (const auto found = before.find((static_cast<std::uint64_t>(entity.id.generation) << 32U) | entity.id.index); found != before.end())
        {
            const auto& old = *found->second;
            blended.position = {old.position.x + (entity.position.x - old.position.x) * t, old.position.y + (entity.position.y - old.position.y) * t,
                                old.position.z + (entity.position.z - old.position.z) * t};
            blended.heading = old.heading + wrap_angle(entity.heading - old.heading) * t;
        }
        result.push_back(blended);
    }
    return result;
}

} // namespace eaw
