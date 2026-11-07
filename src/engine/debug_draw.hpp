#pragma once

// Builders for debug line overlays (engine-native; the renderer only draws the lines).

#include "eaw/visual_scene.hpp"

#include <array>
#include <vector>

namespace eaw {

// Appends the 12 edges of the box [minimum, maximum] transformed by `transform` (row-vector
// convention, translation in elements 12-14, as VisualInstance::transform).
void append_debug_box(std::vector<VisualDebugLine>& lines, const std::array<float, 3>& minimum, const std::array<float, 3>& maximum, const std::array<float, 16>& transform,
                      const std::array<float, 4>& color);

// Appends red/green/blue lines along +X/+Y/+Z from `origin`.
void append_debug_axes(std::vector<VisualDebugLine>& lines, const std::array<float, 3>& origin, float length);

} // namespace eaw
