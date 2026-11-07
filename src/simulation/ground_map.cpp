#include "eaw/ground_map.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

bool GroundMap::valid() const noexcept
{
    return width >= 2 && height >= 2 && spacing > 0.0F && heights.size() == static_cast<std::size_t>(width) * height;
}

float GroundMap::terrain_height(const float world_x, const float world_y) const
{
    if (!valid())
    {
        return 0.0F;
    }
    const auto grid_x = std::clamp(world_x / spacing, 0.0F, static_cast<float>(width - 1));
    const auto grid_y = std::clamp(world_y / spacing, 0.0F, static_cast<float>(height - 1));
    const auto x = std::min(static_cast<std::uint32_t>(grid_x), width - 2);
    const auto y = std::min(static_cast<std::uint32_t>(grid_y), height - 2);
    const auto fx = grid_x - static_cast<float>(x);
    const auto fy = grid_y - static_cast<float>(y);
    const auto sample = [&](const std::uint32_t sx, const std::uint32_t sy)
    {
        return heights[static_cast<std::size_t>(sy) * width + sx];
    };
    const auto h00 = sample(x, y);
    const auto h10 = sample(x + 1, y);
    const auto h01 = sample(x, y + 1);
    const auto h11 = sample(x + 1, y + 1);
    // Triangles (x,y)-(x+1,y)-(x+1,y+1) when fx >= fy, otherwise (x,y)-(x+1,y+1)-(x,y+1).
    return fx >= fy ? h00 + fx * (h10 - h00) + fy * (h11 - h10) : h00 + fx * (h11 - h01) + fy * (h01 - h00);
}

float GroundMap::surface_height(const float x, const float y) const
{
    const auto ground = terrain_height(x, y);
    return liquid_level ? std::max(ground, *liquid_level) : ground;
}

std::optional<std::uint32_t> GroundMap::passability_at(const float x, const float y) const
{
    if (!valid() || passability.size() != heights.size() || !std::isfinite(x) || !std::isfinite(y))
    {
        return std::nullopt;
    }
    const auto column = std::lround(x / spacing);
    const auto row = std::lround(y / spacing);
    if (column < 0 || row < 0 || column >= static_cast<long>(width) || row >= static_cast<long>(height))
    {
        return std::nullopt;
    }
    return passability[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column)];
}

} // namespace eaw
