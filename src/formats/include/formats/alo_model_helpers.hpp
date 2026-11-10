#pragma once

// Values derived from ALO model data.

#include "formats/alo_model.hpp"
#include "math/matrix.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eaw {

[[nodiscard]] std::size_t alo_submesh_count(const AloModelData& model) noexcept;
[[nodiscard]] std::uint64_t alo_vertex_count(const AloModelData& model) noexcept;
[[nodiscard]] std::uint64_t alo_triangle_count(const AloModelData& model) noexcept;
// Each bone's transform relative to its parent, as the file stores it (3x4, transposed into the
// row-vector convention).
[[nodiscard]] std::vector<Matrix4> alo_local_bind_pose(const AloModelData& model);
// Each bone's model-space bind transform.
[[nodiscard]] std::vector<Matrix4> alo_bind_pose(const AloModelData& model);

} // namespace eaw
