#include "assets/alamo_loaders.hpp"
#include "assets/pose.hpp"
#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "formats/dds_texture.hpp"
#include "assets/skeleton.hpp"
#include "assets/type_hardpoints.hpp"
#include "formats/xml_document.hpp"
#include "core/string_utils.hpp"
#include "math/quaternion.hpp"
#include "math/transform.hpp"

#include <algorithm>
#include <atomic>
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

} // namespace

// The loaded model, or why it could not be loaded (cached either way: a failed load depends on the
// file, so replacing it invalidates the entry).
std::shared_ptr<const Result<Model>> cached_model(SceneBuilder& builder, const std::string& path)
{
    return resources(builder).get<Model>(path);
}

// Appends one model as a new mesh (its draws, vertices, indices, and any new textures) and
// returns the mesh index. Vertices stay in bind-pose model space with model-wide bone indices.
// With `animated_visibility`, the mesh's palette has two halves: [0, N) the skin matrices and
// [N, 2N) the same matrices collapsed to a point while their bone is hidden. Rigid attachments
// reference the second half, so meshes on bones hidden in the bind pose are included and appear
// when an animation shows their bone. Without it (maps share one bind-pose palette), such meshes
// are omitted.
// A model that cannot be appended leaves no geometry, indices, or materials behind.
namespace {

// A draw's damage state and detail level (none: every state or level).
struct DrawTag
{
    std::optional<std::uint32_t> state;
    std::optional<std::uint32_t> level;
};

// Lays a mesh's draws out by damage state and detail level: one run of draws per state (state 0,
// then the tagged states in order) and level (most detailed first), each holding the untagged draws
// and those of its state and level, so the default range (state 0 at full detail) is the first run.
// A mesh without tags keeps its draws as they are.
void arrange_states_and_levels(VisualScene& scene, VisualMesh& mesh, const std::vector<DrawTag>& tags)
{
    std::vector<std::uint32_t> states{0};
    std::vector<std::uint32_t> levels;
    for (const auto& tag : tags)
    {
        if (tag.state && std::ranges::find(states, *tag.state) == states.end())
        {
            states.push_back(*tag.state);
        }
        if (tag.level && std::ranges::find(levels, *tag.level) == levels.end())
        {
            levels.push_back(*tag.level);
        }
    }
    if (states.size() == 1 && levels.empty())
    {
        return;
    }
    std::ranges::sort(states);
    // Legacy _LOD<n>: the higher number is the more detailed level.
    std::ranges::sort(levels, std::greater<>{});
    const std::vector<VisualDraw> drawn(scene.draws.begin() + mesh.first_draw, scene.draws.end());
    scene.draws.resize(mesh.first_draw);
    const auto level_count = std::max<std::size_t>(levels.size(), 1U);
    for (const auto state : states)
    {
        VisualMeshState visual_state{state, {}};
        for (std::size_t level_index = 0; level_index < level_count; ++level_index)
        {
            VisualMeshLod level{static_cast<std::uint32_t>(scene.draws.size()), 0};
            for (std::size_t draw = 0; draw < drawn.size(); ++draw)
            {
                const auto& tag = tags[draw];
                const bool in_state = !tag.state || *tag.state == state;
                const bool in_level = !tag.level || levels.empty() || *tag.level == levels[level_index];
                if (in_state && in_level)
                {
                    scene.draws.push_back(drawn[draw]);
                }
            }
            level.draw_count = static_cast<std::uint32_t>(scene.draws.size()) - level.first_draw;
            visual_state.lods.push_back(level);
        }
        mesh.states.push_back(std::move(visual_state));
    }
    mesh.first_draw = mesh.states.front().lods.front().first_draw;
    mesh.draw_count = mesh.states.front().lods.front().draw_count;
}

} // namespace

