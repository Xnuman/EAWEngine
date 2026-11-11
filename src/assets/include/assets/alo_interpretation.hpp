#pragma once

// Interpretation of decoded ALO models (formats/alo_model.hpp): the naming convention for state and
// detail variants and which meshes form a model's default appearance. These are rules about what
// the file's facts mean, kept apart from decoding.

#include "formats/alo_model.hpp"

#include <cstdint>
#include <optional>
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

// A mesh's variant: from its name, or from its attached bone when the mesh name carries none.
[[nodiscard]] AloNameVariant alo_mesh_variant(const AloModelData& model, const AloMesh& mesh) noexcept;

// The alternate states and detail levels a model's meshes name: the highest ALT index (0 without
// ALT meshes) and the LOD indices present, ascending (empty without LOD meshes).
struct AloModelVariants
{
    std::uint32_t highest_alternate{};
    std::vector<std::uint32_t> detail_levels;
};
[[nodiscard]] AloModelVariants alo_model_variants(const AloModelData& model);

// Which appearance to show: an alternate (damage) state, ALT0 being intact, and a detail level,
// the model's highest LOD index by default. In the stock corpus higher LOD indices carry more
// triangles (LOD0 is the sparsest level in 137 of 145 models with LOD meshes). Callers that animate
// bone visibility clear apply_bind_pose_bones and test the sampled pose instead.
struct AloMeshSelection
{
    std::uint32_t alternate{};
    std::optional<std::uint32_t> detail_level;
    bool apply_bind_pose_bones{true};
};

// Which meshes form that appearance: not flagged hidden, not attached to a bone hidden in the bind
// pose, and in the selected state and detail level when they name one. The variant name is taken
// from the mesh, or from its attached bone when the mesh name carries none. The default selection
// is the model's default appearance.
[[nodiscard]] std::vector<bool> alo_mesh_visibility(const AloModelData& model, const AloMeshSelection& selection = {});

} // namespace eaw
