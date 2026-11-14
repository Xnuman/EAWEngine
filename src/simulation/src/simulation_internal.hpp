#pragma once

// Shared constants and helpers of the simulation's implementation files (simulation.cpp: world
// core; simulation_behavior.cpp: orders and behavior; simulation_navigation.cpp: routes;
// simulation_locomotion.cpp: movement; simulation_combat.cpp: targeting, weapons, damage). Not part
// of the public interface.

#include "math/scalar.hpp"
#include "simulation/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace eaw::simulation_detail {

inline constexpr std::uint32_t no_dense = std::numeric_limits<std::uint32_t>::max();
// Arrival tolerance in world units.
inline constexpr float arrival_distance = 0.01F;
// Broad-phase cell size in world units (a little above typical weapon ranges).
inline constexpr float grid_cell_size = 256.0F;
// Separation's grid cells: twice the largest footprint taking part (a query then visits a few cells
// of near neighbours instead of every unit within 256 units: 74% of a 3,300-unit tick went there),
// within these bounds. Engine choices; the pairs found are the same at any cell size.
inline constexpr float separation_min_cell_size = 16.0F;
// Ticks between automatic target searches of an idle weapon, and between replans of an ordered
// chase whose target is out of range. Engine choices, not measured from the original.
inline constexpr std::uint32_t retarget_interval = 10;
inline constexpr std::uint32_t chase_interval = 30;
// A formation member waits while it is more than this much route ahead of its group's furthest
// member (plus two footprint diameters), and leaves its group after this many ticks without
// progress while not waiting. Engine choices.
inline constexpr float cohesion_slack = 30.0F;
inline constexpr std::uint32_t cohesion_stall_ticks = 90;
// Path search nodes expanded per tick at most (about 5 ms in a Release build), always at least one
// request. An engine choice; requests beyond it wait for the next tick in order.
inline constexpr std::uint64_t path_budget_per_tick = 30000;
// Steepest climb or dive of a flyer (radians). An engine choice.
inline constexpr float flight_max_pitch = pi / 3.0F;
// A flyer reverses through a half-loop instead of a flat turn when its goal lies more than this far
// off its heading (radians) and more than two turn circles away. Engine choices; the original's
// trigger angle is not measured.
inline constexpr float flight_reversal_angle = pi * 0.75F;
// Dogfights: flyers weave this far above and below their target (world units) over this many
// seconds, staying within the band around their plane. Engine choices.
inline constexpr float dogfight_weave = 60.0F;
inline constexpr float dogfight_weave_seconds = 8.0F;
inline constexpr float dogfight_band = 150.0F;

