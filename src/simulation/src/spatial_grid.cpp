#include "simulation/spatial_grid.hpp"

#include "core/status.hpp"

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
    build(positions, {}, cell_size);
}

void SpatialGrid::build(const std::span<const Vector3> positions, const std::span<const float> radii, const float cell_size)
{
    EAW_ASSERT_MSG(cell_size > 0.0F && std::isfinite(cell_size), "spatial grid cells must have a positive size");
    EAW_ASSERT_MSG(radii.empty() || radii.size() == positions.size(), "spatial grid radii must match its positions");
    positions_.assign(positions.begin(), positions.end());
    radii_.clear();
    for (const auto radius : radii)
    {
        radii_.push_back(std::isfinite(radius) && radius > 0.0F ? radius : 0.0F);
    }
    const auto radius_of = [&](const std::size_t item)
    {
        return radii_.empty() ? 0.0F : radii_[item];
    };
    auto minimum_x = std::numeric_limits<float>::max();
    auto minimum_y = std::numeric_limits<float>::max();
    auto maximum_x = std::numeric_limits<float>::lowest();
    auto maximum_y = std::numeric_limits<float>::lowest();
    for (std::size_t item = 0; item < positions.size(); ++item)
    {
        const auto& position = positions[item];
        if (finite_xy(position))
        {
            const auto radius = radius_of(item);
            minimum_x = std::min(minimum_x, position.x - radius);
            minimum_y = std::min(minimum_y, position.y - radius);
            maximum_x = std::max(maximum_x, position.x + radius);
            maximum_y = std::max(maximum_y, position.y + radius);
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
    // Counting sort over every cell each item covers.
    starts_.assign(static_cast<std::size_t>(columns_) * rows_ + 1U, 0);
    const auto for_cells = [&](const std::size_t item, const auto& each)
    {
        const auto range = cells_of(positions[item], radius_of(item));
        for (auto row = range.first_row; row <= range.last_row; ++row)
        {
            for (auto column = range.first_column; column <= range.last_column; ++column)
            {
                each(static_cast<std::size_t>(row) * columns_ + column);
            }
        }
    };
    for (std::size_t item = 0; item < positions.size(); ++item)
    {
        if (finite_xy(positions[item]))
        {
            for_cells(item,
                      [&](const std::size_t cell)
                      {
                          ++starts_[cell + 1U];
                      });
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
            for_cells(item,
                      [&](const std::size_t cell)
                      {
                          items_[cursor[cell]++] = item;
                      });
        }
    }
}

SpatialGrid::CellRange SpatialGrid::cells_of(const Vector3 center, const float radius) const noexcept
{
    const auto cell = [&](const float value, const float origin, const std::uint32_t count)
    {
        return static_cast<std::uint32_t>(std::clamp(std::floor((value - origin) / cell_size_), 0.0F, static_cast<float>(count - 1U)));
    };
    return {cell(center.x - radius, origin_x_, columns_), cell(center.x + radius, origin_x_, columns_), cell(center.y - radius, origin_y_, rows_), cell(center.y + radius, origin_y_, rows_)};
}

template <typename Accepts> void SpatialGrid::visit(const Vector3 center, const float reach, std::vector<std::uint32_t>& out, const Accepts& accepts) const
{
    if (columns_ == 0 || !finite_xy(center) || !std::isfinite(reach) || reach < 0.0F)
    {
        return;
    }
    const auto query = cells_of(center, reach);
    for (auto row = query.first_row; row <= query.last_row; ++row)
    {
        for (auto column = query.first_column; column <= query.last_column; ++column)
        {
            const auto cell = static_cast<std::size_t>(row) * columns_ + column;
            for (auto index = starts_[cell]; index < starts_[cell + 1]; ++index)
            {
                const auto item = items_[index];
                // An item in several cells is reported from the first cell its cells share with the
                // query's.
                if (!radii_.empty() && radii_[item] > 0.0F)
                {
                    const auto own = cells_of(positions_[item], radii_[item]);
                    if (column != std::max(own.first_column, query.first_column) || row != std::max(own.first_row, query.first_row))
                    {
                        continue;
                    }
                }
                if (accepts(item))
                {
                    out.push_back(item);
                }
            }
        }
    }
}

void SpatialGrid::query(const Vector3 center, const float radius, std::vector<std::uint32_t>& out) const
{
    const auto radius_squared = radius * radius;
    visit(center, radius, out,
          [&](const std::uint32_t item)
          {
              const auto dx = positions_[item].x - center.x;
              const auto dy = positions_[item].y - center.y;
              return dx * dx + dy * dy <= radius_squared;
          });
}

void SpatialGrid::query_overlapping(const Vector3 center, const float radius, std::vector<std::uint32_t>& out) const
{
    // The query's cells reach its own radius only: an item covers every cell its circle does.
    visit(center, radius, out,
          [&](const std::uint32_t item)
          {
              const auto reach = radius + (radii_.empty() ? 0.0F : radii_[item]);
              const auto dx = positions_[item].x - center.x;
              const auto dy = positions_[item].y - center.y;
              return dx * dx + dy * dy <= reach * reach;
          });
}

std::size_t SpatialGrid::size() const noexcept
{
    return items_.size();
}

} // namespace eaw
