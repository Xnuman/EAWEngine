#pragma once

// Surfaces: how the legacy-mode shading path draws a model's draws.

#include "render/color.hpp"
#include "render/visual_texture.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace eaw {

enum class VisualBlendMode
{
    opaque,
    alpha_test,
    alpha_blend,
    additive,
};

enum class VisualLightingModel
{
    // Texture times material color, no lighting (additive effects, backdrops, debug colors).
    unlit,
    // Fill plus key light on the interpolated surface normal.
    lit,
    // Fill plus key light on a tangent-space normal map.
    normal_mapped,
};

// Which texture channel scales the key-light highlight.
enum class VisualGlossSource
{
    none,
    base_alpha,
    normal_alpha,
    gloss_red,
};

// Engine-native surface description consumed by the legacy-mode shading path. Colors are linear
// multipliers in the 0..1 range authored by the original materials.
struct VisualMaterial
{
    std::string name;
    // Extra multiplier; the builder uses it to tint untextured surfaces.
    Color tint{white};
    std::uint32_t base_color_texture{invalid_visual_texture};
    std::uint32_t normal_texture{invalid_visual_texture};
    std::uint32_t gloss_texture{invalid_visual_texture};
    VisualBlendMode blend{VisualBlendMode::opaque};
    bool two_sided{};
    VisualLightingModel lighting{VisualLightingModel::lit};
    VisualGlossSource gloss{VisualGlossSource::none};
    // Blend base color toward base color x instance team color by base-texture alpha.
    bool team_color_mask{};
    // Add the specular color, unlit, masked by base-texture alpha (self-illumination).
    bool emissive_mask{};
    Color diffuse{white};
    ColorRgb specular{};
    ColorRgb emissive{};
    // Texture-coordinate offset per second of scene time.
    std::array<float, 2> uv_scroll{0.0F, 0.0F};
    // Multiplier applied to texture coordinates (terrain layers tile world X/Y with it).
    float uv_scale{1.0F};
    float alpha_test_threshold{0.5F};
};

} // namespace eaw
