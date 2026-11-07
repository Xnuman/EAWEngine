#include "eaw/asset_environment.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/virtual_file_system.hpp"

#ifdef _WIN32
#include "eaw/d3d12_renderer.hpp"
#include "eaw/platform_window.hpp"
#include "camera_controller.hpp"
#include "visual_scene_builder.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

enum class RunMode
{
    sandbox,
    doctor,
};

struct Options
{
    RunMode mode{RunMode::sandbox};
    std::optional<std::filesystem::path> game_root;
    eaw::GameVariant variant{eaw::GameVariant::forces_of_corruption};
    std::vector<std::filesystem::path> mods;
    std::string probe{"Data/XML/GameObjectFiles.xml"};
    std::string model{"Data/Art/Models/EV_Acclamator.alo"};
    std::optional<std::string> animation;
    std::optional<std::string> map;
    std::filesystem::path pipeline_cache;
    std::uint32_t width{1600};
    std::uint32_t height{900};
    std::uint32_t frame_latency{2};
    std::optional<std::uint64_t> frame_limit;
    bool vsync{true};
    bool use_warp{};
    bool gpu_validation{};
    bool validation_triangle{};
};

void print_usage()
{
    std::cout << "EAWEngine visual sandbox\n\n"
              << "Usage: EAWEngine [sandbox|doctor|bootstrap] [options]\n"
              << "  sandbox            Open the Direct3D 12 visual sandbox (default)\n"
              << "  doctor             Validate discovery, mounts, and an asset probe\n"
              << "  bootstrap          Alias for doctor for older launch scripts\n"
              << "  --game-root <path> Empire at War installation root\n"
              << "  --game <eaw|foc>   Select the base game or expansion\n"
              << "  --mod <path>       Add a mod root (repeatable; first has priority)\n"
              << "  --probe <asset>    Resolve and read a virtual asset path\n"
              << "  --model <asset>    ALO model shown in the visual sandbox\n"
              << "  --animation <asset>  ALA animation played with the model\n"
              << "  --map <asset>      TED map shown with its backdrop and a tactical camera\n"
              << "  --pipeline-cache <dir>  Cache compiled D3D12 pipeline states in this directory\n"
              << "  --width <pixels>   Initial sandbox client width\n"
              << "  --height <pixels>  Initial sandbox client height\n"
              << "  --vsync <on|off>   Select synchronized or tearing-capable present\n"
              << "  --frame-latency <1|2>  Maximum queued presentation frames\n"
              << "  --frame-limit <count>  Exit after submitted frames (automation)\n"
              << "  --warp             Use the Windows software D3D12 adapter\n"
              << "  --gpu-validation   Enable D3D12 GPU-based validation\n"
              << "  --validation-triangle  Skip model loading and show the fallback\n";
}

std::uint32_t parse_unsigned(const std::string_view value, const std::string_view option)
{
    std::size_t consumed{};
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size() || parsed > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument("invalid value for " + std::string(option) + ": " + std::string(value));
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint64_t parse_unsigned_64(const std::string_view value, const std::string_view option)
{
    std::size_t consumed{};
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size())
    {
        throw std::invalid_argument("invalid value for " + std::string(option) + ": " + std::string(value));
    }
    return parsed;
}

std::string require_value(const int argc, char** argv, int& index, const std::string& option)
{
    if (index + 1 >= argc)
    {
        throw std::invalid_argument("missing value after " + option);
    }
    return argv[++index];
}

