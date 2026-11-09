#pragma once

// A sandbox launch configuration ("scenario") for development: which installation, mods, scene, debug
// spawns, and engine options to start `EAWEngine sandbox` with. Scenarios turn into an argument
// vector and are stored as plain text presets:
//
//   [scenario]
//   name=Bespin skirmish
//   game=foc
//   map=DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED
//   spawn=T2B_Tank:6@1
//   option=show-passability
//
// Unknown keys and options are kept as warnings, not errors, so presets written by a newer build
// still load.

#include "eaw/game_installation.hpp"
#include "core/status.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

enum class SandboxScene : std::uint8_t
{
    map,
    model,
    validation_triangle,
};

struct ScenarioSpawn
{
    std::string type;
    std::uint32_t count{1};
    // Owner slot; the engine's default when absent.
    std::optional<std::uint32_t> owner;

    friend bool operator==(const ScenarioSpawn&, const ScenarioSpawn&) = default;
};

struct SandboxScenario
{
    std::string name{"Untitled"};
    GameVariant game{GameVariant::forces_of_corruption};
    // Empty: the engine discovers the installation.
    std::string game_root;
    // Highest priority first, as on the engine command line.
    std::vector<std::string> mods;
    SandboxScene scene{SandboxScene::map};
    std::string map;
    std::string model;
    std::string animation;
    std::vector<ScenarioSpawn> spawns;
    bool warp{};
    bool gpu_validation{};
    bool vsync{true};
    bool hot_reload{};
    bool show_bounds{};
    bool show_passability{};
    bool auto_move{};
    // Start with the statistics panel shown (the engine hides it by default).
    bool overlay{};
    bool no_raw_input{};
    std::uint32_t frame_limit{};
    std::optional<std::uint32_t> upload_budget;
    std::uint32_t width{};
    std::uint32_t height{};
    // Further arguments, split like a command line (double quotes group).
    std::string extra_arguments;

    friend bool operator==(const SandboxScenario&, const SandboxScenario&) = default;
};

// "TYPE[:N][@OWNER]", as `--spawn` takes it.
[[nodiscard]] std::string format_spawn(const ScenarioSpawn& spawn);
[[nodiscard]] Result<ScenarioSpawn> parse_spawn(std::string_view text);

// Splits text into arguments: whitespace separates, double quotes group, a backslash escapes a quote.
[[nodiscard]] Result<std::vector<std::string>> split_arguments(std::string_view text);

// The arguments after the program name, starting with "sandbox". Fails when the scene's asset is
// missing or the extra arguments do not split.
[[nodiscard]] Result<std::vector<std::string>> sandbox_arguments(const SandboxScenario& scenario);

struct ScenarioFile
{
    std::vector<SandboxScenario> scenarios;
    // Unknown keys, options, and values, with their line numbers.
    std::vector<std::string> warnings;
};

[[nodiscard]] std::string serialize_scenarios(std::span<const SandboxScenario> scenarios);
// Fails only on structural problems (a key before the first [scenario], a line without '=');
// recoverable problems become warnings.
[[nodiscard]] Result<ScenarioFile> parse_scenarios(std::string_view text);

} // namespace eaw
