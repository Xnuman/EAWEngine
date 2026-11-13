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
    // A planet (VisualMaterial::planet): normal-mapped surface with an atmosphere rim, a scrolling
    // cloud layer lit by its own normal map, and city lights on the night side.
    planet,
    // A shield (VisualMaterial::shield): a distorted, scrolling energy layer times a wave layer,
    // softened toward vertical edges, unlit (drawn additive).
    shield,
    // A sky dome (VisualMaterial::sky): lit by every map light from inside (its normals turned
    // around), times the vertex color, with a scrolling cloud layer over the base by cloud alpha x
    // vertex alpha.
    sky,
    // A nebula (VisualMaterial::nebula): unlit and additive like `unlit`, its vertices wobbling and
    // its brightness rising steeply where its surface faces along the view.
    nebula,
};

// Which texture channel scales the key-light highlight.
enum class VisualGlossSource
{
    none,
    base_alpha,
    normal_alpha,
    gloss_red,
};

// The planet layers (VisualLightingModel::planet; docs/legacy-shader-mapping.md): the cloud texture
// (alpha is coverage) and its normal map, scrolling along U at `cloud_scroll` per second; the
// atmosphere color added toward the rim, `atmosphere.a` its strength and `atmosphere_power` how
// tightly it hugs the rim; and the city-light color, masked by base alpha on the night side. The
// defaults are the legacy parameters' defaults.
struct VisualPlanet
{
    std::uint32_t cloud_texture{invalid_visual_texture};
    std::uint32_t cloud_normal_texture{invalid_visual_texture};
    Color atmosphere{0.0F, 0.0F, 0.0F, 1.0F};
    float atmosphere_power{4.5F};
    ColorRgb city_color{1.0F, 1.0F, 1.0F};
    float cloud_scroll{0.0025F};
};

// The shield layers (VisualLightingModel::shield; docs/legacy-shader-mapping.md): the base texture
// is the energy layer, shifted by `distortion_texture`; `wave_texture` multiplies it; each layer
// repeats `*_uv_scale` times and scrolls along V at `*_scroll` per second; `edge_brightness` softens
// the vertex color toward vertical edges (saturate(object normal z + edge brightness)). Named by
// what each layer does: the legacy parameters for the distortion layer are WaveUVScale and
// WaveUVScrollRate, and for the wave layer DistortUVScale and DistortUVScrollRate. The defaults are
// the legacy parameters' defaults.
struct VisualShield
{
    std::uint32_t wave_texture{invalid_visual_texture};
    std::uint32_t distortion_texture{invalid_visual_texture};
    float base_uv_scale{20.0F};
    float distortion_uv_scale{1.0F};
    float wave_uv_scale{1.0F};
    float base_scroll{1.0F};
    float distortion_scroll{1.0F};
    float wave_scroll{1.0F};
    float edge_brightness{0.5F};
};

// The sky dome's clouds (VisualLightingModel::sky; docs/legacy-shader-mapping.md): the cloud texture
// (alpha is coverage), scrolling along U at `cloud_scroll` per second before its coordinates are
// scaled by `cloud_scale`. The defaults are the legacy parameters' defaults.
struct VisualSky
{
    std::uint32_t cloud_texture{invalid_visual_texture};
    float cloud_scale{0.0025F};
    float cloud_scroll{0.0025F};
};

// The nebula's wobble (VisualLightingModel::nebula): each vertex moves by `distortion_scale` x
// sin(2 pi frac(`spatial_frequency` x its world coordinate + `time_frequency` x time)) per axis.
// The defaults are the legacy parameters' defaults.
struct VisualNebula
{
    float distortion_scale{25.0F};
    float spatial_frequency{0.002F};
    float time_frequency{0.05F};
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
    // Read only with VisualLightingModel::planet.
    VisualPlanet planet;
    // Read only with VisualLightingModel::shield.
    VisualShield shield;
    // Read only with VisualLightingModel::sky and ::nebula.
    VisualSky sky;
    VisualNebula nebula;
    // The map's water surface: drawn after the opaque surfaces and before the transparent ones,
    // where enhanced water reads the depth of what lies beneath it.
    bool water{};
    // Wind bend (the Tree material's BendScale; render/wind_bend.hpp): vertices sway with the wind
    // by their height above the model's origin, squared; 0 holds still.
    float bend_scale{};
    // Grass (the Grass material): each vertex turns about its clump's center (the second UV set)
    // to face the camera, the clump's top waves with the wind (scaled by bend_scale), and its color
    // swings between `diffuse` and `grass_diffuse_alt` with the wave.
    bool grass{};
    Color grass_diffuse_alt{white};
};

} // namespace eaw
