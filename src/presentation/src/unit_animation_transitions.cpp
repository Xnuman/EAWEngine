// Unit animation, the transitions stage: walkers' turns in place (BEGIN, loop, END) and their start and
// stop (MOVESTART, MOVE_END), and walks and turns played as clips (EntitySnapshot::walk_clip,
// turn_clip), as a clip that replaces the locomotion.

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
    // Walkers' transitions: turning in place plays the turn loop (after its BEGIN clip, and its END
    // clip once the turn stops, with Has_Pre_Turn_Anim); starting and stopping a walk play
    // MOVESTART and the MOVE_END clip of the stride's quarter (Walk_Transition). A one-shot
    // transition replaces locomotion until it ends.
    std::int32_t override_clip = -1;
    double override_time = 0.0;
    // How much of the locomotion the override takes (transitions fade in and out).
    float override_weight = 1.0F;
    {
        const auto rate = dt > 0.0 ? std::remainder(entity.heading - state.last_heading, two_pi) / static_cast<float>(dt) : 0.0F;
        // A walker with walk transitions moves once its actual speed is past a sliver of its top
        // speed (it speeds up over MOVESTART: battle's apply_animation_locomotion), so its start
        // plays from its first step and its turn toward the way is not taken for a turn in place;
        // other units go by the shown speed.
        const bool slow = profile.walk_transition ? entity.speed < entity.max_speed * walk_start_speed_fraction : state.display_speed < entity.max_speed * turn_in_place_speed_fraction;
        // Other units turn in place while their heading changes fast as they stand; an animation-led
        // walker's turns are the simulation's (turn_clip below).
        const int turning = !profile.animation_led && slow && std::fabs(rate) > turn_in_place_rate ? (rate > 0.0F ? 1 : -1) : 0;
        // Moving by the shown speed, not the order: a walker turning in place before it sets off has
        // not started walking yet, and MOVESTART belongs to when it does.
        const bool moving = !slow;
        // Walk transitions play at the move clip's rate at top speed (the type's reference speed
        // against its top speed), turns at the turn rate against the type's turn reference.
        const auto walk_playback = walk_clip_playback(profile.move_reference);
        const auto turn_playback = turn_clip_playback(rate, profile.turn_reference);
        if (turning != 0)
        {
            state.turn_playback = turn_playback;
        }
        const auto start_transition = [&](const std::int32_t clip, const float playback, const bool starts_walk = false)
        {
            if (clip >= 0)
            {
                state.transition = clip;
                state.transition_start = seconds;
                state.transition_playback = playback;
                state.transition_starts_walk = starts_walk;
                state.fading = -1;
            }
        };
        // A walk played as clips shows the simulation's clip (EntitySnapshot::walk_clip); the
        // start and stop below are for the other walkers.
        const bool planned = entity.walk_clip != WalkClip::unplanned;
        if (state.frame_seen)
        {
            if (!planned && profile.walk_transition && moving && !state.was_moving)
            {
                state.stop_begun = false;
                start_transition(find_clip(mesh, AnimationRole::move_start, 0), walk_playback, true);
            }
            else if (!planned && profile.walk_transition && !state.stop_begun && state.move >= 0 && ((entity.arriving && moving) || (!moving && state.was_moving)))
            {
                // An animation-led walker brakes over its MOVE_END clip (battle's
                // apply_animation_locomotion): its stop begins as braking for the destination does.
                state.stop_begun = true;
                // MOVE_ENDONE starts where MOVE is at 0/4 of its stride, ENDTWO at 1/4, and so on:
                // walk on to the next quarter and stop there.
                state.stopping = true;
                state.stop_phase = state.stride_phase;
                // The stride splits into as many parts as the mesh has MOVE_END clips (four on the
                // AT-AT, three on the AT-ST and pod walkers): MOVE_END n starts at part n - 1.
                state.stop_parts = 0;
                while (state.stop_parts < 4 && find_clip(mesh, AnimationRole::move_end, static_cast<std::uint32_t>(state.stop_parts)) >= 0)
                {
                    ++state.stop_parts;
                }
                state.stop_parts = std::max(state.stop_parts, 1);
                state.stop_part = std::clamp(static_cast<int>(std::floor(state.stride_phase * static_cast<double>(state.stop_parts))) + 1, 1, state.stop_parts);
                // Braking from below top speed takes less long: the stop clip plays faster to match.
                state.stop_braking = entity.arriving;
            }
            else if (profile.pre_turn && turning != 0 && state.turning == 0)
            {
                start_transition(find_clip(mesh, turning > 0 ? AnimationRole::turn_left_begin : AnimationRole::turn_right_begin, 0), state.turn_playback);
            }
            else if (profile.pre_turn && turning == 0 && state.turning != 0)
            {
                start_transition(find_clip(mesh, state.turning > 0 ? AnimationRole::turn_left_end : AnimationRole::turn_right_end, 0), state.turn_playback);
            }
        }
        // A stop under way ends when the walker moves on without braking (a new order) or another
        // transition than its start begins.
        const bool starting = state.transition >= 0 && state.transition_starts_walk;
        if (state.stopping && ((moving && !entity.arriving) || (state.transition >= 0 && !starting)))
        {
            state.stopping = false;
            state.stop_begun = state.transition >= 0;
        }
        if (state.stopping && starting)
        {
            // Braking began while MOVESTART still plays (a short walk): MOVESTART ends where MOVE
            // starts, which is where MOVE_ENDONE starts, so the stop follows it at once.
            state.stop_phase = 0.0;
            state.stop_part = 0;
        }
        else if (state.stopping)
        {
            // The stride goes on at the move clip's rate at top speed until it reaches the quarter.
            const auto move_duration = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(state.move)].animation));
            const auto target = static_cast<double>(state.stop_part) / static_cast<double>(state.stop_parts);
            state.stop_phase += move_duration > 0.0 ? dt * static_cast<double>(walk_playback) / move_duration : 1.0;
            if (state.stop_phase >= target)
            {
                state.stopping = false;
                state.stride_phase = std::fmod(target, 1.0);
                start_transition(find_clip(mesh, AnimationRole::move_end, static_cast<std::uint32_t>(state.stop_part % state.stop_parts)),
                                 // Braking from below top speed takes less long: measured as END starts (after the
                                 // walk-on to the quarter), END plays faster to end with it.
                                 state.stop_braking && entity.speed > 0.0F ? walk_playback * std::clamp(entity.max_speed / entity.speed, 1.0F, 4.0F) : walk_playback);
            }
            else
            {
                override_clip = state.move;
                override_time = state.stop_phase * move_duration;
                override_weight = 1.0F;
            }
        }
        state.turning = turning;
        state.was_moving = moving;
        state.last_heading = entity.heading;
        state.frame_seen = true;
        if (state.transition >= 0)
        {
            const auto elapsed = (seconds - state.transition_start) * static_cast<double>(state.transition_playback);
            const auto duration = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(state.transition)].animation));
            if (elapsed < duration)
            {
                override_clip = state.transition;
                override_time = elapsed;
                override_weight = transition_fade_seconds > 0.0 ? static_cast<float>(std::clamp((seconds - state.transition_start) / std::max(transition_fade_seconds, 1.0e-9), 0.0, 1.0)) : 1.0F;
                // MOVE goes on from where MOVESTART ends: its first frame.
                if (state.transition_starts_walk)
                {
                    state.stride_phase = 0.0;
                }
            }
            else
            {
                state.fading = state.transition;
                state.fading_since = state.transition_start + duration / static_cast<double>(state.transition_playback);
                state.transition = -1;
                // IDLE goes on from its first frame where a transition (MOVE_END) leaves it.
                state.idle_start = state.fading_since;
            }
        }
        if (planned)
        {
            // The first variants: the simulation timed the walk by them (battle's
            // apply_animation_locomotion), and their joins are the measured ones.
            const auto move = find_clip(mesh, AnimationRole::move, 0);
            const auto clip = entity.walk_clip == WalkClip::starting   ? find_clip(mesh, AnimationRole::move_start, 0)
                              : entity.walk_clip == WalkClip::moving   ? (move >= 0 ? move : state.move)
                              : entity.walk_clip == WalkClip::stopping ? find_clip(mesh, AnimationRole::move_end, entity.walk_part)
                                                                       : no_clip;
            if (clip >= 0)
            {
                override_clip = clip;
                override_time = static_cast<double>(entity.walk_progress) * static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation));
                override_weight = 1.0F;
                state.transition = -1;
                state.fading = -1;
                if (entity.walk_clip == WalkClip::moving)
                {
                    state.stride_phase = entity.walk_progress;
                }
            }
            // MOVE_END ends in IDLE_00's first frame: IDLE_00 goes on from there.
            if (entity.walk_clip == WalkClip::standing && state.walk_clip == WalkClip::stopping)
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
                override_clip = clip;
                override_time = static_cast<double>(entity.turn_progress) * static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(clip)].animation));
                override_weight = 1.0F;
                state.transition = -1;
                state.fading = -1;
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
        // An ended transition holds its last frame while the next pose fades in.
        if (override_clip < 0 && state.fading >= 0)
        {
            const auto since = seconds - state.fading_since;
            if (since < transition_fade_seconds && turning == 0)
            {
                const auto duration = static_cast<double>(animation_duration(*rigs_[mesh].clips[static_cast<std::size_t>(state.fading)].animation));
                override_clip = state.fading;
                override_time = duration * 0.9999;
                override_weight = static_cast<float>(1.0 - std::clamp(since / std::max(transition_fade_seconds, 1.0e-9), 0.0, 1.0));
            }
            else
            {
                state.fading = -1;
            }
        }
        if (override_clip < 0 && turning != 0 && !state.stopping)
        {
            const auto loop = find_clip(mesh, turning > 0 ? AnimationRole::turn_left : AnimationRole::turn_right, 0);
            if (loop >= 0)
            {
                // TURN_BEGIN ends where the loop starts: the loop goes on from its first frame.
                state.turn_time = state.turn_looping ? state.turn_time + dt * static_cast<double>(state.turn_playback) : 0.0;
                override_clip = loop;
                override_time = state.turn_time;
                override_weight = 1.0F;
            }
        }
        state.turn_looping = override_clip >= 0 && override_clip == find_clip(mesh, turning > 0 ? AnimationRole::turn_left : AnimationRole::turn_right, 0) && turning != 0;
    }
    return {override_clip, override_time, override_weight};
}

} // namespace eaw
