// The ALO naming convention for variants and the default mesh visibility (eaw/alo_interpretation.hpp).

#include "assets/alo_interpretation.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <cctype>
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
    while (digits < name.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(name[name.size() - 1 - digits])) != 0)
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

std::vector<bool> alo_default_mesh_visibility(const AloModelData& model, const bool apply_bind_pose_bones)
{
    const auto variant_of = [&](const AloMesh& mesh)
    {
        return alo_mesh_variant(model, mesh);
    };
    std::uint32_t highest_lod = 0;
    for (const auto& mesh : model.meshes)
    {
        if (const auto variant = variant_of(mesh); variant.kind == AloVariantKind::level_of_detail)
        {
            highest_lod = std::max(highest_lod, variant.index);
        }
    }
    std::vector<bool> visible;
    visible.reserve(model.meshes.size());
    for (const auto& mesh : model.meshes)
    {
        const auto variant = variant_of(mesh);
        const bool hidden_bone = apply_bind_pose_bones && mesh.attached_bone && *mesh.attached_bone < model.bones.size() && !model.bones[*mesh.attached_bone].visible;
        const bool other_state = variant.kind == AloVariantKind::alternate && variant.index != 0;
        const bool other_lod = variant.kind == AloVariantKind::level_of_detail && variant.index != highest_lod;
        visible.push_back(!mesh.hidden && !hidden_bone && !other_state && !other_lod);
    }
    return visible;
}

} // namespace eaw
