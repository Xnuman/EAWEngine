#pragma once

// Where a sound sits for the listener: its volume by distance and its pan by direction. Shared by
// every backend, so they place sounds alike.

#include "math/vector.hpp"

namespace eaw {

struct AudioListener
{
    Vector3 position;
    Vector3 forward{0.0F, 1.0F, 0.0F};
    Vector3 up{0.0F, 0.0F, 1.0F};
};

struct SpatialGain
{
    float volume{1.0F};
    // -1 left, 0 centre, 1 right.
    float pan{};
};

// Full volume within `saturation_distance`; beyond it volume falls as saturation / (saturation +
// rolloff x (distance - saturation)), an inverse-distance law (an engine choice: the original used
// Miles 3D providers with AUDIO.XML's saturation and rolloff factors, whose exact curve is
// unknown). The pan is the source's direction along the listener's right, so a source straight
// ahead or behind is centred.
[[nodiscard]] SpatialGain spatialize(const AudioListener& listener, const Vector3& source, float saturation_distance, float rolloff);

struct StereoGains
{
    float left{};
    float right{};
};

// The equal-power pan law: pan -1, 0, 1 give (1, 0), (0.707, 0.707), (0, 1).
[[nodiscard]] StereoGains stereo_gains(float pan);

} // namespace eaw
