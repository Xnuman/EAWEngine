#include "eaw/child_process.hpp"
#include "vfs/game_installation.hpp"
#include "core/status.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

int main(const int argc, char** argv)
{
    std::optional<std::filesystem::path> explicit_root;
    std::vector<std::string> forwarded{"sandbox"};
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--game-root" && index + 1 < argc)
        {
            explicit_root = argv[index + 1];
        }
        forwarded.push_back(argument);
    }

    const eaw::InstallationLocator locator;
    const auto installations = locator.discover(explicit_root);
    if (installations.empty())
    {
        std::cerr << "EAWLauncher could not find Empire at War. "
                     "Pass --game-root <path>.\n";
        return 2;
    }
    if (!explicit_root)
    {
        forwarded.push_back("--game-root");
        forwarded.push_back(installations.front().string());
    }

    std::error_code path_error;
    const auto launcher = std::filesystem::absolute(argv[0], path_error);
    if (path_error)
    {
        std::cerr << "launcher error: could not resolve the launcher path: " << path_error.message() << '\n';
        return 1;
    }
    const auto engine = launcher.parent_path() / EAW_ENGINE_FILE_NAME;
    if (!std::filesystem::is_regular_file(engine, path_error))
    {
        std::cerr << EAW_ENGINE_FILE_NAME << " is not next to the launcher: " << engine << '\n';
        return 2;
    }

    std::cout << "Launching the new engine against read-only game data at:\n  " << installations.front() << '\n';
    auto child = eaw::ChildProcess::start(engine, forwarded);
    if (!child)
    {
        std::cerr << "launcher error: " << child.error().message << '\n';
        return 1;
    }
    const auto exit_code = child->wait();
    if (!exit_code)
    {
        std::cerr << "launcher error: " << exit_code.error().message << '\n';
        return 1;
    }
    return *exit_code;
}
