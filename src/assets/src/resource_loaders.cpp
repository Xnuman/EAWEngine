#include "assets/resource_loaders.hpp"

#include "core/string_utils.hpp"
#include "formats/ala_animation.hpp"
#include "formats/alo_generic.hpp"
#include "formats/alo_model.hpp"
#include "formats/alo_particle_system.hpp"
#include "assets/alamo_loaders.hpp"
#include "assets/sound_loaders.hpp"

#include <filesystem>

namespace eaw {

std::string ResourceLoader<Model>::key(const std::string_view path)
{
    return std::string(path);
}

Result<Model> ResourceLoader<Model>::load(const AssetReader& reader, const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto bytes, reader.read(path));
    EAW_TRY_ASSIGN(const auto data, read_alo<AloModelData>(bytes));
    return load_model(data);
}

std::string ResourceLoader<Sound>::key(const std::string_view path)
{
    return std::string(path);
}

Result<Sound> ResourceLoader<Sound>::load(const AssetReader& reader, const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto bytes, reader.read(path));
    auto sound = decode_sound(bytes);
    if (!sound)
    {
        return make_error(std::string(path) + ": " + sound.error().message);
    }
    return sound;
}

std::string ResourceLoader<Animation>::key(const std::string_view path)
{
    return std::string(path);
}

Result<Animation> ResourceLoader<Animation>::load(const AssetReader& reader, const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto bytes, reader.read(path));
    EAW_TRY_ASSIGN(const auto data, read_ala(bytes));
    return load_animation(data);
}

std::string ResourceLoader<TedMapData>::key(const std::string_view path)
{
    return std::string(path);
}

Result<TedMapData> ResourceLoader<TedMapData>::load(const AssetReader& reader, const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto bytes, reader.read(path));
    return read_ted(bytes);
}

std::string ResourceLoader<ParticleEffect>::key(const std::string_view path, const ParticleFidelity fidelity)
{
    return ascii_lower(path) + "|" + to_string(fidelity);
}

ParticleEffectLoad ResourceLoader<ParticleEffect>::load(const AssetReader& reader, const std::string_view path, const ParticleFidelity fidelity)
{
    ParticleEffectLoad result;
    const auto bytes = reader.read(path);
    if (!bytes)
    {
        result.error = bytes.error().message;
        return result;
    }
    const auto generic = read_alo<AloGenericData>(*bytes);
    if (!generic || (generic->type != AloType::particle_system && generic->type != AloType::plugin_particle_system))
    {
        result.model_file = true;
        return result;
    }
    auto system = read_alo<AloParticleSystemData>(*bytes);
    if (!system)
    {
        result.error = system.error().message;
        return result;
    }
    result.effect = load_particle_effect(*system, fidelity);
    result.effect->name = std::filesystem::path(path).stem().string();
    result.warnings = system->warnings;
    return result;
}

} // namespace eaw
