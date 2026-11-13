#include "engine_app/sandbox_app.hpp"

#include "core/file_io.hpp"
#include "formats/png_image.hpp"
#include "audio/audio_devices.hpp"
#include "audio/null_audio_device.hpp"
#include "platform/log_sinks.hpp"
#include "engine_app/sandbox_session.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace eaw::engine_app {

SandboxSession::SandboxSession(const eaw::SandboxOptions& session_options, eaw::VirtualFileSystem& session_vfs, eaw::PlatformWindow& session_window, eaw::D3d12Renderer& session_renderer)
    : options(session_options), vfs(session_vfs), window(session_window), renderer(session_renderer)
{
    watchdog.set_threshold(options.use_warp || options.gpu_validation ? std::chrono::seconds(60) : std::chrono::seconds(5));
    if (options.fixed_frame_time || options.capture)
    {
        fixed_frame_step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(options.fixed_frame_time.value_or(1.0F / 30.0F)));
        // Every presentation timestamp starts from the same instant.
        particle_tick = simulation_tick = animation_tick = scene_start = camera_tick = map_animation_start = virtual_now;
    }
    renderer.set_overlay_ui(
        [this]
        {
            draw_panels();
        });
    if (auto device = eaw::create_audio_device(options.audio))
    {
        audio_device = std::move(*device);
    }
    else
    {
        eaw::log_warning("audio", device.error());
        audio_device = std::make_unique<eaw::NullAudioDevice>();
    }
    audio_device->set_master_volume(master_volume);
    std::cout << "Audio: " << audio_device->name() << ", volume " << eaw::format_fixed(master_volume, 2) << '\n';
    if (!options.validation_triangle)
    {
        pending_content = std::async(std::launch::async, eaw::build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, false, std::vector<std::string>{});
        std::cout << "Building " << (options.map ? "map " + *options.map : "model " + options.model) << " in the background...\n";
    }
    if (options.validation_triangle)
    {
        content_shown_at = 0;
    }
    renderer.set_camera(orbit_camera->camera());
    std::cout << "D3D12 adapter: " << renderer.adapter_name() << '\n'
              << "Present mode: " << (options.vsync ? "vsync" : "immediate when tearing is available") << '\n'
              << "Frame latency: " << options.frame_latency << '\n'
              << "Texture upload budget: " << (options.upload_budget_mib == 0 ? std::string("unlimited") : std::to_string(options.upload_budget_mib) + " MiB per frame") << '\n'
              << "Pipeline cache: " << renderer.stats().pipeline_cache_status << " (" << renderer.stats().pipeline_cache_hits << " hits, " << renderer.stats().pipeline_cache_misses << " misses)\n";
}

std::string SandboxSession::describe_instance(const std::size_t index) const
{
    const auto& instance = battle_frame.instances[index];
    const auto type = battle_frame.entities[index].valid() && world != nullptr ? std::string(world->type_name(battle_frame.entities[index])) : std::string{};
    const auto& mesh = presentation->mesh_name(instance.mesh);
    return type.empty() || type == mesh ? mesh : type + " (" + mesh + ")";
}

SandboxSession::~SandboxSession()
{
    renderer.set_overlay_ui({});
}

