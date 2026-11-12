#include "assets/model_geometry.hpp"

#include "math/transform.hpp"

#include <algorithm>

namespace eaw {

DrawnMeshFilter drawn_mesh_filter(const Model& source, const bool attached, const bool animated_visibility)
{
    DrawnMeshFilter filter;
    filter.bind_pose_visibility = attached || !animated_visibility;
    filter.attached = attached;
    if (attached)
    {
        for (const auto& mesh : source.meshes)
        {
            if (mesh.detail_level)
            {
                filter.attached_detail = std::max(filter.attached_detail.value_or(0U), *mesh.detail_level);
            }
        }
    }
    return filter;
}

bool drawn_mesh(const Model& source, const ModelMesh& mesh, const DrawnMeshFilter& filter) noexcept
{
    const auto& bones = source.skeleton.bones;
    const bool hidden_bone = filter.bind_pose_visibility && mesh.attached_bone && *mesh.attached_bone < bones.size() && !bones[*mesh.attached_bone].visible;
    if (mesh.hidden || hidden_bone)
    {
        return false;
    }
    return !filter.attached || ((!mesh.damage_state || *mesh.damage_state == 0) && (!mesh.detail_level || mesh.detail_level == filter.attached_detail));
}

bool drawn_submesh(const ModelSubmesh& submesh) noexcept
{
    if (submesh.vertices.empty() || submesh.indices.empty())
    {
        return false;
    }
    const auto family = submesh.material.family;
    return family != LegacyMaterialFamily::collision && family != LegacyMaterialFamily::shadow && family != LegacyMaterialFamily::heat && family != LegacyMaterialFamily::occluded;
}

Vector3 drawn_vertex_position(const Vector3& position, const ModelMesh& mesh, const ModelSubmesh& submesh, const std::span<const Matrix4> bind_pose)
{
    if (!submesh.skinned && mesh.attached_bone && *mesh.attached_bone < bind_pose.size())
    {
        return transform_point(position, bind_pose[*mesh.attached_bone]);
    }
    return position;
}

ModelBounds drawn_model_bounds(const Model& model, const std::span<const ModelAttachment> attachments, const bool animated_visibility)
{
    ModelBounds bounds;
    const auto add = [&](const Model& source, const std::vector<Matrix4>& source_bind, const Matrix4* host)
    {
        const auto filter = drawn_mesh_filter(source, host != nullptr, animated_visibility);
        for (const auto& mesh : source.meshes)
        {
            if (!drawn_mesh(source, mesh, filter))
            {
                continue;
            }
            for (const auto& submesh : mesh.submeshes)
            {
                if (!drawn_submesh(submesh))
                {
                    continue;
                }
                for (const auto& vertex : submesh.vertices)
                {
                    auto position = drawn_vertex_position(vertex.position, mesh, submesh, source_bind);
                    if (host != nullptr)
                    {
                        position = transform_point(position, *host);
                    }
                    bounds.minimum = {std::min(bounds.minimum.x, position.x), std::min(bounds.minimum.y, position.y), std::min(bounds.minimum.z, position.z)};
                    bounds.maximum = {std::max(bounds.maximum.x, position.x), std::max(bounds.maximum.y, position.y), std::max(bounds.maximum.z, position.z)};
                }
            }
        }
    };
    const auto bind_pose = skeleton_bind_pose(model.skeleton);
    add(model, bind_pose, nullptr);
    for (const auto& attached : attachments)
    {
        if (attached.model && attached.bone < bind_pose.size())
        {
            add(*attached.model, skeleton_bind_pose(attached.model->skeleton), &bind_pose[attached.bone]);
        }
    }
    return bounds;
}

} // namespace eaw
