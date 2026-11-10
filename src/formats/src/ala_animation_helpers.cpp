#include "formats/ala_animation_helpers.hpp"

namespace eaw {

std::uint32_t ala_playable_frame_count(const AlaAnimationData& animation) noexcept
{
    return animation.stored_frame_count == 0 ? 0 : animation.stored_frame_count - 1;
}

float ala_duration_seconds(const AlaAnimationData& animation) noexcept
{
    return animation.frames_per_second > 0.0F ? static_cast<float>(ala_playable_frame_count(animation)) / animation.frames_per_second : 0.0F;
}

} // namespace eaw
