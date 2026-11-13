#pragma once

// What changes from frame to frame: placed instances, lighting, particles, and debug lines.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "render/color.hpp"
#include "render/visual_texture.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace eaw {

// One placed copy of a mesh. `transform` maps mesh space to world space in the row-vector
// convention (translation in elements 12..14). Vertex bone indices are offset by `bone_offset`
// into the scene bone palette, so instances can share a pose or own one.
struct VisualInstance
{
    std::uint32_t mesh{};
    Matrix4 transform{Matrix4::identity()};
    Color tint{white};
    std::uint32_t bone_offset{};
    // Player color applied through materials with a team-color mask.
    Color team_color{white};
    // Background instances (sky domes and star backdrops) are drawn before the world, centered on
    // the camera (translation ignored), at the far plane, without depth testing, in scene order.
    bool background{};
    // Drawn with the selection tint (any number of instances, e.g. a box-selected group).
    bool highlighted{};
    // The damage state to draw (VisualMeshState::alternate; a state the mesh lacks draws state 0).
    std::uint32_t state{};
    // Stable identity across frames (an entity key), so the renderer can follow the instance's
    // detail level and cross-fade changes; 0 picks the level afresh every frame without fading.
    std::uint64_t key{};
    // How much farther than usual the instance keeps its detail (its type's LOD_Bias; 1 as given).
    float lod_bias{1.0F};
    // Darkened where key-light shadows fall on it (backdrops such as planets are not: XML
    // In_Background; backgrounds never are).
    bool receives_shadows{true};
};

// A directional light; `direction` points from the surface toward the light (any length).
struct VisualDirectionalLight
{
    Vector3 direction{0.0F, 0.0F, 1.0F};
    ColorRgb color{};
};

// How the renderer draws the map's fog: not at all; as the original (linear in view distance, the
// legacy profile); or as exponential height fog along each view ray, thickest low down, brighter
// toward the key light, from the same color and range (enhanced; engine choices).
enum class VisualFogMode : std::uint8_t
{
    off,
    legacy,
    enhanced,
};

[[nodiscard]] constexpr std::string_view to_string(const VisualFogMode mode) noexcept
{
    switch (mode)
    {
    case VisualFogMode::off:
        return "off";
    case VisualFogMode::legacy:
        return "legacy";
    case VisualFogMode::enhanced:
        return "enhanced";
    }
    return "off";
}

