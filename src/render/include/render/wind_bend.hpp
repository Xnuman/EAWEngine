#pragma once

// How trees sway in the map's wind (the Tree material's bend, docs/legacy-shader-mapping.md): a
// vertex moves by its material's BendScale x `offset` x (its height above the model's origin)^2 x
// `inverse_height_squared`, so a BendScale-1 vertex at wind_bend_height moves by the whole offset.
// The legacy shader only consumes this vector; how the original engine animated it is not known,
// so the sway below is an engine choice: the offset leans downwind in proportion to the wind's
// speed and swells and eases in a slow gust.

#include "math/vector.hpp"
#include "render/visual_frame.hpp"

namespace eaw {

// Engine choices: the height at which a BendScale-1 vertex moves by the full offset, that offset
// in a strong wind (speed 10, "a strong wind" per the legacy grass shader), and the gust's period
// and share of the offset.
inline constexpr float wind_bend_height = 40.0F;
inline constexpr float wind_bend_strong_offset = 6.0F;
inline constexpr float wind_strong_speed = 10.0F;
inline constexpr float wind_gust_period = 4.0F;
inline constexpr float wind_gust_share = 0.5F;

struct WindBend
{
    Vector3 offset{};
    float inverse_height_squared{1.0F / (wind_bend_height * wind_bend_height)};
};

// The bend at `seconds` of scene time.
[[nodiscard]] WindBend wind_bend(const VisualWind& wind, float seconds) noexcept;

} // namespace eaw
