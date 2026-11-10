#include "assets/pose.hpp"

#include "core/string_utils.hpp"
#include "math/transform.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace eaw {
namespace {

// Below this length a blended rotation has no direction; the target's rotation is taken instead.
constexpr float minimum_blended_rotation_length = 1.0e-6F;

std::vector<Matrix4> local_bind_transforms(const Skeleton& skeleton)
{
    std::vector<Matrix4> local;
    local.reserve(skeleton.bones.size());
    for (const auto& bone : skeleton.bones)
    {
        local.push_back(bone.local_bind);
    }
    return local;
}

} // namespace

std::vector<PoseBone> sample_pose(const Animation& animation, const Skeleton& skeleton, float seconds)
{
    auto local_pose = local_bind_transforms(skeleton);
    std::vector<bool> visibility;
    visibility.reserve(skeleton.bones.size());
    for (const auto& bone : skeleton.bones)
    {
        visibility.push_back(bone.visible);
    }

    const auto playable = animation.loop_frame_count;
    if (playable != 0 && animation.frames_per_second > 0.0F)
    {
        auto position = std::fmod(seconds * animation.frames_per_second, static_cast<float>(playable));
        if (position < 0.0F)
        {
            position += static_cast<float>(playable);
        }
        const auto first = static_cast<std::size_t>(std::floor(position));
        const auto second = std::min(first + 1, static_cast<std::size_t>(animation.key_count - 1));
        const auto amount = position - static_cast<float>(first);
        for (const auto& track : animation.tracks)
        {
            if (track.bone >= local_pose.size() || first >= track.keys.size() || second >= track.keys.size())
            {
                continue;
            }
            const auto& left = track.keys[first];
            const auto& right = track.keys[second];
            const auto interpolation = track.stepped ? 0.0F : amount;
            local_pose[track.bone] =
                compose_transform(lerp(left.scale, right.scale, interpolation), slerp(left.rotation, right.rotation, interpolation), lerp(left.translation, right.translation, interpolation));
            visibility[track.bone] = left.visible;
        }
    }

    std::vector<PoseBone> result;
    result.reserve(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        auto transform = local_pose[index];
        if (skeleton.bones[index].parent >= 0 && static_cast<std::size_t>(skeleton.bones[index].parent) < result.size())
        {
            const auto parent = static_cast<std::size_t>(skeleton.bones[index].parent);
            transform = transform * result[parent].transform;
            visibility[index] = visibility[index] && result[parent].visible;
        }
        result.push_back({transform, visibility[index]});
    }
    return result;
}

Animation retarget_by_bone_name(const Animation& animation, const Skeleton& skeleton)
{
    std::unordered_map<std::string, std::uint32_t> bones;
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        bones.try_emplace(ascii_lower(skeleton.bones[index].name), static_cast<std::uint32_t>(index));
    }
    auto result = animation;
    result.tracks.clear();
    for (const auto& track : animation.tracks)
    {
        if (const auto found = bones.find(ascii_lower(track.bone_name)); found != bones.end())
        {
            result.tracks.push_back(track);
            result.tracks.back().bone = found->second;
        }
    }
    return result;
}

std::vector<LocalBone> bind_local_pose(const Skeleton& skeleton)
{
    std::vector<LocalBone> pose;
    pose.reserve(skeleton.bones.size());
    for (const auto& bone : skeleton.bones)
    {
        const auto parts = decompose_transform(bone.local_bind);
        pose.push_back({parts.translation, parts.scale, parts.rotation, bone.visible});
    }
    return pose;
}

std::vector<LocalBone> sample_local_pose(const Animation& animation, const Skeleton& skeleton, const float seconds)
{
    auto pose = bind_local_pose(skeleton);
    const auto playable = animation.loop_frame_count;
    if (playable == 0 || animation.frames_per_second <= 0.0F)
    {
        return pose;
    }
    auto position = std::fmod(seconds * animation.frames_per_second, static_cast<float>(playable));
    if (position < 0.0F || !std::isfinite(position))
    {
        position = std::isfinite(position) ? position + static_cast<float>(playable) : 0.0F;
    }
    const auto first = static_cast<std::size_t>(std::floor(position));
    const auto second = std::min(first + 1, static_cast<std::size_t>(animation.key_count - 1));
    const auto amount = position - static_cast<float>(first);
    for (const auto& track : animation.tracks)
    {
        if (track.bone >= pose.size() || first >= track.keys.size() || second >= track.keys.size())
        {
            continue;
        }
        const auto& left = track.keys[first];
        const auto& right = track.keys[second];
        const auto interpolation = track.stepped ? 0.0F : amount;
        pose[track.bone] = {lerp(left.translation, right.translation, interpolation), lerp(left.scale, right.scale, interpolation), slerp(left.rotation, right.rotation, interpolation), left.visible};
    }
    return pose;
}

void blend_pose(std::vector<LocalBone>& pose, const std::span<const LocalBone> target, float weight)
{
    weight = std::isfinite(weight) ? std::clamp(weight, 0.0F, 1.0F) : 0.0F;
    if (weight <= 0.0F)
    {
        return;
    }
    const auto count = std::min(pose.size(), target.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        auto& bone = pose[index];
        const auto& other = target[index];
        auto rotation = other.rotation;
        if (dot(bone.rotation, rotation) < 0.0F)
        {
            rotation = {-rotation.x, -rotation.y, -rotation.z, -rotation.w};
        }
        const Quaternion blended{bone.rotation.x + (rotation.x - bone.rotation.x) * weight, bone.rotation.y + (rotation.y - bone.rotation.y) * weight,
                                 bone.rotation.z + (rotation.z - bone.rotation.z) * weight, bone.rotation.w + (rotation.w - bone.rotation.w) * weight};
        const auto length = std::sqrt(dot(blended, blended));
        bone.rotation = length > minimum_blended_rotation_length ? Quaternion{blended.x / length, blended.y / length, blended.z / length, blended.w / length} : other.rotation;
        bone.translation = lerp(bone.translation, other.translation, weight);
        bone.scale = lerp(bone.scale, other.scale, weight);
        bone.visible = weight > 0.5F ? other.visible : bone.visible;
    }
}

std::vector<PoseBone> compose_pose(const Skeleton& skeleton, const std::span<const LocalBone> pose)
{
    std::vector<PoseBone> result;
    result.reserve(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const auto& bone = skeleton.bones[index];
        auto transform = index < pose.size() ? compose_transform(pose[index].scale, pose[index].rotation, pose[index].translation) : bone.local_bind;
        auto visible = index < pose.size() ? pose[index].visible : bone.visible;
        if (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < result.size())
        {
            const auto parent = static_cast<std::size_t>(bone.parent);
            transform = transform * result[parent].transform;
            visible = visible && result[parent].visible;
        }
        result.push_back({transform, visible});
    }
    return result;
}

} // namespace eaw
