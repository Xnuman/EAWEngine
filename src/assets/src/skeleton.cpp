#include "assets/skeleton.hpp"

#include "core/string_utils.hpp"

namespace eaw {

std::vector<Matrix4> skeleton_bind_pose(const Skeleton& skeleton)
{
    std::vector<Matrix4> pose;
    pose.reserve(skeleton.bones.size());
    for (const auto& bone : skeleton.bones)
    {
        if (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < pose.size())
        {
            pose.push_back(bone.local_bind * pose[static_cast<std::size_t>(bone.parent)]);
        }
        else
        {
            pose.push_back(bone.local_bind);
        }
    }
    return pose;
}

std::optional<std::uint32_t> find_skeleton_bone(const Skeleton& skeleton, const std::string_view name)
{
    const auto wanted = ascii_trim(name);
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        if (ascii_equal_ignoring_case(skeleton.bones[index].name, wanted))
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

std::vector<bool> bones_in_subtrees(const std::span<const std::int32_t> parents, const std::span<const std::uint32_t> roots)
{
    std::vector<bool> inside(parents.size(), false);
    for (const auto root : roots)
    {
        if (root < inside.size())
        {
            inside[root] = true;
        }
    }
    // Parents come first, so one forward pass carries membership down the tree.
    for (std::size_t index = 0; index < parents.size(); ++index)
    {
        const auto parent = parents[index];
        if (parent >= 0 && static_cast<std::size_t>(parent) < index && inside[static_cast<std::size_t>(parent)])
        {
            inside[index] = true;
        }
    }
    return inside;
}

} // namespace eaw
