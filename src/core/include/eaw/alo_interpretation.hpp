#pragma once

// Interpretation of decoded ALO models (eaw/alo_model.hpp): the naming convention for state and
// detail variants and which meshes form a model's default appearance. These are rules about what
// the file's facts mean, kept apart from decoding.

#include "eaw/alo_model.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eaw {

// Mesh/bone naming convention for state variants: a trailing "_ALT<n>" marks alternate states
// (stock models use ALT0 for intact and higher numbers for progressively damaged states) and
// "_LOD<n>" marks detail levels. Matching is case-insensitive and needs at least one digit.
enum class AloVariantKind : std::uint8_t
{
    none,
    alternate,
    level_of_detail,
};

struct AloNameVariant
{
    AloVariantKind kind{AloVariantKind::none};
    std::uint32_t index{};
};

[[nodiscard]] AloNameVariant alo_name_variant(std::string_view name) noexcept;

// Which meshes form a model's default appearance: not flagged hidden, not attached to a bone
// hidden in the bind pose, alternate (damage) state ALT0, and the model's highest LOD index. In the
// stock corpus higher LOD indices carry more triangles (LOD0 is the sparsest level in 137 of 145
// models with LOD meshes). The variant name is taken from the mesh, or from its attached bone when
// the mesh name carries none. Callers that animate bone visibility pass apply_bind_pose_bones =
// false and test the sampled pose instead.
[[nodiscard]] std::vector<bool> alo_default_mesh_visibility(const AloModel& model, bool apply_bind_pose_bones = true);

} // namespace eaw
