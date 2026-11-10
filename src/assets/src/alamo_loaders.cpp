#include "assets/alamo_loaders.hpp"

#include "formats/ala_animation_helpers.hpp"
#include "formats/alo_model_helpers.hpp"

namespace eaw {

Skeleton load_skeleton(const AloModelData& model)
{
    const auto local = alo_local_bind_pose(model);
    Skeleton skeleton;
    skeleton.bones.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        const auto& bone = model.bones[index];
        skeleton.bones.push_back({bone.name, bone.parent, local[index], bone.visible});
    }
    return skeleton;
}

Animation load_animation(const AlaAnimationData& animation)
{
    Animation result;
    result.frames_per_second = animation.frames_per_second;
    result.key_count = animation.stored_frame_count;
    result.loop_frame_count = ala_playable_frame_count(animation);
    result.tracks.reserve(animation.tracks.size());
    for (const auto& track : animation.tracks)
    {
        AnimationTrack loaded{track.name, track.bone_index, track.stepped, {}};
        loaded.keys.reserve(track.frames.size());
        for (const auto& frame : track.frames)
        {
            loaded.keys.push_back({frame.translation, frame.scale, frame.rotation, frame.visible});
        }
        result.tracks.push_back(std::move(loaded));
    }
    return result;
}

} // namespace eaw
