#pragma once

// A skeletal animation clip as the engine uses it, whatever file it came from: per-bone key frames
// at a fixed rate.

#include "math/quaternion.hpp"
#include "math/vector.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

// A bone's transform relative to its parent at one key frame, and its visibility.
struct AnimationKey
{
    Vector3 translation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    Quaternion rotation{Quaternion::identity()};
    bool visible{true};
};

struct AnimationTrack
{
    // The bone the clip was authored for, by name (retargeting matches it) and by index.
    std::string bone_name;
    std::uint32_t bone{};
    // Holds each key until the next instead of interpolating.
    bool stepped{};
    std::vector<AnimationKey> keys;
    // The track moves its bone (its rotation, translation, or scale changes over the clip); a track
    // that does not only holds the bone at one transform (an overlay clip leaves such bones alone).
    bool animated{true};
};

struct Animation
{
    float frames_per_second{};
    // Keys stored per track.
    std::uint32_t key_count{};
    // Frames one loop plays (the loop wraps from the last of them back to the first).
    std::uint32_t loop_frame_count{};
    std::vector<AnimationTrack> tracks;
};

// Seconds one loop takes; 0 for a clip without frames or rate.
[[nodiscard]] float animation_duration(const Animation& animation) noexcept;

} // namespace eaw
