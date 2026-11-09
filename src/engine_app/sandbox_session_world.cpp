#include "sandbox_session.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>

namespace eaw {

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
        const auto ticks = world_paused ? 0U : simulation_clock.advance(std::chrono::duration<double>(now - simulation_tick).count());
        simulation_tick = now;
        const auto world_seconds = std::chrono::duration<double>(world_now(now) - map_animation_start).count();
        for (std::uint32_t tick = 0; tick < ticks; ++tick)
        {
            previous_snapshot = world->snapshot();
            watchdog.enter("simulation tick", world->stats().tick);
            const auto tick_start = std::chrono::steady_clock::now();
            world->step();
            last_tick_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick_start).count();
            slowest_tick_ms = std::max(slowest_tick_ms, last_tick_ms);
            // Every tick's deaths and impacts, so none is lost when a frame runs several ticks.
            for (const auto& death : world->snapshot().deaths)
            {
                if (const auto corpse = presentation->corpse_instance(death); corpse && animator)
                {
                    animator->add_corpse(corpse->instance, death.id, world_seconds, corpse->style);
                }
            }
            effects.play_tick_events(world->snapshot());
        }
        const auto entities = eaw::interpolate_snapshots(previous_snapshot, world->snapshot(), simulation_clock.interpolation());
        watchdog.enter("presentation", world->stats().tick);
        presentation->build(entities, selection, frame_instances, frame_entities);
        // Model projectiles follow the entities, without an entity (never picked) and at rest pose.
        const auto first_projectile = frame_instances.size();
        const auto interpolation = static_cast<float>(simulation_clock.interpolation());
        const auto tick_seconds = static_cast<float>(simulation_clock.tick_seconds());
        frame_projectile_serials.clear();
        eaw::append_projectile_instances(world->snapshot().projectiles, effects.projectile_visuals(), interpolation, tick_seconds, frame_instances, frame_projectile_serials);
        frame_entities.resize(frame_instances.size());
        if (animator)
        {
            watchdog.enter("unit animation", world->stats().tick);
            // The palette goes first: set_instances checks bone ranges against it.
            animator->animate(entities, frame_entities, frame_instances, world_seconds, eaw::D3d12Renderer::max_bone_transforms(), frame_palette);
            renderer.set_bone_transforms(frame_palette);
            effects.attach(frame_instances, frame_entities, frame_palette, entities, world_seconds);
            effects.follow_projectiles(std::span(frame_instances).subspan(first_projectile), frame_projectile_serials, frame_palette, world->snapshot().projectiles, interpolation, tick_seconds);
        }
        for (const auto request : unbound_picks)
        {
            pick_entities[request] = frame_entities;
        }
        unbound_picks.clear();
        renderer.set_instances(frame_instances);
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
            auto transform = eaw::Matrix4{};
            eaw::set_translation(transform, effect_center);
            shown_effect_ids[index] = effects.particles().play(shown_effects[index], transform, shown_effect_seed++);
        }
    }
    effects.update(seconds);
    frame_particles.clear();
    const auto eye = tactical_camera ? tactical_camera->camera().eye : orbit_camera ? orbit_camera->camera().eye : Vector3{};
    const auto projectiles = world != nullptr ? std::span<const eaw::ProjectileSnapshot>(world->snapshot().projectiles) : std::span<const eaw::ProjectileSnapshot>{};
    effects.build_sprites(eye, projectiles, static_cast<float>(simulation_clock.interpolation()), static_cast<float>(simulation_clock.tick_seconds()), frame_particles);
    renderer.set_particles(frame_particles);
}

} // namespace eaw
