#include "launcher/child_process.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <limits>
#include <vector>

namespace eaw {
namespace {

Result<std::wstring> widen(const std::string_view text)
{
    if (text.empty())
    {
        return std::wstring{};
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return make_error("command line is too long");
    }
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return make_error("command line is not valid UTF-8");
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

} // namespace

std::string quote_command_argument(const std::string_view argument)
{
    std::string result{"\""};
    std::size_t backslashes = 0;
    for (const char character : argument)
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

std::string build_command_line(const std::filesystem::path& program, const std::span<const std::string> arguments)
{
    const auto program_text = program.u8string();
    auto command = quote_command_argument(std::string_view(reinterpret_cast<const char*>(program_text.data()), program_text.size()));
    for (const auto& argument : arguments)
    {
        command.push_back(' ');
        command += quote_command_argument(argument);
    }
    return command;
}

Result<ChildProcess> ChildProcess::start(const std::filesystem::path& program, const std::span<const std::string> arguments, const std::filesystem::path& working_directory)
{
    EAW_TRY_ASSIGN(auto command, widen(build_command_line(program, arguments)));
    command.push_back(L'\0');
    const auto directory = working_directory.empty() ? program.parent_path() : working_directory;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION information{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &information))
    {
        return make_error("could not start " + program.filename().string() + " (Windows error " + std::to_string(GetLastError()) + ")");
    }
    CloseHandle(information.hThread);
    ChildProcess child;
    child.process_ = information.hProcess;
    return child;
}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept : process_(other.process_)
{
    other.process_ = nullptr;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept
{
    if (this != &other)
    {
        close();
        process_ = other.process_;
        other.process_ = nullptr;
    }
    return *this;
}

ChildProcess::~ChildProcess()
{
    close();
}

void ChildProcess::close() noexcept
{
    if (process_ != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
}

std::optional<int> ChildProcess::exit_code() const
{
    // A zero-timeout wait says whether it ended (STILL_ACTIVE alone is ambiguous: 259 is also a
    // valid exit code).
    DWORD code = 0;
    if (process_ == nullptr || WaitForSingleObject(static_cast<HANDLE>(process_), 0) != WAIT_OBJECT_0 || !GetExitCodeProcess(static_cast<HANDLE>(process_), &code))
    {
        return std::nullopt;
    }
    return static_cast<int>(code);
}

Result<int> ChildProcess::wait()
{
    if (process_ == nullptr)
    {
        return make_error("no process to wait for");
    }
    if (WaitForSingleObject(static_cast<HANDLE>(process_), INFINITE) != WAIT_OBJECT_0)
    {
        return make_error("waiting for the child process failed (Windows error " + std::to_string(GetLastError()) + ")");
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(static_cast<HANDLE>(process_), &code))
    {
        return make_error("could not read the child's exit code (Windows error " + std::to_string(GetLastError()) + ")");
    }
    return static_cast<int>(code);
}

} // namespace eaw
