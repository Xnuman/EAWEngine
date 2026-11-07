#include "eaw/legacy_material.hpp"

#include <algorithm>
#include <cctype>

namespace eaw {
namespace {

std::string fold_ascii(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

bool contains(const std::string_view value, const std::string_view token) noexcept
{
    return value.find(token) != std::string_view::npos;
}

LegacyTextureSemantic texture_semantic(const std::string_view parameter)
{
    if (contains(parameter, "normal") || contains(parameter, "bump"))
    {
        return LegacyTextureSemantic::normal;
    }
    if (contains(parameter, "specular") || contains(parameter, "gloss"))
    {
        return LegacyTextureSemantic::specular;
    }
    if (contains(parameter, "emissive") || contains(parameter, "emission") || contains(parameter, "glow"))
    {
        return LegacyTextureSemantic::emissive;
    }
    if (contains(parameter, "opacity") || contains(parameter, "alpha"))
    {
        return LegacyTextureSemantic::opacity;
    }
    if (contains(parameter, "distort") || contains(parameter, "refraction"))
    {
        return LegacyTextureSemantic::distortion;
    }
    if (contains(parameter, "diffuse") || contains(parameter, "color") || contains(parameter, "albedo") || contains(parameter, "base") || contains(parameter, "cloud") || parameter == "texture" ||
        parameter == "map")
    {
        return LegacyTextureSemantic::base_color;
    }
    if (contains(parameter, "wave"))
    {
        return LegacyTextureSemantic::distortion;
    }
    return LegacyTextureSemantic::unknown;
}

} // namespace

LegacyMaterial LegacyMaterial::translate(const AloSubMesh& submesh)
{
    LegacyMaterial result;
    result.source_shader = submesh.shader;
    result.parameters = submesh.parameters;
    const auto shader = fold_ascii(submesh.shader);

    if (shader == "aldefault.fx" || shader == "default.fx")
    {
        result.family = LegacyMaterialFamily::default_surface;
    }
    else if (contains(shader, "planet"))
    {
        result.family = LegacyMaterialFamily::planet;
    }
    else if (contains(shader, "tree") || contains(shader, "grass"))
    {
        result.family = LegacyMaterialFamily::foliage;
    }
    else if (contains(shader, "skydome") || contains(shader, "nebula"))
    {
        result.family = LegacyMaterialFamily::sky;
    }
    else if (contains(shader, "collision"))
    {
        result.family = LegacyMaterialFamily::collision;
    }
    else if (contains(shader, "shadow"))
    {
        result.family = LegacyMaterialFamily::shadow;
    }
    else if (contains(shader, "particle"))
    {
        result.family = LegacyMaterialFamily::particle;
    }
    else if (contains(shader, "terrain"))
    {
        result.family = LegacyMaterialFamily::terrain;
    }
    else if (contains(shader, "decal"))
    {
        result.family = LegacyMaterialFamily::decal;
    }
    else if (contains(shader, "skin"))
    {
        result.family = LegacyMaterialFamily::skinned_mesh;
    }
    else if (contains(shader, "mesh"))
    {
        result.family = LegacyMaterialFamily::mesh;
    }

    if (contains(shader, "additive") || contains(shader, "add.fx"))
    {
        result.blend = LegacyBlendMode::additive;
    }
    else if (contains(shader, "alphatest") || contains(shader, "alpha_test") || result.family == LegacyMaterialFamily::foliage)
    {
        result.blend = LegacyBlendMode::alpha_test;
    }
    else if (contains(shader, "transparent") || contains(shader, "blend") || contains(shader, "alpha") || contains(shader, "shield"))
    {
        result.blend = LegacyBlendMode::alpha_blend;
    }
    result.two_sided = contains(shader, "twosided") || contains(shader, "two_sided");

    for (const auto& parameter : submesh.parameters)
    {
        if (parameter.type != AloShaderParameterType::texture || parameter.value.empty())
        {
            continue;
        }
        const auto name = fold_ascii(parameter.name);
        result.textures.push_back({parameter.name, parameter.value, texture_semantic(name)});
    }
    if (result.textures.size() == 1 && result.textures.front().semantic == LegacyTextureSemantic::unknown)
    {
        result.textures.front().semantic = LegacyTextureSemantic::base_color;
    }
    return result;
}

std::string_view to_string(const LegacyMaterialFamily family) noexcept
{
    switch (family)
    {
    case LegacyMaterialFamily::mesh:
        return "mesh";
    case LegacyMaterialFamily::skinned_mesh:
        return "skinned-mesh";
    case LegacyMaterialFamily::terrain:
        return "terrain";
    case LegacyMaterialFamily::particle:
        return "particle";
    case LegacyMaterialFamily::decal:
        return "decal";
    case LegacyMaterialFamily::planet:
        return "planet";
    case LegacyMaterialFamily::foliage:
        return "foliage";
    case LegacyMaterialFamily::sky:
        return "sky";
    case LegacyMaterialFamily::default_surface:
        return "default-surface";
    case LegacyMaterialFamily::shadow:
        return "shadow";
    case LegacyMaterialFamily::collision:
        return "collision";
    case LegacyMaterialFamily::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string_view to_string(const LegacyBlendMode blend) noexcept
{
    switch (blend)
    {
    case LegacyBlendMode::opaque:
        return "opaque";
    case LegacyBlendMode::alpha_test:
        return "alpha-test";
    case LegacyBlendMode::alpha_blend:
        return "alpha-blend";
    case LegacyBlendMode::additive:
        return "additive";
    }
    return "opaque";
}

std::string_view to_string(const LegacyTextureSemantic semantic) noexcept
{
    switch (semantic)
    {
    case LegacyTextureSemantic::base_color:
        return "base-color";
    case LegacyTextureSemantic::normal:
        return "normal";
    case LegacyTextureSemantic::specular:
        return "specular";
    case LegacyTextureSemantic::emissive:
        return "emissive";
    case LegacyTextureSemantic::opacity:
        return "opacity";
    case LegacyTextureSemantic::distortion:
        return "distortion";
    case LegacyTextureSemantic::unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace eaw
