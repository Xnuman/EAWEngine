#include "lua_survey_command.hpp"
#include "image_compare_command.hpp"
#include "vfs/asset_environment.hpp"
#include "vfs/game_installation.hpp"
#include "core/status.hpp"
#include "vfs/virtual_file_system.hpp"

#include "battle_runner.hpp"
#include "sandbox_options.hpp"

#ifdef _WIN32
#include "eaw/crash_handler.hpp"
#include "core/fatal_error.hpp"
#include "core/log.hpp"
#include "eaw/log_sinks.hpp"
#include "sandbox_app.hpp"
#endif

#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

int main(const int argc, char** argv)
{
    // Programming errors and unrecoverable failures end the process with a clear report (an error
    // window plus stderr) through the crash handlers. Invalid command lines are usage errors, a
    // window or GPU device that cannot be created is reported before exiting, and malformed game
    // or mod data produces warnings.
#ifdef _WIN32
    bool error_dialogs = true;
    for (int index = 1; index < argc; ++index)
    {
        error_dialogs = error_dialogs && std::string_view(argv[index]) != "--no-error-dialog";
    }
    eaw::install_crash_handlers({"EAWEngine", error_dialogs});
#endif
    // Status lines (builds, streaming, hot reload) appear as they happen even when redirected.
    std::cout << std::unitbuf;
    // Image regression needs no game installation, window, or device.
    if (argc >= 2 && std::string_view(argv[1]) == "compare-images")
    {
        return eaw::run_image_compare(argc, argv);
    }
    auto parse_result = eaw::parse_sandbox_options(argc, argv);
    if (!parse_result)
    {
        std::cerr << "error: " << parse_result.error().message << "\nRun with --help for the available options.\n";
        return 2;
    }
    const eaw::SandboxOptions parsed_options = std::move(parse_result).value();
    if (parsed_options.show_help)
    {
        eaw::print_sandbox_usage();
        return 0;
    }
#ifdef _WIN32
    // Warnings and errors go to stderr, the debugger output window, and a log file.
    if (const auto log_file = eaw::install_log_sinks({"EAWEngine", parsed_options.log_directory}))
    {
        std::cout << "Log: " << log_file->string() << '\n';
    }
    else
    {
        eaw::log_warning("log", log_file.error());
    }
#endif
    {
        const auto& options = parsed_options;
        if (options.crash_test)
        {
            if (*options.crash_test == "assert")
            {
                EAW_ASSERT_MSG(options.crash_test->empty(), "deliberate failure requested by --crash-test assert");
            }
            else if (*options.crash_test == "exception")
            {
                // EAWEngine is built without C++ exceptions; this reports what an unhandled one did.
                eaw::fatal_error("simulated unhandled exception requested by --crash-test exception");
            }
            else
            {
                volatile int* null_pointer = nullptr;
                *null_pointer = 1;
            }
        }
        if (options.game_root)
        {
            if (const auto root = eaw::check_installation_root(*options.game_root); !root)
            {
                std::cerr << "error: --game-root: " << root.error().message << '\n';
                return 2;
            }
        }
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
        const auto mount_report = eaw::mount_game_assets(vfs, *profile, options.mods, options.language);
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
        if (!bytes)
        {
            std::cerr << "Probe could not be read: " << bytes.error().message << '\n';
            return 3;
        }
        std::cout << "Probe: " << resolved->virtual_path << '\n' << "Source: " << resolved->mount_name << " -> " << resolved->container << '\n' << "Size: " << bytes->size() << " bytes\n";

        if (options.mode == eaw::RunMode::doctor)
        {
            std::cout << "Status: compatibility bootstrap succeeded.\n";
            return 0;
        }
        if (options.mode == eaw::RunMode::battle)
        {
            return eaw::run_battle(options, vfs);
        }
        if (options.mode == eaw::RunMode::lua_survey)
        {
            return eaw::run_lua_survey(options, vfs);
        }

#ifdef _WIN32
        return eaw::run_sandbox(options, vfs, *profile);
#else
        std::cerr << "The Direct3D 12 visual sandbox is available on Windows only.
                     ";
            return 2;
#endif
    }
}
