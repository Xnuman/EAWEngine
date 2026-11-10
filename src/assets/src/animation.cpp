#include "assets/animation.hpp"

namespace eaw {

float animation_duration(const Animation& animation) noexcept
{
    return animation.frames_per_second > 0.0F ? static_cast<float>(animation.loop_frame_count) / animation.frames_per_second : 0.0F;
}

} // namespace eaw