void SandboxSession::present()
{
    request_due_capture();
    const bool rendered = renderer.render();
    write_finished_capture();
    if (rendered && reload_scene && renderer.stats().frame_number % *options.reload_every == 0)
    {
        // Load reset the bone palette; restore the bind pose until animation samples again.
        // The copy was accepted before, so a failure here leaves the identical scene shown.
        if (const auto loaded = renderer.load_scene(*reload_scene); loaded)
        {
            if (!bind_palette.empty())
            {
                renderer.set_bone_transforms(bind_palette);
            }
            // The same textures in the same order land at the same indices again.
            if (!particle_texture_copy.empty())
            {
                if (const auto added = renderer.add_textures(particle_texture_copy); !added)
                {
                    eaw::log_warning("particles", added.error());
                }
            }
            ++scene_reloads;
        }
        else
        {
            eaw::log_warning("scene", eaw::with_context("reloading the scene copy", loaded.error()));
        }
    }
    if (content_shown_at && !textures_streamed && rendered && renderer.stats().pending_texture_uploads == 0)
    {
        textures_streamed = true;
        if (renderer.stats().live_textures != 0)
        {
            std::cout << "Textures resident after " << renderer.stats().frame_number - streaming_since << " frames\n";
        }
    }
    if (options.frame_limit && content_shown_at && renderer.stats().frame_number - *content_shown_at >= *options.frame_limit)
    {
        window.request_close();
    }
    if (!rendered)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

std::chrono::steady_clock::time_point SandboxSession::presentation_now() const
{
    return fixed_frame_step ? virtual_now : std::chrono::steady_clock::now();
}

void SandboxSession::advance_presentation_clock()
{
    // A capture's clock waits for its textures, so streaming never changes what it shows.
    if (fixed_frame_step && (!options.capture || (content_shown_at && textures_streamed)))
    {
        virtual_now += *fixed_frame_step;
    }
}

void SandboxSession::request_due_capture()
{
    if (!options.capture || capture_requested || !content_shown_at || !textures_streamed)
    {
        return;
    }
    if (++capture_frames >= options.capture_frame)
    {
        renderer.request_capture();
        capture_requested = true;
    }
}

void SandboxSession::write_finished_capture()
{
    while (auto image = renderer.take_capture())
    {
        if (!options.capture || capture_written)
        {
            continue;
        }
        capture_written = true;
        const auto encoded = eaw::write_png(*image);
        if (const auto written = encoded ? eaw::write_file(*options.capture, *encoded, eaw::WriteMode::replace) : eaw::Status(encoded.error()); !written)
        {
            eaw::log_warning("capture", written.error());
        }
        else
        {
            std::cout << "Captured " << image->width << "x" << image->height << " to " << options.capture->string() << '\n';
        }
        window.request_close();
    }
}

bool SandboxSession::frame()
{
    window.poll_events();
    advance_presentation_clock();
    if (window.input().keys[static_cast<std::size_t>(eaw::Key::escape)].pressed && !renderer.overlay_wants_keyboard())
    {
        window.request_close();
    }
    const auto& window_input = window.input();
    const auto down = [&](const eaw::Key key)
    {
        return window_input.keys[static_cast<std::size_t>(key)].down;
    };
    // The overlay gets the mouse and keyboard first; while it wants the mouse the scene ignores
    // the mouse, and while one of its text fields is active the scene gets no keys.
    eaw::VisualOverlayInput overlay_input{static_cast<float>(window_input.mouse_x), static_cast<float>(window_input.mouse_y),
                                          window_input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down,
                                          window_input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)].down, window_input.wheel_delta};
    overlay_input.text = window_input.text;
    overlay_input.backspace = down(eaw::Key::backspace);
    overlay_input.del = down(eaw::Key::del);
    overlay_input.enter = down(eaw::Key::enter);
    overlay_input.escape = down(eaw::Key::escape);
    overlay_input.tab = down(eaw::Key::tab);
    overlay_input.left = down(eaw::Key::left);
    overlay_input.right = down(eaw::Key::right);
    overlay_input.up = down(eaw::Key::up);
    overlay_input.down = down(eaw::Key::down);
    overlay_input.home = down(eaw::Key::home);
    overlay_input.end = down(eaw::Key::end);
    overlay_input.control = down(eaw::Key::control);
    overlay_input.shift = down(eaw::Key::shift);
    renderer.set_overlay_input(overlay_input);
    auto scene_input = window_input;
    if (renderer.overlay_wants_keyboard())
    {
        for (auto& key : scene_input.keys)
        {
            key = {};
        }
    }
    const auto& input = scene_input;
    const bool scene_mouse = !renderer.overlay_wants_mouse();
    const bool rts_mode = tactical_camera && camera_mode == SandboxCameraMode::fixed;
    const Vector2 mouse{static_cast<float>(input.mouse_x), static_cast<float>(input.mouse_y)};
    const auto tick = world != nullptr ? world->stats().tick : 0U;
    watchdog.enter("camera", tick);
    update_camera(input, scene_mouse, rts_mode);
    watchdog.enter("pointer and orders", tick);
    handle_pointer(input, scene_mouse, rts_mode, mouse);
    handle_command_keys(input, scene_mouse, mouse);
    apply_panel_actions();
    watchdog.enter("picks", tick);
    process_picks();
    handle_view_keys(input);
    if (window.close_requested())
    {
        return false;
    }
    if (window.minimized())
    {
        animation_tick = presentation_now();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        return true;
    }

    watchdog.enter("content", tick);
    renderer.resize(window.client_width(), window.client_height());
    poll_content();
    poll_hot_reload();
    poll_packages();
    const auto now = presentation_now();
    advance_animation(now);
    watchdog.enter("simulation and presentation", tick);
    step_simulation(now);
    watchdog.enter("particles", tick);
    step_particles(now);
    watchdog.enter("automation and panel", tick);
    run_automation();
    // The statistics panel measures real frame rates, so it keeps the wall clock.
    update_panel(input, std::chrono::steady_clock::now());
    watchdog.enter("render", tick);
    present();
    watchdog.enter("events", tick);
    return true;
}

