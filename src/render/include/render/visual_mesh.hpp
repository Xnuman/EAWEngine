#pragma once

// Geometry ranges: terrain, draws, and meshes with their damage states and detail levels.

#include "math/vector.hpp"
#include "render/visual_texture.hpp"
#include "render/visual_vertex.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

// Heightfield terrain in world space. Layer i is scene material first_material + i.
struct VisualTerrain
{
    std::vector<VisualTerrainVertex> vertices;
    std::uint32_t first_material{};
    std::uint32_t layer_count{};
    Vector3 bounds_minimum{};
    Vector3 bounds_maximum{};
    // How much sky each height sample sees (render/sky_visibility.hpp): a scene texture of one texel
    // per sample over the terrain's bounds, or none.
    std::uint32_t sky_visibility_texture{invalid_visual_texture};
};

struct VisualDraw
{
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::int32_t base_vertex{};
    std::uint32_t material{};
};

// One detail level of a mesh state: a contiguous range of the scene's draws (parts shared by
// several levels or states appear in each of their ranges; the geometry is not copied).
struct VisualMeshLod
{
    std::uint32_t first_draw{};
    std::uint32_t draw_count{};
};

// A damage state of a mesh (the legacy _ALT<n> number) and its detail levels, most detailed first.
struct VisualMeshState
{
    std::uint32_t alternate{};
    std::vector<VisualMeshLod> lods;
};

// A model in the scene: a contiguous range of draws sharing one bone index space, its default
// appearance (intact, full detail). A mesh with damage states or detail levels lists them in
// `states` (alternate 0 first); the renderer then draws the instance's state at the level its LOD
// policy picks (render/visual_lod.hpp). Without `states` the default range is the only appearance.
struct VisualMesh
{
    std::string name;
    std::uint32_t first_draw{};
    std::uint32_t draw_count{};
    std::uint32_t bone_count{};
    Vector3 bounds_minimum{};
    Vector3 bounds_maximum{};
    std::vector<VisualMeshState> states;
};

} // namespace eaw
