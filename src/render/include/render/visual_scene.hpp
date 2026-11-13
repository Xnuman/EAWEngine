#pragma once

// A whole scene handed to the renderer at once: geometry, textures, materials, meshes, and the
// instances placed at load.

#include "math/vector.hpp"
#include "render/visual_frame.hpp"
#include "render/visual_material.hpp"
#include "render/visual_mesh.hpp"
#include "render/visual_texture.hpp"
#include "render/visual_vertex.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

struct VisualScene
{
    std::string name;
    std::vector<VisualVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<VisualTexture> textures;
    std::vector<VisualMaterial> materials;
    std::vector<VisualDraw> draws;
    std::vector<VisualMesh> meshes;
    std::vector<VisualInstance> instances;
    // Empty vertices when the scene has no terrain.
    VisualTerrain terrain;
    // Size of the scene bone palette (every instance's bone_offset + mesh bone_count fits in it).
    std::uint32_t bone_count{};
    Vector3 bounds_minimum{};
    Vector3 bounds_maximum{};
    std::vector<std::string> warnings;
};

} // namespace eaw
