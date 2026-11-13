#include "presentation/visual_rig.hpp"

#include "math/quaternion.hpp"
#include "math/transform.hpp"
#include "core/string_utils.hpp"
#include "assets/pose.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>
#include <system_error>

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
    // Stopping clips carry their foot in the name, not the number.
    static constexpr std::array<std::string_view, 4> endings{"move_endone", "move_endtwo", "move_endthree", "move_endfour"};
    for (std::size_t index = 0; index < endings.size(); ++index)
    {
        if (rest.substr(0, underscore) == endings[index])
        {
            return ClipName{AnimationRole::move_end, static_cast<std::uint32_t>(index)};
        }
    }
    static constexpr std::array<std::pair<std::string_view, AnimationRole>, 14> roles{{
        {"idle", AnimationRole::idle},
        {"move", AnimationRole::move},
        {"walkmove", AnimationRole::walk_move},
        {"attack", AnimationRole::attack},
        {"attackidle", AnimationRole::attack_idle},
        {"die", AnimationRole::die},
        {"crushed", AnimationRole::crushed},
        {"turnl", AnimationRole::turn_left},
        {"turnr", AnimationRole::turn_right},
        {"turnl_begin", AnimationRole::turn_left_begin},
        {"turnl_end", AnimationRole::turn_left_end},
        {"turnr_begin", AnimationRole::turn_right_begin},
        {"turnr_end", AnimationRole::turn_right_end},
        {"movestart", AnimationRole::move_start},
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

} // namespace eaw
