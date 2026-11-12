#pragma once

// Screen-ray queries against a land map's ground (GroundMap, from the simulation): order targets
// and placement under the cursor.

#include "simulation/ground_map.hpp"
#include "render/visual_scene.hpp"

#include <cstdint>
#include <optional>

namespace eaw {

// Where the ray through a client pixel first meets the surface (terrain or liquid), or nothing when it
// misses the map. Runs on the CPU in the same frame, so orders need no GPU readback.
[[nodiscard]] std::optional<Vector3> intersect_terrain(const VisualCamera& camera, float width, float height, float x, float y, const GroundMap& field);

} // namespace eaw