Options parse_options(const int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "sandbox")
        {
            options.mode = RunMode::sandbox;
        }
        else if (argument == "doctor" || argument == "bootstrap")
        {
            options.mode = RunMode::doctor;
        }
        else if (argument == "--help" || argument == "-h")
        {
            print_usage();
            std::exit(0);
        }
        else if (argument == "--warp")
        {
            options.use_warp = true;
        }
        else if (argument == "--gpu-validation")
        {
            options.gpu_validation = true;
        }
        else if (argument == "--validation-triangle")
        {
            options.validation_triangle = true;
        }
        else if (argument == "--game-root")
        {
            options.game_root = require_value(argc, argv, index, argument);
        }
        else if (argument == "--game")
        {
            const auto value = require_value(argc, argv, index, argument);
            const auto variant = eaw::parse_game_variant(value);
            if (!variant)
            {
                throw std::invalid_argument("unknown game variant: " + value);
            }
            options.variant = *variant;
        }
        else if (argument == "--mod")
        {
            options.mods.emplace_back(require_value(argc, argv, index, argument));
        }
        else if (argument == "--probe")
        {
            options.probe = require_value(argc, argv, index, argument);
        }
        else if (argument == "--model")
        {
            options.model = require_value(argc, argv, index, argument);
        }
        else if (argument == "--animation")
        {
            options.animation = require_value(argc, argv, index, argument);
        }
        else if (argument == "--map")
        {
            options.map = require_value(argc, argv, index, argument);
        }
        else if (argument == "--pipeline-cache")
        {
            options.pipeline_cache = require_value(argc, argv, index, argument);
        }
        else if (argument == "--width")
        {
            options.width = parse_unsigned(require_value(argc, argv, index, argument), argument);
        }
        else if (argument == "--height")
        {
            options.height = parse_unsigned(require_value(argc, argv, index, argument), argument);
        }
        else if (argument == "--frame-latency")
        {
            options.frame_latency = parse_unsigned(require_value(argc, argv, index, argument), argument);
            if (options.frame_latency < 1 || options.frame_latency > 2)
            {
                throw std::invalid_argument("--frame-latency must be 1 or 2");
            }
        }
        else if (argument == "--frame-limit")
        {
            options.frame_limit = parse_unsigned_64(require_value(argc, argv, index, argument), argument);
            if (*options.frame_limit == 0)
            {
                throw std::invalid_argument("--frame-limit must be greater than zero");
            }
        }
        else if (argument == "--vsync")
        {
            const auto value = require_value(argc, argv, index, argument);
            if (value == "on")
            {
                options.vsync = true;
            }
            else if (value == "off")
            {
                options.vsync = false;
            }
            else
            {
                throw std::invalid_argument("--vsync must be 'on' or 'off'");
            }
        }
        else
        {
            throw std::invalid_argument("unknown option or mode: " + argument);
        }
    }

    if (options.width == 0 || options.height == 0)
    {
        throw std::invalid_argument("sandbox width and height must be greater than zero");
    }
    return options;
}

} // namespace

