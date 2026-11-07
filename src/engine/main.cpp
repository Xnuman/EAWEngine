#include "eaw/asset_environment.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/virtual_file_system.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options
{
    std::optional<std::filesystem::path> game_root;
    eaw::GameVariant variant{eaw::GameVariant::forces_of_corruption};
    std::vector<std::filesystem::path> mods;
    std::string probe{"Data/XML/GameObjectFiles.xml"};
};

void print_usage()
{
    std::cout << "EAWEngine compatibility bootstrap\n\n"
              << "Usage: EAWEngine [doctor|bootstrap] [options]\n"
              << "  --game-root <path>  Empire at War installation root\n"
              << "  --game <eaw|foc>    Select the base game or expansion\n"
              << "  --mod <path>        Add a mod root (repeatable; first has priority)\n"
              << "  --probe <asset>     Resolve and read a virtual asset path\n";
}

Options parse_options(const int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "doctor" || argument == "bootstrap")
        {
            continue;
        }
        if (argument == "--help" || argument == "-h")
        {
            print_usage();
            std::exit(0);
        }
        if (index + 1 >= argc)
        {
            throw std::invalid_argument("missing value after " + argument);
        }
        const std::string value = argv[++index];
        if (argument == "--game-root")
        {
            options.game_root = value;
        }
        else if (argument == "--game")
        {
            const auto variant = eaw::parse_game_variant(value);
            if (!variant)
            {
                throw std::invalid_argument("unknown game variant: " + value);
            }
            options.variant = *variant;
        }
        else if (argument == "--mod")
        {
            options.mods.emplace_back(value);
        }
        else if (argument == "--probe")
        {
            options.probe = value;
        }
        else
        {
            throw std::invalid_argument("unknown option: " + argument);
        }
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

        std::cout << "EAWEngine bootstrap 0.1.0\n"
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
        std::cout << "Probe: " << resolved->virtual_path << '\n'
                  << "Source: " << resolved->mount_name << " -> " << resolved->container << '\n'
                  << "Size: " << bytes.size() << " bytes\n"
                  << "Status: compatibility bootstrap succeeded; gameplay runtime is not implemented yet.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
