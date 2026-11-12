#pragma once

// A battle with its game-object scripts on the fixed simulation clock: what the sandbox and
// headless runs share, so the two cannot step a battle differently. Each tick is the battle's step
// and then the script phase, whose orders apply in the next tick (battle_scripts.hpp). Callers keep
// their loop: they give orders between ticks and react to each tick's events.

#include "simulation/simulation.hpp"
#include "engine/battle.hpp"
#include "engine/battle_scripts.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace eaw {

class AssetReader;

// Simulation ticks per second of real time: an engine choice until the original's rate is
// measured (roadmap, "Performance gates").
inline constexpr double default_ticks_per_second = 30.0;

class BattleRuntime final
{
public:
    // `scripts_reader` resolves game-object scripts; null runs none (--no-scripts).
    BattleRuntime(const BattleMap& map, std::string map_name, const AssetReader* scripts_reader, double ticks_per_second = default_ticks_per_second);
    // The battle's world is referenced by its users, so the runtime stays where it was built.
    BattleRuntime(const BattleRuntime&) = delete;
    BattleRuntime& operator=(const BattleRuntime&) = delete;

    [[nodiscard]] Battle& battle() noexcept
    {
        return battle_;
    }
    [[nodiscard]] const Battle& battle() const noexcept
    {
        return battle_;
    }
    // Null without scripts.
    [[nodiscard]] BattleScripts* scripts() noexcept
    {
        return scripts_ ? &*scripts_ : nullptr;
    }
    [[nodiscard]] const BattleScripts* scripts() const noexcept
    {
        return scripts_ ? &*scripts_ : nullptr;
    }

    // Adds `real_seconds` of real time to the clock and returns how many ticks are due now.
    [[nodiscard]] std::uint32_t ticks_due(double real_seconds);
    // One tick and its script phase.
    void step();
    // Fraction (0..1) of the next tick already elapsed, for interpolating presentation.
    [[nodiscard]] float interpolation() const noexcept
    {
        return clock_.interpolation();
    }
    [[nodiscard]] double tick_seconds() const noexcept
    {
        return clock_.tick_seconds();
    }
    // How long the last step took, script phase included (measured, never hashed).
    [[nodiscard]] double last_step_milliseconds() const noexcept
    {
        return last_step_milliseconds_;
    }

private:
    FixedStepClock clock_;
    Battle battle_;
    std::optional<BattleScripts> scripts_;
    double last_step_milliseconds_{};
};

} // namespace eaw
