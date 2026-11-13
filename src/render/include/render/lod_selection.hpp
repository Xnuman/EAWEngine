#pragma once

// Detail-level selection: which of a mesh's levels (VisualMeshState::lods, most detailed first) an
// instance draws, by the strategy of VisualLodSettings, scaled by the mesh's LOD bias and the
// mesh-detail setting, with a hysteresis band so an instance at a boundary does not flicker. No
// graphics API, so the rules are testable without a device; every number is an engine choice.

#include "render/visual_lod.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace eaw {

// How much farther a mesh keeps its detail: its LOD bias times the mesh-detail factor.
[[nodiscard]] float visual_lod_scale(const VisualLodSettings& settings, float lod_bias) noexcept;

// The continuous level of an instance (0 is the most detailed; level n starts at n) for a bounding
// sphere of `radius` at `distance` from the camera, seen with `vertical_fov` radians over
// `viewport_height` pixels.
[[nodiscard]] float visual_lod_position(const VisualLodSettings& settings, float lod_bias, float distance, float radius, float vertical_fov, float viewport_height) noexcept;

// The level to draw out of `level_count`, given the continuous position and the level drawn last
// (none for an instance seen for the first time): the level changes only once the position is past
// the boundary by the hysteresis.
[[nodiscard]] std::uint32_t visual_lod_level(float position, std::uint32_t level_count, std::optional<std::uint32_t> previous, float hysteresis) noexcept;

// What a keyed instance draws this frame: its level and, while it cross-fades, the level it fades
// from and how far the fade has come (in (0, 1); 0 when not fading).
struct VisualLodChoice
{
    std::uint32_t level{};
    std::optional<std::uint32_t> from;
    float fade{};
    // The level changed in this update (a fade began, or the change was instant).
    bool changed{};
};

// Follows instances across frames by key: each keeps its level within the hysteresis band and
// cross-fades for `fade_seconds` when it changes. A key not updated in a pass is forgotten at the
// pass's end; a key that now shows another mesh or state starts afresh, without a fade.
class VisualLodTracker final
{
public:
    void begin_pass() noexcept
    {
        ++pass_;
    }
    VisualLodChoice update(std::uint64_t key, std::uint32_t mesh, std::uint32_t state, float position, std::uint32_t level_count, float now, const VisualLodSettings& settings);
    void end_pass();
    void clear() noexcept
    {
        tracks_.clear();
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return tracks_.size();
    }

private:
    struct Track
    {
        std::uint32_t mesh{};
        std::uint32_t state{};
        std::uint32_t level{};
        std::optional<std::uint32_t> from;
        float fade_start{};
        std::uint64_t seen{};
    };
    std::unordered_map<std::uint64_t, Track> tracks_;
    std::uint64_t pass_{};
};

} // namespace eaw
