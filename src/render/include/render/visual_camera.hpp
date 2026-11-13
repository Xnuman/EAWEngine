#pragma once

// The view the renderer draws from.

#include "math/vector.hpp"

namespace eaw {

// A perspective view in world space (Z up). The renderer derives the aspect ratio from its target.
struct VisualCamera
{
    Vector3 eye{0.0F, -10.0F, 5.0F};
    Vector3 target{};
    Vector3 up{0.0F, 0.0F, 1.0F};
    float vertical_fov_degrees{55.0F};
    float near_plane{0.1F};
    float far_plane{1000.0F};
};

} // namespace eaw
