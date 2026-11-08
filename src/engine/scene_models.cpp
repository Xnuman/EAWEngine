#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/xml_document.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <ranges>
#include <span>
#include <system_error>

namespace eaw::scene_build {

namespace {

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

Result<AloModel> load_model(const AssetReader& reader, const std::string& path)
{
    EAW_TRY_ASSIGN(const auto bytes, reader.read(path));
    return AloModel::parse(bytes);
}

} // namespace

// The decoded model, or why it could not be decoded (cached either way: a failed decode depends on
// the file, so replacing it invalidates the entry).
std::shared_ptr<const Result<AloModel>> cached_model(SceneBuilder& builder, const std::string& path)
{
    return cached<Result<AloModel>>(builder.cache, "alo", path, builder.vfs,
                                    [&](const AssetReader& reader)
                                    {
                                        return load_model(reader, path);
                                    });
}

// Appends one ALO model as a new mesh (its draws, vertices, indices, and any new textures) and
// returns the mesh index. Vertices stay in bind-pose model space with model-wide bone indices.
// With `animated_visibility`, the mesh's palette has two halves: [0, N) the skin matrices and
// [N, 2N) the same matrices collapsed to a point while their bone is hidden. Rigid attachments
// reference the second half, so meshes on bones hidden in the bind pose are included and appear
// when an animation shows their bone. Without it (maps share one bind-pose palette), such meshes
// are omitted.
// A model that cannot be appended leaves no geometry, indices, or materials behind.
Result<std::uint32_t> append_model(SceneBuilder& builder, const AloModel& model, const std::string_view name, const bool animated_visibility)
{
    auto& scene = builder.scene;
    const auto bind_pose = model.bind_pose();
    if (model.bones.size() > std::numeric_limits<std::uint32_t>::max() / 2U)
    {
        return make_error("ALO skeleton exceeds the visual scene bone index range");
    }
    const auto bone_count = static_cast<std::uint32_t>(model.bones.size());
    VisualMesh visual_mesh;
    visual_mesh.name = name;
    visual_mesh.first_draw = static_cast<std::uint32_t>(scene.draws.size());
    visual_mesh.bone_count = animated_visibility ? bone_count * 2U : bone_count;
    visual_mesh.bounds_minimum.fill(std::numeric_limits<float>::max());
    visual_mesh.bounds_maximum.fill(std::numeric_limits<float>::lowest());
    const auto first_vertex = scene.vertices.size();
    const auto first_scene_index = scene.indices.size();
    const auto first_material = scene.materials.size();
    const auto roll_back = [&]
    {
        scene.vertices.resize(first_vertex);
        scene.indices.resize(first_scene_index);
        scene.materials.resize(first_material);
        scene.draws.resize(visual_mesh.first_draw);
    };
    static constexpr std::array<std::array<float, 4>, 6> palette{{
        {0.35F, 0.78F, 0.82F, 1.0F},
        {0.42F, 0.63F, 0.88F, 1.0F},
        {0.62F, 0.52F, 0.86F, 1.0F},
        {0.35F, 0.78F, 0.59F, 1.0F},
        {0.86F, 0.67F, 0.35F, 1.0F},
        {0.78F, 0.43F, 0.61F, 1.0F},
    }};

    const auto default_visible = alo_default_mesh_visibility(model, !animated_visibility);
    for (std::size_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
    {
        const auto& mesh = model.meshes[mesh_index];
        // Default appearance: intact state and full detail. Bone visibility is either animated
        // through the visibility palette or, for maps, fixed to the bind pose. Damage-state and
        // distance switching are not modeled yet.
        if (!default_visible[mesh_index])
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
                roll_back();
                return make_error("visual scene has too many vertices for a D3D12 base-vertex draw");
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
                    bone_indices[0] = animated_visibility ? bone_count + *mesh.attached_bone : *mesh.attached_bone;
                    bone_weights[0] = 1.0F;
                }
                // Authored vertex colors only drive materials whose legacy profile reads them; other
                // files leave the channel unused, so it stays white.
                std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
                if (material.features.vertex_color)
                {
                    const std::array<float, 4> authored{vertex.color.x, vertex.color.y, vertex.color.z, vertex.color.w};
                    if (std::ranges::all_of(authored,
                                            [](const float value)
                                            {
                                                return std::isfinite(value);
                                            }))
                    {
                        color = authored;
                    }
                }
                scene.vertices.push_back({{position.x, position.y, position.z},
                                          {normal.x, normal.y, normal.z},
                                          {vertex.texture_coordinates[0].x, vertex.texture_coordinates[0].y},
                                          bone_indices,
                                          bone_weights,
                                          {tangent.x, tangent.y, tangent.z},
                                          {binormal.x, binormal.y, binormal.z},
                                          color});
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
        roll_back();
        return make_error("ALO model has no visible renderable geometry");
    }
    const auto mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(visual_mesh));
    return mesh_index;
}

