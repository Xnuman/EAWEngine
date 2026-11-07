#include "visual_scene_builder.hpp"

#include "eaw/alo_model.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/legacy_material.hpp"
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
    const auto& model = asset.model;
    const auto bind_pose = model.bind_pose();
    asset.inverse_bind_pose.reserve(bind_pose.size());
    for (const auto& transform : bind_pose)
    {
        asset.inverse_bind_pose.push_back(alo_inverse(transform));
    }

    auto& scene = asset.scene;
    scene.name = resolved->virtual_path;
    if (model.bones.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("ALO skeleton exceeds the visual scene bone index range");
    }
    scene.bone_count = static_cast<std::uint32_t>(model.bones.size());
    scene.warnings = model.warnings;
    scene.bounds_minimum.fill(std::numeric_limits<float>::max());
    scene.bounds_maximum.fill(std::numeric_limits<float>::lowest());
    std::unordered_map<std::string, std::uint32_t> texture_indices;
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

            VisualMaterial visual_material;
            visual_material.name = material.source_shader;
            visual_material.tint = palette[mesh_index % palette.size()];
            visual_material.blend = visual_blend(material.blend);
            visual_material.two_sided = material.two_sided;
            if (const auto* binding = base_color_texture(material))
            {
                const auto key = fold_ascii(binding->reference);
                const auto existing = texture_indices.find(key);
                if (existing != texture_indices.end())
                {
                    visual_material.base_color_texture = existing->second;
                    visual_material.tint = {1.0F, 1.0F, 1.0F, 1.0F};
                }
                else if (const auto texture_path = resolve_texture(vfs, binding->reference))
                {
                    try
                    {
                        const auto texture_index = static_cast<std::uint32_t>(scene.textures.size());
                        scene.textures.push_back(make_visual_texture(binding->reference, vfs.read(*texture_path)));
                        texture_indices.emplace(key, texture_index);
                        visual_material.base_color_texture = texture_index;
                        visual_material.tint = {1.0F, 1.0F, 1.0F, 1.0F};
                    }
                    catch (const std::exception& error)
                    {
                        scene.warnings.push_back(binding->reference + ": " + error.what());
                    }
                }
                else
                {
                    scene.warnings.push_back(binding->reference + ": matching DDS not found");
                }
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
                scene.vertices.push_back(
                    {{position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {vertex.texture_coordinates[0].x, vertex.texture_coordinates[0].y}, bone_indices, bone_weights});
                scene.bounds_minimum[0] = std::min(scene.bounds_minimum[0], position.x);
                scene.bounds_minimum[1] = std::min(scene.bounds_minimum[1], position.y);
                scene.bounds_minimum[2] = std::min(scene.bounds_minimum[2], position.z);
                scene.bounds_maximum[0] = std::max(scene.bounds_maximum[0], position.x);
                scene.bounds_maximum[1] = std::max(scene.bounds_maximum[1], position.y);
                scene.bounds_maximum[2] = std::max(scene.bounds_maximum[2], position.z);
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

    if (scene.vertices.empty() || scene.indices.empty() || scene.draws.empty())
    {
        throw std::runtime_error("ALO model has no visible renderable geometry");
    }
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
