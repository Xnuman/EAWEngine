#include "eaw/game_installation.hpp"
#include "eaw/status.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string quote(const std::string& value)
{
    std::string result{"\""};
    std::size_t backslashes = 0;
    for (const char character : value)
    {
        if (character == '\\')
        {
            ++backslashes;
        }
        else if (character == '"')
        {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back('"');
            backslashes = 0;
        }
        else
        {
            result.append(backslashes, '\\');
            backslashes = 0;
            result.push_back(character);
        }
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}

#ifdef _WIN32
eaw::Result<int> launch_engine(const std::filesystem::path& executable, const std::vector<std::string>& arguments)
{
    std::ostringstream command;
    command << quote(executable.string());
    for (const auto& argument : arguments)
    {
        command << ' ' << quote(argument);
    }
    auto mutable_command = command.str();
    mutable_command.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(executable.string().c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr, executable.parent_path().string().c_str(), &startup, &process))
    {
        return eaw::make_error("could not start EAWEngine.exe (Windows error " + std::to_string(GetLastError()) + ")");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}
#endif

} // namespace

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
    const auto engine = launcher.parent_path() / "EAWEngine.exe";
    if (!std::filesystem::is_regular_file(engine, path_error))
    {
        std::cerr << "EAWEngine.exe is not next to the launcher: " << engine << '\n';
        return 2;
    }

    std::cout << "Launching the new engine against read-only game data at:\n  " << installations.front() << '\n';
#ifdef _WIN32
    const auto exit_code = launch_engine(engine, forwarded);
    if (!exit_code)
    {
        std::cerr << "launcher error: " << exit_code.error().message << '\n';
        return 1;
    }
    return *exit_code;
#else
    std::cerr << "The bootstrap launcher currently supports Windows only.\n";
    return 2;
#endif
}
