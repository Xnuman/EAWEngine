#pragma once

// Detail-level selection for the renderer: which of a mesh's levels (VisualMeshState::lods, most
// detailed first) an instance draws, by one of three strategies, scaled by the mesh's LOD bias and
// the mesh-detail setting, with a hysteresis band so an instance at a boundary does not flicker.
// Pure functions, so the rules are testable without a device. Every number here is an engine choice
// (the original's selection is not documented); the stock data supplies a per-type LOD_Bias and a
// MeshDetail setting (0..1) per graphics preset (GRAPHICDETAILS.XML).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace eaw {

enum class VisualLodPolicy : std::uint8_t
{
    // Always the most detailed level.
    off,
    // By distance from the camera: one level per `distance_step` world units.
    distance,
    // By the bounding sphere's projected height on screen: one level per halving below
    // `full_detail_pixels`.
    screen_size,
    // Screen size, but the most detailed level within `near_distance` and the least beyond
    // `far_distance`.
    hybrid,
};

[[nodiscard]] constexpr std::string_view to_string(const VisualLodPolicy policy) noexcept
{
    switch (policy)
    {
    case VisualLodPolicy::off:
        return "off";
    case VisualLodPolicy::distance:
        return "distance";
    case VisualLodPolicy::screen_size:
        return "screen";
    case VisualLodPolicy::hybrid:
        return "hybrid";
    }
    return "off";
}

[[nodiscard]] constexpr std::optional<VisualLodPolicy> visual_lod_policy(const std::string_view name) noexcept
{
    for (const auto policy : {VisualLodPolicy::off, VisualLodPolicy::distance, VisualLodPolicy::screen_size, VisualLodPolicy::hybrid})
    {
        if (name == to_string(policy))
        {
            return policy;
        }
    }
    return std::nullopt;
}

struct VisualLodSettings
{
    VisualLodPolicy policy{VisualLodPolicy::hybrid};
    // The graphics preset's mesh detail, 0..1 (MeshDetail): 0.5 uses the thresholds as given, 1
    // keeps detail twice as far, 0 half as far.
    float mesh_detail{1.0F};
    float distance_step{800.0F};
    float full_detail_pixels{160.0F};
    float near_distance{150.0F};
    float far_distance{6000.0F};
    // How far past a boundary (in levels) a change must go before it is taken.
    float hysteresis{0.15F};
    // Cross-fade between levels (seconds; 0 switches at once).
    float fade_seconds{0.25F};
};

// How much farther a mesh keeps its detail: its LOD bias times the mesh-detail factor.
[[nodiscard]] inline float visual_lod_scale(const VisualLodSettings& settings, const float lod_bias) noexcept
{
    const float detail = std::isfinite(settings.mesh_detail) ? std::clamp(settings.mesh_detail, 0.0F, 1.0F) : 1.0F;
    const float bias = std::isfinite(lod_bias) && lod_bias > 0.0F ? lod_bias : 1.0F;
    return bias * (0.5F + detail);
}

// The continuous level of an instance (0 is the most detailed; level n starts at n) for a bounding
// sphere of `radius` at `distance` from the camera, seen with `vertical_fov` radians over
// `viewport_height` pixels.
[[nodiscard]] inline float visual_lod_position(const VisualLodSettings& settings, const float lod_bias, const float distance, const float radius, const float vertical_fov,
                                               const float viewport_height) noexcept
{
    const float scale = visual_lod_scale(settings, lod_bias);
    const auto by_distance = [&]
    {
        return std::max(0.0F, distance) / std::max(settings.distance_step * scale, 1.0e-3F);
    };
    const auto by_screen = [&]
    {
        const float half_angle = std::tan(std::clamp(vertical_fov, 1.0e-3F, 3.0F) * 0.5F);
        const float pixels = 2.0F * std::max(radius, 0.0F) / (std::max(distance, 1.0e-3F) * half_angle) * std::max(viewport_height, 1.0F) * 0.5F;
        return std::max(0.0F, std::log2(settings.full_detail_pixels * scale / std::max(pixels, 1.0e-3F)));
    };
    switch (settings.policy)
    {
    case VisualLodPolicy::off:
        return 0.0F;
    case VisualLodPolicy::distance:
        return by_distance();
    case VisualLodPolicy::screen_size:
        return by_screen();
    case VisualLodPolicy::hybrid:
        if (distance <= settings.near_distance * scale)
        {
            return 0.0F;
        }
        if (distance >= settings.far_distance * scale)
        {
            return 1.0e6F;
        }
        return by_screen();
    }
    return 0.0F;
}

// The level to draw out of `level_count`, given the continuous position and the level drawn last
// (none for an instance seen for the first time): the level changes only once the position is past
// the boundary by the hysteresis.
[[nodiscard]] inline std::uint32_t visual_lod_level(const float position, const std::uint32_t level_count, const std::optional<std::uint32_t> previous, const float hysteresis) noexcept
{
    if (level_count <= 1)
    {
        return 0;
    }
    const auto last = static_cast<float>(level_count - 1U);
    const float clamped = std::isfinite(position) ? std::clamp(position, 0.0F, last) : last;
    const auto plain = static_cast<std::uint32_t>(std::min(std::floor(clamped), last));
    if (!previous || *previous >= level_count)
    {
        return plain;
    }
    const auto held = static_cast<float>(*previous);
    const float band = std::max(hysteresis, 0.0F);
    // Coarser once past the next boundary by the band; finer once below this level's start by it.
    // The band is measured on the unclamped position, so the last level is reachable too.
    const float unclamped = std::isfinite(position) ? std::max(position, 0.0F) : std::numeric_limits<float>::max();
    if (unclamped >= held + 1.0F + band || unclamped < held - band)
    {
        return plain;
    }
    return *previous;
}

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

    VisualLodChoice update(const std::uint64_t key, const std::uint32_t mesh, const std::uint32_t state, const float position, const std::uint32_t level_count, const float now,
                           const VisualLodSettings& settings)
    {
        const float fade_seconds = std::isfinite(settings.fade_seconds) ? std::max(settings.fade_seconds, 0.0F) : 0.0F;
        auto [entry, added] = tracks_.try_emplace(key);
        auto& track = entry->second;
        VisualLodChoice choice;
        if (added || track.mesh != mesh || track.state != state)
        {
            track = {mesh, state, visual_lod_level(position, level_count, std::nullopt, settings.hysteresis), std::nullopt, now, 0};
        }
        else
        {
            const auto level = visual_lod_level(position, level_count, track.level, settings.hysteresis);
            if (level != track.level)
            {
                track.from = fade_seconds > 0.0F ? std::optional(track.level) : std::nullopt;
                track.level = level;
                track.fade_start = now;
                choice.changed = true;
            }
        }
        track.seen = pass_;
        choice.level = track.level;
        const float progress = fade_seconds > 0.0F ? std::clamp((now - track.fade_start) / fade_seconds, 0.0F, 1.0F) : 1.0F;
        if (track.from && progress < 1.0F && *track.from < level_count)
        {
            // Never exactly 0 (drawn whole) or 1 at either end of the fade.
            choice.from = track.from;
            choice.fade = std::clamp(progress, 1.0F / 64.0F, 63.0F / 64.0F);
        }
        else
        {
            track.from.reset();
        }
        return choice;
    }

    void end_pass()
    {
        std::erase_if(tracks_,
                      [&](const auto& entry)
                      {
                          return entry.second.seen != pass_;
                      });
    }

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