Result<std::uint32_t> append_model(SceneBuilder& builder, const Model& model, const std::string_view name, const bool animated_visibility, const std::span<const ModelAttachment> attachments)
{
    auto& scene = builder.scene;
    const auto bind_pose = skeleton_bind_pose(model.skeleton);
    if (model.skeleton.bones.size() > std::numeric_limits<std::uint32_t>::max() / 2U)
    {
        return make_error("ALO skeleton exceeds the visual scene bone index range");
    }
    const auto bone_count = static_cast<std::uint32_t>(model.skeleton.bones.size());
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
    // Each appended draw's damage state and detail level (none: every state or level), parallel to
    // the draws from visual_mesh.first_draw on.
    std::vector<DrawTag> draw_tags;
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
    const auto append_geometry = [&](const Model& source, const std::vector<Matrix4>& source_bind, const Matrix4* host, const std::optional<std::uint32_t> host_bone) -> Status
    {
        // Bone visibility is either animated through the visibility palette or, for maps and
        // attached models, fixed to the bind pose.
        const bool apply_bind_pose_bones = host != nullptr || !animated_visibility;
        const auto& bones = source.skeleton.bones;
        // An attached model (a hardpoint's) shows its own default appearance: intact, full detail.
        std::optional<std::uint32_t> attached_detail;
        if (host != nullptr)
        {
            for (const auto& mesh : source.meshes)
            {
                if (mesh.detail_level)
                {
                    attached_detail = std::max(attached_detail.value_or(0U), *mesh.detail_level);
                }
            }
        }
        for (std::size_t mesh_index = 0; mesh_index < source.meshes.size(); ++mesh_index)
        {
            const auto& mesh = source.meshes[mesh_index];
            const bool hidden_bone = apply_bind_pose_bones && mesh.attached_bone && *mesh.attached_bone < bones.size() && !bones[*mesh.attached_bone].visible;
            if (mesh.hidden || hidden_bone)
            {
                continue;
            }
            if (host != nullptr && ((mesh.damage_state && *mesh.damage_state != 0) || (mesh.detail_level && mesh.detail_level != attached_detail)))
            {
                continue;
            }
            const DrawTag tag = host != nullptr ? DrawTag{} : DrawTag{mesh.damage_state, mesh.detail_level};
            for (const auto& submesh : mesh.submeshes)
            {
                if (submesh.vertices.empty() || submesh.indices.empty())
                {
                    continue;
                }
                const auto& material = submesh.material;
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
                if (!submesh.skinned && mesh.attached_bone && *mesh.attached_bone < source_bind.size())
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
                    else if (submesh.skinned)
                    {
                        bone_indices = vertex.bone_indices;
                        bone_weights = vertex.bone_weights;
                    }
                    else if (mesh.attached_bone && *mesh.attached_bone < bones.size())
                    {
                        bone_indices[0] = animated_visibility ? bone_count + *mesh.attached_bone : *mesh.attached_bone;
                        bone_weights[0] = 1.0F;
                    }
                    scene.vertices.push_back({position, normal, vertex.texture_coordinates, bone_indices, bone_weights, tangent, binormal, vertex.color});
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
                draw_tags.push_back(tag);
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
        if (auto status = append_geometry(*attached.model, skeleton_bind_pose(attached.model->skeleton), &bind_pose[attached.bone], attached.bone); !status)
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
    arrange_states_and_levels(scene, visual_mesh, draw_tags);
    const auto mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(visual_mesh));
    return mesh_index;
}

std::vector<ModelAttachment> type_hardpoint_models(SceneBuilder& builder, const GameObjectCatalog& catalog, const XmlDefinition& type, const Model* host)
{
    std::vector<ModelAttachment> attachments;
    if (host == nullptr || !builder.textures)
    {
        return attachments;
    }
    const auto text_of = [&](const XmlDefinition& definition, const std::string_view name)
    {
        const auto* node = catalog.field(definition, name).field;
        return node != nullptr ? std::string(ascii_trim(node->text)) : std::string{};
    };
    for (const auto& [name, hardpoint] : type_hardpoints(catalog, type))
    {
        if (hardpoint == nullptr)
        {
            continue;
        }
        const auto model_name = text_of(*hardpoint, "Model_To_Attach");
        if (model_name.empty())
        {
            continue;
        }
        const auto bone_name = ascii_lower(text_of(*hardpoint, "Attachment_Bone"));
        const auto bone = find_skeleton_bone(host->skeleton, bone_name);
        const auto path = resolve_model_path(builder.vfs, model_name);
        if (!bone || !path)
        {
            builder.scene.warnings.push_back(type.name + ": hardpoint " + name + " attaches " + model_name + (path ? " to missing bone '" + bone_name + "'" : " (file not found)"));
            continue;
        }
        const auto model = cached_model(builder, *path);
        if (!model->ok())
        {
            builder.scene.warnings.push_back(*path + ": " + model->error().message);
            continue;
        }
        attachments.push_back({*path, std::shared_ptr<const Model>(model, &model->value()), *bone});
    }
    return attachments;
}

std::string attached_mesh_key(const std::string& model_path, const std::span<const ModelAttachment> attachments)
{
    auto key = ascii_lower(model_path);
    for (const auto& attachment : attachments)
    {
        key += "|" + ascii_lower(attachment.path) + "@" + std::to_string(attachment.bone);
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
float mesh_footprint_radius(const VisualScene& scene, const std::uint32_t mesh, const Matrix4& transform)
{
    if (mesh >= scene.meshes.size())
    {
        return 0.0F;
    }
    const auto& bounds = scene.meshes[mesh];
    const auto width = bounds.bounds_maximum[0] - bounds.bounds_minimum[0];
    const auto depth = bounds.bounds_maximum[1] - bounds.bounds_minimum[1];
    const auto scale = std::sqrt(transform.values[0] * transform.values[0] + transform.values[1] * transform.values[1] + transform.values[2] * transform.values[2]);
    const auto radius = 0.25F * (width + depth) * scale;
    return std::isfinite(radius) && radius > 0.0F ? radius : 0.0F;
}

GroundFootprint mesh_ground_footprint(const VisualScene& scene, const std::uint32_t mesh, const Matrix4& transform, const std::uint32_t passability_type)
{
    GroundFootprint footprint;
    footprint.passability = passability_type;
    if (mesh >= scene.meshes.size())
    {
        return footprint;
    }
    const auto& bounds = scene.meshes[mesh];
    const auto scale = std::sqrt(transform.values[0] * transform.values[0] + transform.values[1] * transform.values[1] + transform.values[2] * transform.values[2]);
    const auto local_x = (bounds.bounds_minimum[0] + bounds.bounds_maximum[0]) * 0.5F;
    const auto local_y = (bounds.bounds_minimum[1] + bounds.bounds_maximum[1]) * 0.5F;
    // Row-vector transform: world = local.x * row0 + local.y * row1 + translation.
    footprint.center = {local_x * transform.values[0] + local_y * transform.values[4] + transform.values[12], local_x * transform.values[1] + local_y * transform.values[5] + transform.values[13],
                        transform.values[14]};
    footprint.half_length = std::max(0.0F, (bounds.bounds_maximum[0] - bounds.bounds_minimum[0]) * 0.5F * scale);
    footprint.half_width = std::max(0.0F, (bounds.bounds_maximum[1] - bounds.bounds_minimum[1]) * 0.5F * scale);
    footprint.heading = std::atan2(transform.values[1], transform.values[0]);
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
            const auto point = transform_point(local, instance.transform);
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

float model_forward_heading(const Matrix4& transform) noexcept
{
    // Row 1 is the model's +Y axis in world space; forward is its negation.
    return std::atan2(-transform.values[5], -transform.values[4]);
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
    asset.skeleton = model->value().skeleton;
    for (const auto& transform : skeleton_bind_pose(asset.skeleton))
    {
        const auto inverted = inverse(transform);
        if (!inverted)
        {
            return with_context(resolved->virtual_path, make_error("ALO bone transform is singular"));
        }
        asset.inverse_bind_pose.push_back(*inverted);
    }
    scene.name = resolved->virtual_path;
    scene.warnings = model->value().warnings;
    asset.visibility_palette = true;
    const auto appended = append_model(builder, model->value(), resolved->virtual_path, true);
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
    const auto prefix = ascii_lower(path.stem().string() + "_");
    std::vector<std::string> result;
    for (const auto& asset : reader.list(path.parent_path().generic_string()))
    {
        const auto file = std::filesystem::path(asset.virtual_path);
        if (ascii_lower(file.extension().string()) == ascii_lower(".ala") && ascii_lower(file.filename().string()).starts_with(prefix))
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
void append_pose_palette(const Skeleton& skeleton, const std::span<const Matrix4> inverse_bind_pose, const std::span<const PoseBone> pose, const bool visibility_palette, std::vector<Matrix4>& out,
                         const std::vector<bool>& hidden = {})
{
    const auto bone_count = std::min({pose.size(), inverse_bind_pose.size(), skeleton.bones.size()});
    const auto first = out.size();
    for (std::size_t index = 0; index < bone_count; ++index)
    {
        out.push_back(inverse_bind_pose[index] * pose[index].transform);
    }
    if (visibility_palette)
    {
        // Hidden bones collapse their attachments to a point, which rasterizes nothing.
        for (std::size_t index = 0; index < bone_count; ++index)
        {
            out.push_back(pose[index].visible && !intact_hidden(hidden, index) ? out[first + index] : collapsed_bone);
        }
    }
}

// A rig's rest palette: identity skin matrices, then the visibility half from the bind pose.
void append_rest_palette(const Skeleton& skeleton, std::vector<Matrix4>& out, const std::vector<bool>& hidden = {})
{
    const auto identity = Matrix4::identity();
    out.insert(out.end(), skeleton.bones.size(), identity);
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        out.push_back(skeleton.bones[index].visible && !intact_hidden(hidden, index) ? identity : collapsed_bone);
    }
    if (skeleton.bones.empty())
    {
        out.push_back(identity);
    }
}

} // namespace

std::vector<Matrix4> sample_skin_palette(const VisualModelAsset& asset, const Animation* animation, const float seconds)
{
    std::vector<PoseBone> pose;
    if (animation != nullptr)
    {
        pose = sample_pose(*animation, asset.skeleton, seconds);
    }
    else
    {
        const auto bind_pose = skeleton_bind_pose(asset.skeleton);
        pose.reserve(bind_pose.size());
        for (std::size_t index = 0; index < bind_pose.size(); ++index)
        {
            pose.push_back({bind_pose[index], asset.skeleton.bones[index].visible});
        }
    }
    std::vector<Matrix4> result;
    result.reserve(std::max<std::size_t>(pose.size() * 2U, 1U));
    append_pose_palette(asset.skeleton, asset.inverse_bind_pose, pose, asset.visibility_palette, result);
    if (result.empty())
    {
        result.push_back(Matrix4::identity());
    }
    return result;
}

void append_rig_palette(const VisualRig& rig, const Animation& animation, const float seconds, std::vector<Matrix4>& out)
{
    if (rig.skeleton && rig.inverse_bind_pose.size() == rig.skeleton->bones.size() && !rig.skeleton->bones.empty())
    {
        append_rig_local_palette(rig, sample_local_pose(animation, *rig.skeleton, seconds), out);
        return;
    }
    append_rig_local_palette(rig, {}, out);
}

void append_rig_local_palette(const VisualRig& rig, const std::span<const LocalBone> local_pose, std::vector<Matrix4>& out, const std::span<const BoneTurn> turns)
{
    const auto first = out.size();
    if (rig.skeleton && rig.inverse_bind_pose.size() == rig.skeleton->bones.size() && !rig.skeleton->bones.empty() && local_pose.size() == rig.skeleton->bones.size())
    {
        auto pose = compose_pose(*rig.skeleton, local_pose);
        if (!turns.empty())
        {
            std::vector<std::int32_t> parents;
            parents.reserve(rig.skeleton->bones.size());
            for (const auto& bone : rig.skeleton->bones)
            {
                parents.push_back(bone.parent);
            }
            for (const auto& turn : turns)
            {
                if (turn.bone >= pose.size() || turn.radians == 0.0F)
                {
                    continue;
                }
                // Row vectors: v' = v T(-pivot) R T(pivot).
                const auto pivot = translation(pose[turn.bone].transform);
                const auto around = compose_transform({1.0F, 1.0F, 1.0F}, Quaternion::identity(), {-pivot.x, -pivot.y, -pivot.z}) *
                                    compose_transform({1.0F, 1.0F, 1.0F}, axis_angle(turn.axis, turn.radians), {}) * compose_transform({1.0F, 1.0F, 1.0F}, Quaternion::identity(), pivot);
                const std::array<std::uint32_t, 1> root{turn.bone};
                const auto subtree = bones_in_subtrees(parents, root);
                for (std::size_t bone = 0; bone < pose.size(); ++bone)
                {
                    if (subtree[bone])
                    {
                        pose[bone].transform = pose[bone].transform * around;
                    }
                }
            }
        }
        append_pose_palette(*rig.skeleton, rig.inverse_bind_pose, pose, true, out, rig.intact_hidden);
    }
    if (out.size() - first != rig.palette_size)
    {
        // A pose that does not cover the skeleton, or no rig: the rest pose keeps the mesh intact.
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
        if (rig.skeleton)
        {
            append_rest_palette(*rig.skeleton, out, rig.intact_hidden);
        }
        out.resize(first + rig.palette_size, Matrix4::identity());
    }
}

std::optional<ClipName> parse_clip_name(const std::string_view file_stem, const std::string_view model_stem)
{
    const auto file = ascii_lower(std::string(file_stem));
    const auto prefix = ascii_lower(std::string(model_stem)) + "_";
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
    static constexpr std::array<std::pair<std::string_view, AnimationRole>, 7> roles{{
        {"idle", AnimationRole::idle},
        {"move", AnimationRole::move},
        {"walkmove", AnimationRole::walk_move},
        {"attack", AnimationRole::attack},
        {"attackidle", AnimationRole::attack_idle},
        {"die", AnimationRole::die},
        {"crushed", AnimationRole::crushed},
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

void scene_build::build_map_rigs(SceneBuilder& builder, std::vector<VisualRig>& rigs, std::vector<Matrix4>& rest_palette)
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
            rig.skeleton = std::shared_ptr<const Skeleton>(model, &model->value().skeleton);
            // Scorch marks of hardpoints (their Damage_Decal bones and everything below) wait for
            // the hardpoint's destruction.
            if (const auto decals = builder.decal_bones.find(ascii_lower(mesh.name)); decals != builder.decal_bones.end())
            {
                std::vector<std::int32_t> parents;
                for (const auto& bone : rig.skeleton->bones)
                {
                    parents.push_back(bone.parent);
                }
                const std::vector<std::uint32_t> roots(decals->second.begin(), decals->second.end());
                rig.intact_hidden = bones_in_subtrees(parents, roots);
            }
            append_rest_palette(*rig.skeleton, rest_palette, rig.intact_hidden);
            rig.effects = proxy_effects(builder, model->value(), mesh.name);
            for (const auto& transform : skeleton_bind_pose(*rig.skeleton))
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
            rest_palette.resize(rig.rest_offset + std::max(mesh.bone_count, 1U), Matrix4::identity());
        }
        rig.palette_size = static_cast<std::uint32_t>(rest_palette.size()) - rig.rest_offset;
        if (!rig.skeleton || rig.inverse_bind_pose.empty() || !builder.animation_clips)
        {
            continue;
        }
        // Clips come from the type's animation source when it names one, else from the model.
        auto path = std::filesystem::path(mesh.name);
        bool retarget = false;
        if (const auto source = builder.animation_sources.find(ascii_lower(mesh.name)); source != builder.animation_sources.end())
        {
            const auto resolved = resolve_model_path(builder.vfs, source->second);
            const auto source_path = resolved ? std::filesystem::path(*resolved) : path.parent_path() / std::filesystem::path(source->second).filename();
            retarget = ascii_lower(source_path.stem().string()) != ascii_lower(path.stem().string());
            path = source_path;
        }
        const auto directory = path.parent_path().generic_string();
        auto [listing, inserted] = listings.try_emplace(ascii_lower(directory));
        if (inserted)
        {
            for (const auto& asset : builder.vfs.list(directory))
            {
                if (ascii_lower(std::filesystem::path(asset.virtual_path).extension().string()) == ".ala")
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
            const auto decoded = resources(builder).get<Animation>(clip_path);
            if (!decoded->ok())
            {
                scene.warnings.push_back(clip_path + ": " + decoded->error().message);
                continue;
            }
            if (animation_duration(decoded->value()) <= 0.0F)
            {
                continue;
            }
            auto animation = retarget ? std::make_shared<const Animation>(retarget_by_bone_name(decoded->value(), *rig.skeleton)) : std::shared_ptr<const Animation>(decoded, &decoded->value());
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
