#pragma once

// How the renderer picks detail levels: the strategy and its settings (render/lod_selection.hpp
// applies them). Every number here is an engine choice (the original's selection is not
// documented); the stock data supplies a per-type LOD_Bias and a MeshDetail setting (0..1) per
// graphics preset (GRAPHICDETAILS.XML).

#include <cstdint>
#include <optional>
#include <string_view>

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

} // namespace eaw
