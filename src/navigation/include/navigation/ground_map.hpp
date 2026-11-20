#pragma once

// A land map's ground for gameplay: the terrain heightfield sampled exactly like the rendered mesh,
// the drawn liquid level, and the TED passability raster. Graphics-free; the simulation moves ground
// units over it and presentation uses it for clicks and placement.

#include "math/vector.hpp"
#include "core/status.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace eaw {

// TED passability types (one bit per type; the stock maps paint exactly one per cell). Names come
// from MovementClassTypeDefs.xml. Evidence for the bits (docs/compatibility-research.md): 0x80 and
// 0x100 occur only in Forces of Corruption maps, the expansion that added InfantryOnly and
// Impassable; 0x20 cells lie 95% below the liquid; 0x2 cells are steep. Obstacle as 0x4 is the
// remaining base-game static type (medium confidence). Walls and shields are applied at runtime by
// objects and never appear in the stock rasters, so their bits are unverified guesses.
namespace passability {
inline constexpr std::uint32_t clear = 0x1;
inline constexpr std::uint32_t steep = 0x2;
inline constexpr std::uint32_t obstacle = 0x4;
inline constexpr std::uint32_t rebel_wall = 0x8;   // unverified
inline constexpr std::uint32_t empire_wall = 0x10; // unverified
inline constexpr std::uint32_t water = 0x20;
inline constexpr std::uint32_t shield = 0x40; // unverified
inline constexpr std::uint32_t infantry_only = 0x80;
inline constexpr std::uint32_t impassable = 0x100;
} // namespace passability

// The bit for a passability type name ("Clear", "InfantryOnly", ...; case-insensitive).
[[nodiscard]] std::optional<std::uint32_t> passability_type(std::string_view name) noexcept;

// Parses a MovementClassTypeDefs entry such as " Clear | Shield | InfantryOnly " into a mask of the
// types a movement class may enter. Unknown names are an error naming them.
[[nodiscard]] Result<std::uint32_t> parse_passability_list(std::string_view text);

// The kind of a map's liquid (TED water: water, lava, ice); also the SurfaceFX surface name.
enum class LiquidKind : std::uint8_t
{
    water,
    lava,
    ice,
};

[[nodiscard]] constexpr std::string_view to_string(const LiquidKind kind) noexcept
{
    return kind == LiquidKind::lava ? "lava" : kind == LiquidKind::ice ? "ice" : "water";
}

struct GroundMap
{
    bool operator==(const GroundMap&) const = default;
    std::uint32_t width{};
    std::uint32_t height{};
    // World units between samples along X and Y.
    float spacing{20.0F};
    // Row-major heights in world units (width * height).
    std::vector<float> heights;
    // Height of the drawn liquid surface (water, lava, ice), when there is one.
    std::optional<float> liquid_level;
    LiquidKind liquid_kind{LiquidKind::water};
    // Row-major raw passability values (width * height), or empty when the map has none.
    std::vector<std::uint32_t> passability;

    [[nodiscard]] bool valid() const noexcept;
    // Terrain height at world X/Y using the renderer's two-triangle split per cell; outside the grid
    // the nearest edge is used.
    [[nodiscard]] float terrain_height(float x, float y) const;
    // The walkable or clickable surface: the terrain, or the liquid surface where it is higher.
    [[nodiscard]] float surface_height(float x, float y) const;
    // Whether the ground has a passability value for every sample.
    [[nodiscard]] bool has_passability() const noexcept
    {
        return valid() && passability.size() == heights.size();
    }
    // The cell nearest to world X/Y (row-major index), nothing outside the grid or without data.
    [[nodiscard]] std::optional<std::uint32_t> cell_at(float x, float y) const;
    // Raw passability of the cell nearest to world X/Y (nothing outside the grid or without data).
    [[nodiscard]] std::optional<std::uint32_t> passability_at(float x, float y) const;
    // The liquid over the terrain at world X/Y, if any (the terrain lies below the liquid level).
    [[nodiscard]] std::optional<LiquidKind> liquid_at(float x, float y) const;
    // Where the segment from `from` to `to` first meets the surface (surface_height: the terrain, or
    // the liquid where it is higher): sampled every half cell, then refined by bisection; `from`
    // itself when it starts at or below the surface; nothing when the segment stays above it.
    [[nodiscard]] std::optional<Vector3> surface_crossing(const Vector3& from, const Vector3& to) const;
};

} // namespace eaw
