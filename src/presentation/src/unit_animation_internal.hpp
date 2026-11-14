#pragma once

// Shared constants and helpers of unit animation's implementation files (unit_animation.cpp: the
// animator, its pose cache, cues, and corpses; unit_animation_locomotion.cpp: state and locomotion;
// unit_animation_attack.cpp: aiming and firing; unit_animation_transitions.cpp: walkers' turns,
// starts, and stops; unit_animation_pose.cpp: pose keys and turrets). Not part of the public
// interface.

#include "math/scalar.hpp"
#include "math/transform.hpp"
#include "assets/animation.hpp"
#include "presentation/unit_animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eaw::unit_animation_detail {

// Engine choices (see unit_animation.hpp).
inline constexpr float minimum_slots_per_second = 30.0F;
inline constexpr float speed_smoothing_seconds = 0.15F;
inline constexpr float attack_fade_seconds = 0.2F;
inline constexpr double shot_fade_seconds = 0.15;
inline constexpr float walk_speed_fraction = 0.4F;
inline constexpr float move_without_walk_fraction = 0.25F;
inline constexpr float minimum_playback_rate = 0.5F;
inline constexpr float maximum_playback_rate = 1.5F;
// Layer clip values: no layer, and the mesh's rest pose (for meshes without an IDLE clip).
inline constexpr std::int32_t no_clip = -1;
inline constexpr std::int32_t rest_clip = -2;
// Corpses hold their last frame this long after their DIE clip ends, and at most this many are kept.
inline constexpr double corpse_linger_seconds = 4.0;
inline constexpr std::size_t maximum_corpses = 4096;
// Turret aims are quantized so that units aiming alike share poses: 128 steps of yaw around the
// circle, 64 of pitch over a half turn (engine choices: under 3 degrees each).
inline constexpr float turret_yaw_step = two_pi / 128.0F;
inline constexpr float barrel_pitch_step = pi / 64.0F;
// A unit turns in place when its heading changes faster than this (radians per second) while it
// moves slower than turn_in_place_speed_fraction of its top speed (engine choices).
inline constexpr float turn_in_place_rate = 0.15F;
inline constexpr float turn_in_place_speed_fraction = 0.1F;
// Frames longer than this (a stall, a hidden window) count as this long.
inline constexpr double longest_frame_seconds = 0.25;
// Sampled poses kept across frames (matrices); beyond it the cache starts over.
inline constexpr std::size_t pose_cache_limit = 4U * 1024U * 1024U;
// New poses per sampling job (an engine choice: EAWBenchmarks poses measures about 10-20 us per
// 40-bone pose, so a job of four carries far more than its scheduling cost of about 0.1 us; fewer
// new poses than this in a frame are sampled on the calling thread).
inline constexpr std::size_t pose_sample_grain = 4;

// The most idles one frame moves past (a first sight starts up to a minute in), and the salt that
// spreads an entity's successive idle choices.
inline constexpr std::size_t maximum_idle_switches = 256;
inline constexpr std::uint64_t idle_variant_salt = 0x9e3779b97f4a7c15ULL;
// A unit with a single IDLE clip that does not loop it waits this long before playing it again
// (seconds, random in between; the owner's choice, 2026-11-13, until the original is observed).
inline constexpr double single_idle_pause_min_seconds = 2.0;
inline constexpr double single_idle_pause_max_seconds = 4.0;

inline std::uint64_t mix(std::uint64_t value) noexcept
{
    // SplitMix64 finalizer: a stable per-entity hash for variants and phases.
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// A fraction 0..1 of an entity's hash, for phases.
inline double unit_fraction(const std::uint64_t key, const std::uint64_t salt) noexcept
{
    return static_cast<double>(mix(key ^ salt) % 10000U) / 10000.0;
}

inline std::uint32_t eighths(const float weight) noexcept
{
    return static_cast<std::uint32_t>(std::lround(std::clamp(weight, 0.0F, 1.0F) * 8.0F));
}

} // namespace eaw::unit_animation_detail
