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
#include <optional>

namespace eaw::simulation_detail {

inline constexpr std::uint32_t no_dense = std::numeric_limits<std::uint32_t>::max();
// Arrival tolerance in world units.
inline constexpr float arrival_distance = 0.01F;
// Broad-phase cell size in world units (a little above typical weapon ranges).
inline constexpr float grid_cell_size = 256.0F;
// The hull of an entity with destroyable parts follows them: max health x min(1, S/T + 0.2), where
// S/T is the parts' remaining share of their total health (recorded in the FoC debug build: every
// hull drop of a Nebulon-B under fire lies on this curve, and the hull never loses health to a shot
// at a part; EaW-Revised's S-45, S-49, S-50, docs/compatibility-research.md). Space ships were
// recorded; land objects with parts follow the same rule unobserved.
inline constexpr float hull_floor_share = 0.2F;
// Contact's grid cells (crushing and separation): a few infantry footprints wide; a larger unit lies
// in every cell it covers (SpatialGrid's sized build), so no query reaches farther than its own
// footprint. An engine choice; the pairs found are the same at any cell size.
inline constexpr float contact_cell_size = 16.0F;
// The clearance a field covers at least: the largest of the size buckets of
// docs/pathfinding-design.md, 4.1 (stock footprints up to the giant vehicles' 63); larger units on a
// map extend it to their own radius.
inline constexpr float clearance_minimum_limit = footing_buckets.back();
// Where a spawn or teleport looks for free ground: rings one footprint diameter apart, the k-th with
// 6k points, out to this many rings. Engine choices.
inline constexpr std::uint32_t free_ground_rings = 24;
// How far a group's whole block is shifted to fit before its slots are placed one by one: rings one
// largest member's diameter apart, this many. An engine choice.
inline constexpr std::uint32_t block_shift_rings = 8;
// A group move's destinations keep this share of a member's radius free around it beyond contact, so
// arriving units do not press against those already there. An engine choice.
inline constexpr float group_slot_gap = 0.25F;
// The occupancy grid's cells (an engine choice: a few vehicle footprints wide).
inline constexpr float occupancy_cell_size = 32.0F;
// The fastest separation pushes a flyer, in its radii per second: half a radius per tick at the old
// 30 Hz. An engine choice.
inline constexpr float separation_radii_per_second = 15.0F;
// Ground avoidance (docs/pathfinding-design.md, 4.4): a unit avoids contact this far ahead with at
// most this many of its nearest neighbours, found in a grid of this cell size; an avoidance velocity
// closer than this share of the unit's speed to the one toward its waypoint is not used, so a unit
// with nothing in its way follows its route exactly. Of two allies that both move, each takes the
// other's share of their summed footprint areas, so the larger gives way less (an AT-AT next to
// none against infantry). Engine choices.
inline constexpr float avoidance_horizon_seconds = 1.0F;
// How far (in the unit's radii, beyond its own footprint) a unit looks for neighbours to avoid and
// allies to ask aside, at most: in a dense crowd only the nearest matter, and a second's travel
// reached about 200 soldiers in each query (the large Utapau battle, 2026-11-15). An engine choice.
inline constexpr float avoidance_reach_radii = 4.0F;
inline constexpr std::size_t avoidance_neighbours = 10;
inline constexpr float avoidance_cell_size = 32.0F;
inline constexpr float avoidance_tolerance = 1.0e-3F;
// How far to its right (radians) a unit that cannot go straight its way prefers to turn
// (plan_ground_avoidance). An engine choice.
inline constexpr float avoidance_keep_right = 0.15F;
// Units this close to touching (world units) count as touching, not overlapping: sliding along each
// other leaves them a rounding error inside, which ORCA would otherwise ask them to undo within a
// tick. An engine choice (the hundredth separation tolerated).
inline constexpr float contact_tolerance = 0.01F;
// Giving way (plan_giving_way): an ally a moving unit would meet within this many seconds of its
// travel steps aside, clearing the mover's footprint by this share of its own radius; it gives way at
// most this long. A mover asks only when held up (slower last tick than the held share of its speed).
// A unit gives way when it stands or is slower by nature than this share of the mover's speed,
// whatever its size (the owner, 2026-11-15: slower allies give way to faster ones; companies of
// other sizes did not give way while a larger unit never did). Engine choices.
inline constexpr float give_way_lookahead_seconds = 1.0F;
inline constexpr float give_way_margin = 0.25F;
inline constexpr float give_way_seconds = 4.0F;
inline constexpr float give_way_slower_fraction = 0.9F;
inline constexpr float give_way_held_fraction = 0.5F;
// Where a unit steps to give way: the shortest step out of the mover's corridor in this many
// directions (sideways, diagonally, or back), not along directions closer to the corridor's than this
// sine, nor farther than this many clearances; a line of standing allies opens a lane this many units
// deep, each stepping on the same way. Engine choices.
inline constexpr std::uint32_t give_way_directions = 16;
inline constexpr float give_way_least_lateral = 0.2F;
inline constexpr float give_way_max_shift = 3.0F;
inline constexpr std::uint32_t give_way_chain = 3;
// Route searches run this many at a time on the job system (each with its own search scratch, about
// 16 bytes a ground cell for each of two pathfinders). Engine choice.
inline constexpr std::size_t route_batch = 8;
// Combat aims this many armed entities to a job (target choice and turret). Engine choice.
inline constexpr std::size_t aim_grain = 8;
// Ground avoidance plans this many steering units to a job. Engine choice, from the 500-unit
// benchmark.
inline constexpr std::size_t avoidance_grain = 4;
// A unit whose ask found no place asks again after this long (asking every tick cost 2.5 ms a tick
// in the 500-unit benchmark, nearly every ask failing in its crowds). Engine choice.
inline constexpr float give_way_retry_seconds = 0.25F;
// A moving unit that went this share of its speed along its way last tick asks no one (nothing held
// it up or turned it aside). Engine choice.
inline constexpr float give_way_unhindered_fraction = 0.95F;
// How much nearer than contact a unit's way aside may pass another (rounding). Engine choice.
inline constexpr float give_way_way_tolerance = 0.01F;
// Detours (plan_detours): a unit held up this long plans around the standing units within the window
// (world units beyond its own size), each blocking the cells within their summed radii. Engine
// choices.
inline constexpr float detour_after_seconds = 2.0F;
// RouteColumns::detoured: no detour, a detour no waypoint of which is passed yet, one under way.
inline constexpr std::uint8_t detour_none = 0;
inline constexpr std::uint8_t detour_fresh = 1;
inline constexpr std::uint8_t detour_under_way = 2;
// Detours around held movers a unit plans for one route: a jam in passing dissolves within one or
// two (the owner's Kamino recording); a crowd at the goal does not, and after these the unit is
// held as by standing units (stopping where its way stays closed). An engine choice.
inline constexpr std::uint8_t mover_detour_limit = 3;
inline constexpr float detour_window = 300.0F;
// A unit detours only when a standing unit is within this many summed radii of it (what holds it up).
inline constexpr float detour_beside = 1.5F;
// Detours planned a tick at most (a whole group held up at once plans over a few ticks). An engine
// choice.
inline constexpr std::uint32_t detours_per_tick = 4;
// Route repairs (repair_routes) a tick, in entity order. An engine choice.
inline constexpr std::uint32_t route_repairs_per_tick = 8;
// A unit heading for its destination stops where it touches (within this share of their summed radii
// beyond contact) a standing unit that covers the destination, or, having come no closer to it by the
// progress distance (world units) for the stall time, a standing unit of its owner nearer the
// destination than itself: the closest point to the goal where it overlaps no one, when units
// already stand there (the owner, 2026-11-15; goal_taken). Engine choices.
inline constexpr float taken_goal_margin = 0.25F;
// Allies count as the crowd at a unit's destination when they stopped at destinations within this
// many summed radii of it (goal_taken). An engine choice.
inline constexpr float taken_goal_crowd = 6.0F;
inline constexpr float arrival_progress = 0.5F;
inline constexpr float arrival_stall_seconds = 1.0F;
// A unit that came no closer for this long beside standing allies nearer its goal stops there even
// when its goal is free (a hole among them it cannot reach); giving way, which takes about a second,
// had its chance first. An engine choice.
inline constexpr float arrival_give_up_seconds = 3.0F;
// A unit that came no closer for the stall time within this share of its radius of its goal has
// arrived (packed neighbours held formation members a few units short of their slots, and two units
// sent to one point held each other off it). An engine choice.
inline constexpr float arrival_reach_share = 2.0F;
// Seconds between automatic target searches of an idle weapon, and between replans of an ordered
// chase whose target is out of range (10 and 30 ticks at the old 30 Hz). Engine choices, not
// measured from the original.
inline constexpr float retarget_seconds = 1.0F / 3.0F;
inline constexpr float chase_seconds = 1.0F;
// A formation member waits while it is more than this much route ahead of its group's furthest
// member (plus two footprint diameters), and leaves its group after this many seconds without
// progress while not waiting. Engine choices.
inline constexpr float cohesion_slack = 30.0F;
inline constexpr float cohesion_stall_seconds = 3.0F;
// Following a slot (a squad's or a formation frame's; follow_slot): a member walks to its slot when it
// stands more than the tolerance from it, and is sent again when its slot moved the reslot distance
// from where it was sent; a member on a path it planned around a blockage keeps that path until its
// slot moved the detour distance (sent again sooner, it walked back into the blockage every few ticks
// of a moving frame). Engine choices.
inline constexpr float slot_follow_tolerance = 0.5F;
inline constexpr float slot_reslot_distance = 4.0F;
inline constexpr float slot_detour_distance = 40.0F;
// Formation frames (docs/pathfinding-design.md, 4.3): a frame holds while a member stands more than
// the leash (plus its footprint's diameter) from its slot, for at most the wait limit, after which
// a lagger that came no closer leaves the group and goes on alone; it turns toward its route at the
// turn rate and holds
// its place while more than the hold angle off; an order splits a selection into frames where members
// stand more than the split distance from every member of another part (single linkage). Engine
// choices.
inline constexpr float frame_leash = 30.0F;
inline constexpr float frame_wait_seconds = 3.0F;
// A frame holds for laggers at most this long at a stretch, then goes on (they still follow). An
// engine choice.
inline constexpr float frame_hold_seconds = 5.0F;
// A frame moves at this share of its slowest member's speed, so members behind their slots catch
// up; files in a narrow passage are this many footprint diameters apart. Engine choices.
inline constexpr float frame_pace = 0.9F;
inline constexpr float frame_file_spacing = 1.25F;
inline constexpr float frame_turn_rate = 1.5F;
inline constexpr float frame_turn_hold = 0.6F;
inline constexpr float frame_split_distance = 400.0F;
// A formation forms on the move: its members' slots blend from where they stood into the formation
// over this much of the frame's way (all of it when shorter), so no member walks back to a gathering
// point and the frame never waits for the block to form (the owner's report, 2026-11-15: companies
// stood for seconds and gathered far from where some stood). A final slot on ground a member cannot
// stand on moves to the nearest point it can, searched this many rings of its footprint out. Engine
// choices.
inline constexpr float frame_forming_distance = 150.0F;
inline constexpr std::uint32_t frame_slot_rings = 6;
// Path search nodes expanded per tick at most (about 2.5 ms in a Release build, the share of a frame
// 30000 was at 30 Hz; halved for 60 Hz, the owner, 2026-11-15), always at least one request. An
// engine choice; requests beyond it wait for the next tick in order.
inline constexpr std::uint64_t path_budget_per_tick = 15000;
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

// The point nearest `at` that `free` accepts: `at` itself, else rings `step` apart around it, the k-th
// with 6k points, out to `rings` rings; none when no point is accepted.
template <class Free> std::optional<Vector2> nearest_free_point(const Vector3& at, const float step, const std::uint32_t rings, Free&& free)
{
    if (free(at.x, at.y))
    {
        return Vector2{at.x, at.y};
    }
    for (std::uint32_t ring = 1; ring <= rings; ++ring)
    {
        const auto points = 6U * ring;
        for (std::uint32_t point = 0; point < points; ++point)
        {
            const auto angle = two_pi * static_cast<float>(point) / static_cast<float>(points);
            const auto x = at.x + std::cos(angle) * step * static_cast<float>(ring);
            const auto y = at.y + std::sin(angle) * step * static_cast<float>(ring);
            if (free(x, y))
            {
                return Vector2{x, y};
            }
        }
    }
    return std::nullopt;
}

// The free point nearest `at` on the first ring (`at` itself, then rings `step` apart, the k-th with
// 6k points, out to `rings`) where `free` accepts one, and on that ring the one nearest `toward`
// (ties to the first in angle order); none when no point is accepted.
template <class Free> std::optional<Vector2> nearest_free_point_toward(const Vector3& at, const float step, const std::uint32_t rings, const Vector2 toward, Free&& free)
{
    if (free(at.x, at.y))
    {
        return Vector2{at.x, at.y};
    }
    for (std::uint32_t ring = 1; ring <= rings; ++ring)
    {
        const auto points = 6U * ring;
        std::optional<Vector2> best;
        auto best_distance = 0.0F;
        for (std::uint32_t point = 0; point < points; ++point)
        {
            const auto angle = two_pi * static_cast<float>(point) / static_cast<float>(points);
            const Vector2 candidate{at.x + std::cos(angle) * step * static_cast<float>(ring), at.y + std::sin(angle) * step * static_cast<float>(ring)};
            const auto distance = length(candidate - toward);
            if ((!best || distance < best_distance) && free(candidate.x, candidate.y))
            {
                best = candidate;
                best_distance = distance;
            }
        }
        if (best)
        {
            return best;
        }
    }
    return std::nullopt;
}

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

// How far past its aim point a missed shot flies on, on land, looking for the ground before it is
// lost (world units): an engine choice, so shots fired level or upward end. Most shots have no
// maximum flight of their own (Projectile_Max_Flight_Distance 0).
inline constexpr float stray_flight_limit = 2000.0F;

template <class Data, typename Accepts>
std::optional<std::size_t> SimulationWorld::choose_target(Data& data, const SpatialGrid& grid, std::vector<std::uint32_t>& query, const std::size_t index, const Vector3& center, const float reach,
                                                          const Accepts& accepts)
{
    auto& identity = data.template get<IdentityColumns>();
    auto& placement = data.template get<PlacementColumns>();
    auto& rules = data.template get<Rules>();
    const auto& priorities = combat_state(data, index).profile->targeting;
    query.clear();
    grid.query(center, reach, query);
    std::optional<std::size_t> best;
    float best_rank = 0.0F;
    float best_distance = 0.0F;
    for (const auto candidate : query)
    {
        if (candidate == index || !rules.hostility.hostile(identity.owners[index], identity.owners[candidate]))
        {
            continue;
        }
        const auto& target = combat_state(data, candidate);
        if (!damageable(target) || !target.profile->targetable || !accepts(candidate))
        {
            continue;
        }
        const auto& profile = *target.profile;
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
    bool any = false;
    const auto& hardpoints = combat_state(data, index).profile->hardpoints;
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
    const auto& state = combat_state(data, index);
    return state.profile->hardpoints[hardpoint].health <= 0.0F || state.hardpoint_health[hardpoint] > 0.0F;
}

template <class Data> SimulationWorld::TurnPoint SimulationWorld::turn_point(Data& data, const std::size_t index, const float time) noexcept
{
    auto& locomotor = data.template get<LocomotorColumns>();
    auto& turn_clips = data.template get<ComponentPool<TurnClipColumns>>();
    const auto& movement = *locomotor.movements[index];
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
template <class Data> const SimulationWorld::CombatState& SimulationWorld::combat_state(Data& data, const std::size_t index) noexcept
{
    const auto& pool = data.template get<ComponentPool<CombatColumns>>();
    const auto* state = pool.find(slot_of(data, index), &CombatColumns::states);
    return state != nullptr ? *state : no_combat();
}

template <class Data> auto* SimulationWorld::combat_of(Data& data, const std::size_t index) noexcept
{
    return data.template get<ComponentPool<CombatColumns>>().find(slot_of(data, index), &CombatColumns::states);
}

template <class Data> void SimulationWorld::drop_target(Data& data, const std::size_t index) noexcept
{
    if (auto* state = combat_of(data, index))
    {
        state->target = {};
        state->ordered = false;
    }
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
    std::int32_t best = -1;
    float best_distance = 0.0F;
    const auto& hardpoints = combat_state(data, victim).profile->hardpoints;
    for (std::size_t hardpoint = 0; hardpoint < hardpoints.size(); ++hardpoint)
    {
        // Indestructible parts are not aimed at; destroyed ones no longer are.
        if (!hardpoints[hardpoint].targetable || hardpoints[hardpoint].health <= 0.0F || combat_state(data, victim).hardpoint_health[hardpoint] <= 0.0F)
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
    if (hardpoint >= 0)
    {
        return world_point(data, victim, combat_state(data, victim).profile->hardpoints[static_cast<std::size_t>(hardpoint)].offset);
    }
    const auto& to = placement.positions[victim];
    return {to.x, to.y, to.z + combat_state(data, victim).profile->aim_height};
}

template <class Data> std::optional<std::size_t> SimulationWorld::attack_victim(Data& data, const std::size_t index) noexcept
{
    auto& identity = data.template get<IdentityColumns>();
    auto& rules = data.template get<Rules>();
    const auto& state = combat_state(data, index);
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
    return damageable(combat_state(data, index));
}

template <class Data> bool SimulationWorld::flies(Data& data, const std::size_t index) noexcept
{
    return data.template get<ComponentPool<FlightColumns>>().find(slot_of(data, index)).has_value();
}

template <class Data> bool SimulationWorld::can_crush(Data& data, const std::size_t crusher, const std::size_t victim) noexcept
{
    const auto& identity = data.template get<IdentityColumns>();
    const auto& pool = data.template get<ComponentPool<CrushColumns>>();
    const auto kind = crush_profile_in(pool, identity.ids[crusher].index).crusher;
    if (kind == Crusher::none || victim == crusher)
    {
        return false;
    }
    const auto& squash = crush_profile_in(pool, identity.ids[victim].index);
    const bool may = squash.squashable || (kind == Crusher::supercrusher && squash.squashable_by_supercrusher);
    const auto& state = combat_state(data, victim);
    return may && state.profile->health > 0.0F && state.health > 0.0F && data.template get<Rules>().hostility.hostile(identity.owners[crusher], identity.owners[victim]);
}

template <class Data> bool SimulationWorld::ground_body(Data& data, const std::size_t index) noexcept
{
    return data.template get<PlacementColumns>().radii[index] > 0.0F && data.template get<GroundMoverColumns>().follows_ground[index] != 0 && !flies(data, index);
}

template <class Data> bool SimulationWorld::in_moving_frame(Data& data, const std::size_t index) noexcept
{
    const auto& groups = data.template get<ComponentPool<GroupColumns>>();
    const auto row = groups.find(slot_of(data, index));
    if (!row)
    {
        return false;
    }
    const auto& frames = data.template get<FormationFrames>().list;
    const auto frame = std::ranges::find(frames, groups.columns.ids[*row], &FormationFrame::group);
    return frame != frames.end() && !frame->arrived;
}

template <class Data> float SimulationWorld::unit_radius(Data& data, const std::size_t index) noexcept
{
    const auto* squad = data.template get<ComponentPool<SquadColumns>>().find(slot_of(data, index), &SquadColumns::states);
    return squad != nullptr ? squad->extent : data.template get<PlacementColumns>().radii[index];
}

template <class Data> Footing SimulationWorld::footing_of(Data& data, const std::size_t index) noexcept
{
    auto& ground = data.template get<GroundState>();
    auto& ground_mover = data.template get<GroundMoverColumns>();
    const auto mask = ground_mover.passable_types[index];
    const ClearanceField* field = nullptr;
    for (const auto& candidate : ground.clearance)
    {
        if (candidate.passable_types == mask)
        {
            field = &candidate;
        }
    }
    return {mask, field, ground_mover.footing_radii[index]};
}

template <class Data> bool SimulationWorld::stands_at(Data& data, const std::size_t index, const float x, const float y) noexcept
{
    auto& ground = data.template get<GroundState>();
    return data.template get<GroundMoverColumns>().passable_types[index] == 0 || ground_stands(ground.map, {x, y}, footing_of(data, index));
}

template <class Data> bool SimulationWorld::waits(Data& data, const std::size_t index) noexcept
{
    const auto* waiting = data.template get<ComponentPool<GroupColumns>>().find(slot_of(data, index), &GroupColumns::waiting);
    if (waiting != nullptr && *waiting != 0)
    {
        return true;
    }
    const auto* squad = data.template get<ComponentPool<SquadColumns>>().find(slot_of(data, index), &SquadColumns::states);
    return squad != nullptr && squad->waiting;
}

template <class Data> void SimulationWorld::leave_group(Data& data, const std::size_t index)
{
    data.template get<ComponentPool<GroupColumns>>().remove(slot_of(data, index));
}

template <class Data> void SimulationWorld::follow_slot(Data& data, const std::size_t member, const Vector3& slot, std::optional<Vector3>& sent)
{
    auto& route = data.template get<RouteColumns>();
    const auto& at = data.template get<PlacementColumns>().positions[member];
    const auto away = std::hypot(slot.x - at.x, slot.y - at.y);
    const auto reslot = route.paths[member].empty() ? simulation_detail::slot_reslot_distance : simulation_detail::slot_detour_distance;
    const bool moved = !sent || std::hypot(sent->x - slot.x, sent->y - slot.y) > reslot;
    if (away > simulation_detail::slot_follow_tolerance && (moved || (!route.move_targets[member] && route.pending[member] == 0)))
    {
        route.move_targets[member] = slot;
        route.paths[member].clear();
        route.pending[member] = 0;
        sent = slot;
    }
}

template <class Data> bool SimulationWorld::engages(Data& data, const std::size_t index, const std::size_t candidate) noexcept
{
    const auto& weapons = combat_state(data, index).profile->weapons;
    const auto categories = combat_state(data, candidate).profile->categories;
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
// Shields absorb a shot first (scaled by the shield armor type); what the shield cannot absorb,
// converted back to raw damage, is scaled by the armor type and reaches the part the shot aims at
// or, without one, the hull. A shot at a part damages only the part (recorded: 4000 scripted damage
// on a Nebulon-B laser silenced it for good and left the hull at 5400; S-19), and the hull then
// follows the parts; the entity dies with its last destroyable part unless its type says not to
// (recorded: the Nebulon-B dies with it at 0.35 or 0.23 of its hull). Damage aimed at no part
// (blasts, unaimed shots) reaches the hull directly, an engine choice: that case is not observed.
template <class Data>
ImpactKind SimulationWorld::apply_damage(Data& data, const std::size_t victim, float damage, const std::uint16_t damage_type, const bool damages_shield, const bool damages_health,
                                         const std::int32_t hardpoint)
{
    auto& identity = data.template get<IdentityColumns>();
    auto& events = data.template get<TickEvents>();
    auto& rules = data.template get<Rules>();
    auto& counters = data.template get<CombatCounters>();
    // A victim is damageable: it has combat.
    auto& state = *combat_of(data, victim);
    if (damages_shield && state.shield > 0.0F)
    {
        const auto modifier = rules.damage_table.modifier(damage_type, state.profile->shield_armor_type);
        const auto dealt = damage * modifier;
        if (dealt <= state.shield)
        {
            state.shield -= dealt;
            return ImpactKind::shield;
        }
        damage = modifier > 0.0F ? (dealt - state.shield) / modifier : 0.0F;
        state.shield = 0.0F;
    }
    auto kind = ImpactKind::hull;
    if (damages_health)
    {
        const auto modifier = rules.damage_table.modifier(damage_type, state.profile->armor_type);
        if (modifier < 1.0F)
        {
            kind = ImpactKind::hull_reduced;
        }
        const auto part_index = static_cast<std::size_t>(std::max(hardpoint, 0));
        const bool at_part = hardpoint >= 0 && state.profile->hardpoints[part_index].health > 0.0F && state.hardpoint_health[part_index] > 0.0F;
        if (!at_part)
        {
            (void)wound(state, damage * modifier);
            return kind;
        }
        if (state.invulnerable || state.profile->immune)
        {
            return kind;
        }
        auto& part = state.hardpoint_health[part_index];
        part -= damage * modifier;
        if (part <= 0.0F)
        {
            part = 0.0F;
            ++counters.hardpoints_destroyed;
            const auto& profile = state.profile->hardpoints[part_index];
            events.hardpoint_deaths.push_back(
                {identity.ids[victim], static_cast<std::uint32_t>(part_index), world_point(data, victim, profile.offset), profile.death_sound, profile.death_effect, profile.type_key});
        }
        // The hull follows its parts, and falls with the last of them.
        const auto followed = state.profile->health * hull_share_of_parts(data, victim);
        const auto lost = state.profile->dies_with_hardpoints && parts_all_destroyed(data, victim) ? state.health : std::max(state.health - followed, 0.0F);
        (void)wound(state, lost);
    }
    return kind;
}

template <class Data>
void SimulationWorld::area_blast(Data& data, const Vector3& point, const std::uint32_t owner, const AreaBlast& blast, const std::uint16_t damage_type, const bool damages_shield,
                                 const bool damages_health)
{
    auto& identity = data.template get<IdentityColumns>();
    auto& placement = data.template get<PlacementColumns>();
    auto& rules = data.template get<Rules>();
    if ((blast.damage <= 0.0F && blast.friendly_damage <= 0.0F) || blast.range <= 0.0F)
    {
        return;
    }
    // shortcut: a scan over every entity per blast; use the spatial grid when battles get big.
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        const bool friendly = identity.owners[index] == owner;
        if ((!rules.hostility.hostile(owner, identity.owners[index]) && !(friendly && blast.friendly_damage > 0.0F)) || !damageable(data, index))
        {
            continue;
        }
        const auto& at = placement.positions[index];
        const auto distance = std::max(0.0F, std::sqrt((at.x - point.x) * (at.x - point.x) + (at.y - point.y) * (at.y - point.y) + (at.z - point.z) * (at.z - point.z)) - target_size(data, index));
        if (distance > blast.range)
        {
            continue;
        }
        auto damage = friendly ? blast.friendly_damage : blast.damage;
        if (blast.tiers > 0)
        {
            const auto ring = std::min(static_cast<std::uint32_t>(distance / blast.range * static_cast<float>(blast.tiers)), blast.tiers - 1U);
            damage *= static_cast<float>(blast.tiers - ring) / static_cast<float>(blast.tiers);
        }
        (void)apply_damage(data, index, damage, damage_type, damages_shield, damages_health, -1);
    }
}

template <class Data> float SimulationWorld::hull_share_of_parts(Data& data, const std::size_t index) noexcept
{
    const auto& state = combat_state(data, index);
    float total = 0.0F;
    float remaining = 0.0F;
    for (std::size_t hardpoint = 0; hardpoint < state.profile->hardpoints.size(); ++hardpoint)
    {
        if (state.profile->hardpoints[hardpoint].health > 0.0F)
        {
            total += state.profile->hardpoints[hardpoint].health;
            remaining += std::max(state.hardpoint_health[hardpoint], 0.0F);
        }
    }
    return total > 0.0F ? std::min(1.0F, remaining / total + simulation_detail::hull_floor_share) : 1.0F;
}

template <class Data> bool SimulationWorld::parts_all_destroyed(Data& data, const std::size_t index) noexcept
{
    const auto& state = combat_state(data, index);
    bool any = false;
    for (std::size_t hardpoint = 0; hardpoint < state.profile->hardpoints.size(); ++hardpoint)
    {
        if (state.profile->hardpoints[hardpoint].health > 0.0F)
        {
            any = true;
            if (state.hardpoint_health[hardpoint] > 0.0F)
            {
                return false;
            }
        }
    }
    return any;
}

template <class Data> float SimulationWorld::target_size(Data& data, const std::size_t index) noexcept
{
    auto& placement = data.template get<PlacementColumns>();
    auto& footprints = data.template get<ComponentPool<FootprintColumns>>();
    // The separation footprint, else the obstacle footprint.
    auto size = placement.radii[index];
    if (const auto* footprint = footprints.find(slot_of(data, index), &FootprintColumns::shapes); size <= 0.0F && footprint != nullptr)
    {
        size = std::max(footprint->half_length, footprint->half_width);
    }
    return size;
}

template <class Data> std::uint32_t SimulationWorld::ticks_for(Data& data, const float seconds) noexcept
{
    auto& clock = data.template get<Clock>();
    const auto ticks = std::lround(static_cast<double>(seconds) / clock.tick_seconds);
    return static_cast<std::uint32_t>(std::clamp<long>(ticks, 0L, 1L << 30));
}
} // namespace eaw
