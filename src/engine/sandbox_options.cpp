#include "sandbox_options.hpp"

#include "eaw/combat.hpp"

#include <charconv>
#include <iostream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace eaw {
namespace {

// A whole decimal number (digits only).
Result<std::uint64_t> parse_unsigned_64(const std::string_view value, const std::string_view option)
{
    std::uint64_t parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size())
    {
        return make_error("invalid value for " + std::string(option) + ": " + std::string(value));
    }
    return parsed;
}

Result<std::uint32_t> parse_unsigned(const std::string_view value, const std::string_view option)
{
    EAW_TRY_ASSIGN(const auto parsed, parse_unsigned_64(value, option));
    if (parsed > std::numeric_limits<std::uint32_t>::max())
    {
        return make_error("invalid value for " + std::string(option) + ": " + std::string(value));
    }
    return static_cast<std::uint32_t>(parsed);
}

Result<std::string> require_value(const int argc, const char* const* argv, int& index, const std::string& option)
{
    if (index + 1 >= argc)
    {
        return make_error("missing value after " + option);
    }
    return std::string(argv[++index]);
}

} // namespace

void print_sandbox_usage()
{
    std::cout << "EAWEngine visual sandbox\n\n"
              << "Usage: EAWEngine [sandbox|battle|doctor|bootstrap] [options]\n"
              << "  sandbox            Open the Direct3D 12 visual sandbox (default)\n"
              << "  battle             Run a battle on --map without a window and print its state hash and statistics\n"
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
              << "  --show-passability Start with the map's passability classes drawn (G toggles)\n"
              << "  --overlay          Start with the on-screen statistics panel shown (hidden by default; the F1 menu toggles it)\n"
              << "  --panel <menu|statistics|spawn>  Open a dev panel at start (repeatable; F1 opens the menu)\n"
              << "  --no-raw-input     Read mouse movement and keys from window messages instead of raw input\n"
              << "  --no-error-dialog  Report fatal errors on stderr only (no error window)\n"
              << "  --log-dir <path>   Folder for log files (default: logs next to the executable)\n"
              << "  --crash-test <assert|exception|access-violation>  Fail deliberately to check crash reporting\n"
              << "  --auto-move        Order every mobile unit to the map center once shown (automation)\n"
              << "  --spawn <TYPE>[:N][@OWNER] Make an XML object type spawnable on maps (repeatable); N spawn at the view\n"
              << "                     center when shown (OWNER slot, default 1; each request its own group), N key\n"
              << "                     spawns one at the cursor, Tab cycles types, O cycles the owner\n"
              << "  --spawn-later <TYPE>[:N][@OWNER] Load a type at runtime once the map is shown and spawn N (automation)\n"
              << "  --effect <NAME>          Play a particle effect (e.g. P_EXPLOSION_SMALL) at the map center, repeating (maps)\n"
              << "  --reload-every <frames>  Replace the scene with a fresh copy every N frames (lifetime test)\n"
              << "  --ticks <count>    Battle: total simulation ticks to run (default 3000, or the replayed log's end)\n"
              << "  --record <file>    Write the battle's order log when it ends (battle and sandbox)\n"
              << "  --replay <file>    Battle: replay a recorded order log (its map unless --map is given) and check its state hash\n"
              << "  --check-determinism  Battle: run twice and require identical state hashes\n"
              << "  --warp             Use the Windows software D3D12 adapter\n"
              << "  --gpu-validation   Enable D3D12 GPU-based validation\n"
              << "  --validation-triangle  Skip model loading and show the fallback\n";
}

