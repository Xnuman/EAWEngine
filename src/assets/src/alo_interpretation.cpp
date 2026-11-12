// The ALO naming convention for variants and the default mesh visibility (eaw/alo_interpretation.hpp).

#include "assets/alo_interpretation.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace eaw {

AloNameVariant alo_name_variant(const std::string_view name) noexcept
{
    std::size_t digits = 0;
    while (digits < name.size() && digits < 9 && ascii_is_digit(name[name.size() - 1 - digits]))
    {
        ++digits;
    }
    if (digits == 0 || digits == 9 || name.size() < digits + 4)
    {
        return {};
    }
    const auto marker = name.substr(name.size() - digits - 4, 4);
    const auto folded = ascii_upper(marker);
    AloVariantKind kind = AloVariantKind::none;
    if (folded == "_ALT")
    {
        kind = AloVariantKind::alternate;
    }
    else if (folded == "_LOD")
    {
        kind = AloVariantKind::level_of_detail;
    }
    else
    {
        return {};
    }
    std::uint32_t index = 0;
    for (const auto character : name.substr(name.size() - digits))
    {
        index = index * 10U + static_cast<std::uint32_t>(character - '0');
    }
    return {kind, index};
}

AloNameVariant alo_mesh_variant(const AloModelData& model, const AloMesh& mesh) noexcept
{
    auto variant = alo_name_variant(mesh.name);
    if (variant.kind == AloVariantKind::none && mesh.attached_bone && *mesh.attached_bone < model.bones.size())
    {
        variant = alo_name_variant(model.bones[*mesh.attached_bone].name);
    }
    return variant;
}

AloModelVariants alo_model_variants(const AloModelData& model)
{
    AloModelVariants variants;
    for (const auto& mesh : model.meshes)
    {
        const auto variant = alo_mesh_variant(model, mesh);
        if (variant.kind == AloVariantKind::alternate)
        {
            variants.highest_alternate = std::max(variants.highest_alternate, variant.index);
        }
        else if (variant.kind == AloVariantKind::level_of_detail && std::ranges::find(variants.detail_levels, variant.index) == variants.detail_levels.end())
        {
            variants.detail_levels.push_back(variant.index);
        }
    }
    std::ranges::sort(variants.detail_levels);
    return variants;
}

std::vector<bool> alo_mesh_visibility(const AloModelData& model, const AloMeshSelection& selection)
{
    const auto levels = alo_model_variants(model).detail_levels;
    const auto detail_level = selection.detail_level.value_or(levels.empty() ? 0U : levels.back());
    std::vector<bool> visible;
    visible.reserve(model.meshes.size());
    for (const auto& mesh : model.meshes)
    {
        const auto variant = alo_mesh_variant(model, mesh);
        const bool hidden_bone = selection.apply_bind_pose_bones && mesh.attached_bone && *mesh.attached_bone < model.bones.size() && !model.bones[*mesh.attached_bone].visible;
        const bool other_state = variant.kind == AloVariantKind::alternate && variant.index != selection.alternate;
        const bool other_lod = variant.kind == AloVariantKind::level_of_detail && variant.index != detail_level;
        visible.push_back(!mesh.hidden && !hidden_bone && !other_state && !other_lod);
    }
    return visible;
}

} // namespace eaw
