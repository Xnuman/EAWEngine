#include "eaw/spatial_grid.hpp"

#include "eaw/status.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {
namespace {

// Cells per axis at most; a wider spread of positions grows the cell size instead, so a stray
// far-away entity cannot make the grid allocate without bound.
constexpr std::uint32_t max_cells_per_axis = 1024;

bool finite_xy(const Vector3& position) noexcept
{
    return std::isfinite(position.x) && std::isfinite(position.y);
}

} // namespace

void SpatialGrid::build(const std::span<const Vector3> positions, const float cell_size)
{
    EAW_ASSERT_MSG(cell_size > 0.0F && std::isfinite(cell_size), "spatial grid cells must have a positive size");
    positions_.assign(positions.begin(), positions.end());
    auto minimum_x = std::numeric_limits<float>::max();
    auto minimum_y = std::numeric_limits<float>::max();
    auto maximum_x = std::numeric_limits<float>::lowest();
    auto maximum_y = std::numeric_limits<float>::lowest();
    for (const auto& position : positions)
    {
        if (finite_xy(position))
        {
            minimum_x = std::min(minimum_x, position.x);
            minimum_y = std::min(minimum_y, position.y);
            maximum_x = std::max(maximum_x, position.x);
            maximum_y = std::max(maximum_y, position.y);
        }
    }
    if (minimum_x > maximum_x)
    {
        columns_ = 0;
        rows_ = 0;
        starts_.assign(1, 0);
        items_.clear();
        return;
    }
    const auto span = std::max(maximum_x - minimum_x, maximum_y - minimum_y);
    cell_size_ = std::max(cell_size, span / static_cast<float>(max_cells_per_axis - 1));
    origin_x_ = minimum_x;
    origin_y_ = minimum_y;
    columns_ = std::min(max_cells_per_axis, static_cast<std::uint32_t>((maximum_x - minimum_x) / cell_size_) + 1U);
    rows_ = std::min(max_cells_per_axis, static_cast<std::uint32_t>((maximum_y - minimum_y) / cell_size_) + 1U);
    const auto cell_of = [&](const Vector3& position)
    {
        const auto column = std::min(columns_ - 1U, static_cast<std::uint32_t>((position.x - origin_x_) / cell_size_));
        const auto row = std::min(rows_ - 1U, static_cast<std::uint32_t>((position.y - origin_y_) / cell_size_));
        return static_cast<std::size_t>(row) * columns_ + column;
    };
    starts_.assign(static_cast<std::size_t>(columns_) * rows_ + 1U, 0);
    for (const auto& position : positions)
    {
        if (finite_xy(position))
        {
            ++starts_[cell_of(position) + 1U];
        }
    }
    for (std::size_t cell = 1; cell < starts_.size(); ++cell)
    {
        starts_[cell] += starts_[cell - 1];
    }
    items_.resize(starts_.back());
    auto cursor = starts_;
    for (std::uint32_t item = 0; item < positions.size(); ++item)
    {
        if (finite_xy(positions[item]))
        {
            items_[cursor[cell_of(positions[item])]++] = item;
        }
    }
}

void SpatialGrid::query(const Vector3 center, const float radius, std::vector<std::uint32_t>& out) const
{
    if (columns_ == 0 || !finite_xy(center) || !std::isfinite(radius) || radius < 0.0F)
    {
        return;
    }
    const auto first_column = std::clamp(std::floor((center.x - radius - origin_x_) / cell_size_), 0.0F, static_cast<float>(columns_ - 1));
    const auto last_column = std::clamp(std::floor((center.x + radius - origin_x_) / cell_size_), 0.0F, static_cast<float>(columns_ - 1));
    const auto first_row = std::clamp(std::floor((center.y - radius - origin_y_) / cell_size_), 0.0F, static_cast<float>(rows_ - 1));
    const auto last_row = std::clamp(std::floor((center.y + radius - origin_y_) / cell_size_), 0.0F, static_cast<float>(rows_ - 1));
    const auto radius_squared = radius * radius;
    for (auto row = static_cast<std::uint32_t>(first_row); row <= static_cast<std::uint32_t>(last_row); ++row)
    {
        for (auto column = static_cast<std::uint32_t>(first_column); column <= static_cast<std::uint32_t>(last_column); ++column)
        {
            const auto cell = static_cast<std::size_t>(row) * columns_ + column;
            for (auto index = starts_[cell]; index < starts_[cell + 1]; ++index)
            {
                const auto item = items_[index];
                const auto dx = positions_[item].x - center.x;
                const auto dy = positions_[item].y - center.y;
                if (dx * dx + dy * dy <= radius_squared)
                {
                    out.push_back(item);
                }
            }
        }
    }
}

std::size_t SpatialGrid::size() const noexcept
{
    return items_.size();
}

} // namespace eaw
