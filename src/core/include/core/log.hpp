#pragma once

// Engine log. Non-critical problems (malformed mod data, missing assets, skipped features) are
// warnings: written to every installed sink and the work continues. Sinks are installed by the
// program (the platform layer adds the debugger output window and a log file); with no sinks,
// warnings and errors go to stderr. Thread-safe.

#include "core/status.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace eaw {

enum class LogLevel : std::uint8_t
{
    info,
    warning,
    error,
};

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

struct LogRecord
{
    LogLevel level{LogLevel::info};
    // Subsystem such as "vfs", "map", "render"; free-form.
    std::string_view category;
    std::string_view message;
};

using LogSink = std::function<void(const LogRecord&)>;

// Adds a sink; sinks receive records in the order they were logged, one at a time.
void add_log_sink(LogSink sink);
void clear_log_sinks();

void log(LogLevel level, std::string_view category, std::string_view message);
inline void log_info(const std::string_view category, const std::string_view message)
{
    log(LogLevel::info, category, message);
}
inline void log_warning(const std::string_view category, const std::string_view message)
{
    log(LogLevel::warning, category, message);
}
inline void log_error(const std::string_view category, const std::string_view message)
{
    log(LogLevel::error, category, message);
}
// Logs a failed status as a warning (the common "report and continue" case).
inline void log_warning(const std::string_view category, const Error& error)
{
    log(LogLevel::warning, category, error.message);
}

// Counts of records logged so far per level (for summaries such as "12 warnings").
[[nodiscard]] std::size_t log_count(LogLevel level) noexcept;

// "[12:34:56.789] [warning] [map] message" without a trailing newline.
[[nodiscard]] std::string format_log_record(const LogRecord& record);

// Best effort, for the fatal error path: hands the report to the sinks unless the log is busy on
// this or another thread (it never blocks, so a crash inside a sink cannot deadlock). Returns
// whether any sink received it. Sinks must not log themselves.
bool log_fatal_report(std::string_view report) noexcept;

} // namespace eaw