// Decodes and appends a model referenced by a map (placed object, backdrop, or spawn type). Particle
// ALOs and malformed models are reported once as scene warnings and yield nothing.
std::optional<std::uint32_t> append_scene_model(SceneBuilder& builder, const std::string& model_path)
{
    const auto model = cached_model(builder, model_path);
    if (!model->ok())
    {
        builder.scene.warnings.push_back(model_path + ": " + model->error().message);
        return std::nullopt;
    }
    auto mesh = append_model(builder, model->value(), model_path);
    if (!mesh)
    {
        builder.scene.warnings.push_back(model_path + ": " + mesh.error().message);
        return std::nullopt;
    }
    return *mesh;
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
            // Malformed vertex data must not leave the camera framing non-finite.
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2]))
            {
                continue;
            }
            for (std::size_t axis = 0; axis < point.size(); ++axis)
            {
                scene.bounds_minimum[axis] = std::min(scene.bounds_minimum[axis], point[axis]);
                scene.bounds_maximum[axis] = std::max(scene.bounds_maximum[axis], point[axis]);
            }
        }
    }
}

} // namespace eaw::scene_build

namespace eaw {

using namespace scene_build;

Result<VisualModelAsset> build_visual_model(const AssetReader& vfs, const std::string_view model_path, AssetCache* cache)
{
    const auto resolved = vfs.resolve(model_path);
    if (!resolved)
    {
        return make_error("visual model was not found: " + std::string(model_path));
    }
    VisualModelAsset asset;
    auto& scene = asset.scene;
    SceneBuilder builder{vfs, scene, {}, cache};
    const auto model = cached_model(builder, resolved->virtual_path);
    if (!model->ok())
    {
        return with_context(resolved->virtual_path, model->error());
    }
    asset.model = model->value();
    for (const auto& transform : asset.model.bind_pose())
    {
        auto inverse = alo_inverse(transform);
        if (!inverse)
        {
            return with_context(resolved->virtual_path, inverse.error());
        }
        asset.inverse_bind_pose.push_back(*inverse);
    }
    scene.name = resolved->virtual_path;
    scene.warnings = asset.model.warnings;
    asset.visibility_palette = true;
    const auto appended = append_model(builder, asset.model, resolved->virtual_path, true);
    if (!appended)
    {
        return with_context(resolved->virtual_path, appended.error());
    }
    const auto mesh = *appended;
    VisualInstance instance{mesh};
    instance.team_color = player_team_color(1);
    scene.instances.push_back(instance);
    scene.bone_count = scene.meshes[mesh].bone_count;
    update_scene_bounds(scene);
    return asset;
}

std::vector<std::string> model_animations(const AssetReader& reader, const std::string_view model_path)
{
    const auto path = std::filesystem::path(model_path);
    const auto prefix = fold_ascii(path.stem().string() + "_");
    std::vector<std::string> result;
    for (const auto& asset : reader.list(path.parent_path().generic_string()))
    {
        const auto file = std::filesystem::path(asset.virtual_path);
        if (fold_ascii(file.extension().string()) == fold_ascii(".ala") && fold_ascii(file.filename().string()).starts_with(prefix))
        {
            result.push_back(asset.virtual_path);
        }
    }
    std::ranges::sort(result);
    return result;
}

std::optional<std::string> default_model_animation(const AssetReader& vfs, const std::string_view model_path)
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
    result.reserve(std::max<std::size_t>(bone_count * 2U, 1U));
    for (std::size_t index = 0; index < bone_count; ++index)
    {
        result.push_back(alo_multiply(asset.inverse_bind_pose[index], pose[index].transform).values);
    }
    if (asset.visibility_palette)
    {
        // Hidden bones collapse their attachments to a point, which rasterizes nothing.
        std::array<float, 16> collapsed{};
        collapsed[15] = 1.0F;
        for (std::size_t index = 0; index < bone_count; ++index)
        {
            result.push_back(pose[index].visible ? result[index] : collapsed);
        }
    }
    if (result.empty())
    {
        result.push_back(alo_identity_matrix().values);
    }
    return result;
}

} // namespace eaw
