#pragma once

// Text form of a battle's order log, for recording and replaying battles:
//
//   EAWEngine battle log 1
//   map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED
//   1 spawn Rebel_X-Wing_Squadron 2 1 0 0 0        (tick, type, count, owner, point)
//   40 move 600 200 0 17.1 18.1                     (tick, point, entity ids index.generation)
//   95 attack 23.1 17.1 18.1                        (tick, target, entities)
//   120 stop 17.1
//   130 stance hold_fire 17.1                       (aggressive, hold_position, hold_fire)
//   end 3000 9f3c0a1e5b7d2c44                       (final tick and state hash, hexadecimal)
//
// One order per line, in submission order, ticks never decreasing. Numbers are written in their
// shortest exact form and read back bit-identically, so a replay feeds the simulation the same
// values. Empty lines and lines starting with '#' are ignored.

#include "core/status.hpp"
#include "battle/battle.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace eaw {

[[nodiscard]] std::string format_battle_log(const BattleLog& log);
// Errors name the line and what is wrong with it.
[[nodiscard]] Result<BattleLog> parse_battle_log(std::string_view text);

[[nodiscard]] Status save_battle_log(const std::filesystem::path& path, const BattleLog& log);
// Reads and parses a log file of at most 64 MiB.
[[nodiscard]] Result<BattleLog> load_battle_log(const std::filesystem::path& path);

} // namespace eaw
