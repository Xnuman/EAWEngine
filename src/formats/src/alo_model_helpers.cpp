#include "formats/alo_model_helpers.hpp"

namespace eaw {

std::size_t alo_submesh_count(const AloModelData& model) noexcept
{
    std::size_t count = 0;
    for (const auto& mesh : model.meshes)
    {
        count += mesh.submeshes.size();
    }
    return count;
}

std::uint64_t alo_vertex_count(const AloModelData& model) noexcept
{
    std::uint64_t count = 0;
    for (const auto& mesh : model.meshes)
    {
        for (const auto& submesh : mesh.submeshes)
        {
            count += submesh.vertex_count;
        }
    }
    return count;
}

std::uint64_t alo_triangle_count(const AloModelData& model) noexcept
{
    std::uint64_t count = 0;
    for (const auto& mesh : model.meshes)
    {
        for (const auto& submesh : mesh.submeshes)
        {
            count += submesh.triangle_count;
        }
    }
    return count;
}

std::vector<Matrix4> alo_local_bind_pose(const AloModelData& model)
{
    std::vector<Matrix4> pose;
    pose.reserve(model.bones.size());
    for (const auto& bone : model.bones)
    {
        auto local = Matrix4::identity();
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                local.values[column * 4 + row] = bone.transform[row * 4 + column];
            }
        }
        pose.push_back(local);
    }
    return pose;
}

std::vector<Matrix4> alo_bind_pose(const AloModelData& model)
{
    auto local_pose = alo_local_bind_pose(model);
    std::vector<Matrix4> pose;
    pose.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        if (model.bones[index].parent >= 0 && static_cast<std::size_t>(model.bones[index].parent) < pose.size())
        {
            pose.push_back(local_pose[index] * pose[static_cast<std::size_t>(model.bones[index].parent)]);
        }
        else
        {
            pose.push_back(local_pose[index]);
        }
    }
    return pose;
}

} // namespace eaw