int main(const int argc, char** argv)
{
    try
    {
        const auto options = parse_options(argc, argv);
        const eaw::InstallationLocator locator;
        const auto installations = locator.discover(options.game_root);
        if (installations.empty())
        {
            std::cerr << "No valid Empire at War installation was found. Use --game-root.\n";
            return 2;
        }

        const auto profile = eaw::make_game_profile(installations.front(), options.variant);
        if (!profile)
        {
            std::cerr << "The selected installation does not contain both game data sets.\n";
            return 2;
        }

        eaw::VirtualFileSystem vfs;
        const auto mount_report = eaw::mount_game_assets(vfs, *profile, options.mods);
        for (const auto& warning : mount_report.warnings)
        {
            std::cerr << "warning: " << warning << '\n';
        }

        std::size_t indexed_entries = 0;
        for (const auto& mount : vfs.mounts())
        {
            indexed_entries += mount->entry_count();
        }

        std::cout << "EAWEngine 0.1.0\n"
                  << "Game: " << eaw::to_string(profile->variant) << '\n'
                  << "Installation: " << profile->root << '\n'
                  << "Runtime root: " << profile->runtime_root << '\n'
                  << "Hardware threads: " << std::thread::hardware_concurrency() << '\n'
                  << "Asset mounts: " << vfs.mounts().size() << '\n'
                  << "Indexed MEG entries: " << indexed_entries << '\n';

        const auto resolved = vfs.resolve(options.probe);
        if (!resolved)
        {
            std::cerr << "Probe not found: " << options.probe << '\n';
            return 3;
        }
        const auto bytes = vfs.read(options.probe);
        std::cout << "Probe: " << resolved->virtual_path << '\n' << "Source: " << resolved->mount_name << " -> " << resolved->container << '\n' << "Size: " << bytes.size() << " bytes\n";

        if (options.mode == RunMode::doctor)
        {
            std::cout << "Status: compatibility bootstrap succeeded.\n";
            return 0;
        }

#ifdef _WIN32
        eaw::WindowDescription window_description;
        window_description.title = "EAWEngine Visual Sandbox — " + std::string(eaw::to_string(profile->variant));
        window_description.width = options.width;
        window_description.height = options.height;
        eaw::PlatformWindow window(window_description);

        eaw::D3d12RendererOptions renderer_options;
        renderer_options.vsync = options.vsync;
        renderer_options.use_warp = options.use_warp;
        renderer_options.gpu_validation = options.gpu_validation;
        renderer_options.maximum_frame_latency = options.frame_latency;
        renderer_options.pipeline_cache_directory = options.pipeline_cache;
        eaw::D3d12Renderer renderer(window.native_handle(), window.client_width(), window.client_height(), renderer_options);
        std::optional<eaw::VisualModelAsset> visual_model;
        std::optional<eaw::AlaAnimation> visual_animation;
        // Model inspection orbits the model; a map uses the tactical camera. The validation
        // triangle gets an orbit over its small extent.
        std::optional<eaw::OrbitCamera> orbit_camera;
        std::optional<eaw::TacticalCamera> tactical_camera;
        if (!options.validation_triangle && options.map)
        {
            try
            {
                const auto start = std::chrono::steady_clock::now();
                auto visual_map = eaw::build_visual_map(vfs, *options.map);
                renderer.load_scene(visual_map.scene);
                tactical_camera.emplace(eaw::CameraBounds{visual_map.scene.bounds_minimum, visual_map.scene.bounds_maximum});
                if (visual_map.lighting)
                {
                    renderer.set_lighting(*visual_map.lighting);
                }
                const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                const auto& scene = visual_map.scene;
                std::cout << "Visual map: " << scene.name << (visual_map.land ? " (land)" : " (space)") << '\n'
                          << "Terrain: "
                          << (scene.terrain.vertices.empty() ? std::string("none")
                                                             : std::to_string(scene.terrain.vertices.size() / 3U) + " triangles, " + std::to_string(scene.terrain.layer_count) + " layers")
                          << '\n'
                          << "Objects: " << scene.instances.size() << " instances of " << scene.meshes.size() << " models | squadron members expanded: " << visual_map.expanded_members << '\n'
                          << "Not drawn: " << visual_map.untyped << " untyped, " << visual_map.without_model << " without model, " << visual_map.missing_model_files << " missing model files, "
                          << visual_map.unexpanded_containers << " land teams/companies, " << visual_map.unrenderable_placements << " unrenderable models\n"
                          << "Geometry: " << scene.vertices.size() << " vertices, " << scene.indices.size() / 3U << " triangles, " << scene.draws.size()
                          << " draws | Textures: " << scene.textures.size() << " | Built in " << seconds << " s\n"
                          << "Lighting: " << (visual_map.lighting ? "environment '" + visual_map.environment_name + "'" : std::string("engine default"))
                          << " | team colors: " << visual_map.faction_colored << " faction, " << visual_map.player_colored << " player-slot fallback\n"
                          << "Backdrop: ";
                if (visual_map.backdrops.empty())
                {
                    std::cout << "none";
                }
                for (std::size_t index = 0; index < visual_map.backdrops.size(); ++index)
                {
                    std::cout << (index == 0 ? "" : ", ") << visual_map.backdrops[index];
                }
                std::cout << '\n';
                for (const auto& warning : scene.warnings)
                {
                    std::cerr << "map warning: " << warning << '\n';
                }
            }
            catch (const std::exception& error)
            {
                std::cerr << "map warning: " << error.what() << "; using the validation triangle\n";
            }
        }
        else if (!options.validation_triangle)
        {
            try
            {
                visual_model = eaw::build_visual_model(vfs, options.model);
                renderer.load_scene(visual_model->scene);
                orbit_camera.emplace(eaw::CameraBounds{visual_model->scene.bounds_minimum, visual_model->scene.bounds_maximum});
                const auto bind_palette = eaw::sample_skin_palette(*visual_model, nullptr, 0.0F);
                renderer.set_bone_transforms(bind_palette);
                std::cout << "Visual model: " << visual_model->scene.name << '\n'
                          << "Geometry: " << visual_model->scene.vertices.size() << " vertices, " << visual_model->scene.indices.size() / 3U << " triangles, " << visual_model->scene.draws.size()
                          << " draws\n"
                          << "Textures: " << visual_model->scene.textures.size() << " | Bones: " << visual_model->scene.bone_count << '\n';
                for (const auto& warning : visual_model->scene.warnings)
                {
                    std::cerr << "model warning: " << warning << '\n';
                }
                // Upload staging owns the copied bytes after load_scene returns;
                // retain only the skeleton data needed for animation sampling.
                visual_model->scene = {};
            }
            catch (const std::exception& error)
            {
                std::cerr << "model warning: " << error.what() << "; using the validation triangle\n";
                visual_model.reset();
            }
        }
        if (visual_model)
        {
            try
            {
                const auto animation_path = options.animation ? options.animation : eaw::default_model_animation(vfs, options.model);
                if (animation_path)
                {
                    const auto resolved_animation = vfs.resolve(*animation_path);
                    if (!resolved_animation)
                    {
                        std::cerr << "animation warning: asset not found: " << *animation_path << '\n';
                    }
                    else
                    {
                        visual_animation = eaw::AlaAnimation::parse(vfs.read(resolved_animation->virtual_path));
                        std::cout << "Animation: " << resolved_animation->virtual_path << " | " << visual_animation->playable_frame_count() << " frames at " << visual_animation->frames_per_second
                                  << " fps | " << visual_animation->duration_seconds() << " seconds\n";
                        for (const auto& warning : visual_animation->warnings)
                        {
                            std::cerr << "animation warning: " << warning << '\n';
                        }
                    }
                }
            }
            catch (const std::exception& error)
            {
                std::cerr << "animation warning: " << error.what() << "; using the bind pose\n";
                visual_animation.reset();
            }
        }
        if (!orbit_camera && !tactical_camera)
        {
            orbit_camera.emplace(eaw::CameraBounds{{-0.8F, -0.7F, -0.1F}, {0.8F, 0.8F, 0.1F}});
        }
        renderer.set_camera(tactical_camera ? tactical_camera->camera() : orbit_camera->camera());
        std::cout << "D3D12 adapter: " << renderer.adapter_name() << '\n'
                  << "Present mode: " << (options.vsync ? "vsync" : "immediate when tearing is available") << '\n'
                  << "Frame latency: " << options.frame_latency << '\n'
                  << "Pipeline cache: " << renderer.stats().pipeline_cache_status << " (" << renderer.stats().pipeline_cache_hits << " hits, " << renderer.stats().pipeline_cache_misses << " misses)\n"
                  << (tactical_camera ? "Tactical camera: WASD/arrows or right-drag pan (Shift is faster), left-drag rotates, wheel zooms, Space resets.\n"
                                      : "Visual sandbox ready. Left-drag orbits, wheel zooms, Space resets, P pauses animation.\n");

        auto animation_tick = std::chrono::steady_clock::now();
        const auto scene_start = animation_tick;
        auto camera_tick = animation_tick;
        float animation_time = 0.0F;
        bool animation_playing = true;
        while (!window.close_requested())
        {
            window.poll_events();
            if (window.input().keys[static_cast<std::size_t>(eaw::Key::escape)].pressed)
            {
                window.request_close();
            }
            const auto& input = window.input();
            const auto key_down = [&](const eaw::Key key)
            {
                return input.keys[static_cast<std::size_t>(key)].down;
            };
            const auto camera_now = std::chrono::steady_clock::now();
            const auto camera_seconds = std::min(std::chrono::duration<float>(camera_now - camera_tick).count(), 0.25F);
            camera_tick = camera_now;
            eaw::CameraInput camera_input;
            camera_input.reset = input.keys[static_cast<std::size_t>(eaw::Key::space)].pressed;
            camera_input.zoom_steps = input.wheel_delta;
            if (input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down)
            {
                camera_input.yaw = static_cast<float>(input.mouse_delta_x) * 0.01F;
                camera_input.pitch = static_cast<float>(-input.mouse_delta_y) * 0.01F;
            }
            if (tactical_camera)
            {
                // Pan speed scales with the view distance; panning units are view distances.
                const auto speed = (key_down(eaw::Key::shift) ? 2.4F : 0.8F) * camera_seconds;
                camera_input.pan_right = static_cast<float>((key_down(eaw::Key::d) || key_down(eaw::Key::right) ? 1 : 0) - (key_down(eaw::Key::a) || key_down(eaw::Key::left) ? 1 : 0)) * speed;
                camera_input.pan_forward = static_cast<float>((key_down(eaw::Key::w) || key_down(eaw::Key::up) ? 1 : 0) - (key_down(eaw::Key::s) || key_down(eaw::Key::down) ? 1 : 0)) * speed;
                if (input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)].down)
                {
                    camera_input.pan_right -= static_cast<float>(input.mouse_delta_x) * 0.0015F;
                    camera_input.pan_forward += static_cast<float>(input.mouse_delta_y) * 0.0015F;
                }
                tactical_camera->apply(camera_input);
                renderer.set_camera(tactical_camera->camera());
            }
            else
            {
                orbit_camera->apply(camera_input);
                renderer.set_camera(orbit_camera->camera());
            }
            if (window.input().keys[static_cast<std::size_t>(eaw::Key::p)].pressed && visual_animation)
            {
                animation_playing = !animation_playing;
            }
            if (window.close_requested())
            {
                break;
            }
            if (window.minimized())
            {
                animation_tick = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            renderer.resize(window.client_width(), window.client_height());
            const auto now = std::chrono::steady_clock::now();
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
            renderer.set_scene_time(std::chrono::duration<float>(now - scene_start).count());
            const bool rendered = renderer.render();
            if (options.frame_limit && renderer.stats().frame_number >= *options.frame_limit)
            {
                window.request_close();
            }
            if (!rendered)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        }
        renderer.wait_idle();
        const auto stats = renderer.stats();
        std::cout << "Rendered frames: " << stats.frame_number << '\n';
        return 0;
#else
        std::cerr << "The Direct3D 12 visual sandbox is available on Windows only.\n";
        return 2;
#endif
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