int SandboxSession::finish()
{
    renderer.wait_idle();
    write_finished_capture();
    if (options.capture && !capture_written)
    {
        eaw::log_warning("capture", eaw::Error{"the window closed before the capture was taken: " + options.capture->string()});
    }
    while (const auto pick = renderer.take_pick())
    {
        std::cout << "Selected: "
                  << (pick->instance && *pick->instance < battle_frame.entities.size() && battle_frame.entities[*pick->instance].valid() ? describe_instance(*pick->instance)
                      : pick->terrain                                                                                                    ? std::string("terrain")
                                                                                                                                         : std::string("nothing"))
                  << '\n';
    }
    const auto stats = renderer.stats();
    std::cout << "Rendered frames: " << stats.frame_number << '\n';
    std::cout << "Detail levels (" << to_string(options.lod_policy) << ", mesh detail " << eaw::format_fixed(options.mesh_detail, 2) << ", last frame): " << stats.reduced_detail_instances
              << " instances below full detail, " << stats.fading_instances << " cross-fading; " << stats.fades_started << " cross-fades in the run\n";
    const auto particle_stats = effects.particles().stats();
    std::cout << "Particles at exit: " << particle_stats.particles << " in " << particle_stats.instances << " effects (" << effects.attached_playing() << " attached), " << stats.particles
              << " drawn in the last frame, " << particle_stats.dropped_particles << " dropped, " << particle_stats.skipped_emitters << " emitters without a pass\n";
    std::cout << "Terrain marks at exit: " << effects.decal_count() << " (scorches and footprints), " << effects.trail_count() << " trails\n";
    if (options.reload_every)
    {
        std::cout << "Scene reloads: " << scene_reloads << '\n';
    }
    if (world)
    {
        const auto simulation = world->stats();
        std::cout << "Simulation: " << simulation.tick << " ticks, slowest " << eaw::format_fixed(slowest_tick_ms, 2) << " ms | paths " << simulation.path_searches << " planned, "
                  << simulation.partial_paths << " partial, " << simulation.shared_routes << " shared, " << simulation.path_expansions << " nodes expanded | blocked moves " << simulation.blocked_moves
                  << '\n';
        std::cout << "Combat: " << simulation.shots_fired << " shots, " << simulation.hits << " hits, " << simulation.kills << " kills, " << simulation.hardpoints_destroyed
                  << " hardpoints destroyed, " << simulation.projectiles << " in flight\n";
    }
    if (audio)
    {
        const auto& sounds = audio->stats();
        std::cout << "Audio (" << audio_device->name() << "): " << sounds.played << " played (" << sounds.from_animations << " by animations, " << sounds.replaced << " replacing the oldest), "
                  << sounds.skipped << " skipped, " << sounds.missing << " missing, " << audio->events().size() << " sound events\n";
    }
    if (animator)
    {
        const auto& animation = animator->stats();
        std::cout << "Animation (last frame): " << animation.animated << " units animated, " << animation.blending << " blending, " << animation.corpses << " corpses, " << animation.poses
                  << " poses, palette " << animation.palette_entries << " entries, " << animation.over_budget << " in rest pose (palette full)\n";
    }
    if (options.record && runtime)
    {
        // The current battle's orders (a hot reload or scene replacement starts a new battle).
        auto log = runtime->battle().log();
        log.end_tick = world->stats().tick;
        log.end_hash = world->state_hash();
        if (const auto saved = eaw::save_battle_log(*options.record, log); !saved)
        {
            eaw::log_warning("battle", saved.error());
        }
        else
        {
            std::cout << "Recorded " << log.entries.size() << " orders to " << options.record->string() << " (replay with: EAWEngine battle --replay <file>)\n";
        }
    }
    std::cout << "Resources after idle: " << stats.live_textures << " textures, " << stats.free_texture_slots << " free and " << stats.pending_texture_slots << " pending texture slots, "
              << stats.pending_retirements << " pending retirements, " << stats.pending_texture_uploads << " textures still streaming\n";
    return 0;
}