// A mode by name; "on" is the legacy mode.
[[nodiscard]] constexpr std::optional<VisualFogMode> visual_fog_mode(const std::string_view name) noexcept
{
    if (name == "on")
    {
        return VisualFogMode::legacy;
    }
    for (const auto mode : {VisualFogMode::off, VisualFogMode::legacy, VisualFogMode::enhanced})
    {
        if (name == to_string(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

// How the renderer draws the map's water: as the original (the surface blended at its material's
// opacity, the legacy profile), or from the depth of what lies beneath it, with a soft shore, two
// normal layers, a sun glint, and reflectance growing toward grazing views (enhanced;
// docs/water-research.md; engine choices).
enum class VisualWaterMode : std::uint8_t
{
    legacy,
    enhanced,
};

[[nodiscard]] constexpr std::string_view to_string(const VisualWaterMode mode) noexcept
{
    return mode == VisualWaterMode::enhanced ? "enhanced" : "legacy";
}

[[nodiscard]] constexpr std::optional<VisualWaterMode> visual_water_mode(const std::string_view name) noexcept
{
    for (const auto mode : {VisualWaterMode::legacy, VisualWaterMode::enhanced})
    {
        if (name == to_string(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

// The map environment's wind: the direction it blows toward on the ground (z ignored) and its
// speed on the editor's scale (1-10, 10 a strong wind; the default 2). From TED environment fields
// 0x2b (direction in degrees) and 0x2c (speed), a reading of the stock maps' values.
struct VisualWind
{
    Vector3 direction{1.0F, 0.0F, 0.0F};
    float speed{2.0F};
};

// Distance fog of the map environment: surfaces blend toward `color` linearly with view distance,
// from none at `start` to all at `end`. Additive surfaces and backgrounds are not fogged (as in the
// legacy shaders).
struct VisualFog
{
    bool enabled{};
    ColorRgb color{};
    float start{};
    float end{};
};

// Global lighting for legacy-mode shading, matching the TED environment model: a key light that
// also drives the highlight, two diffuse fill lights, and ambient. Maps supply their active
// environment; the defaults are an engine choice for model inspection.
struct VisualLightRig
{
    VisualDirectionalLight key{{0.45F, -0.35F, 0.82F}, {0.55F, 0.53F, 0.50F}};
    ColorRgb key_specular{0.60F, 0.60F, 0.60F};
    std::array<VisualDirectionalLight, 2> fills{{{{-0.6F, 0.5F, 0.4F}, {0.14F, 0.15F, 0.18F}}, {{0.4F, 0.7F, -0.5F}, {0.08F, 0.08F, 0.10F}}}};
    ColorRgb ambient{0.10F, 0.10F, 0.11F};
    // The environment's fog (none by default).
    VisualFog fog;
    VisualWind wind;
};

enum class VisualParticleShape : std::uint8_t
{
    // A quad facing the camera, rotated by `rotation` in the view plane.
    billboard,
    // A quad lying in the world XY plane (ground decals, shock rings), rotated about Z.
    ground,
    // A diamond facing the camera with its trailing corner (texture top-left) pointing against `axis`
    // (the particle's velocity) on screen and pushed out by max(1, tail x the velocity's on-screen
    // fraction / sqrt 2) (sparks, streaks).
    kite,
    // A bolt centered on `position`: `tail` long along `axis`, 2 x `half_size` wide, turned toward
    // the camera around its axis. The texture cell holds a diagonal bolt (bottom-left to top-right)
    // that is mapped along the axis.
    beam,
    // Like beam, but drawn as a teardrop (round head at the front tapering to a fading tail) in
    // `color`, without a texture.
    teardrop,
};

enum class VisualParticleBlend : std::uint8_t
{
    // Colour x texture added to the scene; alpha is ignored (the original's additive).
    additive,
    alpha,
    // Destination times source color (darkening).
    modulate,
    // Colour x texture x alpha added to the scene (enhanced additive).
    additive_alpha,
};

// One particle sprite in world space, expanded to a quad on the GPU. Particles are drawn after the
// scene's transparent surfaces in the order given (the engine sorts them back to front), without
// writing depth.
struct VisualParticle
{
    Vector3 position{};
    float half_size{1.0F};
    // Linear RGBA multiplier of the texture.
    Color color{white};
    // Texture rectangle: u, v of the top-left corner, then width and height (atlas cells).
    std::array<float, 4> uv_rect{0.0F, 0.0F, 1.0F, 1.0F};
    // Kites: the world-space direction of travel (any length; zero draws a billboard).
    Vector3 axis{};
    // Radians, counter-clockwise in the quad's plane.
    float rotation{};
    // Kites: extra length behind the particle along -axis, in world units.
    float tail{};
    // A texture registered with the renderer (D3d12Renderer::add_textures), or none (white).
    std::uint32_t texture{invalid_visual_texture};
    VisualParticleShape shape{VisualParticleShape::billboard};
    VisualParticleBlend blend{VisualParticleBlend::additive};
    bool depth_test{true};
    // Use the texture's brightness, tinted by `color`, instead of its colours.
    bool monochrome{};
};

// A world-space debug line segment (bounds, axes, paths), drawn depth-tested over the scene.
struct VisualDebugLine
{
    Vector3 from{};
    Vector3 to{};
    Color color{white};
};

enum class VisualDecalMode : std::uint8_t
{
    // The texture's color blended into the ground's by its alpha.
    diffuse,
    // The texture's color is a tangent-space normal bending the ground's by its alpha (a dent).
    bump,
    // The texture's color added to the lit ground by its alpha.
    additive,
};

// A mark on the terrain, seen from above: a rectangle centered at `center` (world X/Y), reaching
// `half_size` along its own axes, turned `rotation` radians about +Z, that shows `uv_rect` (u, v,
// width, height) of `texture` (a renderer texture) with its alpha as the shape, scaled by `opacity`.
// Decals lie on the terrain only, and take part in its lighting, shadows, and fog.
struct VisualDecal
{
    Vector2 center{};
    Vector2 half_size{1.0F, 1.0F};
    float rotation{};
    std::array<float, 4> uv_rect{0.0F, 0.0F, 1.0F, 1.0F};
    std::uint32_t texture{invalid_visual_texture};
    VisualDecalMode mode{VisualDecalMode::diffuse};
    float opacity{1.0F};
};

} // namespace eaw
