#pragma once

#include "eaw/alo_model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace eaw {

enum class LegacyMaterialFamily
{
    mesh,
    skinned_mesh,
    terrain,
    particle,
    decal,
    planet,
    foliage,
    sky,
    default_surface,
    shadow,
    collision,
    unknown,
};

enum class LegacyBlendMode
{
    opaque,
    alpha_test,
    alpha_blend,
    additive,
};

enum class LegacyTextureSemantic
{
    base_color,
    normal,
    specular,
    emissive,
    opacity,
    distortion,
    unknown,
};

struct LegacyTextureBinding
{
    std::string parameter;
    std::string reference;
    LegacyTextureSemantic semantic{LegacyTextureSemantic::unknown};
};

struct LegacyMaterial
{
    std::string source_shader;
    LegacyMaterialFamily family{LegacyMaterialFamily::unknown};
    LegacyBlendMode blend{LegacyBlendMode::opaque};
    bool two_sided{};
    std::vector<LegacyTextureBinding> textures;
    std::vector<AloShaderParameter> parameters;

    [[nodiscard]] static LegacyMaterial translate(const AloSubMesh& submesh);
};

[[nodiscard]] std::string_view to_string(LegacyMaterialFamily family) noexcept;
[[nodiscard]] std::string_view to_string(LegacyBlendMode blend) noexcept;
[[nodiscard]] std::string_view to_string(LegacyTextureSemantic semantic) noexcept;

} // namespace eaw
