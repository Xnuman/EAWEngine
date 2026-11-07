#pragma once

// Fatal errors: unrecoverable failures and broken invariants end the process with a clear report
// instead of being handled. A program installs a handler that presents the report (the platform
// layer shows an error window); without one the report goes to stderr. Malformed game or mod data
// is not a fatal error: decoders keep returning diagnostics for it.

#include <source_location>
#include <string>
#include <string_view>

namespace eaw {

// Presents a fatal error report. Called at most once per process; must not return control to the
// failing code (fatal_error terminates after it returns).
using FatalErrorHandler = void (*)(std::string_view report);

// Replaces the handler; nullptr restores the stderr default. Not synchronized: set it at startup.
void set_fatal_error_handler(FatalErrorHandler handler) noexcept;

// Formats a report (message, file, line, function), passes it to the handler, and terminates the
// process. Safe to call from any thread; a second concurrent fatal error waits for the first.
[[noreturn]] void fatal_error(std::string_view message, std::source_location location = std::source_location::current()) noexcept;

// Like fatal_error, for reports with no meaningful source location (crash handlers describing an
// uncaught exception or a hardware fault).
[[noreturn]] void fatal_error_report(std::string_view report) noexcept;

// The report text fatal_error would present (exposed for handlers and tests).
[[nodiscard]] std::string format_fatal_error(std::string_view message, const std::source_location& location);

} // namespace eaw

// Invariant checks, active in every build. A failed check is a programming error and ends the
// process through fatal_error.
#define EAW_ASSERT_MSG(expression, message) ((expression) ? static_cast<void>(0) : ::eaw::fatal_error(std::string("Assertion failed: " #expression "\n") + std::string(message)))
#define EAW_ASSERT(expression) ((expression) ? static_cast<void>(0) : ::eaw::fatal_error("Assertion failed: " #expression))
