#pragma once

// EAWEngine lua-survey: runs every script under Data/Scripts in its own sandboxed host on recording
// stubs (load, Definitions, then main for a number of ticks) and reports which engine functions and
// methods they call, how often, by how many scripts, with which arguments, and what failed. This is
// the measurement the host API is built from, ranked by use.

#include "engine/sandbox_options.hpp"

#include "vfs/virtual_file_system.hpp"

namespace eaw {

// Returns 0, or 1 when the report cannot be written.
[[nodiscard]] int run_lua_survey(const SandboxOptions& options, const VirtualFileSystem& vfs);

} // namespace eaw
