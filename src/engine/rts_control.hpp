#pragma once

// RTS selection and command helpers shared by the sandbox and, later, the game UI.

#include "eaw/simulation.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace eaw {

// Entities whose position projects inside the client-pixel rectangle (x0, y0, x1, y1; any corner
// order). With `mobile_only`, entities without speed (props, structures) are skipped, as in
// drag-selection; results keep snapshot order.
[[nodiscard]] std::vector<EntityId> entities_in_rectangle(std::span<const EntitySnapshot> entities, const VisualCamera& camera, float width, float height, const std::array<float, 4>& rectangle,
                                                          bool mobile_only);

// Destinations for `count` units ordered to `target`: a centered square grid with `spacing` world
// units between neighbors (straight-line movement has no formations or collision yet).
[[nodiscard]] std::vector<Vector3> grid_targets(const Vector3& target, std::size_t count, float spacing);

} // namespace eaw
