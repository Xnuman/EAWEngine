#include "core/string_utils.hpp"
#include "formats/dds_texture.hpp"
#include "formats/xml_document.hpp"
#include "assets/map_object_layout.hpp"
#include "engine/combat_content.hpp"
#include "engine/scene_builder_internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <span>
#include <system_error>
#include <type_traits>

namespace eaw::scene_build {

namespace {

std::optional<VisualTextureFormat> visual_format(const DdsFormat format)
{
    switch (format)
    {
    case DdsFormat::bc1_unorm:
        return VisualTextureFormat::bc1_unorm;
    case DdsFormat::bc1_srgb:
        return VisualTextureFormat::bc1_srgb;
    case DdsFormat::bc2_unorm:
        return VisualTextureFormat::bc2_unorm;
    case DdsFormat::bc2_srgb:
        return VisualTextureFormat::bc2_srgb;
    case DdsFormat::bc3_unorm:
        return VisualTextureFormat::bc3_unorm;
    case DdsFormat::bc3_srgb:
        return VisualTextureFormat::bc3_srgb;
    case DdsFormat::bc4_unorm:
        return VisualTextureFormat::bc4_unorm;
    case DdsFormat::bc4_snorm:
        return VisualTextureFormat::bc4_snorm;
    case DdsFormat::bc5_unorm:
        return VisualTextureFormat::bc5_unorm;
    case DdsFormat::bc5_snorm:
        return VisualTextureFormat::bc5_snorm;
    case DdsFormat::rgba8_unorm:
        return VisualTextureFormat::rgba8_unorm;
    case DdsFormat::rgba8_srgb:
        return VisualTextureFormat::rgba8_srgb;
    case DdsFormat::bgra8_unorm:
        return VisualTextureFormat::bgra8_unorm;
    case DdsFormat::bgra8_srgb:
        return VisualTextureFormat::bgra8_srgb;
    case DdsFormat::bgrx8_unorm:
        return VisualTextureFormat::bgrx8_unorm;
    case DdsFormat::bgrx8_srgb:
        return VisualTextureFormat::bgrx8_srgb;
    case DdsFormat::r8_unorm:
        return VisualTextureFormat::r8_unorm;
    case DdsFormat::a8_unorm:
        return VisualTextureFormat::a8_unorm;
    case DdsFormat::rg8_unorm:
        return VisualTextureFormat::rg8_unorm;
    case DdsFormat::rgbx8_unorm:
    case DdsFormat::rgb8_unorm:
    case DdsFormat::bgr8_unorm:
        return VisualTextureFormat::rgba8_unorm;
    case DdsFormat::unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

Result<VisualTexture> make_visual_texture(const std::string_view name, const std::span<const std::byte> bytes)
{
    EAW_TRY_ASSIGN(const auto dds, read_dds(bytes));
    if (dds.volume || dds.cube_map || dds.array_size != 1 || dds.faces != 1)
    {
        return make_error("only ordinary 2D DDS textures are supported by the visual sandbox");
    }
    const auto format = visual_format(dds.format);
    if (!format)
    {
        return make_error("DDS format is not supported by the D3D12 sandbox");
    }

    VisualTexture result;
    result.name = name;
    result.format = *format;
    for (const auto& image : dds.subresources)
    {
        if (image.array_layer != 0 || image.face != 0 || image.depth != 1)
        {
            continue;
        }
        if (image.offset > bytes.size() || image.size > bytes.size() - image.offset)
        {
            return make_error("DDS subresource extends beyond its file");
        }

        VisualTextureMip mip;
        mip.width = image.width;
        mip.height = image.height;
        if (dds.format == DdsFormat::rgb8_unorm || dds.format == DdsFormat::bgr8_unorm || dds.format == DdsFormat::rgbx8_unorm)
        {
            const std::size_t source_stride = dds.format == DdsFormat::rgbx8_unorm ? 4U : 3U;
            mip.row_pitch = static_cast<std::size_t>(image.width) * 4U;
            mip.slice_pitch = mip.row_pitch * image.height;
            mip.data.resize(mip.slice_pitch);
            for (std::uint32_t y = 0; y < image.height; ++y)
            {
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    const auto source = image.offset + static_cast<std::size_t>(y) * image.row_pitch + static_cast<std::size_t>(x) * source_stride;
                    const auto destination = static_cast<std::size_t>(y) * mip.row_pitch + static_cast<std::size_t>(x) * 4U;
                    const bool bgr = dds.format == DdsFormat::bgr8_unorm;
                    mip.data[destination] = bytes[source + (bgr ? 2U : 0U)];
                    mip.data[destination + 1U] = bytes[source + 1U];
                    mip.data[destination + 2U] = bytes[source + (bgr ? 0U : 2U)];
                    mip.data[destination + 3U] = std::byte{0xff};
                }
            }
        }
        else
        {
            mip.row_pitch = image.row_pitch;
            mip.slice_pitch = image.slice_pitch;
            mip.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(image.offset), bytes.begin() + static_cast<std::ptrdiff_t>(image.offset + image.size));
        }
        result.mips.push_back(std::move(mip));
    }
    if (result.mips.empty())
    {
        return make_error("DDS contains no usable 2D mip levels");
    }
    return result;
}

std::optional<std::string> resolve_texture(const AssetReader& vfs, const std::string_view reference)
{
    auto filename = std::filesystem::path(reference).filename();
    filename.replace_extension(".DDS");
    const std::array candidates{std::string("DATA/ART/TEXTURES/") + filename.generic_string(), std::string("DATA/ART/") + filename.generic_string(), filename.generic_string()};
    for (const auto& candidate : candidates)
    {
        if (const auto resolved = vfs.resolve(candidate))
        {
            return resolved->virtual_path;
        }
    }
    return std::nullopt;
}

VisualBlendMode visual_blend(const LegacyBlendMode blend)
{
    switch (blend)
    {
    case LegacyBlendMode::opaque:
        return VisualBlendMode::opaque;
    case LegacyBlendMode::alpha_test:
        return VisualBlendMode::alpha_test;
    case LegacyBlendMode::alpha_blend:
        return VisualBlendMode::alpha_blend;
    case LegacyBlendMode::additive:
        return VisualBlendMode::additive;
    }
    return VisualBlendMode::opaque;
}

// Every decode gets a new identity, so a cached texture keeps its identity across builds and a
// re-decoded one gets a fresh one; the renderer then re-uploads exactly the changed textures.
std::string next_texture_identity()
{
    static std::atomic<std::uint64_t> serial{0};
    return "texture#" + std::to_string(serial.fetch_add(1, std::memory_order_relaxed) + 1);
}

// Exact numeric components of a named material parameter, or nullptr when absent or malformed.
const std::vector<float>* material_values(const LegacyMaterial& material, const std::string_view name)
{
    for (const auto& parameter : material.parameters)
    {
        if (!parameter.values.empty() && ascii_lower(parameter.name) == ascii_lower(std::string(name)))
        {
            return &parameter.values;
        }
    }
    return nullptr;
}

// The components a material parameter fills in `Target`: four for a Color, three for a ColorRgb,
// and the array's size for a vector parameter (UVScrollRate fills two).
template <class Target> constexpr std::size_t parameter_components() noexcept
{
    if constexpr (std::is_same_v<Target, Color>)
    {
        return 4U;
    }
    else if constexpr (std::is_same_v<Target, ColorRgb>)
    {
        return 3U;
    }
    else
    {
        return std::tuple_size_v<Target>;
    }
}

// A named material parameter (a color, or UVScrollRate) into `target`, component by component;
// missing and non-finite components keep the target's value.
template <class Target> void read_color(const LegacyMaterial& material, const std::string_view name, Target& target)
{
    constexpr auto channels = parameter_components<Target>();
    if (const auto* values = material_values(material, name))
    {
        for (std::size_t index = 0; index < channels && index < values->size(); ++index)
        {
            if (std::isfinite((*values)[index]))
            {
                target[index] = (*values)[index];
            }
        }
    }
}

const LegacyTextureBinding* texture_binding(const LegacyMaterial& material, const LegacyTextureSemantic semantic)
{
    const auto found = std::ranges::find(material.textures, semantic, &LegacyTextureBinding::semantic);
    return found == material.textures.end() ? nullptr : &*found;
}

} // namespace

const LegacyTextureBinding* base_color_texture(const LegacyMaterial& material)
{
    const auto found = std::ranges::find(material.textures, LegacyTextureSemantic::base_color, &LegacyTextureBinding::semantic);
    if (found != material.textures.end())
    {
        return &*found;
    }
    return material.textures.empty() ? nullptr : &material.textures.front();
}

// Loads (once per scene) the DDS for an ALO texture reference and returns its scene texture
// index, or invalid_visual_texture with a warning when it is missing or unsupported.
std::shared_ptr<const CachedTexture> cached_texture(const AssetReader& vfs, AssetCache* cache, const std::string& reference)
{
    return ResourceManager(vfs, cache).get<CachedTexture>(reference);
}

std::uint32_t scene_texture(SceneBuilder& builder, const std::string& reference)
{
    if (!builder.textures)
    {
        return invalid_visual_texture;
    }
    const auto key = ascii_lower(reference);
    if (const auto existing = builder.texture_indices.find(key); existing != builder.texture_indices.end())
    {
        return existing->second;
    }
    auto index = invalid_visual_texture;
    const auto loaded = cached_texture(builder.vfs, builder.cache, reference);
    if (loaded->texture)
    {
        index = static_cast<std::uint32_t>(builder.scene.textures.size());
        builder.scene.textures.push_back(*loaded->texture);
    }
    else
    {
        builder.scene.warnings.push_back(reference + ": " + loaded->error);
    }
    builder.texture_indices.emplace(key, index);
    return index;
}

// Translates a legacy material profile plus its authored parameters into the engine-native
// legacy-mode surface description (see docs/legacy-shader-mapping.md for the source behavior).
VisualMaterial make_visual_material(SceneBuilder& builder, const LegacyMaterial& material)
{
    VisualMaterial result;
    result.name = material.source_shader;
    result.blend = visual_blend(material.blend);
    result.two_sided = material.two_sided();
    result.alpha_test_threshold = material.render.alpha_test_threshold.value_or(0.5F);
    if (const auto* binding = base_color_texture(material))
    {
        result.base_color_texture = scene_texture(builder, binding->reference);
    }

    const auto& features = material.features;
    const bool unlit_family = material.family == LegacyMaterialFamily::sky || material.family == LegacyMaterialFamily::nebula || material.family == LegacyMaterialFamily::shield ||
                              material.family == LegacyMaterialFamily::solid_color || material.family == LegacyMaterialFamily::selection;
    if (features.lighting == LegacyLightingModel::unlit || features.lighting == LegacyLightingModel::sky_dome || unlit_family)
    {
        result.lighting = VisualLightingModel::unlit;
        // Unlit effects carry their color in "Color" (additive, shield) or "Diffuse".
        read_color(material, "Diffuse", result.diffuse);
        read_color(material, "Color", result.diffuse);
    }
    else
    {
        result.lighting = VisualLightingModel::lit;
        // Legacy material colors default to white specular when the parameter is absent.
        result.specular = {1.0F, 1.0F, 1.0F};
        read_color(material, "Diffuse", result.diffuse);
        read_color(material, "DiffuseColor", result.diffuse);
        read_color(material, "Specular", result.specular);
        read_color(material, "Emissive", result.emissive);
        const bool wants_normal_map = features.lighting == LegacyLightingModel::normal_mapped || features.lighting == LegacyLightingModel::normal_mapped_reflection ||
                                      features.lighting == LegacyLightingModel::planet_atmosphere;
        if (const auto* normal = texture_binding(material, LegacyTextureSemantic::normal); normal != nullptr && wants_normal_map)
        {
            result.normal_texture = scene_texture(builder, normal->reference);
            if (result.normal_texture != invalid_visual_texture)
            {
                result.lighting = VisualLightingModel::normal_mapped;
            }
        }
        switch (features.gloss)
        {
        case LegacyGlossSource::base_alpha:
            result.gloss = VisualGlossSource::base_alpha;
            break;
        case LegacyGlossSource::normal_alpha:
            result.gloss = result.normal_texture != invalid_visual_texture ? VisualGlossSource::normal_alpha : VisualGlossSource::none;
            break;
        case LegacyGlossSource::gloss_texture_red:
            if (const auto* gloss = texture_binding(material, LegacyTextureSemantic::specular))
            {
                result.gloss_texture = scene_texture(builder, gloss->reference);
            }
            result.gloss = result.gloss_texture != invalid_visual_texture ? VisualGlossSource::gloss_red : VisualGlossSource::none;
            break;
        case LegacyGlossSource::none:
            break;
        }
        result.team_color_mask = features.colorize;
        result.emissive_mask = features.base_alpha_emissive;
    }

    if (features.uv_animation == LegacyUvAnimation::scroll)
    {
        read_color(material, "UVScrollRate", result.uv_scroll);
        // The nebula reference scrolls three times faster than its authored rate.
        if (material.family == LegacyMaterialFamily::nebula)
        {
            result.uv_scroll = {result.uv_scroll[0] * 3.0F, result.uv_scroll[1] * 3.0F};
        }
    }
    return result;
}

// Converts an environment's lighting block. Each light's direction comes from its azimuth (about
// +Z) and elevation (above the XY plane) and points toward the light; colors are scaled by their
// intensity. The highlight uses the specular color scaled by the key intensity. The direction
// convention and the specular scaling are inferences pending comparison with the original game.
VisualLightRig environment_light_rig(const TedEnvironment& environment)
{
    const auto light = [](const TedLight& source)
    {
        VisualDirectionalLight result;
        const auto horizontal = std::cos(source.elevation);
        result.direction = {horizontal * std::cos(source.azimuth), horizontal * std::sin(source.azimuth), std::sin(source.elevation)};
        result.color = {source.color[0] * source.intensity, source.color[1] * source.intensity, source.color[2] * source.intensity};
        return result;
    };
    VisualLightRig rig;
    rig.key = light(environment.lights[0]);
    rig.fills = {light(environment.lights[1]), light(environment.lights[2])};
    const auto key_intensity = environment.lights[0].intensity;
    rig.key_specular = {environment.specular[0] * key_intensity, environment.specular[1] * key_intensity, environment.specular[2] * key_intensity};
    rig.ambient = {environment.ambient[0], environment.ambient[1], environment.ambient[2]};
    return rig;
}

// Whether a light rig decoded from map data is usable (finite values, non-zero directions).
bool light_rig_valid(const VisualLightRig& rig) noexcept
{
    const auto valid_light = [&](const VisualDirectionalLight& light)
    {
        const auto& direction = light.direction;
        return is_finite(direction) && is_finite(light.color) && dot(direction, direction) > 1.0e-12F;
    };
    return valid_light(rig.key) && valid_light(rig.fills[0]) && valid_light(rig.fills[1]) && is_finite(rig.key_specular) && is_finite(rig.ambient);
}

// Fallback colors by player slot for types without a faction affiliation. Index 0 is neutral.
Color player_team_color(const std::uint32_t player) noexcept
{
    static constexpr std::array<Color, 8> colors{{
        {0.70F, 0.70F, 0.70F, 1.0F},
        {0.25F, 0.45F, 0.95F, 1.0F},
        {0.90F, 0.25F, 0.20F, 1.0F},
        {0.95F, 0.75F, 0.20F, 1.0F},
        {0.35F, 0.80F, 0.40F, 1.0F},
        {0.65F, 0.40F, 0.85F, 1.0F},
        {0.25F, 0.80F, 0.80F, 1.0F},
        {0.90F, 0.45F, 0.70F, 1.0F},
    }};
    return colors[player % colors.size()];
}

} // namespace eaw::scene_build

namespace eaw {

std::string ResourceLoader<scene_build::CachedTexture>::key(const std::string_view reference)
{
    return ascii_lower(reference);
}

scene_build::CachedTexture ResourceLoader<scene_build::CachedTexture>::load(const AssetReader& reader, const std::string_view reference_view)
{
    using namespace scene_build;
    const std::string reference(reference_view);

    CachedTexture result;
    const auto texture_path = resolve_texture(reader, reference);
    if (!texture_path)
    {
        result.error = "matching DDS not found";
        return result;
    }
    const auto bytes = reader.read(*texture_path);
    if (!bytes)
    {
        result.error = bytes.error().message;
        return result;
    }
    auto texture = make_visual_texture(reference, *bytes);
    if (!texture)
    {
        result.error = texture.error().message;
        return result;
    }
    result.texture = std::move(texture).value();
    result.texture->identity = next_texture_identity();
    return result;
}

} // namespace eaw
