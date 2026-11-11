#pragma once

// Shared constants and helpers of the simulation's implementation files (simulation.cpp: world
// core; simulation_behavior.cpp: orders and behavior; simulation_navigation.cpp: routes;
// simulation_locomotion.cpp: movement; simulation_combat.cpp: targeting, weapons, damage). Not part
// of the public interface.

#include "eaw/simulation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace eaw::simulation_detail {

inline constexpr std::uint32_t no_dense = std::numeric_limits<std::uint32_t>::max();
// Arrival tolerance in world units.
inline constexpr float arrival_distance = 0.01F;
// Broad-phase cell size in world units (a little above typical weapon ranges).
inline constexpr float grid_cell_size = 256.0F;
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
inline constexpr float flight_max_pitch = std::numbers::pi_v<float> / 3.0F;
// A flyer reverses through a half-loop instead of a flat turn when its goal lies more than this far
// off its heading (radians) and more than two turn circles away. Engine choices; the original's
// trigger angle is not measured.
inline constexpr float flight_reversal_angle = std::numbers::pi_v<float> * 0.75F;
// Dogfights: flyers weave this far above and below their target (world units) over this many
// seconds, staying within the band around their plane. Engine choices.
inline constexpr float dogfight_weave = 60.0F;
inline constexpr float dogfight_weave_seconds = 8.0F;
inline constexpr float dogfight_band = 150.0F;

inline bool finite(const Vector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool valid_amount(const float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

inline bool valid_profile(const CombatProfile& profile) noexcept
{
    if (!valid_amount(profile.health) || !valid_amount(profile.shield) || !valid_amount(profile.attack_move_response_range) || !valid_amount(profile.guard_chase_range) ||
        !valid_amount(profile.stickiness_seconds) || !valid_amount(profile.shield_regen_per_second) || !std::isfinite(profile.aim_height))
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
            !valid_amount(weapon.projectile_speed) || !valid_amount(weapon.max_flight_distance) || !finite_point(weapon.mount) || (weapon.alternate_mount && !finite_point(*weapon.alternate_mount)) ||
            !std::isfinite(weapon.cone_direction) || !valid_amount(weapon.cone_half_angle) || weapon.hardpoint < -1 || weapon.hardpoint >= static_cast<std::int32_t>(profile.hardpoints.size()))
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

inline float wrap_angle(float angle) noexcept
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

} // namespace eaw::simulation_detail

namespace eaw {

template <typename Accepts> std::optional<std::size_t> SimulationWorld::choose_target(const std::size_t index, const Vector3& center, const float reach, const Accepts& accepts)
{
    const auto& priorities = combat_[index].profile.targeting;
    query_.clear();
    grid_.query(center, reach, query_);
    std::optional<std::size_t> best;
    float best_rank = 0.0F;
    float best_distance = 0.0F;
    for (const auto candidate : query_)
    {
        if (candidate == index || !damageable(candidate) || !combat_[candidate].profile.targetable || !hostility_.hostile(owners_[index], owners_[candidate]) || !accepts(candidate))
        {
            continue;
        }
        const auto& profile = combat_[candidate].profile;
        const auto rank = priorities.rank(profile.categories, profile.properties, profile.type_key);
        const auto distance = simulation_detail::distance_xy(center, positions_[candidate]);
        if (rank < 0.0F || distance > reach)
        {
            continue;
        }
        const auto lower = best && (ids_[candidate].index != ids_[*best].index ? ids_[candidate].index < ids_[*best].index : ids_[candidate].generation < ids_[*best].generation);
        if (!best || rank < best_rank || (rank == best_rank && (distance < best_distance || (distance == best_distance && lower))))
        {
            best = candidate;
            best_rank = rank;
            best_distance = distance;
        }
    }
    return best;
}

} // namespace eaw
