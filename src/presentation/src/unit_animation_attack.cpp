// Unit animation, the attack stage: aiming (ATTACKIDLE) while standing with a target, ATTACK with
// every shot, and how much of the pose they take from the locomotion.

#include "unit_animation_internal.hpp"

namespace eaw {

using namespace unit_animation_detail;

UnitAnimator::AttackPose UnitAnimator::animate_attack(const EntityFrame& frame)
{
    const auto& entity = frame.entity;
    const auto& profile = frame.profile;
    auto& state = frame.state;
    const auto mesh = frame.mesh;
    const auto wanted = frame.wanted;
    const auto dt = frame.dt;
    // Aiming (ATTACKIDLE) fades in while standing with a target, and out otherwise. A mesh without
    // it shows no aiming pose: its ATTACK belongs to the shots (the T4-B's gun recoil looped
    // while it had a target, in range or not; the owner's report).
    const auto aim_clip = state.attack_idle;
    const auto attack_target = wanted == AnimationRole::attack && aim_clip >= 0 ? 1.0F : 0.0F;
    const auto fade = static_cast<float>(dt) / attack_fade_seconds;
    state.attack_weight = attack_target > state.attack_weight ? std::min(attack_target, state.attack_weight + fade) : std::max(attack_target, state.attack_weight - fade);
    state.attack_time = state.attack_weight > 0.0F ? state.attack_time + dt : 0.0;
    // Each new shot restarts ATTACK.
    const bool new_shot = entity.last_shot_tick > state.last_shot;
    if (new_shot)
    {
        state.last_shot = entity.last_shot_tick;
        state.since_shot = 0.0;
    }
    else
    {
        state.since_shot += dt;
    }
    float fire = 0.0F;
    double fire_time = 0.0;
    if (state.attack >= 0)
    {
        const auto duration = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(state.attack)].animation));
        fire = state.since_shot < duration ? 1.0F : static_cast<float>(std::clamp(1.0 - (state.since_shot - duration) / shot_fade_seconds, 0.0, 1.0));
        fire_time = std::min(state.since_shot, duration * 0.9999);
    }
    const auto fire_eighths = eighths(fire);
    const auto aim_eighths = std::min(eighths(state.attack_weight * (1.0F - fire)), 8U - fire_eighths);
    // An overlay attack leaves the locomotion its whole weight.
    const auto locomotion = profile.attack_overlay ? 1.0F : static_cast<float>(8U - fire_eighths - aim_eighths) / 8.0F;
    return {aim_clip, fire, fire_time, new_shot, fire_eighths, aim_eighths, locomotion};
}

} // namespace eaw
