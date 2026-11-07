#pragma once

// A land map's ground for gameplay: the terrain heightfield sampled exactly like the rendered mesh,
// the drawn liquid level, and the TED passability raster. Graphics-free; the simulation moves ground
// units over it and presentation uses it for clicks and placement.

#include <cstdint>
#include <optional>
#include <vector>

namespace eaw {

// Raw TED passability values (one per cell; the stock corpus uses exactly one of these per cell).
// Only the provisional readings below are evidence-based; see docs/compatibility-research.md.
namespace passability {
inline constexpr std::uint32_t ground = 0x1;        // flat ground (most cells; mean slope 2.5)
inline constexpr std::uint32_t blocked = 0x2;       // steep, void, or deep cells (mean slope 12.3)
inline constexpr std::uint32_t unknown_4 = 0x4;     // rare, flat; meaning unknown
inline constexpr std::uint32_t water = 0x20;        // 95% of its cells lie below the liquid surface
inline constexpr std::uint32_t unknown_80 = 0x80;   // rare, flat; meaning unknown
inline constexpr std::uint32_t unknown_100 = 0x100; // steep and void cells; meaning unknown
} // namespace passability

struct GroundMap
{
    std::uint32_t width{};
    std::uint32_t height{};
    // World units between samples along X and Y.
    float spacing{20.0F};
    // Row-major heights in world units (width * height).
    std::vector<float> heights;
    // Height of the drawn liquid surface (water, lava, ice), when there is one.
    std::optional<float> liquid_level;
    // Row-major raw passability values (width * height), or empty when the map has none.
    std::vector<std::uint32_t> passability;

    [[nodiscard]] bool valid() const noexcept;
    // Terrain height at world X/Y using the renderer's two-triangle split per cell; outside the grid
    // the nearest edge is used.
    [[nodiscard]] float terrain_height(float x, float y) const;
    // The walkable or clickable surface: the terrain, or the liquid surface where it is higher.
    [[nodiscard]] float surface_height(float x, float y) const;
    // Raw passability of the cell nearest to world X/Y (nothing outside the grid or without data).
    [[nodiscard]] std::optional<std::uint32_t> passability_at(float x, float y) const;
};

} // namespace eaw
