#pragma once

// Profiler marks (Tracy, third_party/tracy; docs/tools.md, "Profiling"). Built with EAW_PROFILE they
// reach the Tracy client, which collects only while its viewer is connected; otherwise every mark
// compiles to nothing. Code names Tracy only here. Zone and frame names must outlive the program
// (string literals); a zone's name may also be given with its length (a name from a table).

#if defined(EAW_PROFILE)

#include <tracy/Tracy.hpp>

// A zone from here to the end of the enclosing scope, named by a string literal.
#define EAW_PROFILE_ZONE(name) ZoneScopedN(name)
// The same, named by text that outlives the program, with its length.
#define EAW_PROFILE_ZONE_TEXT(text, size) tracy::ScopedZone eaw_profile_zone(TracyLine, TracyFile, strlen(TracyFile), TracyFunction, strlen(TracyFunction), (text), (size), TRACY_CALLSTACK, true)
// The end of a frame of the named kind (a rendered frame, a simulation tick).
#define EAW_PROFILE_FRAME(name) FrameMarkNamed(name)
// A value plotted over time.
#define EAW_PROFILE_PLOT(name, value) TracyPlot(name, value)
// The calling thread now runs the named fiber; the calling thread runs its own code again.
#define EAW_PROFILE_FIBER_ENTER(name) TracyFiberEnter(name)
#define EAW_PROFILE_FIBER_LEAVE() TracyFiberLeave
// The calling thread's name in the profiler.
#define EAW_PROFILE_THREAD_NAME(name) tracy::SetThreadName(name)

#else

#define EAW_PROFILE_ZONE(name) static_cast<void>(0)
#define EAW_PROFILE_ZONE_TEXT(text, size) static_cast<void>(0)
#define EAW_PROFILE_FRAME(name) static_cast<void>(0)
#define EAW_PROFILE_PLOT(name, value) static_cast<void>(0)
#define EAW_PROFILE_FIBER_ENTER(name) static_cast<void>(0)
#define EAW_PROFILE_FIBER_LEAVE() static_cast<void>(0)
#define EAW_PROFILE_THREAD_NAME(name) static_cast<void>(0)

#endif
