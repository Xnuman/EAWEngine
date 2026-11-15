#pragma once

// Uniform-grid broad phase over entity positions in the XY plane. Rebuilt from dense storage, so
// queries visit items in a fixed order (cells row by row, then ascending item index) that depends
// only on the input: deterministic. Built with radii, an item lies in every cell its circle covers,
// so an overlap query reaches only as far as its own radius however large other items are (one
// walker no longer makes every trooper's query a walker's); a query reports such an item once, from
// the first cell where its cells and the query's meet.

#include "simulation/simulation_types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

class SpatialGrid final
{
public:
    // Items are indices into `positions`; `cell_size` must be positive and finite. Non-finite
    // positions are left out of the grid.
    void build(std::span<const Vector3> positions, float cell_size);
    // The same with each item's radius (`radii` as long as `positions`; negative or non-finite radii
    // count as 0).
    void build(std::span<const Vector3> positions, std::span<const float> radii, float cell_size);

    // Appends to `out` every item whose center lies within `radius` of `center` (XY distance), each
    // once, in the fixed order.
    void query(Vector3 center, float radius, std::vector<std::uint32_t>& out) const;
    // Appends every item whose circle reaches within `radius` of `center` (XY distance at most
    // `radius` plus its radius), each once, in the fixed order.
    void query_overlapping(Vector3 center, float radius, std::vector<std::uint32_t>& out) const;

    [[nodiscard]] std::size_t size() const noexcept;
    bool operator==(const SpatialGrid&) const = default;

private:
    float cell_size_{1.0F};
    float origin_x_{};
    float origin_y_{};
    std::uint32_t columns_{};
    std::uint32_t rows_{};
    // Counting-sort layout: items of cell c are items_[starts_[c] .. starts_[c + 1]).
    std::vector<std::uint32_t> starts_;
    std::vector<std::uint32_t> items_;
    std::vector<Vector3> positions_;
    // Empty when built without radii.
    std::vector<float> radii_;

    struct CellRange
    {
        std::uint32_t first_column{};
        std::uint32_t last_column{};
        std::uint32_t first_row{};
        std::uint32_t last_row{};
    };
    [[nodiscard]] CellRange cells_of(Vector3 center, float radius) const noexcept;
    template <typename Accepts> void visit(Vector3 center, float reach, std::vector<std::uint32_t>& out, const Accepts& accepts) const;
};

} // namespace eaw
