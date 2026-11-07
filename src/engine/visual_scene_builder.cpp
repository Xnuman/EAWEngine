#include "visual_scene_builder.hpp"

#include "eaw/alo_model.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/xml_asset_database.hpp"
#include "eaw/virtual_file_system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace eaw {
namespace {

std::string fold_ascii(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

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

VisualTexture make_visual_texture(const std::string_view name, const std::span<const std::byte> bytes)
{
    const auto dds = DdsTexture::parse(bytes);
    if (dds.volume || dds.cube_map || dds.array_size != 1 || dds.faces != 1)
    {
        throw std::runtime_error("only ordinary 2D DDS textures are supported by the visual sandbox");
    }
    const auto format = visual_format(dds.format);
    if (!format)
    {
        throw std::runtime_error("DDS format is not supported by the D3D12 sandbox");
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
            throw std::runtime_error("DDS subresource extends beyond its file");
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
        throw std::runtime_error("DDS contains no usable 2D mip levels");
    }
    return result;
}

std::optional<std::string> resolve_texture(const VirtualFileSystem& vfs, const std::string_view reference)
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

const LegacyTextureBinding* base_color_texture(const LegacyMaterial& material)
{
    const auto found = std::ranges::find(material.textures, LegacyTextureSemantic::base_color, &LegacyTextureBinding::semantic);
    if (found != material.textures.end())
    {
        return &*found;
    }
    return material.textures.empty() ? nullptr : &material.textures.front();
}

AloVector3 transform_direction(const AloVector3& direction, const AloMatrix4& matrix)
{
    const auto origin = alo_transform_point({}, matrix);
    const auto endpoint = alo_transform_point(direction, matrix);
    AloVector3 result{endpoint.x - origin.x, endpoint.y - origin.y, endpoint.z - origin.z};
    const auto length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
    if (length > 1.0e-6F)
    {
        result.x /= length;
        result.y /= length;
        result.z /= length;
    }
    return result;
}

// Accumulates models into one scene with a shared, deduplicated texture table.
struct SceneBuilder
{
    const VirtualFileSystem& vfs;
    VisualScene& scene;
    std::unordered_map<std::string, std::uint32_t> texture_indices;
};

// Loads (once per scene) the DDS for an ALO texture reference and returns its scene texture
// index, or invalid_visual_texture with a warning when it is missing or unsupported.
std::uint32_t scene_texture(SceneBuilder& builder, const std::string& reference)
{
    const auto key = fold_ascii(reference);
    if (const auto existing = builder.texture_indices.find(key); existing != builder.texture_indices.end())
    {
        return existing->second;
    }
    auto index = invalid_visual_texture;
    if (const auto texture_path = resolve_texture(builder.vfs, reference))
    {
        try
        {
            index = static_cast<std::uint32_t>(builder.scene.textures.size());
            builder.scene.textures.push_back(make_visual_texture(reference, builder.vfs.read(*texture_path)));
        }
        catch (const std::exception& error)
        {
            index = invalid_visual_texture;
            builder.scene.warnings.push_back(reference + ": " + error.what());
        }
    }
    else
    {
        builder.scene.warnings.push_back(reference + ": matching DDS not found");
    }
    builder.texture_indices.emplace(key, index);
    return index;
}

// Exact numeric components of a named material parameter, or nullptr when absent or malformed.
const std::vector<float>* material_values(const LegacyMaterial& material, const std::string_view name)
{
    for (const auto& parameter : material.parameters)
    {
        if (!parameter.values.empty() && fold_ascii(parameter.name) == fold_ascii(std::string(name)))
        {
            return &parameter.values;
        }
    }
    return nullptr;
}

template <std::size_t Count> void read_color(const LegacyMaterial& material, const std::string_view name, std::array<float, Count>& target)
{
    if (const auto* values = material_values(material, name))
    {
        for (std::size_t index = 0; index < Count && index < values->size(); ++index)
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
    rig.ambient = environment.ambient;
    return rig;
}

// Fallback colors by player slot for types without a faction affiliation. Index 0 is neutral.
std::array<float, 4> player_team_color(const std::uint32_t player) noexcept
{
    static constexpr std::array<std::array<float, 4>, 8> colors{{
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

// Appends one ALO model as a new mesh (its draws, vertices, indices, and any new textures) and
// returns the mesh index. Vertices stay in bind-pose model space with model-wide bone indices.
std::uint32_t append_model(SceneBuilder& builder, const AloModel& model, const std::string_view name)
{
    auto& scene = builder.scene;
    const auto bind_pose = model.bind_pose();
    if (model.bones.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("ALO skeleton exceeds the visual scene bone index range");
    }
    VisualMesh visual_mesh;
    visual_mesh.name = name;
    visual_mesh.first_draw = static_cast<std::uint32_t>(scene.draws.size());
    visual_mesh.bone_count = static_cast<std::uint32_t>(model.bones.size());
    visual_mesh.bounds_minimum.fill(std::numeric_limits<float>::max());
    visual_mesh.bounds_maximum.fill(std::numeric_limits<float>::lowest());
    const auto first_vertex = scene.vertices.size();
    const auto first_scene_index = scene.indices.size();
    const auto first_material = scene.materials.size();
    static constexpr std::array<std::array<float, 4>, 6> palette{{
        {0.35F, 0.78F, 0.82F, 1.0F},
        {0.42F, 0.63F, 0.88F, 1.0F},
        {0.62F, 0.52F, 0.86F, 1.0F},
        {0.35F, 0.78F, 0.59F, 1.0F},
        {0.86F, 0.67F, 0.35F, 1.0F},
        {0.78F, 0.43F, 0.61F, 1.0F},
    }};

    for (std::size_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
    {
        const auto& mesh = model.meshes[mesh_index];
        if (mesh.hidden)
        {
            continue;
        }
        // Rigid meshes attached to a bone that is hidden in the bind pose are alternate states
        // (for example destroyed-structure debris) that animations or gameplay reveal later.
        // Visibility changes over time are not modeled yet, so they are omitted.
        if (mesh.attached_bone && *mesh.attached_bone < model.bones.size() && !model.bones[*mesh.attached_bone].visible)
        {
            continue;
        }
        for (const auto& submesh : mesh.submeshes)
        {
            if (submesh.vertices.empty() || submesh.indices.empty())
            {
                continue;
            }
            const auto material = LegacyMaterial::translate(submesh);
            if (material.family == LegacyMaterialFamily::collision || material.family == LegacyMaterialFamily::shadow)
            {
                continue;
            }
            // Heat distortion and occluded-unit overlays target dedicated passes (a distortion buffer
            // and a depth-greater silhouette) that the sandbox does not implement yet. Drawing them
            // into the color pass would be wrong, so they are skipped with a diagnostic.
            if (material.family == LegacyMaterialFamily::heat || material.family == LegacyMaterialFamily::occluded)
            {
                scene.warnings.push_back(mesh.name + ": skipped " + material.source_shader + " (" + std::string(to_string(material.family)) + " pass is not implemented)");
                continue;
            }

            auto visual_material = make_visual_material(builder, material);
            if (visual_material.base_color_texture == invalid_visual_texture)
            {
                visual_material.tint = palette[mesh_index % palette.size()];
            }

            if (scene.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw std::runtime_error("visual scene has too many vertices for a D3D12 base-vertex draw");
            }
            const auto base_vertex = static_cast<std::int32_t>(scene.vertices.size());
            const AloMatrix4* attachment = nullptr;
            if (submesh.skin_bones.empty() && mesh.attached_bone && *mesh.attached_bone < bind_pose.size())
            {
                attachment = &bind_pose[*mesh.attached_bone];
            }
            for (const auto& vertex : submesh.vertices)
            {
                const auto position = attachment != nullptr ? alo_transform_point(vertex.position, *attachment) : vertex.position;
                const auto normal = attachment != nullptr ? transform_direction(vertex.normal, *attachment) : vertex.normal;
                const auto tangent = attachment != nullptr ? transform_direction(vertex.tangent, *attachment) : vertex.tangent;
                const auto binormal = attachment != nullptr ? transform_direction(vertex.binormal, *attachment) : vertex.binormal;
                std::array<std::uint32_t, 4> bone_indices{};
                std::array<float, 4> bone_weights{};
                if (!submesh.skin_bones.empty())
                {
                    for (std::size_t influence = 0; influence < bone_indices.size(); ++influence)
                    {
                        const auto palette_index = vertex.bone_indices[influence];
                        if (vertex.bone_weights[influence] <= 0.0F || palette_index >= submesh.skin_bones.size())
                        {
                            continue;
                        }
                        const auto bone_index = submesh.skin_bones[palette_index];
                        if (bone_index >= model.bones.size())
                        {
                            continue;
                        }
                        bone_indices[influence] = bone_index;
                        bone_weights[influence] = vertex.bone_weights[influence];
                    }
                }
                else if (mesh.attached_bone && *mesh.attached_bone < model.bones.size())
                {
                    bone_indices[0] = *mesh.attached_bone;
                    bone_weights[0] = 1.0F;
                }
                scene.vertices.push_back({{position.x, position.y, position.z},
                                          {normal.x, normal.y, normal.z},
                                          {vertex.texture_coordinates[0].x, vertex.texture_coordinates[0].y},
                                          bone_indices,
                                          bone_weights,
                                          {tangent.x, tangent.y, tangent.z},
                                          {binormal.x, binormal.y, binormal.z}});
                visual_mesh.bounds_minimum[0] = std::min(visual_mesh.bounds_minimum[0], position.x);
                visual_mesh.bounds_minimum[1] = std::min(visual_mesh.bounds_minimum[1], position.y);
                visual_mesh.bounds_minimum[2] = std::min(visual_mesh.bounds_minimum[2], position.z);
                visual_mesh.bounds_maximum[0] = std::max(visual_mesh.bounds_maximum[0], position.x);
                visual_mesh.bounds_maximum[1] = std::max(visual_mesh.bounds_maximum[1], position.y);
                visual_mesh.bounds_maximum[2] = std::max(visual_mesh.bounds_maximum[2], position.z);
            }

            const auto first_index = static_cast<std::uint32_t>(scene.indices.size());
            for (std::size_t index = 0; index + 2 < submesh.indices.size(); index += 3)
            {
                const auto a = submesh.indices[index];
                const auto b = submesh.indices[index + 1];
                const auto c = submesh.indices[index + 2];
                if (a >= submesh.vertices.size() || b >= submesh.vertices.size() || c >= submesh.vertices.size())
                {
                    continue;
                }
                scene.indices.push_back(a);
                scene.indices.push_back(b);
                scene.indices.push_back(c);
            }
            const auto index_count = static_cast<std::uint32_t>(scene.indices.size()) - first_index;
            if (index_count == 0)
            {
                scene.vertices.resize(static_cast<std::size_t>(base_vertex));
                continue;
            }
            const auto material_index = static_cast<std::uint32_t>(scene.materials.size());
            scene.materials.push_back(std::move(visual_material));
            scene.draws.push_back({first_index, index_count, base_vertex, material_index});
        }
    }

    visual_mesh.draw_count = static_cast<std::uint32_t>(scene.draws.size()) - visual_mesh.first_draw;
    if (visual_mesh.draw_count == 0)
    {
        // Roll back so a model without visible geometry leaves no partial data behind.
        scene.vertices.resize(first_vertex);
        scene.indices.resize(first_scene_index);
        scene.materials.resize(first_material);
        throw std::runtime_error("ALO model has no visible renderable geometry");
    }
    const auto mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(visual_mesh));
    return mesh_index;
}

// Terrain scale established from the stock corpus: vertices are 20 world units apart (column ->
// +X, row -> +Y) and stored elevation is 20x the world height, which places map objects within a
// few world units of the interpolated ground.
constexpr float terrain_sample_spacing = 20.0F;
constexpr float terrain_elevation_scale = 20.0F;
// Explicit preview limit (not a format limit): larger grids are skipped with a diagnostic until
// terrain streaming exists.
constexpr std::uint32_t maximum_terrain_dimension = 4096;

// Builds the land heightfield as independent triangles whose vertices carry the triangle's corner
// layers. Each TED layer becomes a material; its texture tiles world X/Y once per tile_size.
void append_terrain(SceneBuilder& builder, const TedMap& map)
{
    auto& scene = builder.scene;
    const auto width = map.terrain_width;
    const auto height = map.terrain_height;
    const auto samples = static_cast<std::size_t>(width) * height;
    if (width < 2 || height < 2 || map.elevation_map.size() < samples || map.terrain_layer_map.size() < samples || map.terrain_layers.empty())
    {
        return;
    }
    if (width > maximum_terrain_dimension || height > maximum_terrain_dimension)
    {
        scene.warnings.push_back("terrain " + std::to_string(width) + "x" + std::to_string(height) + " exceeds the preview limit of " + std::to_string(maximum_terrain_dimension) +
                                 " samples per side and is not drawn");
        return;
    }

    auto& terrain = scene.terrain;
    terrain.first_material = static_cast<std::uint32_t>(scene.materials.size());
    terrain.layer_count = static_cast<std::uint32_t>(map.terrain_layers.size());
    for (std::size_t index = 0; index < map.terrain_layers.size(); ++index)
    {
        const auto& layer = map.terrain_layers[index];
        VisualMaterial material;
        material.name = "terrain layer " + std::to_string(index) + (layer.color_texture.empty() ? std::string{} : " (" + layer.color_texture + ")");
        material.lighting = VisualLightingModel::lit;
        // Terrain highlights are modest; the reference terrain-mesh shader masks them by base alpha.
        material.specular = {0.25F, 0.25F, 0.25F};
        material.uv_scale = 1.0F / (layer.tile_size > 0.0F ? layer.tile_size : 64.0F);
        if (!layer.color_texture.empty())
        {
            material.base_color_texture = scene_texture(builder, layer.color_texture);
        }
        if (!layer.normal_texture.empty())
        {
            material.normal_texture = scene_texture(builder, layer.normal_texture);
            if (material.normal_texture != invalid_visual_texture)
            {
                material.lighting = VisualLightingModel::normal_mapped;
            }
        }
        scene.materials.push_back(std::move(material));
    }

    const auto elevation = [&](const std::uint32_t x, const std::uint32_t y)
    {
        return static_cast<float>(map.elevation_map[static_cast<std::size_t>(y) * width + x]) / terrain_elevation_scale;
    };
    std::size_t invalid_layers = 0;
    const auto layer_at = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const std::uint32_t layer = map.terrain_layer_map[static_cast<std::size_t>(y) * width + x];
        if (layer >= terrain.layer_count)
        {
            ++invalid_layers;
            return 0U;
        }
        return layer;
    };
    const auto vertex_position = [&](const std::uint32_t x, const std::uint32_t y)
    {
        return std::array<float, 3>{static_cast<float>(x) * terrain_sample_spacing, static_cast<float>(y) * terrain_sample_spacing, elevation(x, y)};
    };
    const auto vertex_normal = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto left = elevation(x == 0 ? 0 : x - 1, y);
        const auto right = elevation(std::min(x + 1, width - 1), y);
        const auto down = elevation(x, y == 0 ? 0 : y - 1);
        const auto up = elevation(x, std::min(y + 1, height - 1));
        const auto span_x = static_cast<float>(std::min(x + 1, width - 1) - (x == 0 ? 0 : x - 1)) * terrain_sample_spacing;
        const auto span_y = static_cast<float>(std::min(y + 1, height - 1) - (y == 0 ? 0 : y - 1)) * terrain_sample_spacing;
        const AloVector3 normal{-(right - left) / span_x, -(up - down) / span_y, 1.0F};
        const auto length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        return std::array<float, 3>{normal.x / length, normal.y / length, normal.z / length};
    };

    terrain.vertices.reserve(static_cast<std::size_t>(width - 1) * (height - 1) * 6U);
    terrain.bounds_minimum.fill(std::numeric_limits<float>::max());
    terrain.bounds_maximum.fill(std::numeric_limits<float>::lowest());
    const auto emit_triangle = [&](const std::array<std::array<std::uint32_t, 2>, 3>& corners)
    {
        const std::array<std::uint32_t, 3> layers{layer_at(corners[0][0], corners[0][1]), layer_at(corners[1][0], corners[1][1]), layer_at(corners[2][0], corners[2][1])};
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            VisualTerrainVertex vertex;
            vertex.position = vertex_position(corners[corner][0], corners[corner][1]);
            vertex.normal = vertex_normal(corners[corner][0], corners[corner][1]);
            vertex.layers = layers;
            vertex.barycentric[corner] = 1.0F;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                terrain.bounds_minimum[axis] = std::min(terrain.bounds_minimum[axis], vertex.position[axis]);
                terrain.bounds_maximum[axis] = std::max(terrain.bounds_maximum[axis], vertex.position[axis]);
            }
            terrain.vertices.push_back(vertex);
        }
    };
    for (std::uint32_t y = 0; y + 1 < height; ++y)
    {
        for (std::uint32_t x = 0; x + 1 < width; ++x)
        {
            emit_triangle({{{x, y}, {x + 1, y}, {x + 1, y + 1}}});
            emit_triangle({{{x, y}, {x + 1, y + 1}, {x, y + 1}}});
        }
    }
    if (invalid_layers != 0)
    {
        scene.warnings.push_back(std::to_string(invalid_layers) + " terrain vertex references to layers outside the map's " + std::to_string(terrain.layer_count) + " layers were drawn with layer 0");
    }
}

// Expands the scene bounds by every instance's transformed mesh bounds and the terrain.
void update_scene_bounds(VisualScene& scene)
{
    scene.bounds_minimum.fill(std::numeric_limits<float>::max());
    scene.bounds_maximum.fill(std::numeric_limits<float>::lowest());
    if (!scene.terrain.vertices.empty())
    {
        scene.bounds_minimum = scene.terrain.bounds_minimum;
        scene.bounds_maximum = scene.terrain.bounds_maximum;
    }
    for (const auto& instance : scene.instances)
    {
        // Backgrounds follow the camera, so they do not contribute to the framed world.
        if (instance.background)
        {
            continue;
        }
        const auto& mesh = scene.meshes[instance.mesh];
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            const AloVector3 local{(corner & 1U) != 0 ? mesh.bounds_maximum[0] : mesh.bounds_minimum[0], (corner & 2U) != 0 ? mesh.bounds_maximum[1] : mesh.bounds_minimum[1],
                                   (corner & 4U) != 0 ? mesh.bounds_maximum[2] : mesh.bounds_minimum[2]};
            AloMatrix4 transform;
            transform.values = instance.transform;
            const auto world = alo_transform_point(local, transform);
            const std::array<float, 3> point{world.x, world.y, world.z};
            for (std::size_t axis = 0; axis < point.size(); ++axis)
            {
                scene.bounds_minimum[axis] = std::min(scene.bounds_minimum[axis], point[axis]);
                scene.bounds_maximum[axis] = std::max(scene.bounds_maximum[axis], point[axis]);
            }
        }
    }
}

} // namespace

