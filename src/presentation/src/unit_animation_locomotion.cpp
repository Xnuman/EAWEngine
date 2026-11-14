// Unit animation, the locomotion stages: an entity's animation state (first sight, the idles it
// plays through, its shown speed) and the locomotion clips by speed with the stride they share.

#include "unit_animation_internal.hpp"

namespace eaw {

using namespace unit_animation_detail;

UnitAnimator::EntityState& UnitAnimator::prepare_state(const EntitySnapshot& entity, const AnimationProfile& profile, const std::uint32_t mesh, const std::uint64_t key, const AnimationRole wanted,
                                                       const double seconds, double& dt)
{
    auto& state = states_[key];
    if (state.frame == 0 || state.mesh != mesh)
    {
        state = {};
        state.mesh = mesh;
        state.idle = choose_clip(mesh, AnimationRole::idle, entity.id);
        if (const auto first = find_clip(mesh, AnimationRole::idle, 0U); profile.loop_first_idle && first >= 0)
        {
            state.idle = first;
        }
        state.walk = choose_clip(mesh, AnimationRole::walk_move, entity.id);
        state.move = choose_clip(mesh, AnimationRole::move, entity.id);
        state.attack = choose_clip(mesh, AnimationRole::attack, entity.id);
        state.attack_idle = choose_clip(mesh, AnimationRole::attack_idle, entity.id);
        state.last_shot = entity.last_shot_tick;
        state.display_speed = std::isfinite(entity.speed) ? entity.speed : 0.0F;
        state.idle_start = seconds - unit_fraction(key, 0x1d1eULL) * 60.0;
        state.stride_phase = unit_fraction(key, 0x57e9ULL);
        state.attack_weight = wanted == AnimationRole::attack && state.attack_idle >= 0 ? 1.0F : 0.0F;
        state.last_seconds = seconds;
    }
    dt = std::clamp(seconds - state.last_seconds, 0.0, longest_frame_seconds);
    state.last_seconds = seconds;
    // An idle that ended is followed by a random IDLE variant (AnimationProfile::loop_first_idle);
    // a unit seen for the first time may be several idles in.
    for (std::size_t played = 0; state.idle >= 0 && !profile.loop_first_idle && played < maximum_idle_switches; ++played)
    {
        const auto length = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(state.idle)].animation)) / static_cast<double>(profile.idle_rate);
        if (length <= 0.0 || seconds - state.idle_start < length)
        {
            break;
        }
        state.idle_start += length;
        state.idle = choose_clip(mesh, AnimationRole::idle, entity.id, ++state.idle_plays * idle_variant_salt);
        if (std::ranges::count(rigs_[mesh].clips, AnimationRole::idle, &VisualClip::role) == 1)
        {
            // The lone idle starts again after a pause, holding its first frame until then.
            const auto pause = unit_fraction(key, state.idle_plays * idle_variant_salt);
            state.idle_start += single_idle_pause_min_seconds + pause * (single_idle_pause_max_seconds - single_idle_pause_min_seconds);
        }
    }
    state.frame = frame_;
    const auto target_speed = std::isfinite(entity.speed) ? std::max(entity.speed, 0.0F) : 0.0F;
    state.display_speed += (target_speed - state.display_speed) * std::min(1.0F, static_cast<float>(dt) / speed_smoothing_seconds);
    // A walk played as clips shapes its speed by its clips: standing is IDLE the moment its stop ends.
    if (entity.walk_clip == WalkClip::standing)
    {
        state.display_speed = 0.0F;
    }
    return state;
}

UnitAnimator::LocomotionPose UnitAnimator::animate_locomotion(const EntityFrame& frame)
{
    const auto& entity = frame.entity;
    const auto& profile = frame.profile;
    auto& state = frame.state;
    const auto mesh = frame.mesh;
    const auto dt = frame.dt;
    // Locomotion: two neighbouring clips by speed, falling back to whatever the mesh has.
    const auto blend = locomotion_blend(state.display_speed, entity.max_speed, state.walk >= 0);
    const auto clip_of = [&](const AnimationRole role)
    {
        switch (role)
        {
        case AnimationRole::walk_move:
            return state.walk;
        case AnimationRole::move:
            if (state.move < 0 && state.walk < 0 && profile.idle_when_moving)
            {
                return state.idle >= 0 ? state.idle : rest_clip;
            }
            return state.move >= 0 ? state.move : state.walk;
        default:
            return state.idle >= 0 ? state.idle : rest_clip;
        }
    };
    const auto lower = clip_of(blend.lower);
    const auto upper = clip_of(blend.upper);
    // The stride phase advances at the blended rate of the moving clips, each sped up or slowed
    // down by how fast the unit goes against the speed the clip stands for: the engine's
    // reference (top speed, or its walk speed) within the playback bounds, rescaled to the
    // type's own reference speed when its XML gives one (AnimationProfile).
    if (entity.max_speed > 0.0F)
    {
        double rate = 0.0;
        const auto stride_rate = [&](const AnimationRole role, const std::int32_t clip, const float weight)
        {
            if (clip < 0 || weight <= 0.0F || role == AnimationRole::idle)
            {
                return;
            }
            const bool walking = role == AnimationRole::walk_move;
            const auto engine_reference = walking ? walk_speed_fraction : 1.0F;
            const auto type_reference = walking ? (profile.walk_reference > 0.0F ? profile.walk_reference : walk_speed_fraction) : profile.move_reference;
            const auto playback = std::clamp(state.display_speed / (entity.max_speed * engine_reference), minimum_playback_rate, maximum_playback_rate) * engine_reference / type_reference;
            rate += static_cast<double>(weight * playback) / static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation));
        };
        stride_rate(blend.lower, lower, 1.0F - blend.blend);
        stride_rate(blend.upper, upper, blend.blend);
        state.stride_phase = std::fmod(state.stride_phase + rate * dt, 1.0);
    }
    return {blend, lower, upper};
}

} // namespace eaw
