#pragma once

// Values derived from ALA animation data.

#include "formats/ala_animation.hpp"

#include <cstdint>

namespace eaw {

[[nodiscard]] std::uint32_t ala_playable_frame_count(const AlaAnimationData& animation) noexcept;
[[nodiscard]] float ala_duration_seconds(const AlaAnimationData& animation) noexcept;

} // namespace eaw
