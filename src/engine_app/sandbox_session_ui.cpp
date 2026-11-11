#include "sandbox_session.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace eaw {

void SandboxSession::draw_panels()
{
    eaw::draw_sandbox_menu(panels, show_all_bounds, show_passability);
    eaw::SpawnPanelView view;
    view.names = spawn_names;
    view.loaded = [this](const std::string& name)
    {
        return std::ranges::any_of(spawnable,
                                   [&](const eaw::SpawnableType& type)
                                   {
                                       return ascii_lower(type.type_name) == ascii_lower(name);
                                   });
    };
    view.choice = &spawn_choice;
    view.count = &spawn_count;
    view.owner = &spawn_owner;
    view.neutral_owner = eaw::neutral_map_owner;
    view.can_spawn = world != nullptr && tactical_camera.has_value() && !spawn_names.empty();
    if (pending_package.valid())
    {
        view.status = "Loading " + building_type + "...";
    }
    else if (!pending_spawns.empty())
    {
        view.status = std::to_string(pending_spawns.size()) + " spawns waiting";
    }
    const auto actions = eaw::draw_spawn_panel(panels, view);
    panel_actions.spawn_at_view_center = panel_actions.spawn_at_view_center || actions.spawn_at_view_center;
    static constexpr std::array<std::string_view, eaw::audio_category_count> category_names{"Effects", "Voices", "Music", "Interface"};
    eaw::SoundPanelView sound;
    sound.master = &master_volume;
    sound.categories = category_volumes;
    sound.category_names = category_names;
    sound.status = std::string(audio_device->name()) + ": " + std::to_string(audio_device->active_voices()) + " voices";
    if (audio)
    {
        const auto& stats = audio->stats();
        sound.status += ", " + std::to_string(stats.played) + " played (" + std::to_string(stats.from_animations) + " by animations), " + std::to_string(stats.replaced) + " replaced, " +
                        std::to_string(stats.skipped) + " skipped, " + std::to_string(stats.missing) + " missing";
    }
    volumes_changed = eaw::draw_sound_panel(panels, sound) || volumes_changed;
}

void SandboxSession::apply_panel_actions()
{
    if (std::exchange(volumes_changed, false))
    {
        audio_device->set_master_volume(master_volume);
        for (std::size_t index = 0; index < category_volumes.size(); ++index)
        {
            audio_device->set_category_volume(static_cast<eaw::AudioCategory>(index), category_volumes[index]);
        }
    }
    if (std::exchange(panel_actions.spawn_at_view_center, false) && spawn_choice < spawn_names.size() && world && tactical_camera)
    {
        const auto stats = renderer.stats();
        if (const auto center = ground_target(static_cast<float>(stats.width) * 0.5F, static_cast<float>(stats.height) * 0.5F))
        {
            spawn_named(spawn_names[spawn_choice], *center, spawn_count);
        }
    }
}

void SandboxSession::update_debug_lines()
{
    eaw::SandboxDebugView view;
    view.presentation = presentation ? &*presentation : nullptr;
    view.world = world;
    view.instances = frame_instances;
    view.entities = frame_entities;
    view.selection = &selection;
    // The simulation's ground shows structure footprints stamped into its passability.
    view.ground = world ? &world->ground() : &terrain_heights;
    if (tactical_camera)
    {
        view.focus = tactical_camera->focus();
    }
    view.show_bounds = show_all_bounds;
    view.show_passability = show_passability;
    view.axis_length = axis_length;
    view.friendly_owner = spawn_owner;
    view.projectile_visuals = effects.bolts();
    renderer.set_debug_lines(eaw::build_sandbox_debug_lines(view));
}

void SandboxSession::update_overlay(const std::chrono::steady_clock::time_point now)
{
    eaw::SandboxOverlayView view;
    view.adapter_name = renderer.adapter_name();
    view.stats = renderer.stats();
    view.vsync = options.vsync;
    view.frame_latency = options.frame_latency;
    view.raw_input = window.input().raw_input;
    view.raw_events = window.input().raw_events;
    const auto particle_stats = effects.particles().stats();
    view.paused = world_paused;
    view.particle_effects = particle_stats.instances;
    view.particles = particle_stats.particles;
    view.attached_effects = effects.attached_playing();
    view.dropped_particles = particle_stats.dropped_particles;
    view.skipped_emitters = particle_stats.skipped_emitters;
    view.frames = overlay_frames;
    view.seconds = std::chrono::duration<double>(now - overlay_refreshed).count();
    view.scene_status = scene_status;
    view.eye = tactical_camera ? tactical_camera->camera().eye : orbit_camera->camera().eye;
    if (visual_model)
    {
        view.animation_status = animation_status;
    }
    else if (animator)
    {
        const auto& animation = animator->stats();
        view.animation_status = std::to_string(animation.animated) + " units animated (" + std::to_string(animation.blending) + " blending), " + std::to_string(animation.corpses) + " corpses, " +
                                std::to_string(animation.poses) + " poses (" + std::to_string(animation.sampled) + " sampled this frame), palette " + std::to_string(animation.palette_entries) +
                                (animation.over_budget > 0 ? ", " + std::to_string(animation.over_budget) + " in rest pose (palette full)" : std::string{});
    }
    view.animation_playing = animation_playing;
    std::string selected_text = "none";
    if (selection.size() == 1)
    {
        for (std::size_t index = 0; index < frame_entities.size(); ++index)
        {
            if (frame_entities[index].valid() && selection.contains(eaw::entity_key(frame_entities[index])))
            {
                selected_text = describe_instance(index);
                break;
            }
        }
    }
    else if (!selection.empty())
    {
        selected_text = std::to_string(selection.size()) + " objects";
    }
    view.selected_text = selected_text;
    view.world = world;
    view.selection = &selection;
    view.dropped_ticks = simulation_clock.dropped_ticks();
    view.last_tick_ms = last_tick_ms;
    view.slowest_tick_ms = slowest_tick_ms;
    view.orders_issued = orders_issued;
    if (spawn_choice < spawn_names.size())
    {
        view.spawn_type = spawn_names[spawn_choice] + (building_type.empty() || !pending_package.valid() ? std::string{} : " (loading " + building_type + ")");
    }
    view.spawn_types = spawn_names.size();
    view.spawn_owner = spawn_owner;
    view.reload_status = reload_status;
    view.camera = !tactical_camera ? eaw::SandboxCameraMode::model : free_camera ? eaw::SandboxCameraMode::free : eaw::SandboxCameraMode::rts;
    renderer.set_overlay_text(eaw::sandbox_overlay_lines(view));
    overlay_frames = 0;
    overlay_refreshed = now;
}

void SandboxSession::update_panel(const eaw::InputState& input, const std::chrono::steady_clock::time_point now)
{
    if (input.keys[static_cast<std::size_t>(eaw::Key::f1)].pressed)
    {
        panels.menu = !panels.menu;
    }
    if (!panels.statistics && statistics_shown)
    {
        renderer.set_overlay_text({});
    }
    // Shown now: refresh at once instead of waiting for the next quarter second.
    if (panels.statistics && !statistics_shown)
    {
        overlay_refreshed = now - std::chrono::milliseconds(250);
    }
    statistics_shown = panels.statistics;
    // The panel is refreshed four times a second so the numbers stay readable.
    ++overlay_frames;
    if (panels.statistics && now - overlay_refreshed >= std::chrono::milliseconds(250))
    {
        update_overlay(now);
    }
}

} // namespace eaw
