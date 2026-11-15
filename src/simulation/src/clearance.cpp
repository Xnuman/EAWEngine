#include "simulation/clearance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace eaw {
namespace {

// The clearance of `cell` for `passable_types`, up to `limit`: the distance from its center to the
// nearest cell square the class may not stand on, or to the map's edge, within the cells `reach`
// around it.
float cell_clearance(const GroundMap& ground, const std::uint32_t cell, const std::uint32_t passable_types, const float limit, const long reach) noexcept
{
    if ((ground.passability[cell] & passable_types) == 0)
    {
        return 0.0F;
    }
    const auto half = ground.spacing * 0.5F;
    const auto x = static_cast<long>(cell % ground.width);
    const auto y = static_cast<long>(cell / ground.width);
    // The map's edge: its squares end half a cell beyond the outer samples.
    auto best = std::min({static_cast<float>(x) * ground.spacing + half, static_cast<float>(static_cast<long>(ground.width) - 1 - x) * ground.spacing + half,
                          static_cast<float>(y) * ground.spacing + half, static_cast<float>(static_cast<long>(ground.height) - 1 - y) * ground.spacing + half, limit});
    for (long other_y = std::max(0L, y - reach); other_y <= std::min(static_cast<long>(ground.height) - 1, y + reach); ++other_y)
    {
        for (long other_x = std::max(0L, x - reach); other_x <= std::min(static_cast<long>(ground.width) - 1, x + reach); ++other_x)
        {
            const auto other = static_cast<std::uint32_t>(other_y) * ground.width + static_cast<std::uint32_t>(other_x);
            if ((ground.passability[other] & passable_types) != 0)
            {
                continue;
            }
            const auto gap_x = std::max(0.0F, static_cast<float>(std::abs(other_x - x)) * ground.spacing - half);
            const auto gap_y = std::max(0.0F, static_cast<float>(std::abs(other_y - y)) * ground.spacing - half);
            best = std::min(best, std::hypot(gap_x, gap_y));
        }
    }
    return best;
}

// Cells around a cell whose squares can lie within `limit` of its center.
long clearance_reach(const GroundMap& ground, const float limit) noexcept
{
    return static_cast<long>(std::ceil((limit + ground.spacing * 0.5F) / ground.spacing));
}

} // namespace

bool footing_fits(const GroundMap& ground, const std::uint32_t cell, const Footing& footing) noexcept
{
    if ((ground.passability[cell] & footing.passable_types) == 0)
    {
        return false;
    }
    return footing.clearance == nullptr || footing.radius <= 0.0F || footing.clearance->cells.size() != ground.passability.size() || footing.clearance->cells[cell] >= footing.radius;
}

ClearanceField clearance_field(const GroundMap& ground, const std::uint32_t passable_types, const float limit)
{
    ClearanceField field{.passable_types = passable_types, .limit = limit};
    if (!ground.has_passability())
    {
        return field;
    }
    field.cells.assign(ground.passability.size(), 0.0F);
    const auto reach = clearance_reach(ground, limit);
    for (std::uint32_t cell = 0; cell < field.cells.size(); ++cell)
    {
        field.cells[cell] = cell_clearance(ground, cell, passable_types, limit, reach);
    }
    return field;
}

void update_clearance(ClearanceField& field, const GroundMap& ground, const CellRect changed)
{
    if (!ground.has_passability() || field.cells.size() != ground.passability.size())
    {
        field = clearance_field(ground, field.passable_types, field.limit);
        return;
    }
    const auto reach = clearance_reach(ground, field.limit);
    const auto first_x = static_cast<long>(changed.first_column) - reach;
    const auto first_y = static_cast<long>(changed.first_row) - reach;
    const auto last_x = static_cast<long>(changed.last_column) + reach;
    const auto last_y = static_cast<long>(changed.last_row) + reach;
    for (long y = std::max(0L, first_y); y <= std::min(static_cast<long>(ground.height) - 1, last_y); ++y)
    {
        for (long x = std::max(0L, first_x); x <= std::min(static_cast<long>(ground.width) - 1, last_x); ++x)
        {
            const auto cell = static_cast<std::uint32_t>(y) * ground.width + static_cast<std::uint32_t>(x);
            field.cells[cell] = cell_clearance(ground, cell, field.passable_types, field.limit, reach);
        }
    }
}

float footing_bucket(const float radius) noexcept
{
    for (const auto bucket : footing_buckets)
    {
        if (radius <= bucket)
        {
            return bucket;
        }
    }
    return std::ceil(radius);
}

bool ground_stands(const GroundMap& ground, const Vector2 point, const Footing& footing) noexcept
{
    // Off the grid, or without passability data, nothing constrains a unit.
    const auto cell = ground.cell_at(point.x, point.y);
    return !cell || footing_fits(ground, *cell, footing);
}

} // namespace eaw
