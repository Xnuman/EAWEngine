#pragma once

// How the renderer smooths edges: engine choices, the original drawing without anti-aliasing as far
// as is known.

#include <cstdint>
#include <optional>
#include <string_view>

namespace eaw {

enum class VisualAntiAliasing : std::uint8_t
{
    off,
    // An edge pass over the finished frame (in the manner of FXAA).
    fxaa,
    // Temporal: a jittered frame blended with the reprojected history.
    taa,
    // Temporal, then the edge pass over its result (for the edges TAA leaves).
    hybrid,
};

[[nodiscard]] constexpr std::string_view to_string(const VisualAntiAliasing mode) noexcept
{
    switch (mode)
    {
    case VisualAntiAliasing::off:
        return "off";
    case VisualAntiAliasing::fxaa:
        return "fxaa";
    case VisualAntiAliasing::taa:
        return "taa";
    case VisualAntiAliasing::hybrid:
        return "hybrid";
    }
    return "off";
}

[[nodiscard]] constexpr std::optional<VisualAntiAliasing> visual_anti_aliasing(const std::string_view name) noexcept
{
    for (const auto mode : {VisualAntiAliasing::off, VisualAntiAliasing::fxaa, VisualAntiAliasing::taa, VisualAntiAliasing::hybrid})
    {
        if (name == to_string(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

} // namespace eaw
