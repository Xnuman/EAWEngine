#include "engine_app/sandbox_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>

namespace eaw::engine_app {

namespace {

// Ticks a frame a replay runs toward its end when it is to be captured: the end of a long log in a
// few dozen frames, each still presented (an engine choice).
constexpr std::uint32_t replay_capture_ticks_per_frame = 240;

} // namespace

std::chrono::steady_clock::time_point SandboxSession::world_now(const std::chrono::steady_clock::time_point now) const
{
    return world_paused ? paused_since : now;
}

void SandboxSession::toggle_world_pause()
{
    // A finished replay stays at its end: running on would be a battle the log never had.
    if (replay_done)
    {
        std::cout << "Replay: ended at its last tick; it does not run on\n";
        return;
    }
    const auto now = presentation_now();
    if (!world_paused)
    {
        world_paused = true;
        paused_since = now;
    }
    else
    {
        const auto span = now - paused_since;
        map_animation_start += span;
        scene_start += span;
        world_paused = false;
    }
    std::cout << (world_paused ? "World paused" : "World resumed") << '\n';
}

void SandboxSession::advance_animation(const std::chrono::steady_clock::time_point now)
{
    const auto animation_delta = std::chrono::duration<float>(now - animation_tick).count();
    animation_tick = now;
    if (animation_playing)
    {
        animation_time += animation_delta;
    }
    if (visual_model && visual_animation)
    {
        const auto skin_palette = eaw::sample_skin_palette(*visual_model, &*visual_animation, animation_time);
        renderer.set_bone_transforms(skin_palette);
    }
    renderer.set_scene_time(std::chrono::duration<float>(world_now(now) - scene_start).count());
}

void SandboxSession::step_simulation(const std::chrono::steady_clock::time_point now)
{
    if (world && presentation)
    {
        eaw::BattlePresenters presenters{*presentation, animator ? &*animator : nullptr, effects, audio ? &*audio : nullptr, &jobs};
        auto ticks = world_paused ? 0U : runtime->ticks_due(std::chrono::duration<double>(now - simulation_tick).count());
        if (replay_log && !replay_done && options.capture)
        {
            // A replay's capture shows its end: run there fast (an engine choice of ticks a frame).
            ticks = std::max(ticks, replay_capture_ticks_per_frame);
        }
        simulation_tick = now;
        const auto world_seconds = std::chrono::duration<double>(world_now(now) - map_animation_start).count();
        for (std::uint32_t tick = 0; tick < ticks; ++tick)
        {
            if (replay_log && replay_done)
            {
                break;
            }
            previous_snapshot = world->snapshot();
            watchdog.enter("simulation tick", world->stats().tick);
            if (replay_log)
            {
                feed_replay();
            }
            runtime->step();
            last_tick_ms = runtime->last_step_milliseconds();
            slowest_tick_ms = std::max(slowest_tick_ms, last_tick_ms);
            // Every tick's events, so none is lost when a frame runs several ticks.
            eaw::present_tick(*runtime, presenters, world_seconds);
            finish_replay();
        }
        watchdog.enter("presentation", world->stats().tick);
        eaw::build_battle_frame(*runtime, previous_snapshot, selection, world_seconds, eaw::D3d12Renderer::max_bone_transforms(), presenters, battle_frame);
        if (animator)
        {
            // The palette goes first: set_instances checks bone ranges against it.
            renderer.set_bone_transforms(battle_frame.palette);
        }
        for (const auto request : unbound_picks)
        {
            pick_entities[request] = battle_frame.entities;
        }
        unbound_picks.clear();
        renderer.set_instances(battle_frame.instances);
        update_debug_lines();
    }
}

bool SandboxSession::give_order(const eaw::BattleCommand& command)
{
    if (replay_log)
    {
        // A replay's orders are the log's: the player's would make it another battle.
        if (!replay_orders_refused)
        {
            replay_orders_refused = true;
            std::cout << "Replay: orders and spawns are disabled while a log replays\n";
        }
        return false;
    }
    if (const auto submitted = runtime->battle().submit(command); !submitted)
    {
        eaw::log_warning("simulation", submitted.error());
        return false;
    }
    return true;
}

void SandboxSession::feed_replay()
{
    const auto tick = world->stats().tick;
    for (; replay_next < replay_log->entries.size() && replay_log->entries[replay_next].tick <= tick; ++replay_next)
    {
        if (const auto submitted = runtime->battle().submit(replay_log->entries[replay_next].command); !submitted)
        {
            eaw::log_warning("replay", eaw::Error{"the order of tick " + std::to_string(replay_log->entries[replay_next].tick) + " was rejected: " + submitted.error().message});
        }
    }
}

void SandboxSession::finish_replay()
{
    if (!replay_log || replay_done || !replay_log->end_tick || world->stats().tick < *replay_log->end_tick)
    {
        return;
    }
    replay_done = true;
    world_paused = true;
    paused_since = std::chrono::steady_clock::now();
    const auto hash = world->state_hash();
    const bool same = replay_log->end_hash && *replay_log->end_hash == hash;
    std::cout << "Replay: reached tick " << world->stats().tick << "; state hash " << eaw::format_hex(hash)
              << (same ? ", as recorded" : ", the recording's " + eaw::format_hex(replay_log->end_hash.value_or(0))) << " (paused at the end)\n";
}

void SandboxSession::step_particles(const std::chrono::steady_clock::time_point now)
{
    const auto seconds = world_paused ? 0.0F : std::chrono::duration<float>(now - particle_tick).count();
    particle_tick = now;
    for (std::size_t index = 0; index < shown_effects.size(); ++index)
    {
        if (shown_effects[index] && !effects.particles().playing(shown_effect_ids[index]))
        {
            auto transform = eaw::Matrix4::identity();
            eaw::set_translation(transform, effect_center);
            shown_effect_ids[index] = effects.particles().play(shown_effects[index], transform, shown_effect_seed++);
        }
    }
    if (weather.active() && (tactical_camera || orbit_camera))
    {
        const auto camera = current_camera();
        weather.update(effects.particles(), seconds, camera.eye, camera.target);
        renderer.set_lighting(eaw::lightning_rig(base_lighting, weather.flash()));
    }
    effects.update(seconds);
    if (audio && world && tactical_camera)
    {
        audio->update(current_camera(), world->snapshot(), *world, seconds);
    }
    frame_particles.clear();
    const auto eye = tactical_camera || orbit_camera ? current_camera().eye : Vector3{};
    const auto projectiles = world != nullptr ? std::span<const eaw::ProjectileSnapshot>(world->snapshot().projectiles) : std::span<const eaw::ProjectileSnapshot>{};
    // Without a battle there are no shots to place between ticks.
    effects.build_sprites(eye, projectiles, runtime ? runtime->interpolation() : 0.0F, runtime ? static_cast<float>(runtime->tick_seconds()) : 0.0F, frame_particles);
    renderer.set_particles(frame_particles);
    frame_decals.clear();
    if (show_decals)
    {
        effects.build_decals(frame_decals);
    }
    renderer.set_decals(frame_decals);
}

} // namespace eaw::engine_app
