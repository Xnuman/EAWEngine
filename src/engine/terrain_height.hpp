#pragma once

// CPU copy of a land map's ground surface for gameplay queries (order targets, placement, and later
// movement): the terrain heightfield sampled exactly like the rendered mesh, plus the liquid level
// where a liquid surface is drawn.

#include "eaw/visual_scene.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace eaw {

struct TerrainHeightField
{
    std::uint32_t width{};
    std::uint32_t height{};
    // World units between samples along X and Y.
    float spacing{20.0F};
    // Row-major heights in world units (width * height).
    std::vector<float> heights;
    // Height of the drawn liquid surface (water, lava, ice), when there is one.
    std::optional<float> liquid_level;

    [[nodiscard]] bool valid() const noexcept;
    // Terrain height at world X/Y using the renderer's two-triangle split per cell; outside the grid
    // the nearest edge is used.
    [[nodiscard]] float terrain_height(float x, float y) const;
    // The walkable or clickable surface: the terrain, or the liquid surface where it is higher.
    [[nodiscard]] float surface_height(float x, float y) const;
};

// Where the ray through a client pixel first meets the surface (terrain or liquid), or nothing when it
// misses the map. Runs on the CPU in the same frame, so orders need no GPU readback.
[[nodiscard]] std::optional<std::array<float, 3>> intersect_terrain(const VisualCamera& camera, float width, float height, float x, float y, const TerrainHeightField& field);

} // namespace eaw
