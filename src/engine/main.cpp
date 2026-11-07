#include "eaw/asset_environment.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/virtual_file_system.hpp"

#ifdef _WIN32
#include "eaw/d3d12_renderer.hpp"
#include "eaw/platform_window.hpp"
#include "asset_cache.hpp"
#include "camera_controller.hpp"
#include "debug_draw.hpp"
#include "visual_scene_builder.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
    // Reload the scene every N rendered frames to exercise resource replacement and retirement.
    std::optional<std::uint64_t> reload_every;
    // Per-frame texture upload budget in MiB; zero uploads everything at once.
    std::uint64_t upload_budget_mib{64};
    // Watch the scene's source files and archives and rebuild when they change.
    bool hot_reload{};
    // Pick this client pixel once the scene is shown (automation for the selection path).
    std::optional<std::array<std::uint32_t, 2>> pick;
    // Start with every instance's bounds drawn (B toggles at runtime).
    bool show_bounds{};
    // On-screen statistics panel (F1 toggles at runtime).
    bool overlay{true};
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
              << "  --frame-limit <count>  Exit after this many frames once the scene is shown (automation)\n"
              << "  --upload-budget <MiB>  Texture bytes streamed per frame (default 64; 0 uploads all at once)\n"
              << "  --hot-reload       Rebuild the scene when its loose files or MEG archives change\n"
              << "  --pick <x> <y>     Select whatever is at this client pixel once the scene is shown\n"
              << "  --show-bounds      Start with every object's bounds drawn (B toggles)\n"
              << "  --no-overlay       Start without the on-screen statistics panel (F1 toggles)\n"
              << "  --reload-every <frames>  Replace the scene with a fresh copy every N frames (lifetime test)\n"
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
        else if (argument == "--hot-reload")
        {
            options.hot_reload = true;
        }
        else if (argument == "--show-bounds")
        {
            options.show_bounds = true;
        }
        else if (argument == "--no-overlay")
        {
            options.overlay = false;
        }
        else if (argument == "--pick")
        {
            const auto x = parse_unsigned(require_value(argc, argv, index, argument), argument);
            const auto y = parse_unsigned(require_value(argc, argv, index, argument), argument);
            options.pick = std::array<std::uint32_t, 2>{x, y};
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
        else if (argument == "--upload-budget")
        {
            options.upload_budget_mib = parse_unsigned_64(require_value(argc, argv, index, argument), argument);
            if (options.upload_budget_mib > 1024ULL * 1024ULL)
            {
                throw std::invalid_argument("--upload-budget is limited to 1048576 MiB");
            }
        }
        else if (argument == "--reload-every")
        {
            options.reload_every = parse_unsigned_64(require_value(argc, argv, index, argument), argument);
            if (*options.reload_every == 0)
            {
                throw std::invalid_argument("--reload-every must be greater than zero");
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

std::string format_fixed(const double value, const int decimals)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals) << value;
    return stream.str();
}

// Everything the sandbox shows, built off the render thread. Reading the VFS from a worker is safe
// because its const reads open their own streams, and the main thread does not use the VFS while
// a build runs.
struct SandboxContent
{
    std::optional<eaw::VisualMapAsset> map;
    std::optional<eaw::VisualModelAsset> model;
    std::optional<eaw::AlaAnimation> animation;
    std::string animation_path;
    // Every animation beside the model, for runtime selection.
    std::vector<std::string> animations;
    std::string animation_error;
    // When set, nothing was built and the current view stays.
    std::string error;
    double build_seconds{};
    // Files the build consulted (also after a failure, so fixing a file triggers a rebuild).
    eaw::DependencySet dependencies;
    // Cache entries dropped because their files changed (rebuilds only).
    std::vector<std::string> invalidated;
    // Cache entries the successful build no longer used.
    std::size_t evicted{};
    bool reload{};
};

// Result of a background hot-reload check: archives whose file changed and scene dependencies
// whose resolved source changed.
struct HotReloadCheck
{
    std::vector<std::size_t> archives;
    std::vector<std::string> files;
    double seconds{};
};

SandboxContent build_sandbox_content(const eaw::VirtualFileSystem& vfs, const Options& options, eaw::AssetCache* cache, const bool reload)
{
    SandboxContent content;
    content.reload = reload;
    const auto start = std::chrono::steady_clock::now();
    if (reload && cache != nullptr)
    {
        content.invalidated = cache->invalidate(vfs);
    }
    const auto epoch = cache != nullptr ? cache->begin_epoch() : 0;
    const eaw::RecordingAssetReader reader(vfs);
    try
    {
        if (options.map)
        {
            content.map = eaw::build_visual_map(reader, *options.map, cache);
        }
        else
        {
            content.model = eaw::build_visual_model(reader, options.model, cache);
        }
    }
    catch (const std::exception& error)
    {
        content.error = error.what();
        content.dependencies = reader.dependencies();
        return content;
    }
    if (content.model)
    {
        try
        {
            content.animations = eaw::model_animations(reader, options.model);
        }
        catch (const std::exception& error)
        {
            content.animation_error = std::string("animation list unavailable: ") + error.what();
        }
        try
        {
            const auto animation_path = options.animation ? options.animation : eaw::default_model_animation(reader, options.model);
            if (animation_path)
            {
                const auto resolved_animation = reader.resolve(*animation_path);
                if (!resolved_animation)
                {
                    content.animation_error = "asset not found: " + *animation_path;
                }
                else
                {
                    content.animation = eaw::AlaAnimation::parse(reader.read(resolved_animation->virtual_path));
                    content.animation_path = resolved_animation->virtual_path;
                }
            }
        }
        catch (const std::exception& error)
        {
            content.animation_error = std::string(error.what()) + "; using the bind pose";
            content.animation.reset();
        }
    }
    content.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    content.dependencies = reader.dependencies();
    if (cache != nullptr)
    {
        content.evicted = cache->evict_unused_since(epoch);
    }
    return content;
}

} // namespace

