#pragma once

// How much of the sky each terrain sample sees (the global-illumination track's second step,
// docs/sky-research.md): in each of sky_visibility_directions directions, the steepest rise of the
// surrounding terrain within sky_visibility_reach world units gives the horizon's angle; a flat
// patch facing up sees cos^2 of it of the (cosine-weighted) sky that way, and the visibility is the
// mean over the directions: 1 on open ground and peaks, less in craters, canyons, and at cliff feet.
// Computed once per map from the height grid; the counts are engine choices.

#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

inline constexpr std::uint32_t sky_visibility_directions = 16;
inline constexpr float sky_visibility_reach = 600.0F;

// `heights` is `width` x `height` samples, row by row, `spacing` world units apart; the result has
// one visibility (0..1) per sample. A grid smaller than 2 x 2 or with too few heights yields an
// empty result.
[[nodiscard]] std::vector<float> compute_sky_visibility(std::span<const float> heights, std::uint32_t width, std::uint32_t height, float spacing);

} // namespace eaw
