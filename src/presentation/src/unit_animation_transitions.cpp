// Unit animation, the transitions stage: walks and turns played as clips (EntitySnapshot::walk_clip,
// turn_clip: the simulation plans them for walkers with transition clips), and the turn loop of
// units whose turns are not planned, as a clip that replaces the locomotion.

#include "unit_animation_internal.hpp"

namespace eaw {

using namespace unit_animation_detail;

UnitAnimator::ClipOverride UnitAnimator::animate_transitions(const EntityFrame& frame)
{
    const auto& entity = frame.entity;
    const auto& profile = frame.profile;
    auto& state = frame.state;
    const auto mesh = frame.mesh;
    const auto seconds = frame.seconds;
    const auto dt = frame.dt;
    ClipOverride replaced;
    const auto clip_time = [&](const std::int32_t clip, const float progress)
    {
        return static_cast<double>(progress) * static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation));
    };
    // A unit whose turns are not planned (its only turn clips are the loops: infantry, the
    // Juggernaut) plays its turn loop while its heading changes fast as it stands, at the turn rate
    // against its type's turn reference.
    {
        const auto rate = dt > 0.0 ? std::remainder(entity.heading - state.last_heading, two_pi) / static_cast<float>(dt) : 0.0F;
        const bool slow = state.display_speed < entity.max_speed * turn_in_place_speed_fraction;
        const int turning = !entity.turns_as_clips && slow && std::fabs(rate) > turn_in_place_rate ? (rate > 0.0F ? 1 : -1) : 0;
        if (const auto loop = turning != 0 ? find_clip(mesh, turning > 0 ? AnimationRole::turn_left : AnimationRole::turn_right, 0) : no_clip; loop >= 0)
        {
            state.turn_time = state.turning == turning ? state.turn_time + dt * static_cast<double>(turn_clip_playback(rate, profile.turn_reference)) : 0.0;
            replaced = {loop, state.turn_time, 1.0F};
        }
        state.turning = turning;
        state.last_heading = entity.heading;
    }
    // A walk played as clips shows the simulation's clip, its first variant: the simulation timed the
    // walk by them (battle's apply_animation_locomotion), and their joins are the measured ones.
    if (entity.walk_clip != WalkClip::unplanned)
    {
        const auto move = find_clip(mesh, AnimationRole::move, 0);
        // A raised leg shows IDLE_LEGLIFT going up (its last frame while it stays up) and IDLE_LEGDROP.
        const auto clip = entity.walk_clip == WalkClip::starting                                          ? find_clip(mesh, AnimationRole::move_start, 0)
                          : entity.walk_clip == WalkClip::moving                                          ? (move >= 0 ? move : state.move)
                          : entity.walk_clip == WalkClip::stopping                                        ? find_clip(mesh, AnimationRole::move_end, entity.walk_part)
                          : entity.walk_clip == WalkClip::raising || entity.walk_clip == WalkClip::raised ? find_clip(mesh, AnimationRole::idle_leg_lift, 0)
                          : entity.walk_clip == WalkClip::lowering                                        ? find_clip(mesh, AnimationRole::idle_leg_drop, 0)
                                                                                                          : no_clip;
        if (clip >= 0)
        {
            // The last frame of a held clip is sampled just before its end.
            replaced = {clip, clip_time(clip, std::min(entity.walk_progress, 0.9999F)), 1.0F};
            if (entity.walk_clip == WalkClip::moving)
            {
                state.stride_phase = entity.walk_progress;
            }
        }
        // MOVE_END and IDLE_LEGDROP end in IDLE_00's first frame: IDLE_00 goes on from there.
        if (entity.walk_clip == WalkClip::standing && (state.walk_clip == WalkClip::stopping || state.walk_clip == WalkClip::lowering))
        {
            state.idle_start = seconds;
            if (const auto first = find_clip(mesh, AnimationRole::idle, 0); first >= 0)
            {
                state.idle = first;
            }
        }
        state.walk_clip = entity.walk_clip;
    }
    // A turn played as clips shows the simulation's clip, its first variant.
    if (entity.turn_clip != TurnClip::none)
    {
        const auto left = entity.turn_left;
        AnimationRole role = AnimationRole::turn_left;
        switch (entity.turn_clip)
        {
        case TurnClip::begin:
            role = left ? AnimationRole::turn_left_begin : AnimationRole::turn_right_begin;
            break;
        case TurnClip::half:
            role = left ? AnimationRole::turn_left_half : AnimationRole::turn_right_half;
            break;
        case TurnClip::quarter:
            role = left ? AnimationRole::turn_left_quarter : AnimationRole::turn_right_quarter;
            break;
        case TurnClip::end:
            role = left ? AnimationRole::turn_left_end : AnimationRole::turn_right_end;
            break;
        case TurnClip::none:
        case TurnClip::loop:
            role = left ? AnimationRole::turn_left : AnimationRole::turn_right;
            break;
        }
        if (const auto clip = find_clip(mesh, role, 0); clip >= 0)
        {
            replaced = {clip, clip_time(clip, entity.turn_progress), 1.0F};
        }
    }
    // A turn's END ends in IDLE_00's first frame: IDLE_00 goes on from there.
    if (entity.turn_clip == TurnClip::none && state.turn_clip == TurnClip::end)
    {
        state.idle_start = seconds;
        if (const auto first = find_clip(mesh, AnimationRole::idle, 0); first >= 0)
        {
            state.idle = first;
        }
    }
    state.turn_clip = entity.turn_clip;
    return replaced;
}

} // namespace eaw
