#include "core/string_utils.hpp"
#include "engine_app/sandbox_session.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace eaw::engine_app {

void SandboxSession::draw_panels()
{
    draw_sandbox_bar(panels, show_all_bounds, show_passability);
    draw_statistics_panel(panels, statistics_lines);
    auto mode = camera_mode;
    CameraPanelView camera_view{&mode, tactical_camera.has_value(), &fly_speed};
    if (draw_camera_panel(panels, camera_view) && mode != camera_mode)
    {
        set_camera_mode(mode);
    }
    SpawnPanelView view;
    view.names = spawn_names;
    if (spawn_types.size() == spawn_names.size())
    {
        view.types = spawn_types;
    }
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
    const auto actions = draw_spawn_panel(panels, view);
    panel_actions.spawn_at_view_center = panel_actions.spawn_at_view_center || actions.spawn_at_view_center;
    static constexpr std::array<std::string_view, eaw::audio_category_count> category_names{"Effects", "Voices", "Music", "Interface"};
    SoundPanelView sound;
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
    volumes_changed = draw_sound_panel(panels, sound) || volumes_changed;
    graphics_changed = draw_graphics_panel(panels, {&graphics_settings, &lod_settings, &show_decals}) || graphics_changed;
    if (draw_controls_panel(panels, {&bindings, &rebinding}))
    {
        save_bindings();
    }
}

void SandboxSession::apply_panel_actions()
{
    if (std::exchange(graphics_changed, false))
    {
        renderer.set_graphics_settings(graphics_settings);
        renderer.set_lod_settings(lod_settings);
    }
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
    view.instances = battle_frame.instances;
    view.entities = battle_frame.entities;
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
    SandboxOverlayView view;
    view.adapter_name = renderer.adapter_name();
    view.stats = renderer.stats();
    view.vsync = options.vsync;
    view.frame_latency = options.frame_latency;
    const auto particle_stats = effects.particles().stats();
    view.particle_effects = particle_stats.instances;
    view.particles = particle_stats.particles;
    view.attached_effects = effects.attached_playing();
    view.dropped_particles = particle_stats.dropped_particles;
    view.skipped_emitters = particle_stats.skipped_emitters;
    view.frames = overlay_frames;
    view.seconds = std::chrono::duration<double>(now - overlay_refreshed).count();
    statistics_lines = sandbox_overlay_lines(view);
    overlay_frames = 0;
    overlay_refreshed = now;
}

void SandboxSession::update_panel(const eaw::InputState& input, const std::chrono::steady_clock::time_point now)
{
    if (input.keys[static_cast<std::size_t>(eaw::Key::f1)].pressed)
    {
        panels.bar = !panels.bar;
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

} // namespace eaw::engine_app
