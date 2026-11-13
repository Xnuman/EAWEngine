#include "engine_app/sandbox_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>

namespace eaw::engine_app {

std::chrono::steady_clock::time_point SandboxSession::world_now(const std::chrono::steady_clock::time_point now) const
{
    return world_paused ? paused_since : now;
}

void SandboxSession::toggle_world_pause()
{
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
        eaw::BattlePresenters presenters{*presentation, animator ? &*animator : nullptr, effects, audio ? &*audio : nullptr};
        const auto ticks = world_paused ? 0U : runtime->ticks_due(std::chrono::duration<double>(now - simulation_tick).count());
        simulation_tick = now;
        const auto world_seconds = std::chrono::duration<double>(world_now(now) - map_animation_start).count();
        for (std::uint32_t tick = 0; tick < ticks; ++tick)
        {
            previous_snapshot = world->snapshot();
            watchdog.enter("simulation tick", world->stats().tick);
            runtime->step();
            last_tick_ms = runtime->last_step_milliseconds();
            slowest_tick_ms = std::max(slowest_tick_ms, last_tick_ms);
            // Every tick's events, so none is lost when a frame runs several ticks.
            eaw::present_tick(*runtime, presenters, world_seconds);
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
