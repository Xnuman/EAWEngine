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

// Placeholder player colors until faction colors are read from XML. Index 0 is neutral.
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

// Expands the scene bounds by every instance's transformed mesh bounds.
void update_scene_bounds(VisualScene& scene)
{
    scene.bounds_minimum.fill(std::numeric_limits<float>::max());
    scene.bounds_maximum.fill(std::numeric_limits<float>::lowest());
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
        instance.team_color = player_team_color(map.placed_objects[placement.object_index].player);
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
    if (std::ranges::none_of(scene.instances,
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