int run_sandbox(const SandboxOptions& options, VirtualFileSystem& vfs, const GameProfile& profile)
{
    eaw::WindowDescription window_description;
    window_description.title = "EAWEngine Visual Sandbox — " + std::string(to_string(profile.variant));
    window_description.width = options.width;
    window_description.height = options.height;
    window_description.raw_input = options.raw_input;
    auto created_window = eaw::PlatformWindow::create(window_description);
    if (!created_window)
    {
        std::cerr << "error: " << created_window.error().message << '\n';
        return 1;
    }
    auto window = std::move(created_window).value();

    eaw::D3d12RendererOptions renderer_options;
    renderer_options.graphics.vsync = options.vsync;
    renderer_options.use_warp = options.use_warp;
    renderer_options.gpu_validation = options.gpu_validation;
    renderer_options.maximum_frame_latency = options.frame_latency;
    renderer_options.pipeline_cache_directory = options.pipeline_cache;
    renderer_options.texture_upload_budget_bytes = options.upload_budget_mib * 1024ULL * 1024ULL;
    renderer_options.graphics.bloom = options.bloom_strength > 0.0F;
    renderer_options.graphics.bloom_strength = options.bloom_strength;
    renderer_options.graphics.anti_aliasing = options.anti_aliasing;
    renderer_options.graphics.shadows = options.shadows;
    renderer_options.graphics.fog = options.fog;
    renderer_options.overlay_settings_file = eaw::executable_directory() / "sandbox_imgui.ini";
    renderer_options.lod.policy = options.lod_policy;
    renderer_options.lod.mesh_detail = options.mesh_detail;
    auto created_renderer = eaw::D3d12Renderer::create(window.native_handle(), window.client_width(), window.client_height(), renderer_options);
    if (!created_renderer)
    {
        std::cerr << "error: " << created_renderer.error().message << '\n';
        return 1;
    }
    auto& renderer = **created_renderer;
    SandboxSession session(options, vfs, window, renderer);
    while (!window.close_requested())
    {
        if (!session.frame())
        {
            break;
        }
    }
    return session.finish();
}

} // namespace eaw::engine_app
