#pragma once

// The original's units, converted to the engine's (world units, seconds, radians). Its XML gives
// speeds, turn rates, and accelerations per logic frame and some delays in frames; the logic rate
// is unmeasured, so every value is read as if per 1/30 s. That is a provisional engine assumption,
// not compatibility evidence (roadmap, verification questions: the x30 speed scale): change it here
// and every reader follows. Each conversion keeps the order of operations the readers had, so the
// state hash does not move with this file.

#include "math/scalar.hpp"

namespace eaw::legacy_units {

// Logic frames per second of the original, a guess (stock evidence agrees with it so far: XML
// Max_Speed 4.5 for TIE fighters, Max_Rate_Of_Turn 16 for infantry; docs/compatibility-research.md).
inline constexpr float frames_per_second = 30.0F;

// A distance per frame (XML Max_Speed, Min_Speed, a projectile's Max_Speed) in world units per
// second.
[[nodiscard]] constexpr float speed_per_frame(const float value) noexcept
{
    return value * frames_per_second;
}

// A speed change per frame (XML OverrideAcceleration, OverrideDeceleration,
// Projectile_Grenade_Gravity) in world units per second squared.
[[nodiscard]] constexpr float acceleration_per_frame(const float value) noexcept
{
    return value * (frames_per_second * frames_per_second);
}

// Degrees per frame (XML Max_Rate_Of_Turn, Stopped_Rate_Of_Turn, Turret_Rotate_Speed) in radians per
// second.
[[nodiscard]] constexpr float degrees_per_frame(const float value) noexcept
{
    return value * (frames_per_second * pi / 180.0F);
}

// A frame count (XML Projectile_Appearance_Delay_Frames) in seconds.
[[nodiscard]] constexpr float frames(const float count) noexcept
{
    return count / frames_per_second;
}

} // namespace eaw::legacy_units
