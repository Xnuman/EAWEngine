#pragma once

// `EAWEngine battle`: a battle on a map without a window. It builds the map (with the spawnable
// types it needs), runs the battle for a number of ticks as fast as it can, and prints the state
// hash and statistics. Orders come from --spawn and --auto-move or from a recorded log (--replay,
// which also checks the recorded state hash); --record writes the battle's order log and
// --check-determinism runs it twice. Returns the process exit code: 0 success, 1 a map that cannot
// be built or an unreadable log, 4 a replay or determinism mismatch.

#include "vfs/virtual_file_system.hpp"
#include "engine/sandbox_options.hpp"

namespace eaw {

[[nodiscard]] int run_battle(const SandboxOptions& options, const VirtualFileSystem& vfs);

} // namespace eaw
