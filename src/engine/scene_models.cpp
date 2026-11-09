#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "eaw/alo_interpretation.hpp"
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
#include <memory>
#include <ranges>
#include <span>
#include <system_error>
#include <unordered_map>

namespace eaw::scene_build {

namespace {

// A unit normal, tangent, or binormal carried through a transform (left as is when it collapses).
Vector3 transform_normal(const Vector3& direction, const Matrix4& matrix)
{
    const auto origin = transform_point({}, matrix);
    const auto endpoint = transform_point(direction, matrix);
    Vector3 result{endpoint.x - origin.x, endpoint.y - origin.y, endpoint.z - origin.z};
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
Result<std::uint32_t> append_model(SceneBuilder& builder, const AloModel& model, const std::string_view name, const bool animated_visibility, const std::span<const ModelAttachment> attachments)
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
    visual_mesh.bounds_minimum = empty_bounds_minimum;
    visual_mesh.bounds_maximum = empty_bounds_maximum;
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

    // Appends `source`'s geometry. Attached models (host != null) are placed at the host bone's bind
    // pose and bound rigidly to that bone, in the host's palette.
    const auto append_geometry = [&](const AloModel& source, const std::vector<Matrix4>& source_bind, const Matrix4* host, const std::optional<std::uint32_t> host_bone) -> Status
    {
        const auto default_visible = alo_default_mesh_visibility(source, host != nullptr || !animated_visibility);
        for (std::size_t mesh_index = 0; mesh_index < source.meshes.size(); ++mesh_index)
        {
            const auto& mesh = source.meshes[mesh_index];
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
                    return make_error("visual scene has too many vertices for a D3D12 base-vertex draw");
                }
                const auto base_vertex = static_cast<std::int32_t>(scene.vertices.size());
                const Matrix4* attachment = nullptr;
                if (submesh.skin_bones.empty() && mesh.attached_bone && *mesh.attached_bone < source_bind.size())
                {
                    attachment = &source_bind[*mesh.attached_bone];
                }
                for (const auto& vertex : submesh.vertices)
                {
                    auto position = attachment != nullptr ? transform_point(vertex.position, *attachment) : vertex.position;
                    auto normal = attachment != nullptr ? transform_normal(vertex.normal, *attachment) : vertex.normal;
                    auto tangent = attachment != nullptr ? transform_normal(vertex.tangent, *attachment) : vertex.tangent;
                    auto binormal = attachment != nullptr ? transform_normal(vertex.binormal, *attachment) : vertex.binormal;
                    if (host != nullptr)
                    {
                        position = transform_point(position, *host);
                        normal = transform_normal(normal, *host);
                        tangent = transform_normal(tangent, *host);
                        binormal = transform_normal(binormal, *host);
                    }
                    std::array<std::uint32_t, 4> bone_indices{};
                    std::array<float, 4> bone_weights{};
                    if (host_bone)
                    {
                        // Rigid on the host bone, through the visibility half when there is one.
                        bone_indices[0] = animated_visibility ? bone_count + *host_bone : *host_bone;
                        bone_weights[0] = 1.0F;
                    }
                    else if (!submesh.skin_bones.empty())
                    {
                        for (std::size_t influence = 0; influence < bone_indices.size(); ++influence)
                        {
                            const auto palette_index = vertex.bone_indices[influence];
                            if (vertex.bone_weights[influence] <= 0.0F || palette_index >= submesh.skin_bones.size())
                            {
                                continue;
                            }
                            const auto bone_index = submesh.skin_bones[palette_index];
                            if (bone_index >= source.bones.size())
                            {
                                continue;
                            }
                            bone_indices[influence] = bone_index;
                            bone_weights[influence] = vertex.bone_weights[influence];
                        }
                    }
                    else if (mesh.attached_bone && *mesh.attached_bone < source.bones.size())
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
        return {};
    };

    if (auto status = append_geometry(model, bind_pose, nullptr, std::nullopt); !status)
    {
        roll_back();
        return status.error();
    }
    for (const auto& attached : attachments)
    {
        if (!attached.model || attached.bone >= bind_pose.size())
        {
            continue;
        }
        if (auto status = append_geometry(*attached.model, attached.model->bind_pose(), &bind_pose[attached.bone], attached.bone); !status)
        {
            roll_back();
            return status.error();
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

std::vector<ModelAttachment> type_hardpoint_models(SceneBuilder& builder, const GameObjectCatalog& catalog, const XmlDefinition& type, const AloModel* host)
{
    std::vector<ModelAttachment> attachments;
    const auto* list = catalog.field(type, "HardPoints").field;
    if (host == nullptr || list == nullptr || !builder.textures)
    {
        return attachments;
    }
    const auto text_of = [&](const XmlDefinition& definition, const std::string_view name)
    {
        const auto* node = catalog.field(definition, name).field;
        return node != nullptr ? std::string(trim_ascii(node->text)) : std::string{};
    };
    for (const auto name : split_type_names(list->text))
    {
        const XmlDefinition* hardpoint = nullptr;
        for (const auto* candidate : catalog.find(name))
        {
            if (fold_ascii(candidate->type) == "hardpoint" || hardpoint == nullptr)
            {
                hardpoint = candidate;
            }
        }
        if (hardpoint == nullptr)
        {
            continue;
        }
        const auto model_name = text_of(*hardpoint, "Model_To_Attach");
        if (model_name.empty())
        {
            continue;
        }
        const auto bone_name = fold_ascii(text_of(*hardpoint, "Attachment_Bone"));
        std::optional<std::uint32_t> bone;
        for (std::size_t index = 0; index < host->bones.size(); ++index)
        {
            if (fold_ascii(host->bones[index].name) == bone_name)
            {
                bone = static_cast<std::uint32_t>(index);
                break;
            }
        }
        const auto path = resolve_model_path(builder.vfs, model_name);
        if (!bone || !path)
        {
            builder.scene.warnings.push_back(type.name + ": hardpoint " + std::string(name) + " attaches " + model_name + (path ? " to missing bone '" + bone_name + "'" : " (file not found)"));
            continue;
        }
        const auto model = cached_model(builder, *path);
        if (!model->ok())
        {
            builder.scene.warnings.push_back(*path + ": " + model->error().message);
            continue;
        }
        attachments.push_back({*path, std::shared_ptr<const AloModel>(model, &model->value()), *bone});
    }
    return attachments;
}

std::string attached_mesh_key(const std::string& model_path, const std::span<const ModelAttachment> attachments)
{
    auto key = fold_ascii(model_path);
    for (const auto& attachment : attachments)
    {
        key += "|" + fold_ascii(attachment.path) + "@" + std::to_string(attachment.bone);
    }
    return key;
}

// Decodes and appends a model referenced by a map (placed object, backdrop, or spawn type). Particle
// ALOs and malformed models are reported once as scene warnings and yield nothing.
std::optional<std::uint32_t> append_scene_model(SceneBuilder& builder, const std::string& model_path, const std::span<const ModelAttachment> attachments)
{
    const auto model = cached_model(builder, model_path);
    if (!model->ok())
    {
        builder.scene.warnings.push_back(model_path + ": " + model->error().message);
        return std::nullopt;
    }
    // Map meshes carry the visibility half of the palette so units can animate (VisualRig).
    auto mesh = append_model(builder, model->value(), model_path, true, attachments);
    if (!mesh)
    {
        builder.scene.warnings.push_back(model_path + ": " + mesh.error().message);
        return std::nullopt;
    }
    return *mesh;
}

// Expands the scene bounds by every instance's transformed mesh bounds and the terrain.
float mesh_footprint_radius(const VisualScene& scene, const std::uint32_t mesh, const std::array<float, 16>& transform)
{
    if (mesh >= scene.meshes.size())
    {
        return 0.0F;
    }
    const auto& bounds = scene.meshes[mesh];
    const auto width = bounds.bounds_maximum[0] - bounds.bounds_minimum[0];
    const auto depth = bounds.bounds_maximum[1] - bounds.bounds_minimum[1];
    const auto scale = std::sqrt(transform[0] * transform[0] + transform[1] * transform[1] + transform[2] * transform[2]);
    const auto radius = 0.25F * (width + depth) * scale;
    return std::isfinite(radius) && radius > 0.0F ? radius : 0.0F;
}

GroundFootprint mesh_ground_footprint(const VisualScene& scene, const std::uint32_t mesh, const std::array<float, 16>& transform, const std::uint32_t passability_type)
{
    GroundFootprint footprint;
    footprint.passability = passability_type;
    if (mesh >= scene.meshes.size())
    {
        return footprint;
    }
    const auto& bounds = scene.meshes[mesh];
    const auto scale = std::sqrt(transform[0] * transform[0] + transform[1] * transform[1] + transform[2] * transform[2]);
    const auto local_x = (bounds.bounds_minimum[0] + bounds.bounds_maximum[0]) * 0.5F;
    const auto local_y = (bounds.bounds_minimum[1] + bounds.bounds_maximum[1]) * 0.5F;
    // Row-vector transform: world = local.x * row0 + local.y * row1 + translation.
    footprint.center = {local_x * transform[0] + local_y * transform[4] + transform[12], local_x * transform[1] + local_y * transform[5] + transform[13], transform[14]};
    footprint.half_length = std::max(0.0F, (bounds.bounds_maximum[0] - bounds.bounds_minimum[0]) * 0.5F * scale);
    footprint.half_width = std::max(0.0F, (bounds.bounds_maximum[1] - bounds.bounds_minimum[1]) * 0.5F * scale);
    footprint.heading = std::atan2(transform[1], transform[0]);
    if (!std::isfinite(footprint.center.x) || !std::isfinite(footprint.center.y) || !std::isfinite(footprint.half_length) || !std::isfinite(footprint.half_width) || !std::isfinite(footprint.heading))
    {
        footprint = GroundFootprint{};
        footprint.passability = passability_type;
    }
    return footprint;
}

void update_scene_bounds(VisualScene& scene)
{
    scene.bounds_minimum = empty_bounds_minimum;
    scene.bounds_maximum = empty_bounds_maximum;
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
            const Vector3 local{(corner & 1U) != 0 ? mesh.bounds_maximum[0] : mesh.bounds_minimum[0], (corner & 2U) != 0 ? mesh.bounds_maximum[1] : mesh.bounds_minimum[1],
                                (corner & 4U) != 0 ? mesh.bounds_maximum[2] : mesh.bounds_minimum[2]};
            const auto point = transform_point(local, Matrix4{instance.transform});
            // Malformed vertex data must not leave the camera framing non-finite.
            if (!is_finite(point))
            {
                continue;
            }
            for (std::size_t axis = 0; axis < 3; ++axis)
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

float model_forward_heading(const std::array<float, 16>& transform) noexcept
{
    // Row 1 is the model's +Y axis in world space; forward is its negation.
    return std::atan2(-transform[5], -transform[4]);
}

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
        const auto inverted = inverse(transform);
        if (!inverted)
        {
            return with_context(resolved->virtual_path, make_error("ALO bone transform is singular"));
        }
        asset.inverse_bind_pose.push_back(*inverted);
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

namespace {

bool intact_hidden(const std::vector<bool>& hidden, const std::size_t bone)
{
    return bone < hidden.size() && hidden[bone];
}

// Skin matrices of `pose` (and the visibility half) appended to `out`; `hidden` bones collapse too.
void append_pose_palette(const AloModel& model, const std::span<const Matrix4> inverse_bind_pose, const std::span<const AlaPoseBone> pose, const bool visibility_palette,
                         std::vector<std::array<float, 16>>& out, const std::vector<bool>& hidden = {})
{
    const auto bone_count = std::min({pose.size(), inverse_bind_pose.size(), model.bones.size()});
    const auto first = out.size();
    for (std::size_t index = 0; index < bone_count; ++index)
    {
        out.push_back((inverse_bind_pose[index] * pose[index].transform).values);
    }
    if (visibility_palette)
    {
        // Hidden bones collapse their attachments to a point, which rasterizes nothing.
        std::array<float, 16> collapsed{};
        collapsed[15] = 1.0F;
        for (std::size_t index = 0; index < bone_count; ++index)
        {
            out.push_back(pose[index].visible && !intact_hidden(hidden, index) ? out[first + index] : collapsed);
        }
    }
}

// A rig's rest palette: identity skin matrices, then the visibility half from the bind pose.
void append_rest_palette(const AloModel& model, std::vector<std::array<float, 16>>& out, const std::vector<bool>& hidden = {})
{
    const auto identity = Matrix4{}.values;
    std::array<float, 16> collapsed{};
    collapsed[15] = 1.0F;
    out.insert(out.end(), model.bones.size(), identity);
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        out.push_back(model.bones[index].visible && !intact_hidden(hidden, index) ? identity : collapsed);
    }
    if (model.bones.empty())
    {
        out.push_back(identity);
    }
}

} // namespace

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
    std::vector<std::array<float, 16>> result;
    result.reserve(std::max<std::size_t>(pose.size() * 2U, 1U));
    append_pose_palette(asset.model, asset.inverse_bind_pose, pose, asset.visibility_palette, result);
    if (result.empty())
    {
        result.push_back(Matrix4{}.values);
    }
    return result;
}

void append_rig_palette(const VisualRig& rig, const AlaAnimation& animation, const float seconds, std::vector<std::array<float, 16>>& out)
{
    if (rig.model && rig.inverse_bind_pose.size() == rig.model->bones.size() && !rig.model->bones.empty())
    {
        append_rig_local_palette(rig, animation.sample_local_pose(*rig.model, seconds), out);
        return;
    }
    append_rig_local_palette(rig, {}, out);
}

void append_rig_local_palette(const VisualRig& rig, const std::span<const AlaLocalBone> local_pose, std::vector<std::array<float, 16>>& out)
{
    const auto first = out.size();
    if (rig.model && rig.inverse_bind_pose.size() == rig.model->bones.size() && !rig.model->bones.empty() && local_pose.size() == rig.model->bones.size())
    {
        const auto pose = ala_compose_pose(*rig.model, local_pose);
        append_pose_palette(*rig.model, rig.inverse_bind_pose, pose, true, out, rig.intact_hidden);
    }
    if (out.size() - first != rig.palette_size)
    {
        // A pose that does not cover the skeleton, or no rig: the rest pose keeps the mesh intact.
        out.resize(first);
        if (rig.model)
        {
            append_rest_palette(*rig.model, out, rig.intact_hidden);
        }
        out.resize(first + rig.palette_size, Matrix4{}.values);
    }
}

std::optional<ClipName> parse_clip_name(const std::string_view file_stem, const std::string_view model_stem)
{
    const auto file = fold_ascii(std::string(file_stem));
    const auto prefix = fold_ascii(std::string(model_stem)) + "_";
    if (!file.starts_with(prefix))
    {
        return std::nullopt;
    }
    const auto rest = std::string_view(file).substr(prefix.size());
    const auto underscore = rest.rfind('_');
    if (underscore == std::string_view::npos || underscore + 1U >= rest.size())
    {
        return std::nullopt;
    }
    const auto digits = rest.substr(underscore + 1U);
    std::uint32_t variant{};
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), variant);
    if (error != std::errc{} || end != digits.data() + digits.size())
    {
        return std::nullopt;
    }
    static constexpr std::array<std::pair<std::string_view, AnimationRole>, 6> roles{{
        {"idle", AnimationRole::idle},
        {"move", AnimationRole::move},
        {"walkmove", AnimationRole::walk_move},
        {"attack", AnimationRole::attack},
        {"attackidle", AnimationRole::attack_idle},
        {"die", AnimationRole::die},
    }};
    const auto role = rest.substr(0, underscore);
    for (const auto& [name, value] : roles)
    {
        if (role == name)
        {
            return ClipName{value, variant};
        }
    }
    return std::nullopt;
}

void scene_build::build_map_rigs(SceneBuilder& builder, std::vector<VisualRig>& rigs, std::vector<std::array<float, 16>>& rest_palette)
{
    auto& scene = builder.scene;
    rigs.assign(scene.meshes.size(), {});
    rest_palette.clear();
    // Clip files by folded directory, listed once each.
    std::unordered_map<std::string, std::vector<std::string>> listings;
    for (std::size_t mesh_index = 0; mesh_index < scene.meshes.size(); ++mesh_index)
    {
        auto& mesh = scene.meshes[mesh_index];
        auto& rig = rigs[mesh_index];
        rig.rest_offset = static_cast<std::uint32_t>(rest_palette.size());
        const auto model = cached_model(builder, mesh.name);
        if (model->ok())
        {
            rig.model = std::shared_ptr<const AloModel>(model, &model->value());
            // Scorch marks of hardpoints (their Damage_Decal bones and everything below) wait for
            // the hardpoint's destruction.
            if (const auto decals = builder.decal_bones.find(fold_ascii(mesh.name)); decals != builder.decal_bones.end())
            {
                const auto& bones = rig.model->bones;
                rig.intact_hidden.assign(bones.size(), false);
                for (std::size_t index = 0; index < bones.size(); ++index)
                {
                    // The bone itself or any ancestor named by a Damage_Decal hides it.
                    auto bone = static_cast<std::int32_t>(index);
                    for (std::size_t depth = 0; bone >= 0 && depth <= bones.size(); ++depth)
                    {
                        if (decals->second.contains(static_cast<std::uint32_t>(bone)))
                        {
                            rig.intact_hidden[index] = true;
                            break;
                        }
                        bone = static_cast<std::size_t>(bone) < bones.size() ? bones[static_cast<std::size_t>(bone)].parent : -1;
                    }
                }
            }
            append_rest_palette(*rig.model, rest_palette, rig.intact_hidden);
            rig.effects = proxy_effects(builder, *rig.model, mesh.name);
            for (const auto& transform : rig.model->bind_pose())
            {
                const auto inverted = inverse(transform);
                if (!inverted)
                {
                    scene.warnings.push_back(mesh.name + ": bind pose cannot be inverted, so the model is not animated: ALO bone transform is singular");
                    rig.inverse_bind_pose.clear();
                    break;
                }
                rig.inverse_bind_pose.push_back(*inverted);
            }
        }
        // Every mesh owns at least its own palette range.
        const auto used = static_cast<std::uint32_t>(rest_palette.size()) - rig.rest_offset;
        if (used < std::max(mesh.bone_count, 1U))
        {
            rest_palette.resize(rig.rest_offset + std::max(mesh.bone_count, 1U), Matrix4{}.values);
        }
        rig.palette_size = static_cast<std::uint32_t>(rest_palette.size()) - rig.rest_offset;
        if (!rig.model || rig.inverse_bind_pose.empty() || !builder.animation_clips)
        {
            continue;
        }
        // Clips come from the type's animation source when it names one, else from the model.
        auto path = std::filesystem::path(mesh.name);
        bool retarget = false;
        if (const auto source = builder.animation_sources.find(fold_ascii(mesh.name)); source != builder.animation_sources.end())
        {
            const auto resolved = resolve_model_path(builder.vfs, source->second);
            const auto source_path = resolved ? std::filesystem::path(*resolved) : path.parent_path() / std::filesystem::path(source->second).filename();
            retarget = fold_ascii(source_path.stem().string()) != fold_ascii(path.stem().string());
            path = source_path;
        }
        const auto directory = path.parent_path().generic_string();
        auto [listing, inserted] = listings.try_emplace(fold_ascii(directory));
        if (inserted)
        {
            for (const auto& asset : builder.vfs.list(directory))
            {
                if (fold_ascii(std::filesystem::path(asset.virtual_path).extension().string()) == ".ala")
                {
                    listing->second.push_back(asset.virtual_path);
                }
            }
            std::ranges::sort(listing->second);
        }
        const auto stem = path.stem().string();
        for (const auto& clip_path : listing->second)
        {
            const auto name = parse_clip_name(std::filesystem::path(clip_path).stem().string(), stem);
            if (!name)
            {
                continue;
            }
            const auto decoded = cached<Result<AlaAnimation>>(builder.cache, "ala", clip_path, builder.vfs,
                                                              [&](const AssetReader& reader) -> Result<AlaAnimation>
                                                              {
                                                                  EAW_TRY_ASSIGN(const auto bytes, reader.read(clip_path));
                                                                  return AlaAnimation::parse(bytes);
                                                              });
            if (!decoded->ok())
            {
                scene.warnings.push_back(clip_path + ": " + decoded->error().message);
                continue;
            }
            if (decoded->value().duration_seconds() <= 0.0F)
            {
                continue;
            }
            auto animation = retarget ? std::make_shared<const AlaAnimation>(ala_retarget_by_bone_name(decoded->value(), *rig.model)) : std::shared_ptr<const AlaAnimation>(decoded, &decoded->value());
            if (animation->tracks.empty())
            {
                continue;
            }
            rig.clips.push_back({name->role, name->variant, clip_path, std::move(animation)});
        }
        std::ranges::sort(rig.clips,
                          [](const VisualClip& left, const VisualClip& right)
                          {
                              return left.role != right.role ? left.role < right.role : left.variant < right.variant;
                          });
    }
    // Instances start in their mesh's rest pose.
    for (auto& instance : scene.instances)
    {
        if (instance.mesh < rigs.size())
        {
            instance.bone_offset = rigs[instance.mesh].rest_offset;
        }
    }
    scene.bone_count = static_cast<std::uint32_t>(std::max<std::size_t>(rest_palette.size(), 1U));
}

} // namespace eaw
