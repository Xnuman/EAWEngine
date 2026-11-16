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
    if (mesh.hidden || hidden_bone || mesh.stealth)
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

bool has_drawn_geometry(const Model& model, const std::span<const ModelAttachment> attachments, const bool animated_visibility)
{
    const auto any = [&](const Model& source, const bool attached)
    {
        const auto filter = drawn_mesh_filter(source, attached, animated_visibility);
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
                for (std::size_t index = 0; index + 2 < submesh.indices.size(); index += 3)
                {
                    const auto count = submesh.vertices.size();
                    if (submesh.indices[index] < count && submesh.indices[index + 1] < count && submesh.indices[index + 2] < count)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    };
    if (any(model, false))
    {
        return true;
    }
    const auto bones = model.skeleton.bones.size();
    for (const auto& attached : attachments)
    {
        if (attached.model && attached.bone < bones && any(*attached.model, true))
        {
            return true;
        }
    }
    return false;
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

void add_placed_bounds(ModelBounds& bounds, const ModelBounds& local, const Matrix4& transform) noexcept
{
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        const Vector3 point = transform_point(
            {(corner & 1U) != 0 ? local.maximum.x : local.minimum.x, (corner & 2U) != 0 ? local.maximum.y : local.minimum.y, (corner & 4U) != 0 ? local.maximum.z : local.minimum.z}, transform);
        if (!is_finite(point))
        {
            continue;
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
            bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
        }
    }
}

void assign_billboard_centers(const std::span<VisualVertex> vertices, const std::span<const std::uint32_t> indices)
{
    // Connected pieces by union-find over the triangles' corners.
    std::vector<std::uint32_t> parent(vertices.size());
    for (std::uint32_t index = 0; index < parent.size(); ++index)
    {
        parent[index] = index;
    }
    const auto root = [&](std::uint32_t index)
    {
        while (parent[index] != index)
        {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };
    for (std::size_t corner = 0; corner + 2 < indices.size(); corner += 3)
    {
        const auto a = indices[corner];
        const auto b = indices[corner + 1];
        const auto c = indices[corner + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
        {
            continue;
        }
        parent[root(b)] = root(a);
        parent[root(c)] = root(a);
    }
    std::vector<Vector2> sums(vertices.size());
    std::vector<std::uint32_t> counts(vertices.size(), 0U);
    for (std::uint32_t index = 0; index < vertices.size(); ++index)
    {
        const auto piece = root(index);
        sums[piece] = sums[piece] + Vector2{vertices[index].position.x, vertices[index].position.y};
        ++counts[piece];
    }
    for (std::uint32_t index = 0; index < vertices.size(); ++index)
    {
        const auto piece = root(index);
        vertices[index].texture_coordinates_1 = sums[piece] * (1.0F / static_cast<float>(counts[piece]));
    }
}

} // namespace eaw
