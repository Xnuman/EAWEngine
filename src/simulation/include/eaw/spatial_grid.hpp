#pragma once

// Uniform-grid broad phase over entity positions in the XY plane. Rebuilt from dense storage, so
// queries visit items in a fixed order (cells row by row, then ascending item index) that depends
// only on the input: deterministic.

#include "eaw/simulation_types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

class SpatialGrid final
{
public:
    // Items are indices into `positions`; `cell_size` must be positive and finite. Non-finite
    // positions are left out of the grid.
    void build(std::span<const SimVector3> positions, float cell_size);

    // Appends to `out` every item within `radius` of `center` (XY distance), in the fixed order.
    void query(SimVector3 center, float radius, std::vector<std::uint32_t>& out) const;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    float cell_size_{1.0F};
    float origin_x_{};
    float origin_y_{};
    std::uint32_t columns_{};
    std::uint32_t rows_{};
    // Counting-sort layout: items of cell c are items_[starts_[c] .. starts_[c + 1]).
    std::vector<std::uint32_t> starts_;
    std::vector<std::uint32_t> items_;
    std::vector<SimVector3> positions_;
};

} // namespace eaw
