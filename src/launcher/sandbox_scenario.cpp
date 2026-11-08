#include "eaw/sandbox_scenario.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <utility>

namespace eaw {
namespace {

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<std::uint32_t> parse_unsigned(const std::string_view text) noexcept
{
    std::uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

// Boolean options, by their command-line and preset name.
struct BoolOption
{
    std::string_view name;
    bool SandboxScenario::* field;
};
constexpr std::array<BoolOption, 8> bool_options{{
    {"warp", &SandboxScenario::warp},
    {"gpu-validation", &SandboxScenario::gpu_validation},
    {"hot-reload", &SandboxScenario::hot_reload},
    {"show-bounds", &SandboxScenario::show_bounds},
    {"show-passability", &SandboxScenario::show_passability},
    {"auto-move", &SandboxScenario::auto_move},
    {"overlay", &SandboxScenario::overlay},
    {"no-raw-input", &SandboxScenario::no_raw_input},
}};

// Numeric options; zero means "not set" except for the optional upload budget.
struct NumberOption
{
    std::string_view name;
    std::uint32_t SandboxScenario::* field;
};
constexpr std::array<NumberOption, 3> number_options{{
    {"frame-limit", &SandboxScenario::frame_limit},
    {"width", &SandboxScenario::width},
    {"height", &SandboxScenario::height},
}};

std::string_view scene_name(const SandboxScene scene) noexcept
{
    switch (scene)
    {
    case SandboxScene::map:
        return "map";
    case SandboxScene::model:
        return "model";
    case SandboxScene::validation_triangle:
        return "validation-triangle";
    }
    return "map";
}

} // namespace

std::string format_spawn(const ScenarioSpawn& spawn)
{
    auto text = spawn.type + ":" + std::to_string(spawn.count);
    if (spawn.owner)
    {
        text += "@" + std::to_string(*spawn.owner);
    }
    return text;
}

Result<ScenarioSpawn> parse_spawn(std::string_view text)
{
    text = trim(text);
    ScenarioSpawn spawn;
    if (const auto at = text.rfind('@'); at != std::string_view::npos)
    {
        const auto owner = parse_unsigned(text.substr(at + 1U));
        if (!owner)
        {
            return make_error("spawn '" + std::string(text) + "' has an invalid owner");
        }
        spawn.owner = owner;
        text = text.substr(0, at);
    }
    if (const auto colon = text.rfind(':'); colon != std::string_view::npos)
    {
        const auto count = parse_unsigned(text.substr(colon + 1U));
        if (!count)
        {
            return make_error("spawn '" + std::string(text) + "' has an invalid count");
        }
        spawn.count = *count;
        text = text.substr(0, colon);
    }
    spawn.type = std::string(trim(text));
    if (spawn.type.empty())
    {
        return make_error("spawn has no object type");
    }
    return spawn;
}

Result<std::vector<std::string>> split_arguments(const std::string_view text)
{
    std::vector<std::string> arguments;
    std::string current;
    bool in_argument = false;
    bool quoted = false;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const char character = text[index];
        if (character == '\\' && index + 1 < text.size() && text[index + 1] == '"')
        {
            current.push_back('"');
            in_argument = true;
            ++index;
        }
        else if (character == '"')
        {
            quoted = !quoted;
            in_argument = true;
        }
        else if (!quoted && std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            if (in_argument)
            {
                arguments.push_back(std::move(current));
                current.clear();
                in_argument = false;
            }
        }
        else
        {
            current.push_back(character);
            in_argument = true;
        }
    }
    if (quoted)
    {
        return make_error("extra arguments have an unclosed quote");
    }
    if (in_argument)
    {
        arguments.push_back(std::move(current));
    }
    return arguments;
}

Result<std::vector<std::string>> sandbox_arguments(const SandboxScenario& scenario)
{
    std::vector<std::string> arguments{"sandbox", "--game", std::string(to_string(scenario.game))};
    if (!scenario.game_root.empty())
    {
        arguments.insert(arguments.end(), {"--game-root", scenario.game_root});
    }
    for (const auto& mod : scenario.mods)
    {
        arguments.insert(arguments.end(), {"--mod", mod});
    }
    switch (scenario.scene)
    {
    case SandboxScene::map:
        if (scenario.map.empty())
        {
            return make_error("the scenario has no map");
        }
        arguments.insert(arguments.end(), {"--map", scenario.map});
        for (const auto& spawn : scenario.spawns)
        {
            if (spawn.type.empty())
            {
                return make_error("a spawn has no object type");
            }
            arguments.insert(arguments.end(), {"--spawn", format_spawn(spawn)});
        }
        break;
    case SandboxScene::model:
        if (scenario.model.empty())
        {
            return make_error("the scenario has no model");
        }
        arguments.insert(arguments.end(), {"--model", scenario.model});
        if (!scenario.animation.empty())
        {
            arguments.insert(arguments.end(), {"--animation", scenario.animation});
        }
        break;
    case SandboxScene::validation_triangle:
        arguments.emplace_back("--validation-triangle");
        break;
    }
    for (const auto& option : bool_options)
    {
        if (scenario.*option.field)
        {
            arguments.push_back("--" + std::string(option.name));
        }
    }
    if (!scenario.vsync)
    {
        arguments.insert(arguments.end(), {"--vsync", "off"});
    }
    for (const auto& option : number_options)
    {
        if (const auto value = scenario.*option.field; value != 0)
        {
            arguments.insert(arguments.end(), {"--" + std::string(option.name), std::to_string(value)});
        }
    }
    if (scenario.upload_budget)
    {
        arguments.insert(arguments.end(), {"--upload-budget", std::to_string(*scenario.upload_budget)});
    }
    EAW_TRY_ASSIGN(const auto extra, split_arguments(scenario.extra_arguments));
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    return arguments;
}

std::string serialize_scenarios(const std::span<const SandboxScenario> scenarios)
{
    std::string text;
    const auto line = [&](const std::string_view key, const std::string_view value)
    {
        text.append(key).append("=").append(value).append("\n");
    };
    for (const auto& scenario : scenarios)
    {
        if (!text.empty())
        {
            text += "\n";
        }
        text += "[scenario]\n";
        line("name", scenario.name);
        line("game", to_string(scenario.game));
        if (!scenario.game_root.empty())
        {
            line("game-root", scenario.game_root);
        }
        for (const auto& mod : scenario.mods)
        {
            line("mod", mod);
        }
        line("scene", scene_name(scenario.scene));
        if (!scenario.map.empty())
        {
            line("map", scenario.map);
        }
        if (!scenario.model.empty())
        {
            line("model", scenario.model);
        }
        if (!scenario.animation.empty())
        {
            line("animation", scenario.animation);
        }
        for (const auto& spawn : scenario.spawns)
        {
            line("spawn", format_spawn(spawn));
        }
        for (const auto& option : bool_options)
        {
            if (scenario.*option.field)
            {
                line("option", option.name);
            }
        }
        line("vsync", scenario.vsync ? "on" : "off");
        for (const auto& option : number_options)
        {
            if (const auto value = scenario.*option.field; value != 0)
            {
                line(option.name, std::to_string(value));
            }
        }
        if (scenario.upload_budget)
        {
            line("upload-budget", std::to_string(*scenario.upload_budget));
        }
        if (!scenario.extra_arguments.empty())
        {
            line("extra", scenario.extra_arguments);
        }
    }
    return text;
}

Result<ScenarioFile> parse_scenarios(const std::string_view text)
{
    ScenarioFile file;
    SandboxScenario* current = nullptr;
    std::size_t start = 0;
    std::size_t number = 0;
    while (start < text.size())
    {
        const auto end = text.find('\n', start);
        auto raw = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() : end + 1;
        ++number;
        const auto line = trim(raw);
        const auto where = "line " + std::to_string(number);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (line == "[scenario]")
        {
            current = &file.scenarios.emplace_back();
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            return make_error(where + ": expected [scenario] or key=value");
        }
        if (current == nullptr)
        {
            return make_error(where + ": key before the first [scenario]");
        }
        const auto key = trim(line.substr(0, equals));
        const auto value = trim(line.substr(equals + 1U));
        auto& scenario = *current;
        if (key == "name")
        {
            scenario.name = std::string(value);
        }
        else if (key == "game")
        {
            if (const auto game = parse_game_variant(value))
            {
                scenario.game = *game;
            }
            else
            {
                file.warnings.push_back(where + ": unknown game '" + std::string(value) + "'");
            }
        }
        else if (key == "game-root")
        {
            scenario.game_root = std::string(value);
        }
        else if (key == "mod")
        {
            scenario.mods.emplace_back(value);
        }
        else if (key == "scene")
        {
            if (value == "map")
            {
                scenario.scene = SandboxScene::map;
            }
            else if (value == "model")
            {
                scenario.scene = SandboxScene::model;
            }
            else if (value == "validation-triangle")
            {
                scenario.scene = SandboxScene::validation_triangle;
            }
            else
            {
                file.warnings.push_back(where + ": unknown scene '" + std::string(value) + "'");
            }
        }
        else if (key == "map")
        {
            scenario.map = std::string(value);
        }
        else if (key == "model")
        {
            scenario.model = std::string(value);
        }
        else if (key == "animation")
        {
            scenario.animation = std::string(value);
        }
        else if (key == "spawn")
        {
            if (auto spawn = parse_spawn(value))
            {
                scenario.spawns.push_back(std::move(spawn).value());
            }
            else
            {
                file.warnings.push_back(where + ": " + spawn.error().message);
            }
        }
        else if (key == "option")
        {
            bool known = false;
            for (const auto& option : bool_options)
            {
                if (option.name == value)
                {
                    scenario.*option.field = true;
                    known = true;
                }
            }
            // Options that became defaults load silently.
            if (!known && value != "no-overlay")
            {
                file.warnings.push_back(where + ": unknown option '" + std::string(value) + "'");
            }
        }
        else if (key == "vsync")
        {
            scenario.vsync = value != "off";
        }
        else if (key == "upload-budget")
        {
            if (const auto budget = parse_unsigned(value))
            {
                scenario.upload_budget = budget;
            }
            else
            {
                file.warnings.push_back(where + ": upload-budget '" + std::string(value) + "' is not a number");
            }
        }
        else if (key == "extra")
        {
            scenario.extra_arguments = std::string(value);
        }
        else
        {
            bool known = false;
            for (const auto& option : number_options)
            {
                if (option.name == key)
                {
                    known = true;
                    if (const auto number_value = parse_unsigned(value))
                    {
                        scenario.*option.field = *number_value;
                    }
                    else
                    {
                        file.warnings.push_back(where + ": " + std::string(key) + " '" + std::string(value) + "' is not a number");
                    }
                }
            }
            if (!known)
            {
                file.warnings.push_back(where + ": unknown key '" + std::string(key) + "'");
            }
        }
    }
    return file;
}

} // namespace eaw
