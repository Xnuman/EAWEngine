#include "eaw/alamo_pose.hpp"

#include "core/string_utils.hpp"
#include "math/transform.hpp"
#include "formats/alo_model_helpers.hpp"
#include "formats/ala_animation_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace eaw {

std::vector<AlaPoseBone> ala_sample_pose(const AlaAnimationData& animation, const AloModelData& model, float seconds)
{
    auto local_pose = alo_local_bind_pose(model);
    std::vector<bool> visibility;
    visibility.reserve(model.bones.size());
    for (const auto& bone : model.bones)
    {
        visibility.push_back(bone.visible);
    }

    const auto playable = ala_playable_frame_count(animation);
    if (playable != 0 && animation.frames_per_second > 0.0F)
    {
        auto position = std::fmod(seconds * animation.frames_per_second, static_cast<float>(playable));
        if (position < 0.0F)
        {
            position += static_cast<float>(playable);
        }
        const auto first = static_cast<std::size_t>(std::floor(position));
        const auto second = std::min(first + 1, static_cast<std::size_t>(animation.stored_frame_count - 1));
        const auto amount = position - static_cast<float>(first);
        for (const auto& track : animation.tracks)
        {
            if (track.bone_index >= local_pose.size() || first >= track.frames.size() || second >= track.frames.size())
            {
                continue;
            }
            const auto& left = track.frames[first];
            const auto& right = track.frames[second];
            const auto interpolation = track.stepped ? 0.0F : amount;
            local_pose[track.bone_index] =
                compose_transform(lerp(left.scale, right.scale, interpolation), slerp(left.rotation, right.rotation, interpolation), lerp(left.translation, right.translation, interpolation));
            visibility[track.bone_index] = left.visible;
        }
    }

    std::vector<AlaPoseBone> result;
    result.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        auto transform = local_pose[index];
        if (model.bones[index].parent >= 0 && static_cast<std::size_t>(model.bones[index].parent) < result.size())
        {
            const auto parent = static_cast<std::size_t>(model.bones[index].parent);
            transform = transform * result[parent].transform;
            visibility[index] = visibility[index] && result[parent].visible;
        }
        result.push_back({transform, visibility[index]});
    }
    return result;
}

AlaAnimationData ala_retarget_by_bone_name(const AlaAnimationData& animation, const AloModelData& model)
{
    std::unordered_map<std::string, std::uint32_t> bones;
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        bones.try_emplace(ascii_lower(model.bones[index].name), static_cast<std::uint32_t>(index));
    }
    auto result = animation;
    result.tracks.clear();
    for (const auto& track : animation.tracks)
    {
        if (const auto found = bones.find(ascii_lower(track.name)); found != bones.end())
        {
            result.tracks.push_back(track);
            result.tracks.back().bone_index = found->second;
        }
    }
    return result;
}

std::vector<AlaLocalBone> ala_bind_local_pose(const AloModelData& model)
{
    const auto local = alo_local_bind_pose(model);
    std::vector<AlaLocalBone> pose;
    pose.reserve(local.size());
    for (std::size_t index = 0; index < local.size(); ++index)
    {
        const auto parts = decompose_transform(local[index]);
        pose.push_back({parts.translation, parts.scale, parts.rotation, model.bones[index].visible});
    }
    return pose;
}

std::vector<AlaLocalBone> ala_sample_local_pose(const AlaAnimationData& animation, const AloModelData& model, const float seconds)
{
    auto pose = ala_bind_local_pose(model);
    const auto playable = ala_playable_frame_count(animation);
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
    const auto second = std::min(first + 1, static_cast<std::size_t>(animation.stored_frame_count - 1));
    const auto amount = position - static_cast<float>(first);
    for (const auto& track : animation.tracks)
    {
        if (track.bone_index >= pose.size() || first >= track.frames.size() || second >= track.frames.size())
        {
            continue;
        }
        const auto& left = track.frames[first];
        const auto& right = track.frames[second];
        const auto interpolation = track.stepped ? 0.0F : amount;
        pose[track.bone_index] = {lerp(left.translation, right.translation, interpolation), lerp(left.scale, right.scale, interpolation), slerp(left.rotation, right.rotation, interpolation),
                                  left.visible};
    }
    return pose;
}

void ala_blend_pose(std::vector<AlaLocalBone>& pose, const std::span<const AlaLocalBone> target, float weight)
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
        if (bone.rotation.x * rotation.x + bone.rotation.y * rotation.y + bone.rotation.z * rotation.z + bone.rotation.w * rotation.w < 0.0F)
        {
            rotation = {-rotation.x, -rotation.y, -rotation.z, -rotation.w};
        }
        Quaternion blended{bone.rotation.x + (rotation.x - bone.rotation.x) * weight, bone.rotation.y + (rotation.y - bone.rotation.y) * weight,
                           bone.rotation.z + (rotation.z - bone.rotation.z) * weight, bone.rotation.w + (rotation.w - bone.rotation.w) * weight};
        const auto length = std::sqrt(blended.x * blended.x + blended.y * blended.y + blended.z * blended.z + blended.w * blended.w);
        bone.rotation = length > 1.0e-6F ? Quaternion{blended.x / length, blended.y / length, blended.z / length, blended.w / length} : other.rotation;
        bone.translation = lerp(bone.translation, other.translation, weight);
        bone.scale = lerp(bone.scale, other.scale, weight);
        bone.visible = weight > 0.5F ? other.visible : bone.visible;
    }
}

std::vector<AlaPoseBone> ala_compose_pose(const AloModelData& model, const std::span<const AlaLocalBone> pose)
{
    const auto bind = alo_local_bind_pose(model);
    std::vector<AlaPoseBone> result;
    result.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        auto transform = index < pose.size() ? compose_transform(pose[index].scale, pose[index].rotation, pose[index].translation) : bind[index];
        auto visible = index < pose.size() ? pose[index].visible : model.bones[index].visible;
        if (model.bones[index].parent >= 0 && static_cast<std::size_t>(model.bones[index].parent) < result.size())
        {
            const auto parent = static_cast<std::size_t>(model.bones[index].parent);
            transform = transform * result[parent].transform;
            visible = visible && result[parent].visible;
        }
        result.push_back({transform, visible});
    }
    return result;
}

} // namespace eaw
