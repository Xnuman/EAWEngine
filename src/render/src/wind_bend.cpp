#include "render/wind_bend.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eaw {

WindBend wind_bend(const VisualWind& wind, const float seconds) noexcept
{
    WindBend bend;
    const auto speed = std::isfinite(wind.speed) ? std::max(wind.speed, 0.0F) : 0.0F;
    const auto direction = normalize_or(Vector3{wind.direction.x, wind.direction.y, 0.0F}, Vector3{1.0F, 0.0F, 0.0F});
    const auto time = std::isfinite(seconds) ? seconds : 0.0F;
    const auto gust = 1.0F - wind_gust_share + wind_gust_share * std::sin(2.0F * std::numbers::pi_v<float> * time / wind_gust_period);
    const auto amount = speed / wind_strong_speed * wind_bend_strong_offset * gust;
    bend.offset = {direction.x * amount, direction.y * amount, 0.0F};
    return bend;
}

} // namespace eaw
