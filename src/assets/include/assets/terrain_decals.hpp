#pragma once

// Terrain decals (XML TerrainDecalFX.xml, `Decal`): marks laid on the ground, footprints and
// explosion scorches, named by surface effects (SurfaceFX Decal_Name). Each is a cell of the master
// decal atlas, D_MASTER_DECAL.DDS: a 4x4 grid of 128-texel cells (read from the stock atlas: the
// footprints fill rows 0-1, the scorches row 3, the massive scorch a 2x2 block at 2,1) whose alpha
// is the mark's shape and whose color is a tangent-space normal for bump decals and black for the
// darkening ones.

#include "assets/xml_asset_database.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

inline constexpr std::string_view decal_atlas_path = "DATA/ART/TEXTURES/D_MASTER_DECAL.DDS";
inline constexpr std::uint32_t decal_atlas_cells = 4;

enum class TerrainDecalMode : std::uint8_t
{
    // Its color blended in by its alpha (DIFFUSE_ALPHA: the scorches darken the ground).
    diffuse_alpha,
    // A dent: its color bends the ground's normal where its alpha covers it (DECAL_BUMP_ALPHA:
    // footprints).
    bump_alpha,
    // Its color added (ADDITIVE).
    additive,
};

struct TerrainDecalCell
{
    std::uint32_t column{};
    std::uint32_t row{};
};

struct TerrainDecal
{
    std::string name;
    TerrainDecalMode mode{TerrainDecalMode::diffuse_alpha};
    // Opacity, 0..1 (Intensity).
    float intensity{1.0F};
    // Width and height on the ground, world units (Scale).
    float width{};
    float height{};
    // Fixed turn (Z_Angle, radians), or a random one per mark (Random_Rotate).
    float angle{};
    bool random_rotate{};
    // Each mark's size varies by up to this share (Random_Size_Offset).
    float size_variation{};
    // Seconds before it fades (Fadeout_Time), unless it stays (Permanent).
    float fade_seconds{};
    bool permanent{};
    // The atlas cells to pick from (UV_Slot pairs), each spanning this many cells per side
    // (Slot_Scale). Cells outside the atlas are dropped with a warning, so a decal may have none.
    std::vector<TerrainDecalCell> cells;
    std::uint32_t cell_span{1};
};

class TerrainDecalCatalog final
{
public:
    // Malformed fields are reported to `warnings` when given.
    [[nodiscard]] static TerrainDecalCatalog build(const XmlAssetDatabase& database, std::vector<std::string>* warnings = nullptr);

    // A decal by name, ignoring case; null when unknown.
    [[nodiscard]] const TerrainDecal* find(std::string_view name) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, TerrainDecal> decals_;
};

} // namespace eaw