inline bool valid_amount(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

inline bool valid_profile(const CombatProfile& profile) noexcept
{
    if (!valid_amount(profile.health) || !valid_amount(profile.shield) || !valid_amount(profile.attack_move_response_range) || !valid_amount(profile.guard_chase_range) ||
        !valid_amount(profile.stickiness_seconds) || !valid_amount(profile.death_blast.damage) || !valid_amount(profile.death_blast.range) || !valid_amount(profile.spin_chance) ||
        (profile.turret && (!valid_amount(profile.turret->rate) || !valid_amount(profile.turret->yaw_limit))) || !valid_amount(profile.spin_seconds) || !valid_amount(profile.shield_refresh) ||
        !valid_amount(profile.shield_refresh_seconds) || !std::isfinite(profile.aim_height))
    {
        return false;
    }
    const auto finite_point = [](const LocalPoint& point)
    {
        return std::isfinite(point.forward) && std::isfinite(point.left) && std::isfinite(point.up);
    };
    for (const auto& hardpoint : profile.hardpoints)
    {
        if (!valid_amount(hardpoint.health) || !finite_point(hardpoint.offset))
        {
            return false;
        }
    }
    for (const auto& weapon : profile.weapons)
    {
        if (!valid_amount(weapon.range) || !valid_amount(weapon.min_range) || !valid_amount(weapon.recharge_seconds) || !valid_amount(weapon.pulse_delay_seconds) || !valid_amount(weapon.damage) ||
            !valid_amount(weapon.projectile_speed) || !valid_amount(weapon.max_flight_distance) || !valid_amount(weapon.blast.damage) || !valid_amount(weapon.blast.range) ||
            !valid_amount(weapon.blast.friendly_damage) || !valid_amount(weapon.max_lifetime) || !valid_amount(weapon.scan_range) || !valid_amount(weapon.appearance_delay_seconds) ||
            !finite_point(weapon.mount) || !std::ranges::all_of(weapon.extra_mounts, finite_point) || !std::isfinite(weapon.cone_direction) || !valid_amount(weapon.cone_half_angle) ||
            weapon.hardpoint < -1 || weapon.hardpoint >= static_cast<std::int32_t>(profile.hardpoints.size()))
        {
            return false;
        }
        for (const auto& entry : weapon.inaccuracy)
        {
            if (!valid_amount(entry.distance))
            {
                return false;
            }
        }
    }
    return true;
}

inline float distance_xy(const Vector3& from, const Vector3& to) noexcept
{
    return std::hypot(to.x - from.x, to.y - from.y);
}

inline std::uint64_t fnv1a(std::uint64_t hash, const void* data, const std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

template <class T> inline std::uint64_t hash_value(const std::uint64_t hash, const T& value) noexcept
{
    return fnv1a(hash, &value, sizeof(value));
}

// The state hash and its subject hashes in one pass: every field goes into the total, in the order
// state_hash always used, and into the hash of the current subject.
struct StateHasher
{
    static constexpr std::uint64_t basis = 14695981039346656037ULL;

    std::uint64_t total = basis;
    StateHashes parts{};
    StateSubject subject = StateSubject::bodies;

    StateHasher() noexcept
    {
        parts.fill(basis);
    }
    void add_bytes(const void* data, const std::size_t size) noexcept
    {
        total = fnv1a(total, data, size);
        auto& part = parts[static_cast<std::size_t>(subject)];
        part = fnv1a(part, data, size);
    }
    template <class T> void add(const T& value) noexcept
    {
        add_bytes(&value, sizeof(value));
    }
};

inline float wrap_angle(float angle) noexcept
{
    angle = std::fmod(angle, two_pi);
    if (angle > pi)
    {
        angle -= two_pi;
    }
    else if (angle < -pi)
    {
        angle += two_pi;
    }
    return angle;
}

} // namespace eaw::simulation_detail

namespace eaw {

template <class Data, typename Accepts>
std::optional<std::size_t> SimulationWorld::choose_target(Data& data, const SpatialGrid& grid, std::vector<std::uint32_t>& query, const std::size_t index, const Vector3& center, const float reach,
                                                          const Accepts& accepts)
{
    auto& identity = data.template get<IdentityColumns>();
    auto& placement = data.template get<PlacementColumns>();
    auto& combat = data.template get<CombatColumns>();
    auto& rules = data.template get<Rules>();
    const auto& priorities = combat.states[index].profile.targeting;
    query.clear();
    grid.query(center, reach, query);
    std::optional<std::size_t> best;
    float best_rank = 0.0F;
    float best_distance = 0.0F;
    for (const auto candidate : query)
    {
        if (candidate == index || !damageable(data, candidate) || !combat.states[candidate].profile.targetable || !rules.hostility.hostile(identity.owners[index], identity.owners[candidate]) ||
            !accepts(candidate))
        {
            continue;
        }
        const auto& profile = combat.states[candidate].profile;
        const auto rank = priorities.rank(profile.categories, profile.properties, profile.type_key);
        const auto distance = simulation_detail::distance_xy(center, placement.positions[candidate]);
        if (rank < 0.0F || distance > reach)
        {
            continue;
        }
        const auto lower = best && (identity.ids[candidate].index != identity.ids[*best].index ? identity.ids[candidate].index < identity.ids[*best].index
                                                                                               : identity.ids[candidate].generation < identity.ids[*best].generation);
        if (!best || rank < best_rank || (rank == best_rank && (distance < best_distance || (distance == best_distance && lower))))
        {
            best = candidate;
            best_rank = rank;
            best_distance = distance;
        }
    }
    return best;
}

template <class Data> float SimulationWorld::travel_speed(Data& data, const std::size_t index) noexcept
{
    auto& locomotor = data.template get<LocomotorColumns>();
    const auto limit = locomotor.speed_limits[index];
    const auto own = keeps(data, index, HardpointProfile::Kind::engine) ? locomotor.max_speeds[index] : locomotor.max_speeds[index] * 0.25F;
    return limit > 0.0F ? std::min(own, limit) : own;
}

template <class Data> bool SimulationWorld::keeps(Data& data, const std::size_t index, const HardpointProfile::Kind kind) noexcept
{
    auto& combat = data.template get<CombatColumns>();
    bool any = false;
    const auto& hardpoints = combat.states[index].profile.hardpoints;
    for (std::size_t hardpoint = 0; hardpoint < hardpoints.size(); ++hardpoint)
    {
        if (hardpoints[hardpoint].kind == kind)
        {
            any = true;
            if (hardpoint_alive(data, index, hardpoint))
            {
                return true;
            }
        }
    }
    return !any;
}

template <class Data> bool SimulationWorld::hardpoint_alive(Data& data, const std::size_t index, const std::size_t hardpoint) noexcept
{
    auto& combat = data.template get<CombatColumns>();
    const auto& state = combat.states[index];
    return state.profile.hardpoints[hardpoint].health <= 0.0F || state.hardpoint_health[hardpoint] > 0.0F;
}

template <class Data> SimulationWorld::TurnPoint SimulationWorld::turn_point(Data& data, const std::size_t index, const float time) noexcept
{
    auto& locomotor = data.template get<LocomotorColumns>();
    auto& turn_clips = data.template get<ComponentPool<TurnClipColumns>>();
    const auto& movement = locomotor.movements[index];
    const auto& plan = turn_clips.get(slot_of(data, index), &TurnClipColumns::plans);
    const auto side = plan.way > 0.0F ? 0U : 1U;
    const auto begin = std::max(movement.turn_begin_seconds[side], 1.0e-6F);
    const auto end = std::max(movement.turn_end_seconds[side], 1.0e-6F);
    const auto loop = movement.turn_loop_seconds[side];
    const auto half = movement.turn_half_seconds[side];
    const auto quarter = movement.turn_quarter_seconds[side];
    // Each cycle turns at its nominal rate: the loop at the stopped rate, HALF and QUARTER through a
    // half and a quarter of the loop's angle in their own time; all scaled to end on the way.
    const auto rate = movement.stopped_turn_rate * plan.scale;
    struct Cycle
    {
        TurnClip clip;
        float seconds;
        float rate;
        std::uint32_t count;
    };
    const std::array<Cycle, 3> cycles{{
        {TurnClip::loop, loop, rate, loop > 0.0F ? plan.loops : 0U},
        {TurnClip::half, half, half > 0.0F ? rate * loop / (2.0F * half) : 0.0F, half > 0.0F ? plan.halves : 0U},
        {TurnClip::quarter, quarter, quarter > 0.0F ? rate * loop / (4.0F * quarter) : 0.0F, quarter > 0.0F ? plan.quarters : 0U},
    }};
    // BEGIN winds up to the first cycle's rate and END down from the last one's (the loop's rate
    // without cycles), linearly.
    auto first = rate;
    auto last = rate;
    bool any = false;
    for (const auto& cycle : cycles)
    {
        if (cycle.count > 0U)
        {
            first = any ? first : cycle.rate;
            last = cycle.rate;
            any = true;
        }
    }
    if (time < begin)
    {
        return {TurnClip::begin, time / begin, first * time * time / (2.0F * begin)};
    }
    auto turned = 0.5F * first * begin;
    auto at = begin;
    for (const auto& cycle : cycles)
    {
        const auto span = static_cast<float>(cycle.count) * cycle.seconds;
        if (cycle.count > 0U && time < at + span)
        {
            const auto into = time - at;
            const auto whole = std::floor(into / cycle.seconds);
            return {cycle.clip, (into - whole * cycle.seconds) / cycle.seconds, turned + cycle.rate * into};
        }
        turned += cycle.rate * span;
        at += span;
    }
    const auto into = std::clamp(time - at, 0.0F, end);
    return {TurnClip::end, into / end, turned + last * (into - into * into / (2.0F * end))};
}

template <class Data> bool SimulationWorld::turning_as_clips(Data& data, const std::size_t index) noexcept
{
    auto& turn_clips = data.template get<ComponentPool<TurnClipColumns>>();
    const auto* plan = turn_clips.find(slot_of(data, index), &TurnClipColumns::plans);
    return plan != nullptr && plan->active;
}

template <class Data> void SimulationWorld::plan_move(Data& data, const std::size_t index, const Vector3& target)
{
    auto& identity = data.template get<IdentityColumns>();
    auto& ground_mover = data.template get<GroundMoverColumns>();
    auto& route = data.template get<RouteColumns>();
    auto& walk_clips = data.template get<ComponentPool<WalkClipColumns>>();
    auto& ground = data.template get<GroundState>();
    auto& path_queue = data.template get<PathQueue>();
    // A walk played as clips given a new destination (not a route planned again to the same one)
    // looks at its first way to it before it turns (steer).
    if (auto* walk = walk_clips.find(slot_of(data, index), &WalkClipColumns::plans);
        walk != nullptr && (walk->clip == WalkClip::starting || walk->clip == WalkClip::moving) &&
        (!route.move_targets[index] || std::hypot(route.move_targets[index]->x - target.x, route.move_targets[index]->y - target.y) > simulation_detail::arrival_distance))
    {
        walk->new_way = true;
    }
    route.loiter_points[index].reset();
    route.paths[index].clear();
    route.pending[index] = 0;
    if (ground_mover.follows_ground[index] == 0 || ground_mover.passable_types[index] == 0 || ground.map.passability.size() != ground.map.heights.size() || !ground.map.valid())
    {
        route.move_targets[index] = target;
        return;
    }
    // The unit holds still (its target set, so it counts as moving) until the queue plans it.
    route.move_targets[index] = target;
    const auto serial = path_queue.next_serial++;
    if (path_queue.next_serial == 0)
    {
        path_queue.next_serial = 1;
    }
    route.pending[index] = serial;
    path_queue.requests.push_back({{identity.ids[index]}, {target}, {serial}, target});
}
template <class Data> std::optional<std::size_t> SimulationWorld::dense_index(Data& data, const EntityId id) noexcept
{
    auto& slots = data.template get<SlotTable>();
    if (!id.valid() || id.index >= slots.generations.size() || slots.generations[id.index] != id.generation || slots.dense[id.index] == simulation_detail::no_dense)
    {
        return std::nullopt;
    }
    return slots.dense[id.index];
}
template <class Data> std::int32_t SimulationWorld::aim_hardpoint(Data& data, const std::size_t victim, const Vector3& from) noexcept
{
    auto& combat = data.template get<CombatColumns>();
    std::int32_t best = -1;
    float best_distance = 0.0F;
    const auto& hardpoints = combat.states[victim].profile.hardpoints;
    for (std::size_t hardpoint = 0; hardpoint < hardpoints.size(); ++hardpoint)
    {
        // Indestructible parts are not aimed at; destroyed ones no longer are.
        if (!hardpoints[hardpoint].targetable || hardpoints[hardpoint].health <= 0.0F || combat.states[victim].hardpoint_health[hardpoint] <= 0.0F)
        {
            continue;
        }
        const auto point = world_point(data, victim, hardpoints[hardpoint].offset);
        const auto distance = (point.x - from.x) * (point.x - from.x) + (point.y - from.y) * (point.y - from.y) + (point.z - from.z) * (point.z - from.z);
        if (best < 0 || distance < best_distance)
        {
            best = static_cast<std::int32_t>(hardpoint);
            best_distance = distance;
        }
    }
    return best;
}

template <class Data> Vector3 SimulationWorld::aim_point(Data& data, const std::size_t victim, const std::int32_t hardpoint) noexcept
{
    auto& placement = data.template get<PlacementColumns>();
    auto& combat = data.template get<CombatColumns>();
    if (hardpoint >= 0)
    {
        return world_point(data, victim, combat.states[victim].profile.hardpoints[static_cast<std::size_t>(hardpoint)].offset);
    }
    const auto& to = placement.positions[victim];
    return {to.x, to.y, to.z + combat.states[victim].profile.aim_height};
}

template <class Data> std::optional<std::size_t> SimulationWorld::attack_victim(Data& data, const std::size_t index) noexcept
{
    auto& identity = data.template get<IdentityColumns>();
    auto& combat = data.template get<CombatColumns>();
    auto& rules = data.template get<Rules>();
    const auto& state = combat.states[index];
    if (!state.target.valid())
    {
        return std::nullopt;
    }
    const auto victim = dense_index(data, state.target);
    if (!victim || !damageable(data, *victim) || !rules.hostility.hostile(identity.owners[index], identity.owners[*victim]))
    {
        return std::nullopt;
    }
    return victim;
}

template <class Data> bool SimulationWorld::damageable(Data& data, const std::size_t index) noexcept
{
    auto& combat = data.template get<CombatColumns>();
    return combat.states[index].profile.health > 0.0F && combat.states[index].health > 0.0F;
}

template <class Data> bool SimulationWorld::engages(Data& data, const std::size_t index, const std::size_t candidate) noexcept
{
    auto& combat = data.template get<CombatColumns>();
    const auto& weapons = combat.states[index].profile.weapons;
    const auto categories = combat.states[candidate].profile.categories;
    return weapons.empty() || std::ranges::any_of(weapons,
                                                  [&](const WeaponProfile& weapon)
                                                  {
                                                      return (weapon.excluded_categories & categories) == 0;
                                                  });
}

template <class Data> Vector3 SimulationWorld::world_point(Data& data, const std::size_t index, const LocalPoint& point) noexcept
{
    auto& placement = data.template get<PlacementColumns>();
    const auto heading = placement.headings[index];
    const auto cos_heading = std::cos(heading);
    const auto sin_heading = std::sin(heading);
    const auto& position = placement.positions[index];
    return {position.x + point.forward * cos_heading - point.left * sin_heading, position.y + point.forward * sin_heading + point.left * cos_heading, position.z + point.up};
}
} // namespace eaw
