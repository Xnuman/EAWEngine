#pragma once

// Command line of EAWEngine: the run mode (sandbox, battle, or doctor) and their options.

#include "core/status.hpp"
#include "vfs/asset_environment.hpp"
#include "vfs/game_installation.hpp"
#include "render/visual_anti_aliasing.hpp"
#include "render/visual_lod.hpp"
#include "assets/particle_rules.hpp"

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
    // A battle on a map without a window (battle_runner.hpp).
    battle,
    // Every script under Data/Scripts run headless on recording stubs (lua_survey_command.hpp).
    lua_survey,
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

// The sandbox's default master volume: a fifth of the samples as recorded (an engine choice: the
// owner found full volume too loud, 2026-11-10).
inline constexpr float default_sandbox_volume = 0.2F;

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
    // Screen capture for image regression: the scene (without debug lines or the overlay) is written
    // as PNG `capture_frame` presented frames after its textures became resident, then the window
    // closes. Captures run on a fixed frame time (default 1/30 s when none is given), and that clock
    // holds still until the textures are resident, so the same command renders the same frame.
    std::optional<std::filesystem::path> capture;
    std::uint64_t capture_frame{1};
    // Presentation advances by this many seconds per frame instead of the wall clock (simulation
    // ticks, animation, particles, cameras), so runs repeat frame for frame.
    std::optional<float> fixed_frame_time;
    // Tactical camera on maps: focus (x, y) and view distance, instead of the map's default view.
    std::optional<std::array<float, 3>> camera_view;
    // Starts in the free flying camera at this eye, looking at this target (x, y, z each).
    std::optional<std::array<float, 6>> free_camera;
    // Reload the scene every N rendered frames to exercise resource replacement and retirement.
    std::optional<std::uint64_t> reload_every;
    // Per-frame texture upload budget in MiB; zero uploads everything at once.
    std::uint64_t upload_budget_mib{64};
    // Bloom strength (0 turns bloom off; --no-bloom is the same).
    float bloom_strength{0.8F};
    // Anti-aliasing (--aa off|fxaa|taa|hybrid; --no-aa is off).
    VisualAntiAliasing anti_aliasing{VisualAntiAliasing::fxaa};
    // Key-light shadows (--shadows on|off).
    bool shadows{true};
    // The map's distance fog (--fog on|off).
    bool fog{true};
    // Detail-level selection (--lod) and the mesh-detail setting (--mesh-detail, 0..1).
    VisualLodPolicy lod_policy{VisualLodPolicy::hybrid};
    float mesh_detail{1.0F};
    // How particle effects are interpreted: legacy (as the original) or enhanced.
    ParticleFidelity particle_fidelity{ParticleFidelity::legacy};
    // Watch the scene's source files and archives and rebuild when they change.
    bool hot_reload{};
    // Pick this client pixel once the scene is shown (automation for the selection path).
    std::optional<std::array<std::uint32_t, 2>> pick;
    // Start with every instance's bounds drawn (B toggles at runtime).
    bool show_bounds{};
    // Start with the passability overlay drawn (G toggles at runtime).
    bool show_passability{};
    // The statistics panel starts hidden; the F1 menu shows it.
    bool overlay{};
    // --panel menu|statistics|spawn (repeatable): dev panels open at start.
    struct OpenPanels
    {
        // Accepted for older scenarios: the bar now starts shown.
        bool menu{};
        bool statistics{};
        bool spawn{};
        bool sound{};
        bool camera{};
    };
    OpenPanels panels;
    // Mouse movement and keys through raw input (--no-raw-input uses window messages).
    bool raw_input{true};
    // Deliberately fail in one way to check crash reporting: assert, exception, access-violation.
    std::optional<std::string> crash_test;
    // Folder for log files (default: "logs" next to the executable).
    std::filesystem::path log_directory;
    // Once the scene is shown, select every mobile entity and order it to the scene center.
    bool auto_move{};
    // Game-object scripts (XML Lua_Script) run in battles; --no-scripts turns them off (sandbox and
    // headless battles alike, so recordings replay the same way).
    bool scripts{true};
    // The audio backend (audio_backends(): xaudio2 by default, fmod when built, none) and the language
    // of localized text and speech archives (one setting for both for now).
    std::string audio{"xaudio2"};
    // Master volume, 0 to 1 (--volume; the sound panel changes it at runtime).
    float volume{default_sandbox_volume};
    std::string language{default_language};
    // XML object types made spawnable on maps; counts spawn when shown, each request as its own
    // group for its owner slot.
    std::vector<SandboxSpawnRequest> spawns;
    // --spawn-later: types not built into the map; once it is shown they are loaded at runtime
    // (built and registered like a first N press on an unloaded type) and spawned (automation).
    std::vector<SandboxSpawnRequest> later_spawns;
    // --effect: particle effects (model references) played at the map center, restarting when they
    // end (repeatable).
    std::vector<std::string> effects;
    // Battles: total simulation ticks to run (battle mode; default 3000, or the replayed log's end),
    // a file to write the order log to when the battle ends (battle and sandbox modes), a log to
    // replay instead of the spawn and auto-move options, and whether to run the battle twice and
    // compare the state hashes.
    std::optional<std::uint64_t> ticks;
    std::optional<std::filesystem::path> record;
    std::optional<std::filesystem::path> replay;
    // lua-survey: a folder for its api.tsv and scripts.tsv reports.
    std::optional<std::filesystem::path> report;
    bool check_determinism{};
    bool vsync{true};
    bool use_warp{};
    bool gpu_validation{};
    bool validation_triangle{};
};

void print_sandbox_usage();

// Parses argv[1..argc). Unknown options and invalid values are errors naming them.
[[nodiscard]] Result<SandboxOptions> parse_sandbox_options(int argc, const char* const* argv);

} // namespace eaw
