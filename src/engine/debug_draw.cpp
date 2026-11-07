#include "debug_draw.hpp"

#include <cstddef>

namespace eaw {
namespace {

std::array<float, 3> transform_point(const std::array<float, 3>& point, const std::array<float, 16>& matrix)
{
    return {
        point[0] * matrix[0] + point[1] * matrix[4] + point[2] * matrix[8] + matrix[12],
        point[0] * matrix[1] + point[1] * matrix[5] + point[2] * matrix[9] + matrix[13],
        point[0] * matrix[2] + point[1] * matrix[6] + point[2] * matrix[10] + matrix[14],
    };
}

} // namespace

void append_debug_box(std::vector<VisualDebugLine>& lines, const std::array<float, 3>& minimum, const std::array<float, 3>& maximum, const std::array<float, 16>& transform,
                      const std::array<float, 4>& color)
{
    // Corner bit 0 selects X, bit 1 Y, bit 2 Z (0 = minimum, 1 = maximum).
    std::array<std::array<float, 3>, 8> corners{};
    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
        const std::array<float, 3> local{(corner & 1U) != 0 ? maximum[0] : minimum[0], (corner & 2U) != 0 ? maximum[1] : minimum[1], (corner & 4U) != 0 ? maximum[2] : minimum[2]};
        corners[corner] = transform_point(local, transform);
    }
    // Every pair of corners differing in exactly one bit is an edge.
    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
        for (std::size_t bit = 1; bit < corners.size(); bit <<= 1U)
        {
            if ((corner & bit) == 0)
            {
                lines.push_back({corners[corner], corners[corner | bit], color});
            }
        }
    }
}

void append_debug_axes(std::vector<VisualDebugLine>& lines, const std::array<float, 3>& origin, const float length)
{
    lines.push_back({origin, {origin[0] + length, origin[1], origin[2]}, {1.0F, 0.2F, 0.2F, 1.0F}});
    lines.push_back({origin, {origin[0], origin[1] + length, origin[2]}, {0.2F, 1.0F, 0.2F, 1.0F}});
    lines.push_back({origin, {origin[0], origin[1], origin[2] + length}, {0.3F, 0.5F, 1.0F, 1.0F}});
}

} // namespace eaw
