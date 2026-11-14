#include "presentation/visual_rig.hpp"

#include "math/quaternion.hpp"
#include "math/transform.hpp"
#include "assets/pose.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace eaw {

namespace {

bool intact_hidden(const std::vector<bool>& hidden, const std::size_t bone)
{
    return bone < hidden.size() && hidden[bone];
}

} // namespace

// Skin matrices of `pose` (and the visibility half) appended to `out`; `hidden` bones collapse too.
void append_pose_palette(const Skeleton& skeleton, const std::span<const Matrix4> inverse_bind_pose, const std::span<const PoseBone> pose, const bool visibility_palette, std::vector<Matrix4>& out,
                         const std::vector<bool>& hidden)
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
void append_rest_palette(const Skeleton& skeleton, std::vector<Matrix4>& out, const std::vector<bool>& hidden)
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

void append_rig_palette(const VisualRig& rig, const Animation& animation, const float seconds, std::vector<Matrix4>& out)
{
    if (rig.skeleton && rig.inverse_bind_pose.size() == rig.skeleton->bones.size() && !rig.skeleton->bones.empty())
    {
        append_rig_local_palette(rig, sample_local_pose(animation, *rig.skeleton, seconds), out);
        return;
    }
    append_rig_local_palette(rig, {}, out);
}

void append_rig_local_palette(const VisualRig& rig, const std::span<const LocalBone> local_pose, std::vector<Matrix4>& out, const std::span<const BoneTurn> turns, const std::vector<bool>* hidden)
{
    const auto& mask = hidden != nullptr ? *hidden : rig.intact_hidden;
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
        append_pose_palette(*rig.skeleton, rig.inverse_bind_pose, pose, true, out, mask);
    }
    if (out.size() - first != rig.palette_size)
    {
        // A pose that does not cover the skeleton, or no rig: the rest pose keeps the mesh intact.
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
        if (rig.skeleton)
        {
            append_rest_palette(*rig.skeleton, out, mask);
        }
        out.resize(first + rig.palette_size, Matrix4::identity());
    }
}

} // namespace eaw
