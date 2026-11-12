#include "battle/battle_runtime.hpp"

#include <chrono>
#include <utility>

namespace eaw {

BattleRuntime::BattleRuntime(const BattleMap& map, std::string map_name, const AssetReader* scripts_reader, const double ticks_per_second)
    : clock_(ticks_per_second), battle_(map, std::move(map_name), clock_.tick_seconds())
{
    if (scripts_reader != nullptr)
    {
        scripts_.emplace(*scripts_reader);
    }
}

std::uint32_t BattleRuntime::ticks_due(const double real_seconds)
{
    return clock_.advance(real_seconds);
}

void BattleRuntime::step()
{
    const auto start = std::chrono::steady_clock::now();
    // Through the battle, never the world alone: it also spawns the survivors of the dead.
    battle_.step();
    if (scripts_)
    {
        scripts_->run(battle_);
    }
    last_step_milliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace eaw
