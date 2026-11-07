#include "eaw/asset_environment.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/virtual_file_system.hpp"

#ifdef _WIN32
#include "eaw/d3d12_renderer.hpp"
#include "eaw/platform_window.hpp"
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
    std::uint32_t width{1600};
    std::uint32_t height{900};
    std::uint32_t frame_latency{2};
    std::optional<std::uint64_t> frame_limit;
    bool vsync{true};
    bool use_warp{};
    bool gpu_validation{};
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
              << "  --width <pixels>   Initial sandbox client width\n"
              << "  --height <pixels>  Initial sandbox client height\n"
              << "  --vsync <on|off>   Select synchronized or tearing-capable present\n"
              << "  --frame-latency <1|2>  Maximum queued presentation frames\n"
              << "  --frame-limit <count>  Exit after submitted frames (automation)\n"
              << "  --warp             Use the Windows software D3D12 adapter\n"
              << "  --gpu-validation   Enable D3D12 GPU-based validation\n";
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
        eaw::D3d12Renderer renderer(window.native_handle(), window.client_width(), window.client_height(), renderer_options);
        std::cout << "D3D12 adapter: " << renderer.adapter_name() << '\n'
                  << "Present mode: " << (options.vsync ? "vsync" : "immediate when tearing is available") << '\n'
                  << "Frame latency: " << options.frame_latency << '\n'
                  << "Visual sandbox ready. Press Escape or close the window to exit.\n";

        while (!window.close_requested())
        {
            window.poll_events();
            if (window.input().keys[static_cast<std::size_t>(eaw::Key::escape)].pressed)
            {
                window.request_close();
            }
            if (window.close_requested())
            {
                break;
            }
            if (window.minimized())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            renderer.resize(window.client_width(), window.client_height());
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
