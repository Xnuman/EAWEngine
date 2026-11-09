#pragma once

// Scalar constants and angle conversions shared by the math types.

#include <numbers>

namespace eaw {

inline constexpr float pi = std::numbers::pi_v<float>;
inline constexpr float two_pi = 2.0F * pi;
inline constexpr float half_pi = 0.5F * pi;

[[nodiscard]] constexpr float degrees_to_radians(const float degrees) noexcept
{
    return degrees * (pi / 180.0F);
}

[[nodiscard]] constexpr float radians_to_degrees(const float radians) noexcept
{
    return radians * (180.0F / pi);
}

} // namespace eaw
