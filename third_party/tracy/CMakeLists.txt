# Tracy (BSD-3-Clause, github.com/wolfpld/tracy), the engine's profiler (the owner, 2026-11-16):
# only with EAW_PROFILE, so a default build neither fetches nor compiles it. Pinned by the hash of
# the release archive; the viewer that connects must be the same release (0.14.1). The client is
# built on demand (TRACY_ON_DEMAND: it collects only while a viewer is connected) and with fiber
# support for the job system (TRACY_FIBERS), listening on this machine only (TRACY_ONLY_LOCALHOST,
# not every network interface). Third-party code: its own warning level, not the
# project's rules. Code uses it only through core/profile.hpp.
include(FetchContent)
FetchContent_Declare(
    tracy
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
    URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
    # Sources only: the client target is this file's, not Tracy's own CMake project.
    SOURCE_SUBDIR no-cmake-project
)
FetchContent_MakeAvailable(tracy)

add_library(eaw_tracy STATIC ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
set_target_properties(eaw_tracy PROPERTIES FOLDER "third_party")
target_include_directories(eaw_tracy SYSTEM PUBLIC ${tracy_SOURCE_DIR}/public)
target_compile_definitions(eaw_tracy PUBLIC TRACY_ENABLE TRACY_ON_DEMAND TRACY_FIBERS TRACY_ONLY_LOCALHOST)
if(MSVC)
    target_compile_options(eaw_tracy PRIVATE /W0)
endif()
