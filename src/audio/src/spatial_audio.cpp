#include "audio/spatial_audio.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

SpatialGain spatialize(const AudioListener& listener, const Vector3& source, const float saturation_distance, const float rolloff)
{
    const Vector3 offset{source.x - listener.position.x, source.y - listener.position.y, source.z - listener.position.z};
    const float distance = std::sqrt(dot(offset, offset));
    SpatialGain gain;
    const float saturation = std::max(saturation_distance, 0.0F);
    if (distance > saturation)
    {
        gain.volume = saturation + std::max(rolloff, 0.0F) * (distance - saturation) > 0.0F ? saturation / (saturation + std::max(rolloff, 0.0F) * (distance - saturation)) : 1.0F;
    }
    const auto right = normalize_or(cross(listener.forward, listener.up), Vector3{1.0F, 0.0F, 0.0F});
    if (distance > 0.0F)
    {
        gain.pan = std::clamp(dot(offset, right) / distance, -1.0F, 1.0F);
    }
    return gain;
}

StereoGains stereo_gains(const float pan)
{
    const float angle = (std::clamp(pan, -1.0F, 1.0F) + 1.0F) * pi * 0.25F;
    return {std::cos(angle), std::sin(angle)};
}

} // namespace eaw
