#pragma once

// How the game asset layer's resources load from the VFS: bytes, a format reader, then a loader.

#include "assets/animation.hpp"
#include "assets/model.hpp"
#include "assets/particle_rules.hpp"
#include "assets/resource_manager.hpp"
#include "core/status.hpp"
#include "formats/ted_map.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

template <> struct ResourceLoader<Model>
{
    using Value = Result<Model>;
    static constexpr std::string_view kind = "model";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

template <> struct ResourceLoader<Animation>
{
    using Value = Result<Animation>;
    static constexpr std::string_view kind = "animation";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

// TED map data stays file-level for now: the map builders read it directly.
template <> struct ResourceLoader<TedMapData>
{
    using Value = Result<TedMapData>;
    static constexpr std::string_view kind = "ted";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

// A particle effect file loaded under a fidelity profile: the effect, or why there is none. A file
// holding a model rather than a particle system is not an error (`model_file`): callers that look
// effects up by name skip it without a warning.
struct ParticleEffectLoad
{
    std::optional<ParticleEffect> effect;
    bool model_file{};
    std::string error;
    // What decoding the file reported.
    std::vector<std::string> warnings;
};

template <> struct ResourceLoader<ParticleEffect>
{
    using Value = ParticleEffectLoad;
    static constexpr std::string_view kind = "particle-effect";
    // Folded path and profile: the same file loads differently per profile.
    [[nodiscard]] static std::string key(std::string_view path, ParticleFidelity fidelity);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path, ParticleFidelity fidelity);
};

} // namespace eaw
