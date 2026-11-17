#pragma once

// `EAWEngine movement-scenarios`: small synthetic group moves on flat generated ground, needing no
// game installation (docs/group-movement-research.md): ten tanks through a one-tank gap as a
// formation and as a group move, a head-on pair, three tanks converging on one point, and two
// groups crossing. Each runs to its end or a time limit and prints its movement measurements
// (movement_metrics.hpp), route changes by kind, blocked moves, and state hash, so a change to
// movement can be compared with the baseline. Returns the process exit code (0).

namespace eaw {

[[nodiscard]] int run_movement_scenarios();

} // namespace eaw
