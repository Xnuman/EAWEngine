#pragma once

// Screen <-> world conversions matching the renderer's camera (right-handed look-at, perspective
// field of view, depth 0 at the near plane and 1 at the far plane; pixel y grows downward).

#include "math/vector.hpp"
#include "render/visual_camera.hpp"

#include <optional>

namespace eaw {

// Client-pixel position of a world point, or nothing when it is behind the camera.
[[nodiscard]] std::optional<Vector2> project_to_screen(const VisualCamera& camera, float width, float height, const Vector3& point);

// World point at a client pixel and depth-buffer value (0..1).
[[nodiscard]] Vector3 unproject_screen(const VisualCamera& camera, float width, float height, float x, float y, float depth);

// Where the ray through a client pixel meets the horizontal plane z = `plane_z`, or nothing when it
// points away from it.
[[nodiscard]] std::optional<Vector3> intersect_ground_plane(const VisualCamera& camera, float width, float height, float x, float y, float plane_z);

} // namespace eaw