VisualModelAsset build_visual_model(const VirtualFileSystem& vfs, const std::string_view model_path)
{
    const auto resolved = vfs.resolve(model_path);
    if (!resolved)
    {
        throw std::runtime_error("visual model was not found: " + std::string(model_path));
    }
    VisualModelAsset asset;
    asset.model = AloModel::parse(vfs.read(resolved->virtual_path));
    for (const auto& transform : asset.model.bind_pose())
    {
        asset.inverse_bind_pose.push_back(alo_inverse(transform));
    }
    auto& scene = asset.scene;
    scene.name = resolved->virtual_path;
    scene.warnings = asset.model.warnings;
    SceneBuilder builder{vfs, scene, {}};
    const auto mesh = append_model(builder, asset.model, resolved->virtual_path);
    VisualInstance instance{mesh};
    instance.team_color = player_team_color(1);
    scene.instances.push_back(instance);
    scene.bone_count = scene.meshes[mesh].bone_count;
    update_scene_bounds(scene);
    return asset;
}

VisualMapAsset build_visual_map(const VirtualFileSystem& vfs, const std::string_view map_path)
{
    const auto resolved = vfs.resolve(map_path);
    if (!resolved)
    {
        throw std::runtime_error("map was not found: " + std::string(map_path));
    }
    VisualMapAsset asset;
    const auto map = TedMap::parse(vfs.read(resolved->virtual_path));
    asset.land = map.type == TedMapType::land;
    const auto database = XmlAssetDatabase::build(vfs);
    const auto catalog = GameObjectCatalog::build(database);
    const auto layout = build_map_object_layout(map, catalog, vfs);
    asset.untyped = layout.untyped;
    asset.without_model = layout.without_model;
    asset.missing_model_files = layout.missing_model_files;
    asset.expanded_members = layout.expanded_members;
    asset.unexpanded_containers = layout.unexpanded_containers;

    auto& scene = asset.scene;
    scene.name = resolved->virtual_path;
    SceneBuilder builder{vfs, scene, {}};
    if (asset.land)
    {
        append_terrain(builder, map);
    }
    std::unordered_map<std::string, std::optional<std::uint32_t>> meshes;
    for (const auto& placement : layout.placements)
    {
        if (placement.model_path.empty())
        {
            continue;
        }
        auto [entry, inserted] = meshes.try_emplace(placement.model_path);
        if (inserted)
        {
            try
            {
                entry->second = append_model(builder, AloModel::parse(vfs.read(placement.model_path)), placement.model_path);
            }
            catch (const std::exception& error)
            {
                // Particle ALOs and malformed models are reported once and skipped.
                scene.warnings.push_back(placement.model_path + ": " + error.what());
            }
        }
        if (!entry->second)
        {
            ++asset.unrenderable_placements;
            continue;
        }
        // Every instance shares the identity (bind-pose) palette; per-instance animation is a
        // simulation-driven feature for later milestones.
        VisualInstance instance{*entry->second, placement.transform.values};
        if (const auto faction = placement.type != nullptr ? catalog.affiliation_color(*placement.type) : std::nullopt)
        {
            instance.team_color = *faction;
            ++asset.faction_colored;
        }
        else
        {
            instance.team_color = player_team_color(map.placed_objects[placement.object_index].player);
            ++asset.player_colored;
        }
        scene.instances.push_back(instance);
    }

    // Backdrop: the active environment names primary and secondary sky domes. Each name is an XML
    // object (SpacePrimarySkydome, LandPrimarySkydome, ...) whose model is drawn as a camera-
    // centered background layer, primary first. The stored index can be out of range in
    // malformed data; the first environment is used then.
    if (!map.environments.empty())
    {
        auto environment_index = static_cast<std::size_t>(map.active_environment);
        if (environment_index >= map.environments.size())
        {
            scene.warnings.push_back("active environment " + std::to_string(map.active_environment) + " is out of range; using environment 0");
            environment_index = 0;
        }
        const auto& environment = map.environments[environment_index];
        asset.environment_name = environment.name;
        if (environment.has_lighting)
        {
            asset.lighting = environment_light_rig(environment);
        }
        const auto context = asset.land ? GameModelContext::land : GameModelContext::space;
        for (const auto& name : {environment.skybox_primary, environment.skybox_secondary})
        {
            if (name.empty())
            {
                continue;
            }
            const XmlDefinition* definition = nullptr;
            std::optional<GameObjectModelReference> model;
            for (const auto* candidate : catalog.find(name))
            {
                if ((model = catalog.model(*candidate, context)))
                {
                    definition = candidate;
                    break;
                }
            }
            const auto path = model ? resolve_model_path(vfs, model->value) : std::nullopt;
            if (definition == nullptr || !path)
            {
                scene.warnings.push_back("backdrop '" + name + "' has no resolvable " + std::string(to_string(context)) + " model");
                continue;
            }
            auto [entry, inserted] = meshes.try_emplace(*path);
            if (inserted)
            {
                try
                {
                    entry->second = append_model(builder, AloModel::parse(vfs.read(*path)), *path);
                }
                catch (const std::exception& error)
                {
                    scene.warnings.push_back(*path + ": " + error.what());
                }
            }
            if (!entry->second)
            {
                continue;
            }
            VisualInstance backdrop{*entry->second};
            backdrop.background = true;
            scene.instances.push_back(backdrop);
            asset.backdrops.push_back(definition->name + " (" + *path + ")");
        }
    }

    for (const auto& mesh : scene.meshes)
    {
        scene.bone_count = std::max(scene.bone_count, mesh.bone_count);
    }
    scene.bone_count = std::max(scene.bone_count, 1U);
    constexpr std::size_t maximum_reported_diagnostics = 50;
    for (std::size_t index = 0; index < layout.diagnostics.size() && index < maximum_reported_diagnostics; ++index)
    {
        scene.warnings.push_back(layout.diagnostics[index]);
    }
    if (layout.diagnostics.size() > maximum_reported_diagnostics)
    {
        scene.warnings.push_back(std::to_string(layout.diagnostics.size() - maximum_reported_diagnostics) + " more object diagnostics omitted");
    }
    if (scene.terrain.vertices.empty() && std::ranges::none_of(scene.instances,
                                                               [](const VisualInstance& instance)
                                                               {
                                                                   return !instance.background;
                                                               }))
    {
        throw std::runtime_error("map has no placed objects with renderable models: " + resolved->virtual_path);
    }
    update_scene_bounds(scene);
    return asset;
}

