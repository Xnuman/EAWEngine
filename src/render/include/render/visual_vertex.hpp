#pragma once

// Vertex layouts of models and terrain.

#include "math/vector.hpp"
#include "render/color.hpp"

#include <array>
#include <cstdint>

namespace eaw {

struct VisualVertex
{
    Vector3 position{};
    Vector3 normal{0.0F, 0.0F, 1.0F};
    Vector2 texture_coordinates{};
    std::array<std::uint32_t, 4> bone_indices{};
    std::array<float, 4> bone_weights{};
    // Tangent frame for normal mapping, in the same space as the normal.
    Vector3 tangent{1.0F, 0.0F, 0.0F};
    Vector3 binormal{0.0F, 1.0F, 0.0F};
    // Linear RGBA multiplier (authored vertex colors, or opacity fades along track ribbons).
    Color color{white};
};

// One terrain vertex. Terrain is drawn as independent triangles: each vertex repeats its
// triangle's three corner layers and carries its own barycentric weights, so the pixel shader can
// blend the corner layers smoothly.
struct VisualTerrainVertex
{
    Vector3 position{};
    Vector3 normal{0.0F, 0.0F, 1.0F};
    std::array<std::uint32_t, 3> layers{};
    std::array<float, 3> barycentric{};
};

} // namespace eaw
