#pragma once

// Starting EAWEngine (or another program) as a child process with an argument vector. Arguments are
// UTF-8 and quoted with the rules CommandLineToArgvW and the MSVC runtime use, so any text survives
// the round trip.

#include "eaw/status.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace eaw {

// One argument quoted for a Windows command line (always quoted; backslashes before quotes doubled).
[[nodiscard]] std::string quote_command_argument(std::string_view argument);
// The program followed by its quoted arguments.
[[nodiscard]] std::string build_command_line(const std::filesystem::path& program, std::span<const std::string> arguments);

// A started child process. Destroying it closes the handles but does not end the process.
class ChildProcess final
{
public:
    // Starts `program` with `arguments` in `working_directory` (the program's folder when empty).
    [[nodiscard]] static Result<ChildProcess> start(const std::filesystem::path& program, std::span<const std::string> arguments, const std::filesystem::path& working_directory = {});

    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess();

    // The exit code once the process has ended; nothing while it runs. Never blocks.
    [[nodiscard]] std::optional<int> exit_code() const;
    // Blocks until the process ends and returns its exit code.
    [[nodiscard]] Result<int> wait();

private:
    ChildProcess() = default;
    void close() noexcept;

    void* process_{};
};

} // namespace eaw