std::optional<std::string> default_model_animation(const VirtualFileSystem& vfs, const std::string_view model_path)
{
    const auto path = std::filesystem::path(model_path);
    const auto candidate = path.parent_path() / (path.stem().string() + "_IDLE_00.ALA");
    if (const auto resolved = vfs.resolve(candidate.generic_string()))
    {
        return resolved->virtual_path;
    }
    return std::nullopt;
}

std::vector<std::array<float, 16>> sample_skin_palette(const VisualModelAsset& asset, const AlaAnimation* animation, const float seconds)
{
    std::vector<AlaPoseBone> pose;
    if (animation != nullptr)
    {
        pose = animation->sample_pose(asset.model, seconds);
    }
    else
    {
        const auto bind_pose = asset.model.bind_pose();
        pose.reserve(bind_pose.size());
        for (std::size_t index = 0; index < bind_pose.size(); ++index)
        {
            pose.push_back({bind_pose[index], asset.model.bones[index].visible});
        }
    }

    const auto bone_count = std::min(pose.size(), asset.inverse_bind_pose.size());
    std::vector<std::array<float, 16>> result;
    result.reserve(std::max<std::size_t>(bone_count, 1U));
    for (std::size_t index = 0; index < bone_count; ++index)
    {
        result.push_back(alo_multiply(asset.inverse_bind_pose[index], pose[index].transform).values);
    }
    if (result.empty())
    {
        result.push_back(alo_identity_matrix().values);
    }
    return result;
}

} // namespace eaw
