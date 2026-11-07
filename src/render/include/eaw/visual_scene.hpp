#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace eaw {

enum class VisualTextureFormat
{
    bc1_unorm,
    bc1_srgb,
    bc2_unorm,
    bc2_srgb,
    bc3_unorm,
    bc3_srgb,
    bc4_unorm,
    bc4_snorm,
    bc5_unorm,
    bc5_snorm,
    rgba8_unorm,
    rgba8_srgb,
    bgra8_unorm,
    bgra8_srgb,
    bgrx8_unorm,
    bgrx8_srgb,
    r8_unorm,
    a8_unorm,
    rg8_unorm,
};

enum class VisualBlendMode
{
    opaque,
    alpha_test,
    alpha_blend,
    additive,
};

struct VisualTextureMip
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_pitch{};
    std::size_t slice_pitch{};
    std::vector<std::byte> data;
};

struct VisualTexture
{
    std::string name;
    VisualTextureFormat format{VisualTextureFormat::rgba8_unorm};
    std::vector<VisualTextureMip> mips;
};

struct VisualVertex
{
    std::array<float, 3> position{};
    std::array<float, 3> normal{0.0F, 0.0F, 1.0F};
    std::array<float, 2> texture_coordinates{};
    std::array<std::uint32_t, 4> bone_indices{};
    std::array<float, 4> bone_weights{};
    // Tangent frame for normal mapping, in the same space as the normal.
    std::array<float, 3> tangent{1.0F, 0.0F, 0.0F};
    std::array<float, 3> binormal{0.0F, 1.0F, 0.0F};
};

inline constexpr std::uint32_t invalid_visual_texture = std::numeric_limits<std::uint32_t>::max();

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
    std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
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
    std::array<float, 4> diffuse{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 3> specular{0.0F, 0.0F, 0.0F};
    std::array<float, 3> emissive{0.0F, 0.0F, 0.0F};
    // Texture-coordinate offset per second of scene time.
    std::array<float, 2> uv_scroll{0.0F, 0.0F};
    float alpha_test_threshold{0.5F};
};

struct VisualDraw
{
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::int32_t base_vertex{};
    std::uint32_t material{};
};

// A model in the scene: a contiguous range of draws sharing one bone index space.
struct VisualMesh
{
    std::string name;
    std::uint32_t first_draw{};
    std::uint32_t draw_count{};
    std::uint32_t bone_count{};
    std::array<float, 3> bounds_minimum{};
    std::array<float, 3> bounds_maximum{};
};

inline constexpr std::array<float, 16> visual_identity_transform{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};

// One placed copy of a mesh. `transform` maps mesh space to world space in the row-vector
// convention (translation in elements 12..14). Vertex bone indices are offset by `bone_offset`
// into the scene bone palette, so instances can share a pose or own one.
struct VisualInstance
{
    std::uint32_t mesh{};
    std::array<float, 16> transform{visual_identity_transform};
    std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
    std::uint32_t bone_offset{};
    // Player color applied through materials with a team-color mask.
    std::array<float, 4> team_color{1.0F, 1.0F, 1.0F, 1.0F};
    // Background instances (sky domes and star backdrops) are drawn before the world, centered on
    // the camera (translation ignored), at the far plane, without depth testing, in scene order.
    bool background{};
};

// Global lighting for legacy-mode shading: a directional key light (direction points from the
// surface toward the light) plus a hemispherical fill between ground and sky colors. Defaults are
// engine choices until per-map environment lighting is decoded.
struct VisualLightRig
{
    std::array<float, 3> key_direction{0.45F, -0.35F, 0.82F};
    std::array<float, 3> key_diffuse{0.55F, 0.53F, 0.50F};
    std::array<float, 3> key_specular{0.60F, 0.60F, 0.60F};
    std::array<float, 3> fill_sky{0.20F, 0.21F, 0.24F};
    std::array<float, 3> fill_ground{0.09F, 0.09F, 0.10F};
};

// A perspective view in world space (Z up). The renderer derives the aspect ratio from its target.
struct VisualCamera
{
    std::array<float, 3> eye{0.0F, -10.0F, 5.0F};
    std::array<float, 3> target{};
    std::array<float, 3> up{0.0F, 0.0F, 1.0F};
    float vertical_fov_degrees{55.0F};
    float near_plane{0.1F};
    float far_plane{1000.0F};
};

struct VisualScene
{
    std::string name;
    std::vector<VisualVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<VisualTexture> textures;
    std::vector<VisualMaterial> materials;
    std::vector<VisualDraw> draws;
    std::vector<VisualMesh> meshes;
    std::vector<VisualInstance> instances;
    // Size of the scene bone palette (every instance's bone_offset + mesh bone_count fits in it).
    std::uint32_t bone_count{};
    std::array<float, 3> bounds_minimum{};
    std::array<float, 3> bounds_maximum{};
    std::vector<std::string> warnings;
};

} // namespace eaw
