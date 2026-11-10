#pragma once

// An .alo file holds a model or a particle system. Reading it as AloGenericData tells which, so a
// caller that does not know can choose read_alo<AloModelData> or read_alo<AloParticleSystemData>.

#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace eaw {

enum class AloType : std::uint8_t
{
    // The file starts with a skeleton (chunk 0x0200), as models do.
    model,
    // 0x0900: an EaW/FoC particle system.
    particle_system,
    // 0x1500: the particle format of later Alamo games; not supported.
    plugin_particle_system,
    // Any other first chunk.
    unknown,
};

struct AloGenericData
{
    AloType type{AloType::unknown};
};

// Readers of the kinds of data an .alo holds (AloGenericData, AloModelData, AloParticleSystemData).
template <class T> [[nodiscard]] Result<T> read_alo(std::span<const std::byte> bytes);

// Errors only when the file is too short for a chunk header.
template <> [[nodiscard]] Result<AloGenericData> read_alo<AloGenericData>(std::span<const std::byte> bytes);

} // namespace eaw
