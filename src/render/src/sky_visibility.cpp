#include "render/sky_visibility.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eaw {

std::vector<float> compute_sky_visibility(const std::span<const float> heights, const std::uint32_t width, const std::uint32_t height, const float spacing)
{
    if (width < 2 || height < 2 || heights.size() < static_cast<std::size_t>(width) * height || !(spacing > 0.0F))
    {
        return {};
    }
    const auto at = [&](const std::int64_t x, const std::int64_t y)
    {
        return heights[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    };
    const auto steps = static_cast<std::int64_t>(std::ceil(sky_visibility_reach / spacing));
    std::vector<float> visibility(static_cast<std::size_t>(width) * height, 1.0F);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto here = at(x, y);
            if (!std::isfinite(here))
            {
                continue;
            }
            float seen = 0.0F;
            for (std::uint32_t direction = 0; direction < sky_visibility_directions; ++direction)
            {
                const auto angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(direction) / static_cast<float>(sky_visibility_directions);
                const auto dx = std::cos(angle);
                const auto dy = std::sin(angle);
                // The steepest rise toward the horizon (tangent of its angle), sampled one spacing
                // at a time out to the reach or the grid's edge.
                float steepest = 0.0F;
                for (std::int64_t step = 1; step <= steps; ++step)
                {
                    const auto sx = static_cast<std::int64_t>(std::lround(static_cast<float>(x) + dx * static_cast<float>(step)));
                    const auto sy = static_cast<std::int64_t>(std::lround(static_cast<float>(y) + dy * static_cast<float>(step)));
                    if (sx < 0 || sy < 0 || sx >= width || sy >= height)
                    {
                        break;
                    }
                    const auto rise = at(sx, sy) - here;
                    if (std::isfinite(rise))
                    {
                        steepest = std::max(steepest, rise / (static_cast<float>(step) * spacing));
                    }
                }
                // cos^2 of the horizon's angle: 1 / (1 + tan^2).
                seen += 1.0F / (1.0F + steepest * steepest);
            }
            visibility[static_cast<std::size_t>(y) * width + x] = seen / static_cast<float>(sky_visibility_directions);
        }
    }
    return visibility;
}

} // namespace eaw
