#include "formats/alo_generic.hpp"

#include "formats/alamo_chunks.hpp"

namespace eaw {
namespace {

constexpr std::uint32_t skeleton_chunk = 0x0200;
constexpr std::uint32_t particle_system_chunk = 0x0900;
constexpr std::uint32_t plugin_particle_system_chunk = 0x1500;
// A chunk header is a type and a size, four bytes each.
constexpr std::size_t chunk_header_bytes = 8;

} // namespace

template <> Result<AloGenericData> read_alo<AloGenericData>(const std::span<const std::byte> bytes)
{
    if (bytes.size() < chunk_header_bytes)
    {
        return make_error("ALO file is too short for a chunk header");
    }
    AloGenericData data;
    switch (chunk::read_u32(bytes, 0))
    {
    case skeleton_chunk:
        data.type = AloType::model;
        break;
    case particle_system_chunk:
        data.type = AloType::particle_system;
        break;
    case plugin_particle_system_chunk:
        data.type = AloType::plugin_particle_system;
        break;
    default:
        data.type = AloType::unknown;
        break;
    }
    return data;
}

} // namespace eaw
