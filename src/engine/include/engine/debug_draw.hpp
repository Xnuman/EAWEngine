#pragma once

// Builders for debug line overlays (engine-native; the renderer only draws the lines).

#include "render/visual_scene.hpp"
#include "math/vector.hpp"

#include <array>
#include <vector>

namespace eaw {

// Appends the 12 edges of the box [minimum, maximum] transformed by `transform` (row-vector
// convention, translation in elements 12-14, as VisualInstance::transform).
void append_debug_box(std::vector<VisualDebugLine>& lines, const Vector3& minimum, const Vector3& maximum, const Matrix4& transform, const std::array<float, 4>& color);

// Appends red/green/blue lines along +X/+Y/+Z from `origin`.
void append_debug_axes(std::vector<VisualDebugLine>& lines, const Vector3& origin, float length);

} // namespace eaw
