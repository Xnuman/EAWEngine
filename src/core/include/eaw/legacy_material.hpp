#pragma once

#include "eaw/alo_model.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

// Engine-native interpretation of a legacy Alamo material. The source shader name and every raw
// parameter are preserved; the remaining fields are normalized hints derived either from the
// documented reference-profile table (see docs/legacy-shader-mapping.md) or, for shader names
// outside that table, from conservative name heuristics.
//
// This describes how Alamo meant a material to look. Legacy mode renders it directly with newly
// written shaders to approximate the original appearance; a later physically based path will
// convert it into engine-native materials.

enum class LegacyMaterialFamily
{
    mesh,
    terrain,
    particle,
    decal,
    planet,
    foliage,
    sky,
    nebula,
    shield,
    heat,
    occluded,
    selection,
    solid_color,
    debug,
    default_surface,
    shadow,
    collision,
    unknown,
};

enum class LegacyBlendMode
{
    opaque,
    alpha_test,
    alpha_blend,
    additive,
};

enum class LegacyTextureSemantic
{
    base_color,
    normal,
    specular,
    emissive,
    opacity,
    distortion,
    cloud,
    cloud_normal,
    modulation,
    unknown,
};

// Where the translation came from. Reference profiles are backed by the owner-provided legacy
// shader corpus; heuristics are inference from the shader name only.
enum class LegacyTranslationSource
{
    reference_profile,
    name_heuristic,
};

// The legacy engine sorted effects into named render phases. They are recorded as observed so a
// renderer can schedule equivalent passes; they are not renderer architecture.
enum class LegacyRenderPhase
{
    opaque,
    transparent,
    terrain_mesh,
    heat,
    shadow,
    occluded,
    unknown,
};

enum class LegacyDepthTest
{
    less_equal,
    less,
    greater,
};

enum class LegacyCullMode
{
    back,
    none,
};

enum class LegacyLightingModel
{
    unlit,
    vertex_lit,
    normal_mapped,
    normal_mapped_reflection,
    planet_atmosphere,
    sky_dome,
    lighting_debug,
};

enum class LegacyGlossSource
{
    none,
    base_alpha,
    normal_alpha,
    gloss_texture_red,
};

enum class LegacyUvAnimation
{
    none,
    scroll,
    offset,
    shield_layers,
    cloud_scroll,
};

enum class LegacyVertexAnimation
{
    none,
    wind_bend,
    grass_wind,
    nebula_wobble,
};

struct LegacyRenderState
{
    LegacyRenderPhase phase{LegacyRenderPhase::unknown};
    bool depth_write{true};
    LegacyDepthTest depth_test{LegacyDepthTest::less_equal};
    LegacyCullMode cull{LegacyCullMode::back};
    std::optional<float> alpha_test_threshold;
    bool depth_sorted{};
    bool color_write{true};
    bool stencil{};
    // Blending is enabled only while the engine fades the object (legacy light-scale alpha < 1).
    bool fade_blend{};
};

struct LegacyMaterialFeatures
{
    LegacyLightingModel lighting{LegacyLightingModel::vertex_lit};
    LegacyGlossSource gloss{LegacyGlossSource::none};
    bool colorize{};
    bool base_alpha_opacity{};
    bool base_alpha_emissive{};
    bool vertex_color{};
    bool fog_of_war{};
    bool cloud_shadow{};
    bool distance_fade{};
    bool cube_reflection{};
    LegacyUvAnimation uv_animation{LegacyUvAnimation::none};
    LegacyVertexAnimation vertex_animation{LegacyVertexAnimation::none};
};

struct LegacyVertexRequirements
{
    bool tangent_frame{};
    bool rigid_skinning{};
    bool billboard{};
};

struct LegacyTextureBinding
{
    std::string parameter;
    std::string reference;
    LegacyTextureSemantic semantic{LegacyTextureSemantic::unknown};
};

struct LegacyMaterial
{
    std::string source_shader;
    LegacyTranslationSource source{LegacyTranslationSource::name_heuristic};
    LegacyMaterialFamily family{LegacyMaterialFamily::unknown};
    LegacyBlendMode blend{LegacyBlendMode::opaque};
    LegacyRenderState render;
    LegacyMaterialFeatures features;
    LegacyVertexRequirements vertex;
    std::vector<LegacyTextureBinding> textures;
    std::vector<AloShaderParameter> parameters;

    [[nodiscard]] bool two_sided() const noexcept
    {
        return render.cull == LegacyCullMode::none;
    }

    [[nodiscard]] static LegacyMaterial translate(const AloSubMesh& submesh);
};

// Names (folded, without extension) of every legacy shader that has a reference profile.
[[nodiscard]] std::span<const std::string_view> legacy_reference_shader_names() noexcept;

[[nodiscard]] std::string_view to_string(LegacyMaterialFamily family) noexcept;
[[nodiscard]] std::string_view to_string(LegacyBlendMode blend) noexcept;
[[nodiscard]] std::string_view to_string(LegacyTextureSemantic semantic) noexcept;
[[nodiscard]] std::string_view to_string(LegacyTranslationSource source) noexcept;
[[nodiscard]] std::string_view to_string(LegacyRenderPhase phase) noexcept;
[[nodiscard]] std::string_view to_string(LegacyLightingModel lighting) noexcept;
[[nodiscard]] std::string_view to_string(LegacyGlossSource gloss) noexcept;
[[nodiscard]] std::string_view to_string(LegacyUvAnimation animation) noexcept;
[[nodiscard]] std::string_view to_string(LegacyVertexAnimation animation) noexcept;

} // namespace eaw
