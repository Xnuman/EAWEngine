#pragma once

// Standard log destinations for EAWEngine programs: stderr (warnings and errors), the debugger's
// output window (all records; the Visual Studio Output pane), and one log file per run.

#include "core/status.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace eaw {

struct LogSinkOptions
{
    // Program name used in the log file name.
    std::string application_name{"EAWEngine"};
    // Folder for log files; empty means "logs" next to the executable (inside the build folder
    // during development, never the game installation).
    std::filesystem::path directory;
    // Older log files of this program beyond this count are deleted.
    std::size_t keep_files{20};
    // Also echo warnings and errors to stderr.
    bool console{true};
};

// Installs the sinks and returns the log file path. When the file cannot be created, the console
// and debugger sinks are still installed and the error says why.
Result<std::filesystem::path> install_log_sinks(const LogSinkOptions& options = {});

} // namespace eaw
