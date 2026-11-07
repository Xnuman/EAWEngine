#include "eaw/fatal_error.hpp"

#include "eaw/log.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace eaw {
namespace {

std::atomic<FatalErrorHandler> fatal_handler{nullptr};

std::mutex& fatal_mutex() noexcept
{
    static std::mutex mutex;
    return mutex;
}

} // namespace

void set_fatal_error_handler(const FatalErrorHandler handler) noexcept
{
    fatal_handler.store(handler);
}

std::string format_fatal_error(const std::string_view message, const std::source_location& location)
{
    std::string report(message);
    report += "\n\nFile: ";
    report += location.file_name();
    report += "\nLine: ";
    report += std::to_string(location.line());
    report += "\nFunction: ";
    report += location.function_name();
    return report;
}

void fatal_error(const std::string_view message, const std::source_location location) noexcept
{
    // Formatting allocates; running out of memory here ends the process like any other fatal error.
    const auto report = format_fatal_error(message, location);
    fatal_error_report(report);
}

void fatal_error_report(const std::string_view text) noexcept
{
    // Only the first fatal error is reported; other threads failing meanwhile wait here and the
    // process ends with the first report.
    fatal_mutex().lock();
    // The log sinks (stderr, debugger output, log file) receive the report when the log is free;
    // otherwise it goes straight to stderr.
    if (!log_fatal_report(text))
    {
        std::fprintf(stderr, "fatal error: %.*s\n", static_cast<int>(text.size()), text.data());
    }
    std::fflush(stderr);
    if (const auto handler = fatal_handler.load())
    {
        handler(text);
    }
    // _Exit skips destructors and atexit handlers, which may touch the state that just failed.
    std::_Exit(3);
}

} // namespace eaw