int main(const int argc, char** argv)
{
    // Status lines (builds, streaming, hot reload) appear as they happen even when redirected.
    std::cout << std::unitbuf;
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
        renderer_options.texture_upload_budget_bytes = options.upload_budget_mib * 1024ULL * 1024ULL;
        eaw::D3d12Renderer renderer(window.native_handle(), window.client_width(), window.client_height(), renderer_options);
        std::optional<eaw::VisualModelAsset> visual_model;
        std::optional<eaw::AlaAnimation> visual_animation;
        // Model inspection orbits the model; a map uses the tactical camera. The validation
        // triangle, shown while content builds, gets an orbit over its small extent.
        std::optional<eaw::OrbitCamera> orbit_camera(std::in_place, eaw::CameraBounds{{-0.8F, -0.7F, -0.1F}, {0.8F, 0.8F, 0.1F}});
        std::optional<eaw::TacticalCamera> tactical_camera;
        // Kept only for --reload-every; otherwise the CPU copy moves into the renderer.
        std::optional<eaw::VisualScene> reload_scene;
        std::vector<std::array<float, 16>> bind_palette;
        // What each scene instance shows (model path), for reporting selections.
        std::vector<std::string> instance_labels;
        std::optional<std::uint32_t> selected_instance;
        // Per-instance bounds for debug drawing (model-space box and world transform).
        struct InstanceBounds
        {
            std::array<float, 3> minimum{};
            std::array<float, 3> maximum{};
            std::array<float, 16> transform{};
            bool background{};
        };
        std::vector<InstanceBounds> instance_bounds;
        float axis_length = 1.0F;
        bool show_all_bounds = options.show_bounds;
        // Text shown on the statistics overlay; updated where the console reports the same events.
        std::string scene_status = options.validation_triangle ? "validation triangle" : "building in the background...";
        std::string animation_status;
        std::string reload_status = options.hot_reload ? "waiting for the scene" : "off (--hot-reload)";
        const auto update_debug_lines = [&]
        {
            std::vector<eaw::VisualDebugLine> lines;
            if (show_all_bounds)
            {
                eaw::append_debug_axes(lines, {0.0F, 0.0F, 0.0F}, axis_length);
                for (const auto& bounds : instance_bounds)
                {
                    if (!bounds.background)
                    {
                        eaw::append_debug_box(lines, bounds.minimum, bounds.maximum, bounds.transform, {0.3F, 0.85F, 0.95F, 0.45F});
                    }
                }
            }
            if (selected_instance && *selected_instance < instance_bounds.size())
            {
                const auto& bounds = instance_bounds[*selected_instance];
                eaw::append_debug_box(lines, bounds.minimum, bounds.maximum, bounds.transform, {1.0F, 0.85F, 0.25F, 1.0F});
            }
            renderer.set_debug_lines(lines);
            if (renderer.stats().dropped_debug_lines != 0)
            {
                std::cerr << "debug warning: " << renderer.stats().dropped_debug_lines << " debug lines exceed the per-frame capacity of " << eaw::D3d12Renderer::max_debug_lines() << '\n';
            }
        };
        const auto label_instances = [&](const eaw::VisualScene& scene)
        {
            instance_labels.clear();
            instance_labels.reserve(scene.instances.size());
            instance_bounds.clear();
            instance_bounds.reserve(scene.instances.size());
            for (const auto& instance : scene.instances)
            {
                const auto& mesh = scene.meshes[instance.mesh];
                instance_labels.push_back(mesh.name + (instance.background ? " (backdrop)" : ""));
                instance_bounds.push_back({mesh.bounds_minimum, mesh.bounds_maximum, instance.transform, instance.background});
            }
            float extent = 0.0F;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                extent = std::max(extent, scene.bounds_maximum[axis] - scene.bounds_minimum[axis]);
            }
            axis_length = std::max(extent * 0.1F, 1.0F);
            selected_instance.reset();
            update_debug_lines();
        };
        // Model mode: Left/Right step through [bind pose, animations...].
        std::vector<std::string> animation_paths;
        std::size_t animation_choice = 0;

        // The window stays responsive while the map or model builds on a worker thread. The future
        // is declared after the VFS, so leaving early waits for the worker before the VFS goes away.
        // Decoded assets shared by every build; a hot reload re-decodes only what changed.
        eaw::AssetCache asset_cache;
        eaw::ArchiveWatch archive_watch(vfs);
        std::optional<eaw::DependencySet> scene_dependencies;
        std::future<SandboxContent> pending_content;
        std::future<HotReloadCheck> pending_check;
        auto next_reload_check = std::chrono::steady_clock::now();
        if (!options.validation_triangle)
        {
            pending_content = std::async(std::launch::async, build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, false);
            std::cout << "Building " << (options.map ? "map " + *options.map : "model " + options.model) << " in the background...\n";
        }
        // Frame count at which the content was shown; --frame-limit counts from here.
        std::optional<std::uint64_t> content_shown_at;
        if (options.validation_triangle)
        {
            content_shown_at = 0;
        }

        const auto apply_content = [&](SandboxContent content)
        {
            scene_dependencies = std::move(content.dependencies);
            if (content.reload)
            {
                const auto stats = asset_cache.stats();
                std::cout << "Hot reload: rebuilt in " << content.build_seconds << " s; " << content.invalidated.size() << " cached assets re-decoded, " << content.evicted << " unused evicted, "
                          << stats.entries << " cached\n";
                reload_status = "rebuilt in " + format_fixed(content.build_seconds, 2) + " s, " + std::to_string(content.invalidated.size()) + " assets re-decoded";
            }
            if (!content.error.empty())
            {
                std::cerr << (options.map ? "map" : "model") << " warning: " << content.error << (content.reload ? "; keeping the current view\n" : "; using the validation triangle\n");
                return;
            }
            if (content.map)
            {
                auto& visual_map = *content.map;
                const auto& scene = visual_map.scene;
                scene_status =
                    scene.name + (visual_map.land ? " (land)" : " (space)") + ", " + std::to_string(scene.instances.size()) + " objects, built in " + format_fixed(content.build_seconds, 2) + " s";
                std::cout << "Visual map: " << scene.name << (visual_map.land ? " (land)" : " (space)") << '\n'
                          << "Terrain: "
                          << (scene.terrain.vertices.empty() ? std::string("none")
                                                             : std::to_string(scene.terrain.vertices.size() / 3U) + " triangles, " + std::to_string(scene.terrain.layer_count) + " layers" +
                                                                   (visual_map.water == eaw::TedWaterType::water  ? ", water"
                                                                    : visual_map.water == eaw::TedWaterType::lava ? ", lava"
                                                                    : visual_map.water == eaw::TedWaterType::ice  ? ", ice"
                                                                                                                  : "") +
                                                                   ", " + std::to_string(visual_map.tracks) + " tracks")
                          << '\n'
                          << "Objects: " << scene.instances.size() << " instances of " << scene.meshes.size() << " models | squadron members expanded: " << visual_map.expanded_members << '\n'
                          << "Not drawn: " << visual_map.untyped << " untyped, " << visual_map.without_model << " without model, " << visual_map.missing_model_files << " missing model files, "
                          << visual_map.unexpanded_containers << " land teams/companies, " << visual_map.unrenderable_placements << " unrenderable models\n"
                          << "Geometry: " << scene.vertices.size() << " vertices, " << scene.indices.size() / 3U << " triangles, " << scene.draws.size()
                          << " draws | Textures: " << scene.textures.size() << " | Built in " << content.build_seconds << " s\n"
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
                const eaw::CameraBounds bounds{scene.bounds_minimum, scene.bounds_maximum};
                if (options.reload_every)
                {
                    reload_scene = scene;
                }
                label_instances(visual_map.scene);
                renderer.load_scene(std::move(visual_map.scene));
                if (visual_map.lighting)
                {
                    renderer.set_lighting(*visual_map.lighting);
                }
                // A hot reload keeps the viewer's camera.
                if (!content.reload || !tactical_camera)
                {
                    tactical_camera.emplace(bounds);
                    orbit_camera.reset();
                    renderer.set_camera(tactical_camera->camera());
                    std::cout << "Tactical camera: WASD/arrows or right-drag pan (Shift is faster), left-drag rotates, wheel zooms, Space resets.\n";
                }
            }
            else if (content.model)
            {
                visual_model = std::move(content.model);
                auto& scene = visual_model->scene;
                scene_status = scene.name + ", " + std::to_string(scene.bone_count) + " palette entries, built in " + format_fixed(content.build_seconds, 2) + " s";
                animation_status = "bind pose";
                std::cout << "Visual model: " << scene.name << '\n'
                          << "Geometry: " << scene.vertices.size() << " vertices, " << scene.indices.size() / 3U << " triangles, " << scene.draws.size() << " draws\n"
                          << "Textures: " << scene.textures.size() << " | Bones: " << scene.bone_count << " | Built in " << content.build_seconds << " s\n";
                for (const auto& warning : scene.warnings)
                {
                    std::cerr << "model warning: " << warning << '\n';
                }
                const eaw::CameraBounds bounds{scene.bounds_minimum, scene.bounds_maximum};
                if (options.reload_every)
                {
                    reload_scene = scene;
                }
                // The renderer takes the scene; keep only the skeleton data needed for animation.
                label_instances(scene);
                renderer.load_scene(std::exchange(scene, {}));
                bind_palette = eaw::sample_skin_palette(*visual_model, nullptr, 0.0F);
                renderer.set_bone_transforms(bind_palette);
                if (!content.reload || !orbit_camera)
                {
                    orbit_camera.emplace(bounds);
                    renderer.set_camera(orbit_camera->camera());
                }
                if (!content.animation_error.empty())
                {
                    std::cerr << "animation warning: " << content.animation_error << '\n';
                }
                visual_animation.reset();
                animation_paths = std::move(content.animations);
                animation_choice = 0;
                if (content.animation)
                {
                    if (const auto found = std::ranges::find(animation_paths, content.animation_path); found != animation_paths.end())
                    {
                        animation_choice = static_cast<std::size_t>(found - animation_paths.begin()) + 1U;
                    }
                    visual_animation = std::move(content.animation);
                    animation_status = content.animation_path;
                    std::cout << "Animation: " << content.animation_path << " | " << visual_animation->playable_frame_count() << " frames at " << visual_animation->frames_per_second << " fps | "
                              << visual_animation->duration_seconds() << " seconds\n";
                    for (const auto& warning : visual_animation->warnings)
                    {
                        std::cerr << "animation warning: " << warning << '\n';
                    }
                }
                if (!content.reload)
                {
                    std::cout << "Left-drag orbits, wheel zooms, Space resets, P pauses animation, Left/Right select among " << animation_paths.size() << " animations.\n";
                }
            }
            if (content.reload)
            {
                const auto stats = renderer.stats();
                std::cout << "Hot reload: " << stats.reused_textures << " textures kept on the GPU, " << stats.pending_texture_uploads << " uploading\n";
            }
        };

        renderer.set_camera(orbit_camera->camera());
        std::cout << "D3D12 adapter: " << renderer.adapter_name() << '\n'
                  << "Present mode: " << (options.vsync ? "vsync" : "immediate when tearing is available") << '\n'
                  << "Frame latency: " << options.frame_latency << '\n'
                  << "Texture upload budget: " << (options.upload_budget_mib == 0 ? std::string("unlimited") : std::to_string(options.upload_budget_mib) + " MiB per frame") << '\n'
                  << "Pipeline cache: " << renderer.stats().pipeline_cache_status << " (" << renderer.stats().pipeline_cache_hits << " hits, " << renderer.stats().pipeline_cache_misses
                  << " misses)\n";

        auto animation_tick = std::chrono::steady_clock::now();
        const auto scene_start = animation_tick;
        auto camera_tick = animation_tick;
        float animation_time = 0.0F;
        bool animation_playing = true;
        std::uint64_t scene_reloads = 0;
        bool textures_streamed = false;
        bool reload_check_reported = false;
        std::int64_t click_travel = 0;
        bool overlay_visible = options.overlay;
        std::uint64_t overlay_frames = 0;
        auto overlay_refreshed = std::chrono::steady_clock::now();
        bool automated_pick_sent = false;
        std::uint64_t streaming_since = 0;
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
            // A left click that does not drag selects what is under the cursor; dragging orbits or
            // rotates as before.
            if (input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].pressed)
            {
                click_travel = 0;
            }
            if (input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down)
            {
                click_travel += std::abs(input.mouse_delta_x) + std::abs(input.mouse_delta_y);
            }
            if (input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].released && click_travel <= 3 && input.mouse_x >= 0 && input.mouse_y >= 0)
            {
                renderer.request_pick(static_cast<std::uint32_t>(input.mouse_x), static_cast<std::uint32_t>(input.mouse_y));
            }
            if (const auto pick = renderer.take_pick())
            {
                if (pick->instance && *pick->instance < instance_labels.size())
                {
                    selected_instance = pick->instance;
                    std::cout << "Selected instance " << *pick->instance << ": " << instance_labels[*pick->instance] << '\n';
                }
                else
                {
                    selected_instance.reset();
                    std::cout << "Selected: " << (pick->terrain ? "terrain" : "nothing") << '\n';
                }
                renderer.set_highlight(selected_instance);
                update_debug_lines();
            }
            if (input.keys[static_cast<std::size_t>(eaw::Key::b)].pressed)
            {
                show_all_bounds = !show_all_bounds;
                update_debug_lines();
                std::cout << "Bounds overlay " << (show_all_bounds ? "on" : "off") << '\n';
            }
            const int animation_step = (input.keys[static_cast<std::size_t>(eaw::Key::right)].pressed ? 1 : 0) - (input.keys[static_cast<std::size_t>(eaw::Key::left)].pressed ? 1 : 0);
            if (visual_model && !tactical_camera && animation_step != 0 && !animation_paths.empty())
            {
                // Choice 0 is the bind pose; reading here is safe because builds only read too, and
                // remounts happen on this thread between builds.
                const auto choices = animation_paths.size() + 1U;
                animation_choice = (animation_choice + (animation_step > 0 ? 1U : choices - 1U)) % choices;
                animation_time = 0.0F;
                visual_animation.reset();
                if (animation_choice == 0)
                {
                    std::cout << "Animation: bind pose\n";
                    animation_status = "bind pose";
                    renderer.set_bone_transforms(bind_palette);
                }
                else
                {
                    const auto& path = animation_paths[animation_choice - 1U];
                    try
                    {
                        visual_animation = eaw::AlaAnimation::parse(vfs.read(path));
                        animation_status = path;
                        std::cout << "Animation " << animation_choice << "/" << animation_paths.size() << ": " << path << " | " << visual_animation->playable_frame_count() << " frames, "
                                  << visual_animation->duration_seconds() << " seconds\n";
                    }
                    catch (const std::exception& error)
                    {
                        std::cerr << "animation warning: " << path << ": " << error.what() << "; showing the bind pose\n";
                        renderer.set_bone_transforms(bind_palette);
                    }
                }
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
            if (pending_content.valid() && pending_content.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                // A failed load (for example an invalid scene) keeps the current view.
                try
                {
                    apply_content(pending_content.get());
                }
                catch (const std::exception& error)
                {
                    std::cerr << "scene warning: " << error.what() << "; keeping the validation triangle\n";
                }
                if (!content_shown_at)
                {
                    content_shown_at = renderer.stats().frame_number;
                }
                textures_streamed = false;
                streaming_since = renderer.stats().frame_number;
                animation_tick = std::chrono::steady_clock::now();
                next_reload_check = animation_tick + std::chrono::seconds(1);
            }
            // Hot reload: once a second (and never during a build) a worker compares the scene's
            // dependencies and the MEG archives with their recorded state. Rebuilt archives are
            // remounted here, while no other thread reads the file system, then the scene rebuilds
            // in the background from the cache.
            if (options.hot_reload && scene_dependencies && !pending_content.valid())
            {
                if (pending_check.valid() && pending_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    auto check = pending_check.get();
                    if (!reload_check_reported)
                    {
                        reload_check_reported = true;
                        std::cout << "Hot reload: watching " << scene_dependencies->file_count() << " files and " << scene_dependencies->listing_count() << " listings; a check takes " << check.seconds
                                  << " s\n";
                        reload_status = "watching " + std::to_string(scene_dependencies->file_count()) + " files";
                    }
                    next_reload_check = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    bool remounted = true;
                    for (const auto index : check.archives)
                    {
                        const auto& mount = vfs.mounts()[index];
                        const auto archive = mount->source_path();
                        try
                        {
                            vfs.replace_mount(index, std::make_unique<eaw::MegMount>(std::string(mount->name()), archive));
                            archive_watch.accept(vfs, index);
                            std::cout << "Hot reload: remounted " << archive.string() << '\n';
                        }
                        catch (const std::exception& error)
                        {
                            // Typically the archive is still being written; try again next check.
                            std::cerr << "hot reload warning: " << archive.string() << " could not be remounted (" << error.what() << "); retrying\n";
                            remounted = false;
                        }
                    }
                    if (remounted && (!check.archives.empty() || !check.files.empty()))
                    {
                        std::cout << "Hot reload: " << check.files.size() << " changed dependencies";
                        for (std::size_t index = 0; index < check.files.size() && index < 3; ++index)
                        {
                            std::cout << (index == 0 ? " (" : ", ") << check.files[index];
                        }
                        std::cout << (check.files.size() > 3 ? ", ...)" : check.files.empty() ? "" : ")") << "; rebuilding\n";
                        pending_content = std::async(std::launch::async, build_sandbox_content, std::cref(vfs), std::cref(options), &asset_cache, true);
                        reload_status = std::to_string(check.files.size()) + " changes, rebuilding...";
                    }
                }
                else if (!pending_check.valid() && std::chrono::steady_clock::now() >= next_reload_check)
                {
                    pending_check = std::async(std::launch::async,
                                               [&vfs, &archive_watch, dependencies = *scene_dependencies]
                                               {
                                                   const auto start = std::chrono::steady_clock::now();
                                                   HotReloadCheck result{archive_watch.changed(vfs), dependencies.changes(vfs), 0.0};
                                                   result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                                                   return result;
                                               });
                }
            }
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
            if (options.pick && content_shown_at && !automated_pick_sent)
            {
                renderer.request_pick((*options.pick)[0], (*options.pick)[1]);
                automated_pick_sent = true;
            }
            if (input.keys[static_cast<std::size_t>(eaw::Key::f1)].pressed)
            {
                overlay_visible = !overlay_visible;
                if (!overlay_visible)
                {
                    renderer.set_overlay_text({});
                }
            }
            // The panel is refreshed four times a second so the numbers stay readable.
            ++overlay_frames;
            if (overlay_visible && now - overlay_refreshed >= std::chrono::milliseconds(250))
            {
                const auto seconds = std::chrono::duration<double>(now - overlay_refreshed).count();
                const auto stats = renderer.stats();
                const auto mebibytes = [](const std::uint64_t bytes)
                {
                    return std::to_string(bytes / (1024ULL * 1024ULL));
                };
                const auto& eye = tactical_camera ? tactical_camera->camera().eye : orbit_camera->camera().eye;
                const auto used_slots = stats.texture_slot_capacity - std::min<std::size_t>(stats.free_texture_slots + stats.pending_texture_slots, stats.texture_slot_capacity);
                std::vector<std::string> lines{
                    "EAWEngine sandbox | " + renderer.adapter_name(),
                    std::to_string(stats.width) + "x" + std::to_string(stats.height) + " | " +
                        (options.vsync   ? "vsync"
                         : stats.tearing ? "immediate (tearing)"
                                         : "immediate") +
                        " | latency " + std::to_string(options.frame_latency),
                    "",
                    "FPS " + format_fixed(static_cast<double>(overlay_frames) / seconds, 1) + " | CPU " + format_fixed(seconds * 1000.0 / static_cast<double>(overlay_frames), 2) + " ms | GPU " +
                        format_fixed(stats.gpu_frame_milliseconds, 2) + " ms",
                    "Draw calls " + std::to_string(stats.draw_calls) + " | triangles " + std::to_string(stats.triangles) + " | instances " + std::to_string(stats.instances),
                    "Video memory " + mebibytes(stats.video_memory_usage) + " / " + mebibytes(stats.video_memory_budget) + " MiB budget",
                    "Textures " + std::to_string(stats.live_textures) + " resident, " + std::to_string(stats.pending_texture_uploads) + " streaming | slots " + std::to_string(used_slots) + "/" +
                        std::to_string(stats.texture_slot_capacity) + " | retiring " + std::to_string(stats.pending_retirements),
                    "",
                    "Scene: " + scene_status,
                    "Camera: " + format_fixed(eye[0], 0) + ", " + format_fixed(eye[1], 0) + ", " + format_fixed(eye[2], 0),
                };
                if (visual_model)
                {
                    lines.push_back("Animation: " + animation_status + (animation_playing ? "" : " (paused)"));
                }
                lines.push_back("Selected: " + (selected_instance && *selected_instance < instance_labels.size() ? instance_labels[*selected_instance] : std::string("none")));
                lines.push_back("Hot reload: " + reload_status);
                lines.push_back("");
                lines.push_back(tactical_camera ? "Click select | B bounds | F1 panel | WASD pan, drag rotate, wheel zoom"
                                                : "Click select | B bounds | F1 panel | drag orbit, wheel zoom, P pause, Left/Right animation");
                renderer.set_overlay_text(lines);
                overlay_frames = 0;
                overlay_refreshed = now;
            }
            const bool rendered = renderer.render();
            if (rendered && reload_scene && renderer.stats().frame_number % *options.reload_every == 0)
            {
                // Load reset the bone palette; restore the bind pose until animation samples again.
                renderer.load_scene(*reload_scene);
                if (!bind_palette.empty())
                {
                    renderer.set_bone_transforms(bind_palette);
                }
                ++scene_reloads;
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
        renderer.wait_idle();
        if (const auto pick = renderer.take_pick())
        {
            std::cout << "Selected: " << (pick->instance && *pick->instance < instance_labels.size() ? instance_labels[*pick->instance] : pick->terrain ? "terrain" : "nothing") << '\n';
        }
        const auto stats = renderer.stats();
        std::cout << "Rendered frames: " << stats.frame_number << '\n';
        if (options.reload_every)
        {
            std::cout << "Scene reloads: " << scene_reloads << '\n';
        }
        std::cout << "Resources after idle: " << stats.live_textures << " textures, " << stats.free_texture_slots << " free and " << stats.pending_texture_slots << " pending texture slots, "
                  << stats.pending_retirements << " pending retirements, " << stats.pending_texture_uploads << " textures still streaming\n";
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
