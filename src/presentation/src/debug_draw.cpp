#include "presentation/debug_draw.hpp"

#include "math/matrix.hpp"

#include <cstddef>

namespace eaw {

void append_debug_box(std::vector<VisualDebugLine>& lines, const Vector3& minimum, const Vector3& maximum, const Matrix4& transform, const Color& color)
{
    // Corner bit 0 selects X, bit 1 Y, bit 2 Z (0 = minimum, 1 = maximum).
    std::array<Vector3, 8> corners{};
    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
        const Vector3 local{(corner & 1U) != 0 ? maximum.x : minimum.x, (corner & 2U) != 0 ? maximum.y : minimum.y, (corner & 4U) != 0 ? maximum.z : minimum.z};
        corners[corner] = transform_point(local, Matrix4{transform});
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

void append_debug_axes(std::vector<VisualDebugLine>& lines, const Vector3& origin, const float length)
{
    lines.push_back({origin, {origin.x + length, origin.y, origin.z}, {1.0F, 0.2F, 0.2F, 1.0F}});
    lines.push_back({origin, {origin.x, origin.y + length, origin.z}, {0.2F, 1.0F, 0.2F, 1.0F}});
    lines.push_back({origin, {origin.x, origin.y, origin.z + length}, {0.3F, 0.5F, 1.0F, 1.0F}});
}

} // namespace eaw
