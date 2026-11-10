#include "assets/legacy_material.hpp"
#include "core/string_utils.hpp"

#include <algorithm>

namespace eaw {
namespace {

bool contains(const std::string_view value, const std::string_view token) noexcept
{
    return value.find(token) != std::string_view::npos;
}

// Strips any directory and a trailing ".fx"/".fxo" so "Shaders\\MeshGloss.fx" and "meshgloss"
// both find the same reference profile.
std::string shader_key(const std::string& shader)
{
    auto key = ascii_lower(shader);
    if (const auto separator = key.find_last_of("/\\"); separator != std::string::npos)
    {
        key.erase(0, separator + 1);
    }
    for (const std::string_view extension : {std::string_view{".fxo"}, std::string_view{".fx"}})
    {
        if (key.size() > extension.size() && key.ends_with(extension))
        {
            key.resize(key.size() - extension.size());
            break;
        }
    }
    return key;
}

LegacyTextureSemantic texture_semantic(const std::string_view parameter)
{
    if (contains(parameter, "cloudnormal") || contains(parameter, "cloud_normal"))
    {
        return LegacyTextureSemantic::cloud_normal;
    }
    if (contains(parameter, "cloud"))
    {
        return LegacyTextureSemantic::cloud;
    }
    if (contains(parameter, "normal") || contains(parameter, "bump"))
    {
        return LegacyTextureSemantic::normal;
    }
    if (contains(parameter, "specular") || contains(parameter, "gloss"))
    {
        return LegacyTextureSemantic::specular;
    }
    if (contains(parameter, "emissive") || contains(parameter, "emission") || contains(parameter, "glow"))
    {
        return LegacyTextureSemantic::emissive;
    }
    if (contains(parameter, "opacity") || contains(parameter, "alpha"))
    {
        return LegacyTextureSemantic::opacity;
    }
    if (contains(parameter, "distort") || contains(parameter, "refraction"))
    {
        return LegacyTextureSemantic::distortion;
    }
    // The shield's WaveTexture multiplies the distorted energy layer; it is a color mask.
    if (contains(parameter, "wave"))
    {
        return LegacyTextureSemantic::modulation;
    }
    if (contains(parameter, "diffuse") || contains(parameter, "color") || contains(parameter, "albedo") || contains(parameter, "base") || parameter == "texture" || parameter == "map")
    {
        return LegacyTextureSemantic::base_color;
    }
    return LegacyTextureSemantic::unknown;
}

struct ReferenceProfile
{
    std::string_view name;
    LegacyMaterialFamily family{LegacyMaterialFamily::mesh};
    LegacyBlendMode blend{LegacyBlendMode::opaque};
    LegacyRenderState render;
    LegacyMaterialFeatures features;
    LegacyVertexRequirements vertex;
};

LegacyRenderState opaque_state(const bool fade_blend = true)
{
    return {.phase = LegacyRenderPhase::opaque, .fade_blend = fade_blend};
}

LegacyRenderState transparent_state(const bool depth_sorted)
{
    return {.phase = LegacyRenderPhase::transparent, .depth_write = false, .depth_sorted = depth_sorted};
}

ReferenceProfile rigid_skinned(ReferenceProfile profile, const std::string_view name)
{
    profile.name = name;
    profile.vertex.rigid_skinning = true;
    return profile;
}

// Every entry below records behavior observed in the legacy shader sources from Petroglyph's
// official mod tools (kept locally in Reference Materials/Shaders/), expressed in engine-native
// terms. The evidence, uncertainty, and remaining differences for each entry
// are documented in docs/legacy-shader-mapping.md; keep the two in sync.
std::vector<ReferenceProfile> make_reference_profiles()
{
    using enum LegacyMaterialFamily;
    std::vector<ReferenceProfile> profiles;

    // Vertex-lit opaque surfaces: 2x diffuse texture plus vertex specular.
    const ReferenceProfile gloss{.name = "meshgloss", .render = opaque_state(), .features = {.gloss = LegacyGlossSource::base_alpha}};
    const ReferenceProfile gloss_colorize{.name = "meshglosscolorize", .render = opaque_state(), .features = {.gloss = LegacyGlossSource::gloss_texture_red, .colorize = true}};
    const ReferenceProfile bump_colorize{
        .name = "meshbumpcolorize",
        .render = opaque_state(),
        .features = {.lighting = LegacyLightingModel::normal_mapped, .gloss = LegacyGlossSource::normal_alpha, .colorize = true, .uv_animation = LegacyUvAnimation::offset},
        .vertex = {.tangent_frame = true}};
    const ReferenceProfile bump_reflect_colorize{.name = "meshbumpreflectcolorize",
                                                 .render = opaque_state(),
                                                 .features = {.lighting = LegacyLightingModel::normal_mapped_reflection,
                                                              .gloss = LegacyGlossSource::normal_alpha,
                                                              .colorize = true,
                                                              .cube_reflection = true,
                                                              .uv_animation = LegacyUvAnimation::offset},
                                                 .vertex = {.tangent_frame = true}};
    profiles.push_back(gloss);
    profiles.push_back(rigid_skinned(gloss, "rskingloss"));
    profiles.push_back(gloss_colorize);
    profiles.push_back(rigid_skinned(gloss_colorize, "rskinglosscolorize"));
    profiles.push_back(bump_colorize);
    profiles.push_back(rigid_skinned(bump_colorize, "rskinbumpcolorize"));
    profiles.push_back(bump_reflect_colorize);
    profiles.push_back(rigid_skinned(bump_reflect_colorize, "rskinbumpreflectcolorize"));
    profiles.push_back({.name = "batchmeshgloss", .render = opaque_state(), .features = {.gloss = LegacyGlossSource::base_alpha, .fog_of_war = true, .distance_fade = true}});
    // The glow shader adds the unlit Specular color masked by base alpha: a self-illumination mask.
    profiles.push_back({.name = "fsameshglow", .render = opaque_state(), .features = {.base_alpha_emissive = true}});

    // Alpha-blended vertex-lit surfaces.
    const ReferenceProfile alpha{.name = "meshalpha", .blend = LegacyBlendMode::alpha_blend, .render = transparent_state(true), .features = {.base_alpha_opacity = true}};
    const ReferenceProfile alpha_gloss{
        .name = "meshalphagloss", .blend = LegacyBlendMode::alpha_blend, .render = transparent_state(false), .features = {.gloss = LegacyGlossSource::gloss_texture_red, .base_alpha_opacity = true}};
    profiles.push_back(alpha);
    auto skinned_alpha = rigid_skinned(alpha, "rskinalpha");
    skinned_alpha.render.depth_sorted = false;
    profiles.push_back(skinned_alpha);
    auto alpha_scroll = alpha;
    alpha_scroll.name = "meshalphascroll";
    alpha_scroll.features.uv_animation = LegacyUvAnimation::scroll;
    profiles.push_back(alpha_scroll);
    auto batch_alpha = alpha;
    batch_alpha.name = "batchmeshalpha";
    batch_alpha.features.fog_of_war = true;
    batch_alpha.features.distance_fade = true;
    profiles.push_back(batch_alpha);
    profiles.push_back(alpha_gloss);
    profiles.push_back(rigid_skinned(alpha_gloss, "rskinalphagloss"));

    // Unlit additive effects. Color (or vertex color) is pre-multiplied by the engine fade.
    const ReferenceProfile additive{
        .name = "meshadditive", .blend = LegacyBlendMode::additive, .render = transparent_state(true), .features = {.lighting = LegacyLightingModel::unlit, .uv_animation = LegacyUvAnimation::scroll}};
    profiles.push_back(additive);
    auto skinned_additive = rigid_skinned(additive, "rskinadditive");
    skinned_additive.render.depth_sorted = false;
    profiles.push_back(skinned_additive);
    auto additive_vcolor = additive;
    additive_vcolor.name = "meshadditivevcolor";
    additive_vcolor.features.vertex_color = true;
    profiles.push_back(additive_vcolor);
    auto skinned_additive_vcolor = rigid_skinned(additive_vcolor, "rskinadditivevcolor");
    skinned_additive_vcolor.render.depth_sorted = false;
    profiles.push_back(skinned_additive_vcolor);
    auto additive_offset = additive;
    additive_offset.name = "meshadditiveoffset";
    additive_offset.features.uv_animation = LegacyUvAnimation::offset;
    profiles.push_back(additive_offset);

    // Special effect families.
    profiles.push_back({.name = "meshshield",
                        .family = shield,
                        .blend = LegacyBlendMode::additive,
                        .render = {.phase = LegacyRenderPhase::transparent, .depth_write = false, .cull = LegacyCullMode::none, .depth_sorted = true},
                        .features = {.lighting = LegacyLightingModel::unlit, .vertex_color = true, .uv_animation = LegacyUvAnimation::shield_layers}});
    const ReferenceProfile heat_profile{.name = "meshheat",
                                        .family = heat,
                                        .blend = LegacyBlendMode::alpha_blend,
                                        .render = {.phase = LegacyRenderPhase::heat, .depth_write = false},
                                        .features = {.lighting = LegacyLightingModel::unlit, .vertex_color = true, .uv_animation = LegacyUvAnimation::scroll}};
    profiles.push_back(heat_profile);
    // RSkinHeat declares the Transparent phase rather than Heat; the observed declaration is kept.
    auto skinned_heat = rigid_skinned(heat_profile, "rskinheat");
    skinned_heat.render.phase = LegacyRenderPhase::transparent;
    profiles.push_back(skinned_heat);
    const ReferenceProfile occluded_profile{.name = "meshoccludedunit",
                                            .family = occluded,
                                            .blend = LegacyBlendMode::alpha_blend,
                                            .render = {.phase = LegacyRenderPhase::occluded, .depth_write = false, .depth_test = LegacyDepthTest::greater, .stencil = true},
                                            .features = {.lighting = LegacyLightingModel::unlit}};
    profiles.push_back(occluded_profile);
    profiles.push_back(rigid_skinned(occluded_profile, "rskinoccludedunit"));
    const ReferenceProfile shadow_profile{
        .name = "meshshadowvolume",
        .family = shadow,
        .render = {.phase = LegacyRenderPhase::shadow, .depth_write = false, .depth_test = LegacyDepthTest::less, .cull = LegacyCullMode::none, .color_write = false, .stencil = true},
        .features = {.lighting = LegacyLightingModel::unlit}};
    profiles.push_back(shadow_profile);
    profiles.push_back(rigid_skinned(shadow_profile, "rskinshadowvolume"));
    profiles.push_back({.name = "blobstencilmasked",
                        .family = selection,
                        .blend = LegacyBlendMode::alpha_blend,
                        .render = {.phase = LegacyRenderPhase::transparent, .depth_write = false, .alpha_test_threshold = 128.0F / 255.0F, .stencil = true},
                        .features = {.lighting = LegacyLightingModel::unlit}});

    // Environment and world surfaces.
    profiles.push_back({.name = "planet",
                        .family = planet,
                        .render = opaque_state(),
                        .features = {.lighting = LegacyLightingModel::planet_atmosphere, .gloss = LegacyGlossSource::normal_alpha, .uv_animation = LegacyUvAnimation::cloud_scroll},
                        .vertex = {.tangent_frame = true}});
    profiles.push_back({.name = "skydome",
                        .family = sky,
                        .render = {.phase = LegacyRenderPhase::opaque, .depth_write = false},
                        .features = {.lighting = LegacyLightingModel::sky_dome, .vertex_color = true, .uv_animation = LegacyUvAnimation::cloud_scroll}});
    profiles.push_back(
        {.name = "nebula",
         .family = nebula,
         .blend = LegacyBlendMode::additive,
         .render = {.phase = LegacyRenderPhase::transparent, .depth_write = false, .cull = LegacyCullMode::none, .depth_sorted = true},
         .features = {.lighting = LegacyLightingModel::unlit, .vertex_color = true, .uv_animation = LegacyUvAnimation::scroll, .vertex_animation = LegacyVertexAnimation::nebula_wobble}});
    profiles.push_back({.name = "terrainmeshbump",
                        .family = terrain,
                        .render = {.phase = LegacyRenderPhase::terrain_mesh},
                        .features = {.lighting = LegacyLightingModel::normal_mapped, .gloss = LegacyGlossSource::base_alpha, .fog_of_war = true, .cloud_shadow = true},
                        .vertex = {.tangent_frame = true}});
    profiles.push_back({.name = "terrainmeshgloss",
                        .family = terrain,
                        .render = {.phase = LegacyRenderPhase::terrain_mesh},
                        .features = {.gloss = LegacyGlossSource::base_alpha, .fog_of_war = true, .cloud_shadow = true}});
    // Tree declares the Transparent phase but renders alpha-tested with depth writes.
    profiles.push_back({.name = "tree",
                        .family = foliage,
                        .blend = LegacyBlendMode::alpha_test,
                        .render = {.phase = LegacyRenderPhase::transparent, .alpha_test_threshold = 128.0F / 255.0F, .fade_blend = true},
                        .features = {.gloss = LegacyGlossSource::base_alpha, .base_alpha_opacity = true, .vertex_animation = LegacyVertexAnimation::wind_bend}});
    profiles.push_back({.name = "grass",
                        .family = foliage,
                        .blend = LegacyBlendMode::alpha_blend,
                        .render = {.phase = LegacyRenderPhase::transparent, .depth_write = false, .alpha_test_threshold = 8.0F / 255.0F},
                        .features = {.base_alpha_opacity = true, .fog_of_war = true, .distance_fade = true, .vertex_animation = LegacyVertexAnimation::grass_wind},
                        .vertex = {.billboard = true}});

    // Utility and debug shaders.
    profiles.push_back({.name = "meshcollision", .family = collision, .render = opaque_state(false), .features = {.lighting = LegacyLightingModel::unlit}});
    profiles.push_back({.name = "meshsolidcolor", .family = solid_color, .render = {.phase = LegacyRenderPhase::opaque, .depth_write = false}, .features = {.lighting = LegacyLightingModel::unlit}});
    profiles.push_back({.name = "meshlightvisualize", .family = debug, .render = opaque_state(false), .features = {.lighting = LegacyLightingModel::lighting_debug}});
    return profiles;
}

const std::vector<ReferenceProfile>& reference_profiles()
{
    static const auto profiles = make_reference_profiles();
    return profiles;
}

const std::vector<std::string_view>& reference_profile_names()
{
    static const auto names = []
    {
        std::vector<std::string_view> result;
        for (const auto& profile : reference_profiles())
        {
            result.push_back(profile.name);
        }
        return result;
    }();
    return names;
}

const ReferenceProfile* find_reference_profile(const std::string_view key)
{
    const auto& profiles = reference_profiles();
    const auto found = std::ranges::find(profiles, key, &ReferenceProfile::name);
    return found == profiles.end() ? nullptr : &*found;
}

// Fallback for shaders outside the reference corpus (for example alDefault.fx or mod shaders).
// These rules are name inference only and are reported as such.
void apply_name_heuristics(LegacyMaterial& result, const std::string_view shader)
{
    using enum LegacyMaterialFamily;
    result.source = LegacyTranslationSource::name_heuristic;
    result.render.phase = LegacyRenderPhase::opaque;

    if (shader == "aldefault" || shader == "default")
    {
        result.family = default_surface;
    }
    else if (contains(shader, "planet"))
    {
        result.family = planet;
    }
    else if (contains(shader, "tree") || contains(shader, "grass"))
    {
        result.family = foliage;
    }
    else if (contains(shader, "skydome"))
    {
        result.family = sky;
    }
    else if (contains(shader, "nebula"))
    {
        result.family = nebula;
    }
    else if (contains(shader, "shield"))
    {
        result.family = shield;
    }
    else if (contains(shader, "heat"))
    {
        result.family = heat;
    }
    else if (contains(shader, "collision"))
    {
        result.family = collision;
    }
    else if (contains(shader, "shadow"))
    {
        result.family = shadow;
    }
    else if (contains(shader, "particle"))
    {
        result.family = particle;
    }
    else if (contains(shader, "terrain"))
    {
        result.family = terrain;
    }
    else if (contains(shader, "decal"))
    {
        result.family = decal;
    }
    else if (contains(shader, "mesh") || contains(shader, "skin"))
    {
        result.family = mesh;
    }

    result.vertex.rigid_skinning = contains(shader, "rskin");
    if (contains(shader, "bump"))
    {
        result.features.lighting = LegacyLightingModel::normal_mapped;
        result.vertex.tangent_frame = true;
    }
    result.features.colorize = contains(shader, "colorize");

    if (contains(shader, "additive") || result.family == shield || result.family == nebula)
    {
        result.blend = LegacyBlendMode::additive;
    }
    else if (contains(shader, "alphatest") || contains(shader, "alpha_test") || result.family == foliage)
    {
        result.blend = LegacyBlendMode::alpha_test;
        result.render.alpha_test_threshold = 0.5F;
    }
    else if (contains(shader, "transparent") || contains(shader, "blend") || contains(shader, "alpha") || result.family == heat)
    {
        result.blend = LegacyBlendMode::alpha_blend;
    }
    if (result.blend == LegacyBlendMode::additive || result.blend == LegacyBlendMode::alpha_blend)
    {
        result.render.phase = result.family == heat ? LegacyRenderPhase::heat : LegacyRenderPhase::transparent;
        result.render.depth_write = false;
        result.render.depth_sorted = true;
        result.features.base_alpha_opacity = result.blend == LegacyBlendMode::alpha_blend;
    }
    if (result.blend == LegacyBlendMode::additive || result.family == heat)
    {
        result.features.lighting = LegacyLightingModel::unlit;
    }
    if (result.family == shadow)
    {
        result.render.phase = LegacyRenderPhase::shadow;
        result.render.color_write = false;
        result.render.depth_write = false;
    }
    if (contains(shader, "twosided") || contains(shader, "two_sided") || result.family == shield || result.family == nebula)
    {
        result.render.cull = LegacyCullMode::none;
    }
}

} // namespace

LegacyMaterial LegacyMaterial::translate(const AloSubMesh& submesh)
{
    LegacyMaterial result;
    result.source_shader = submesh.shader;
    result.parameters = submesh.parameters;
    const auto key = shader_key(submesh.shader);

    if (const auto* profile = find_reference_profile(key))
    {
        result.source = LegacyTranslationSource::reference_profile;
        result.family = profile->family;
        result.blend = profile->blend;
        result.render = profile->render;
        result.features = profile->features;
        result.vertex = profile->vertex;
    }
    else
    {
        apply_name_heuristics(result, key);
    }

    for (const auto& parameter : submesh.parameters)
    {
        if (parameter.type != AloShaderParameterType::texture || parameter.value.empty())
        {
            continue;
        }
        const auto name = ascii_lower(parameter.name);
        result.textures.push_back({parameter.name, parameter.value, texture_semantic(name)});
    }
    if (result.textures.size() == 1 && result.textures.front().semantic == LegacyTextureSemantic::unknown)
    {
        result.textures.front().semantic = LegacyTextureSemantic::base_color;
    }
    return result;
}

std::span<const std::string_view> legacy_reference_shader_names() noexcept
{
    return reference_profile_names();
}

std::string_view to_string(const LegacyMaterialFamily family) noexcept
{
    switch (family)
    {
    case LegacyMaterialFamily::mesh:
        return "mesh";
    case LegacyMaterialFamily::terrain:
        return "terrain";
    case LegacyMaterialFamily::particle:
        return "particle";
    case LegacyMaterialFamily::decal:
        return "decal";
    case LegacyMaterialFamily::planet:
        return "planet";
    case LegacyMaterialFamily::foliage:
        return "foliage";
    case LegacyMaterialFamily::sky:
        return "sky";
    case LegacyMaterialFamily::nebula:
        return "nebula";
    case LegacyMaterialFamily::shield:
        return "shield";
    case LegacyMaterialFamily::heat:
        return "heat";
    case LegacyMaterialFamily::occluded:
        return "occluded";
    case LegacyMaterialFamily::selection:
        return "selection";
    case LegacyMaterialFamily::solid_color:
        return "solid-color";
    case LegacyMaterialFamily::debug:
        return "debug";
    case LegacyMaterialFamily::default_surface:
        return "default-surface";
    case LegacyMaterialFamily::shadow:
        return "shadow";
    case LegacyMaterialFamily::collision:
        return "collision";
    case LegacyMaterialFamily::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view to_string(const LegacyBlendMode blend) noexcept
{
    switch (blend)
    {
    case LegacyBlendMode::opaque:
        return "opaque";
    case LegacyBlendMode::alpha_test:
        return "alpha-test";
    case LegacyBlendMode::alpha_blend:
        return "alpha-blend";
    case LegacyBlendMode::additive:
        return "additive";
    }
    return "opaque";
}

std::string_view to_string(const LegacyTextureSemantic semantic) noexcept
{
    switch (semantic)
    {
    case LegacyTextureSemantic::base_color:
        return "base-color";
    case LegacyTextureSemantic::normal:
        return "normal";
    case LegacyTextureSemantic::specular:
        return "specular";
    case LegacyTextureSemantic::emissive:
        return "emissive";
    case LegacyTextureSemantic::opacity:
        return "opacity";
    case LegacyTextureSemantic::distortion:
        return "distortion";
    case LegacyTextureSemantic::cloud:
        return "cloud";
    case LegacyTextureSemantic::cloud_normal:
        return "cloud-normal";
    case LegacyTextureSemantic::modulation:
        return "modulation";
    case LegacyTextureSemantic::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view to_string(const LegacyTranslationSource source) noexcept
{
    switch (source)
    {
    case LegacyTranslationSource::reference_profile:
        return "reference-profile";
    case LegacyTranslationSource::name_heuristic:
        return "name-heuristic";
    }
    return "name-heuristic";
}

std::string_view to_string(const LegacyRenderPhase phase) noexcept
{
    switch (phase)
    {
    case LegacyRenderPhase::opaque:
        return "opaque";
    case LegacyRenderPhase::transparent:
        return "transparent";
    case LegacyRenderPhase::terrain_mesh:
        return "terrain-mesh";
    case LegacyRenderPhase::heat:
        return "heat";
    case LegacyRenderPhase::shadow:
        return "shadow";
    case LegacyRenderPhase::occluded:
        return "occluded";
    case LegacyRenderPhase::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view to_string(const LegacyLightingModel lighting) noexcept
{
    switch (lighting)
    {
    case LegacyLightingModel::unlit:
        return "unlit";
    case LegacyLightingModel::vertex_lit:
        return "vertex-lit";
    case LegacyLightingModel::normal_mapped:
        return "normal-mapped";
    case LegacyLightingModel::normal_mapped_reflection:
        return "normal-mapped-reflection";
    case LegacyLightingModel::planet_atmosphere:
        return "planet-atmosphere";
    case LegacyLightingModel::sky_dome:
        return "sky-dome";
    case LegacyLightingModel::lighting_debug:
        return "lighting-debug";
    }
    return "vertex-lit";
}

std::string_view to_string(const LegacyGlossSource gloss) noexcept
{
    switch (gloss)
    {
    case LegacyGlossSource::none:
        return "none";
    case LegacyGlossSource::base_alpha:
        return "base-alpha";
    case LegacyGlossSource::normal_alpha:
        return "normal-alpha";
    case LegacyGlossSource::gloss_texture_red:
        return "gloss-texture-red";
    }
    return "none";
}

std::string_view to_string(const LegacyUvAnimation animation) noexcept
{
    switch (animation)
    {
    case LegacyUvAnimation::none:
        return "none";
    case LegacyUvAnimation::scroll:
        return "scroll";
    case LegacyUvAnimation::offset:
        return "offset";
    case LegacyUvAnimation::shield_layers:
        return "shield-layers";
    case LegacyUvAnimation::cloud_scroll:
        return "cloud-scroll";
    }
    return "none";
}

std::string_view to_string(const LegacyVertexAnimation animation) noexcept
{
    switch (animation)
    {
    case LegacyVertexAnimation::none:
        return "none";
    case LegacyVertexAnimation::wind_bend:
        return "wind-bend";
    case LegacyVertexAnimation::grass_wind:
        return "grass-wind";
    case LegacyVertexAnimation::nebula_wobble:
        return "nebula-wobble";
    }
    return "none";
}

} // namespace eaw
