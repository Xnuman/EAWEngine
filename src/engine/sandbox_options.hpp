#pragma once

// Command line of EAWEngine: the run mode (sandbox or doctor) and the sandbox's options.

#include "eaw/game_installation.hpp"
#include "eaw/status.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace eaw {

enum class RunMode
{
    sandbox,
    doctor,
};

// One --spawn TYPE[:COUNT][@OWNER] request.
struct SandboxSpawnRequest
{
    std::string type;
    std::uint32_t count{};
    // Owner slot; the sandbox's current spawn owner when absent.
    std::optional<std::uint32_t> owner;
};

struct SandboxOptions
{
    RunMode mode{RunMode::sandbox};
    // --help: print the usage and exit.
    bool show_help{};
    std::optional<std::filesystem::path> game_root;
    GameVariant variant{GameVariant::forces_of_corruption};
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
    // Start with the passability overlay drawn (G toggles at runtime).
    bool show_passability{};
    // The statistics panel starts hidden; F1 shows it.
    bool overlay{};
    // Mouse movement and keys through raw input (--no-raw-input uses window messages).
    bool raw_input{true};
    // Deliberately fail in one way to check crash reporting: assert, exception, access-violation.
    std::optional<std::string> crash_test;
    // Folder for log files (default: "logs" next to the executable).
    std::filesystem::path log_directory;
    // Once the scene is shown, select every mobile entity and order it to the scene center.
    bool auto_move{};
    // XML object types made spawnable on maps; counts spawn when shown, each request as its own
    // group for its owner slot.
    std::vector<SandboxSpawnRequest> spawns;
    bool vsync{true};
    bool use_warp{};
    bool gpu_validation{};
    bool validation_triangle{};
};

void print_sandbox_usage();

// Parses argv[1..argc). Unknown options and invalid values are errors naming them.
[[nodiscard]] Result<SandboxOptions> parse_sandbox_options(int argc, const char* const* argv);

} // namespace eaw
