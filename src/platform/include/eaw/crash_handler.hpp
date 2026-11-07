#pragma once

// Process-wide crash reporting. Every way a process can die unexpectedly ends in one fatal error
// report: eaw::fatal_error and failed EAW_ASSERTs, uncaught C++ exceptions (std::terminate), hardware
// faults such as access violations or stack overflows, pure virtual calls, and invalid CRT
// parameters. The report always goes to stderr; an error window shows it as well unless dialogs are
// disabled or stderr is redirected (automated runs must never wait for a click).

#include <string>

namespace eaw {

struct CrashHandlerOptions
{
    // Title of the error window.
    std::string application_name{"EAWEngine"};
    // Show the error window (still skipped automatically when stderr is redirected to a file or
    // pipe, so automation never blocks).
    bool dialogs{true};
};

// Installs the handlers for the whole process. Call once, early in main (again to change options).
void install_crash_handlers(const CrashHandlerOptions& options = {});

} // namespace eaw
