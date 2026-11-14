// Unit animation, the pose stage: the layers of an entity's pose key (locomotion, a clip replacing it,
// aiming, firing) and its turret's aim.

#include "unit_animation_internal.hpp"

#include "presentation/scene_presentation.hpp"

namespace eaw {

using namespace unit_animation_detail;

UnitAnimator::PoseKey UnitAnimator::compose_pose(const EntityFrame& frame, const LocomotionPose& locomotion_pose, const AttackPose& attack, const ClipOverride& replaced, const std::uint64_t mask)
{
    const auto& profile = frame.profile;
    auto& state = frame.state;
    const auto mesh = frame.mesh;
    const auto seconds = frame.seconds;
    const auto& blend = locomotion_pose.blend;
    const auto lower = locomotion_pose.lower;
    const auto upper = locomotion_pose.upper;
    const auto aim_clip = attack.aim_clip;
    const auto fire_time = attack.fire_time;
    const auto fire_eighths = attack.fire_eighths;
    const auto aim_eighths = attack.aim_eighths;
    const auto locomotion = attack.locomotion;
    const auto override_clip = replaced.clip;
    const auto override_time = replaced.time;
    const auto override_weight = replaced.weight;
    const auto layer = [&](const std::int32_t clip, const AnimationRole role, const float weight) -> Layer
    {
        const auto amount = eighths(weight);
        if (clip == no_clip || amount == 0)
        {
            return {};
        }
        if (clip == rest_clip)
        {
            return {rest_clip, 0, amount};
        }
        const auto duration = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation));
        const auto time = role == AnimationRole::idle ? (seconds - state.idle_start) * static_cast<double>(profile.idle_rate) : state.stride_phase * duration;
        return {clip, slot_of(mesh, clip, time), amount};
    };
    PoseKey pose_key{mesh, {}};
    pose_key.mask = mask;
    pose_key.overlay = profile.attack_overlay;
    pose_key.layers[0] = layer(lower, blend.lower, locomotion * (1.0F - blend.blend));
    pose_key.layers[1] = upper == lower ? Layer{} : layer(upper, blend.upper, locomotion * blend.blend);
    if (upper == lower)
    {
        pose_key.layers[0] = layer(lower, blend.lower, locomotion);
    }
    if (override_clip >= 0)
    {
        // While it fades, the override shares the locomotion with the clip that leads it.
        const auto override_eighths = eighths(locomotion * override_weight);
        const auto rest = eighths(locomotion) > override_eighths ? eighths(locomotion) - override_eighths : 0U;
        const bool upper_leads = upper != lower && blend.blend > 0.5F;
        const auto leading = upper_leads ? upper : lower;
        pose_key.layers[0] = rest > 0U ? layer(leading, upper_leads ? blend.upper : blend.lower, static_cast<float>(rest) / 8.0F) : Layer{};
        pose_key.layers[1] = {override_clip, slot_of(mesh, override_clip, override_time), override_eighths};
    }
    if (aim_eighths > 0)
    {
        pose_key.layers[2] = {aim_clip, slot_of(mesh, aim_clip, state.attack_time), aim_eighths};
    }
    if (fire_eighths > 0)
    {
        pose_key.layers[3] = {state.attack, slot_of(mesh, state.attack, fire_time), fire_eighths};
    }
    return pose_key;
}

void UnitAnimator::aim_turret(const EntityFrame& frame, const std::span<const EntitySnapshot> snapshot, PoseKey& pose_key)
{
    const auto& entity = frame.entity;
    const auto& profile = frame.profile;
    auto& state = frame.state;
    const auto mesh = frame.mesh;
    const auto dt = frame.dt;
    // A turret turns toward the unit's target at its rate, and back to rest without one.
    if (profile.turret_bone != 0)
    {
        pose_key.turret = bone_of(mesh, profile.turret_bone);
        pose_key.barrel = bone_of(mesh, profile.barrel_bone);
    }
    if (pose_key.turret >= 0)
    {
        float want_pitch = 0.0F;
        if (const auto aimed = entity.target.valid() ? snapshot_index_.find(entity_key(entity.target)) : snapshot_index_.end(); aimed != snapshot_index_.end())
        {
            const auto& to = snapshot[aimed->second].position;
            const auto dx = to.x - entity.position.x;
            const auto dy = to.y - entity.position.y;
            want_pitch = std::atan2(to.z - entity.position.z, std::max(std::hypot(dx, dy), 1.0F));
        }
        want_pitch = std::clamp(want_pitch, -profile.turret_pitch_limit, profile.turret_pitch_limit);
        const auto turn = profile.turret_rate * static_cast<float>(dt);
        // The simulation's turret direction: what is drawn is what fires.
        state.turret_yaw = entity.turret_yaw;
        state.barrel_pitch += std::clamp(want_pitch - state.barrel_pitch, -turn, turn);
        pose_key.yaw = static_cast<std::int16_t>(std::lround(state.turret_yaw / turret_yaw_step));
        pose_key.pitch = pose_key.barrel >= 0 ? static_cast<std::int16_t>(std::lround(state.barrel_pitch / barrel_pitch_step)) : std::int16_t{0};
    }
}

} // namespace eaw