Result<SandboxOptions> parse_sandbox_options(const int argc, const char* const* argv)
{
    SandboxOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "sandbox")
        {
            options.mode = RunMode::sandbox;
        }
        else if (argument == "battle")
        {
            options.mode = RunMode::battle;
        }
        else if (argument == "--ticks")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.ticks, parse_unsigned_64(text, argument));
            if (*options.ticks == 0 || *options.ticks > 100'000'000ULL)
            {
                return make_error("--ticks must be from 1 to 100000000");
            }
        }
        else if (argument == "--record")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            options.record = std::filesystem::path(value);
        }
        else if (argument == "--replay")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            options.replay = std::filesystem::path(value);
        }
        else if (argument == "--check-determinism")
        {
            options.check_determinism = true;
        }
        else if (argument == "doctor" || argument == "bootstrap")
        {
            options.mode = RunMode::doctor;
        }
        else if (argument == "--help" || argument == "-h")
        {
            options.show_help = true;
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
        else if (argument == "--show-passability")
        {
            options.show_passability = true;
        }
        else if (argument == "--overlay")
        {
            options.overlay = true;
        }
        else if (argument == "--panel")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            if (value == "menu")
            {
                options.panels.menu = true;
            }
            else if (value == "statistics")
            {
                options.panels.statistics = true;
            }
            else if (value == "spawn")
            {
                options.panels.spawn = true;
            }
            else
            {
                return make_error("--panel must be menu, statistics, or spawn");
            }
        }
        else if (argument == "--no-overlay")
        {
            // The default since the panel starts hidden; still accepted for older scripts.
            options.overlay = false;
        }
        else if (argument == "--no-raw-input")
        {
            options.raw_input = false;
        }
        else if (argument == "--log-dir")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            options.log_directory = std::filesystem::path(value);
        }
        else if (argument == "--crash-test")
        {
            EAW_TRY_ASSIGN(options.crash_test, require_value(argc, argv, index, argument));
            if (*options.crash_test != "assert" && *options.crash_test != "exception" && *options.crash_test != "access-violation")
            {
                return make_error("--crash-test must be assert, exception, or access-violation");
            }
        }
        else if (argument == "--no-error-dialog")
        {
            // Read before parsing (crash handlers are installed first); accepted here as known.
        }
        else if (argument == "--auto-move")
        {
            options.auto_move = true;
        }
        else if (argument == "--spawn" || argument == "--spawn-later")
        {
            EAW_TRY_ASSIGN(std::string value, require_value(argc, argv, index, argument));
            std::optional<std::uint32_t> owner;
            if (const auto at = value.rfind('@'); at != std::string::npos)
            {
                EAW_TRY_ASSIGN(const auto slot, parse_unsigned(std::string_view(value).substr(at + 1U), argument));
                if (slot >= Hostility::max_owners)
                {
                    return make_error("--spawn owners must be below " + std::to_string(Hostility::max_owners));
                }
                owner = slot;
                value.resize(at);
            }
            const auto colon = value.rfind(':');
            std::uint32_t count = 0;
            auto type = value;
            if (colon != std::string::npos)
            {
                type = value.substr(0, colon);
                EAW_TRY_ASSIGN(count, parse_unsigned(std::string_view(value).substr(colon + 1U), argument));
                if (count > 1000)
                {
                    return make_error("--spawn counts are limited to 1000 per type");
                }
            }
            if (type.empty())
            {
                return make_error("--spawn needs an XML object type name");
            }
            (argument == "--spawn" ? options.spawns : options.later_spawns).push_back({std::move(type), count, owner});
        }
        else if (argument == "--effect")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            options.effects.emplace_back(value);
        }
        else if (argument == "--pick")
        {
            EAW_TRY_ASSIGN(const auto x_text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(const auto x, parse_unsigned(x_text, argument));
            EAW_TRY_ASSIGN(const auto y_text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(const auto y, parse_unsigned(y_text, argument));
            options.pick = std::array<std::uint32_t, 2>{x, y};
        }
        else if (argument == "--validation-triangle")
        {
            options.validation_triangle = true;
        }
        else if (argument == "--game-root")
        {
            EAW_TRY_ASSIGN(options.game_root, require_value(argc, argv, index, argument));
        }
        else if (argument == "--game")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            const auto variant = parse_game_variant(value);
            if (!variant)
            {
                return make_error("unknown game variant: " + value);
            }
            options.variant = *variant;
        }
        else if (argument == "--mod")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
            options.mods.emplace_back(value);
        }
        else if (argument == "--probe")
        {
            EAW_TRY_ASSIGN(options.probe, require_value(argc, argv, index, argument));
        }
        else if (argument == "--model")
        {
            EAW_TRY_ASSIGN(options.model, require_value(argc, argv, index, argument));
        }
        else if (argument == "--animation")
        {
            EAW_TRY_ASSIGN(options.animation, require_value(argc, argv, index, argument));
        }
        else if (argument == "--map")
        {
            EAW_TRY_ASSIGN(options.map, require_value(argc, argv, index, argument));
        }
        else if (argument == "--pipeline-cache")
        {
            EAW_TRY_ASSIGN(options.pipeline_cache, require_value(argc, argv, index, argument));
        }
        else if (argument == "--width")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.width, parse_unsigned(text, argument));
        }
        else if (argument == "--height")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.height, parse_unsigned(text, argument));
        }
        else if (argument == "--frame-latency")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.frame_latency, parse_unsigned(text, argument));
            if (options.frame_latency < 1 || options.frame_latency > 2)
            {
                return make_error("--frame-latency must be 1 or 2");
            }
        }
        else if (argument == "--upload-budget")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.upload_budget_mib, parse_unsigned_64(text, argument));
            if (options.upload_budget_mib > 1024ULL * 1024ULL)
            {
                return make_error("--upload-budget is limited to 1048576 MiB");
            }
        }
        else if (argument == "--reload-every")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.reload_every, parse_unsigned_64(text, argument));
            if (*options.reload_every == 0)
            {
                return make_error("--reload-every must be greater than zero");
            }
        }
        else if (argument == "--frame-limit")
        {
            EAW_TRY_ASSIGN(const auto text, require_value(argc, argv, index, argument));
            EAW_TRY_ASSIGN(options.frame_limit, parse_unsigned_64(text, argument));
            if (*options.frame_limit == 0)
            {
                return make_error("--frame-limit must be greater than zero");
            }
        }
        else if (argument == "--vsync")
        {
            EAW_TRY_ASSIGN(const auto value, require_value(argc, argv, index, argument));
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
                return make_error("--vsync must be 'on' or 'off'");
            }
        }
        else
        {
            return make_error("unknown option or mode: " + argument);
        }
    }

    if (options.width == 0 || options.height == 0)
    {
        return make_error("sandbox width and height must be greater than zero");
    }
    if (options.mode == RunMode::battle && !options.map && !options.replay)
    {
        return make_error("battle needs --map or --replay");
    }
    if (options.replay && (options.mode != RunMode::battle || !options.spawns.empty() || options.auto_move))
    {
        return make_error("--replay runs in battle mode and takes its orders from the log, not --spawn or --auto-move");
    }
    return options;
}

} // namespace eaw
