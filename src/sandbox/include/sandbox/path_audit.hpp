#pragma once

// The pathfinding audit (EAWEngine path-audit; docs/pathfinding-design.md, 5 step 2): on land maps
// (--map, or every land map under Data/Art/Maps), for each movement class of
// MovementClassTypeDefs.xml, plans seeded random pairs of start and goal cells the class may enter,
// on the battle's ground with its stamped footprints, and reports how many reached their goal, the
// nodes expanded, the time, and the path length. The baseline every pathfinding change is measured
// against: --report <folder> writes one row per pair (path-audit.tsv), so two runs compare pair by
// pair.

#include "sandbox/sandbox_options.hpp"

namespace eaw {

class VirtualFileSystem;

// Exit codes: 0 success, 1 no land map could be built.
[[nodiscard]] int run_path_audit(const SandboxOptions& options, const VirtualFileSystem& vfs);

} // namespace eaw
