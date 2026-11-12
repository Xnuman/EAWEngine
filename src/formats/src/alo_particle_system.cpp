#include "formats/alamo_chunks.hpp"
#include "formats/alo_particle_system.hpp"

#include <algorithm>
#include <bit>
#include <sstream>

namespace eaw {
namespace {

// The shared chunk container (formats/alamo_chunks.hpp), with this format named in diagnostics.
using Chunk = ChunkRange;
using MiniChunk = MiniChunkRange;
using chunk::read_float;
using chunk::read_u32;
using chunk::read_vector3;

Result<std::vector<Chunk>> read_chunks(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end, const std::string_view where)
{
    return chunk::read_chunks(bytes, begin, end, "particle ALO", where);
}

Result<std::vector<MiniChunk>> read_minis(const std::span<const std::byte> bytes, const Chunk& parent, const std::string_view where)
{
    return chunk::read_minis(bytes, parent, "particle ALO", where);
}

constexpr std::size_t group_size = 64;
// The legacy particle shader table has 14 entries (docs/compatibility-research.md).
constexpr std::uint32_t known_blend_modes = 14;

template <class Range> std::string read_string(const std::span<const std::byte> bytes, const Range& range)
{
    return chunk::read_string(bytes, range.payload, range.end);
}

std::string hex(const std::uint32_t value)
{
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}

// Property payload sizes by kind.
enum class PropertyKind : std::uint8_t
{
    unknown,
    // Read but not interpreted by the known runtime (0x14 is an index, the others are unused).
    ignored,
    u8,
    u32,
    f32,
    vector3,
    color,
};

PropertyKind property_kind(const std::uint8_t id)
{
    switch (id)
    {
    case 0x07:
    case 0x08:
    case 0x23:
    case 0x2D:
    case 0x2E:
    case 0x31:
    case 0x35:
    case 0x3B:
    case 0x3D:
    case 0x41:
    case 0x43:
    case 0x46:
    case 0x48:
        return PropertyKind::u8;
    case 0x04:
    case 0x05:
    case 0x10:
    case 0x26:
    case 0x27:
    case 0x2A:
    case 0x2F:
    case 0x34:
        return PropertyKind::u32;
    case 0x09:
    case 0x0B:
    case 0x0C:
    case 0x0F:
    case 0x12:
    case 0x13:
    case 0x17:
    case 0x24:
    case 0x25:
    case 0x28:
    case 0x30:
    case 0x32:
    case 0x33:
    case 0x3C:
    case 0x3E:
    case 0x40:
    case 0x42:
    case 0x47:
        return PropertyKind::f32;
    case 0x0A:
        return PropertyKind::vector3;
    case 0x2C:
        return PropertyKind::color;
    case 0x06:
    case 0x11:
    case 0x14:
    case 0x15:
    case 0x2B:
    case 0x3F:
    case 0x44:
    case 0x49:
        return PropertyKind::ignored;
    default:
        return PropertyKind::unknown;
    }
}

std::size_t property_size(const PropertyKind kind)
{
    switch (kind)
    {
    case PropertyKind::u8:
        return 1;
    case PropertyKind::u32:
    case PropertyKind::f32:
        return 4;
    case PropertyKind::vector3:
        return 12;
    case PropertyKind::color:
        return 16;
    default:
        return 0;
    }
}

void apply_property(AloParticleEmitter& emitter, const std::uint8_t id, const std::span<const std::byte> bytes, const std::size_t offset)
{
    const auto flag = [&]
    {
        return std::to_integer<unsigned char>(bytes[offset]) != 0;
    };
    const auto u32 = [&]
    {
        return read_u32(bytes, offset);
    };
    const auto f32 = [&]
    {
        return read_float(bytes, offset);
    };
    switch (id)
    {
    case 0x04:
        emitter.blend_mode = u32();
        break;
    case 0x05:
        emitter.triangle_count = u32() + 1U;
        break;
    case 0x07:
        emitter.use_bursts = flag();
        break;
    case 0x08:
        emitter.link_to_system = flag();
        break;
    case 0x09:
        emitter.inward_speed = f32();
        break;
    case 0x0A:
        emitter.acceleration = read_vector3(bytes, offset);
        break;
    case 0x0B:
        emitter.inward_acceleration = f32();
        break;
    case 0x0C:
        emitter.gravity = f32();
        break;
    case 0x0F:
        emitter.lifetime = f32();
        break;
    case 0x10:
        emitter.texture_cells = u32();
        break;
    case 0x12:
        emitter.random_scale = f32();
        break;
    case 0x13:
        emitter.random_lifetime = f32();
        break;
    case 0x17:
        emitter.random_rotation_variance = f32();
        break;
    case 0x23:
        emitter.random_rotation_direction = flag();
        break;
    case 0x24:
        emitter.initial_delay = f32();
        break;
    case 0x25:
        emitter.burst_delay = f32();
        break;
    case 0x26:
        emitter.particles_per_burst = u32();
        break;
    case 0x27:
        emitter.burst_count = static_cast<std::int32_t>(u32());
        break;
    case 0x28:
        emitter.parent_link_strength = f32();
        break;
    case 0x2A:
        emitter.particles_per_second = u32();
        break;
    case 0x2C:
        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            emitter.random_colors[channel] = read_float(bytes, offset + channel * 4);
        }
        break;
    case 0x2D:
        emitter.random_color_grayscale = flag();
        break;
    case 0x2E:
        emitter.world_oriented = flag();
        break;
    case 0x2F:
        emitter.ground_behavior = u32();
        break;
    case 0x30:
        emitter.bounciness = f32();
        break;
    case 0x31:
        emitter.affected_by_wind = flag();
        break;
    case 0x32:
        emitter.freeze_time = f32();
        break;
    case 0x33:
        emitter.skip_time = f32();
        break;
    case 0x34:
        emitter.emit_from_mesh = u32();
        break;
    case 0x35:
        emitter.object_space_acceleration = flag();
        break;
    case 0x3B:
        emitter.heat = flag();
        break;
    case 0x3C:
        emitter.emit_from_mesh_offset = f32();
        break;
    case 0x3D:
        emitter.weather = flag();
        break;
    case 0x3E:
        emitter.weather_cube_size = f32();
        break;
    case 0x40:
        emitter.weather_fadeout_distance = f32();
        break;
    case 0x41:
        emitter.has_tail = flag();
        break;
    case 0x42:
        emitter.tail_size = f32();
        break;
    case 0x43:
        emitter.use_link_strength = flag();
        break;
    case 0x46:
        emitter.no_depth_test = flag();
        break;
    case 0x47:
        emitter.weather_cube_distance = f32();
        break;
    case 0x48:
        emitter.random_rotation = flag();
        break;
    default:
        break;
    }
}

Status read_properties(const std::span<const std::byte> bytes, const Chunk& chunk, AloParticleEmitter& emitter, const std::string& where, std::vector<std::string>& warnings)
{
    EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, chunk, where + " properties"));
    for (const auto& mini : minis)
    {
        const auto id = static_cast<std::uint8_t>(mini.type);
        emitter.properties.push_back({id, std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(mini.payload), bytes.begin() + static_cast<std::ptrdiff_t>(mini.end))});
        const auto kind = property_kind(id);
        if (kind == PropertyKind::unknown)
        {
            warnings.push_back(where + ": unknown property " + hex(id) + " (" + std::to_string(mini.size()) + " bytes) kept raw");
            continue;
        }
        if (kind == PropertyKind::ignored)
        {
            continue;
        }
        if (mini.size() < property_size(kind))
        {
            return make_error("particle ALO " + where + ": property " + hex(id) + " has " + std::to_string(mini.size()) + " bytes, expected " + std::to_string(property_size(kind)));
        }
        if (mini.size() > property_size(kind))
        {
            warnings.push_back(where + ": property " + hex(id) + " has " + std::to_string(mini.size() - property_size(kind)) + " trailing bytes");
        }
        apply_property(emitter, id, bytes, mini.payload);
    }
    // The known runtime ignores the link strength unless its flag is set.
    if (!emitter.use_link_strength)
    {
        emitter.parent_link_strength = 0.0F;
    }
    if (emitter.blend_mode >= known_blend_modes)
    {
        warnings.push_back(where + ": blend mode " + std::to_string(emitter.blend_mode) + " is outside the known table");
    }
    return {};
}

Status read_groups(const std::span<const std::byte> bytes, const Chunk& chunk, AloParticleEmitter& emitter, const std::string& where)
{
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end, where + " groups"));
    if (children.size() != emitter.groups.size())
    {
        return make_error("particle ALO " + where + ": expected 3 shape groups, found " + std::to_string(children.size()));
    }
    for (std::size_t index = 0; index < children.size(); ++index)
    {
        if (children[index].type != 0x1100)
        {
            return make_error("particle ALO " + where + ": shape group " + std::to_string(index) + " is chunk " + hex(children[index].type) + ", expected 0x1100");
        }
        EAW_TRY_ASSIGN(const auto data, read_chunks(bytes, children[index].payload, children[index].end, where + " group"));
        if (data.size() != 1 || data.front().type != 0x1101 || data.front().size() != group_size)
        {
            return make_error("particle ALO " + where + ": shape group " + std::to_string(index) + " does not hold one 64-byte 0x1101 chunk");
        }
        const auto at = data.front().payload;
        auto& group = emitter.groups[index];
        group.type = read_u32(bytes, at);
        group.minimum = read_vector3(bytes, at + 4);
        group.maximum = read_vector3(bytes, at + 16);
        group.side_length = read_float(bytes, at + 28);
        group.sphere_radius = read_float(bytes, at + 32);
        group.sphere_hollow = read_u32(bytes, at + 36) != 0;
        group.cylinder_radius = read_float(bytes, at + 40);
        group.cylinder_hollow = read_u32(bytes, at + 44) != 0;
        group.cylinder_height = read_float(bytes, at + 48);
        group.value = read_vector3(bytes, at + 52);
    }
    return {};
}

Status read_track(const std::span<const std::byte> bytes, const Chunk& head, const Chunk& body, const bool colour, AloParticleTrack& track, const std::string& where,
                  std::vector<std::string>& warnings)
{
    EAW_TRY_ASSIGN(const auto head_minis, read_minis(bytes, head, where));
    const MiniChunk* first = nullptr;
    const MiniChunk* last = nullptr;
    const MiniChunk* interpolation = nullptr;
    for (const auto& mini : head_minis)
    {
        switch (mini.type)
        {
        case 0x02:
            first = &mini;
            break;
        case 0x03:
            last = &mini;
            break;
        case 0x04:
            interpolation = &mini;
            break;
        default:
            warnings.push_back(where + ": unknown track mini-chunk " + hex(mini.type));
            break;
        }
    }
    const auto value_size = colour ? std::size_t{1} : std::size_t{4};
    if (first == nullptr || last == nullptr || interpolation == nullptr || first->size() < value_size || last->size() < value_size || interpolation->size() < 4)
    {
        return make_error("particle ALO " + where + ": track header lacks its first, last, or interpolation value");
    }
    const auto value_at = [&](const std::size_t offset)
    {
        return colour ? static_cast<float>(std::to_integer<unsigned char>(bytes[offset])) / 255.0F : read_float(bytes, offset);
    };
    track.raw_interpolation = read_u32(bytes, interpolation->payload);
    switch (track.raw_interpolation)
    {
    case 1:
        track.interpolation = AloParticleInterpolation::smooth;
        break;
    case 2:
        track.interpolation = AloParticleInterpolation::step;
        break;
    case 0:
        track.interpolation = AloParticleInterpolation::linear;
        break;
    default:
        warnings.push_back(where + ": interpolation " + std::to_string(track.raw_interpolation) + " is unknown; read as linear");
        track.interpolation = AloParticleInterpolation::linear;
        break;
    }
    track.keys.clear();
    track.keys.push_back({0.0F, value_at(first->payload)});

    EAW_TRY_ASSIGN(const auto key_minis, read_minis(bytes, body, where + " keys"));
    for (const auto& mini : key_minis)
    {
        if (mini.type != 0x05 || mini.size() < 8)
        {
            warnings.push_back(where + ": skipped key mini-chunk " + hex(mini.type) + " of " + std::to_string(mini.size()) + " bytes");
            continue;
        }
        // Colour keys hold a 0..255 integer, the others a float; the time follows.
        const auto value = colour ? static_cast<float>(read_u32(bytes, mini.payload)) / 255.0F : read_float(bytes, mini.payload);
        const auto time = read_float(bytes, mini.payload + 4);
        if (!(time >= track.keys.back().time && time <= 1.0F))
        {
            warnings.push_back(where + ": key at time " + std::to_string(time) + " is out of order; clamped");
        }
        track.keys.push_back({std::clamp(time, track.keys.back().time, 1.0F), value});
    }
    track.keys.push_back({1.0F, value_at(last->payload)});
    track.present = true;
    return {};
}

Status read_tracks(const std::span<const std::byte> bytes, const Chunk& chunk, AloParticleEmitter& emitter, const std::string& where, std::vector<std::string>& warnings)
{
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end, where + " tracks"));
    if (children.size() != emitter.tracks.size() * 2)
    {
        return make_error("particle ALO " + where + ": expected 7 tracks (14 chunks), found " + std::to_string(children.size()) + " chunks");
    }
    for (std::size_t index = 0; index < emitter.tracks.size(); ++index)
    {
        const auto& head = children[index * 2];
        const auto& body = children[index * 2 + 1];
        if (head.type != 0x00 || body.type != 0x01)
        {
            return make_error("particle ALO " + where + ": track " + std::to_string(index) + " is not a 0x0 header followed by 0x1 keys");
        }
        EAW_TRY(read_track(bytes, head, body, index < 4, emitter.tracks[index], where + " track " + std::to_string(index), warnings));
    }
    return {};
}

void set_default_tracks(AloParticleEmitter& emitter)
{
    for (std::size_t index = 0; index < emitter.tracks.size(); ++index)
    {
        auto value = 0.0F;
        if (index < 4)
        {
            value = 1.0F;
        }
        else if (index == static_cast<std::size_t>(AloParticleTrackIndex::scale))
        {
            value = 20.0F;
        }
        auto& track = emitter.tracks[index];
        track.interpolation = index == static_cast<std::size_t>(AloParticleTrackIndex::texture_index) ? AloParticleInterpolation::step : AloParticleInterpolation::linear;
        track.keys = {{0.0F, value}, {1.0F, value}};
    }
}

Result<AloParticleEmitter> read_emitter(const std::span<const std::byte> bytes, const Chunk& chunk, const std::size_t index, std::vector<std::string>& warnings)
{
    const auto where = "emitter " + std::to_string(index);
    AloParticleEmitter emitter;
    set_default_tracks(emitter);
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end, where));
    bool has_groups = false;
    bool has_tracks = false;
    for (const auto& child : children)
    {
        switch (child.type)
        {
        case 0x02:
            EAW_TRY(read_properties(bytes, child, emitter, where, warnings));
            break;
        case 0x03:
            emitter.texture = read_string(bytes, child);
            break;
        case 0x16:
            emitter.name = read_string(bytes, child);
            break;
        case 0x29:
            EAW_TRY(read_groups(bytes, child, emitter, where));
            has_groups = true;
            break;
        case 0x01:
            EAW_TRY(read_tracks(bytes, child, emitter, where, warnings));
            has_tracks = true;
            break;
        case 0x36:
        {
            EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, child, where + " links"));
            for (const auto& mini : minis)
            {
                if ((mini.type != 0x37 && mini.type != 0x39) || mini.size() < 4)
                {
                    warnings.push_back(where + ": unknown link mini-chunk " + hex(mini.type));
                    continue;
                }
                (mini.type == 0x37 ? emitter.death_child : emitter.life_child) = static_cast<std::int32_t>(read_u32(bytes, mini.payload));
            }
            break;
        }
        case 0x45:
            emitter.normal_texture = read_string(bytes, child);
            break;
        default:
            warnings.push_back(where + ": unknown chunk " + hex(child.type) + " (" + std::to_string(child.size()) + " bytes) ignored");
            break;
        }
    }
    if (!has_groups || !has_tracks)
    {
        warnings.push_back(where + ": missing " + std::string(has_groups ? "tracks" : "shape groups") + "; defaults used");
    }
    return emitter;
}

} // namespace

template <> Result<AloParticleSystemData> read_alo<AloParticleSystemData>(const std::span<const std::byte> bytes)
{
    EAW_TRY_ASSIGN(const auto generic, read_alo<AloGenericData>(bytes));
    if (generic.type == AloType::plugin_particle_system)
    {
        return make_error("particle ALO uses the plugin format (root chunk 0x1500), which is not supported");
    }
    if (generic.type != AloType::particle_system)
    {
        return make_error("ALO is not a particle system (root chunk is not 0x0900)");
    }
    EAW_TRY_ASSIGN(const auto roots, read_chunks(bytes, 0, bytes.size(), "file"));
    AloParticleSystemData system;
    if (roots.size() > 1)
    {
        system.warnings.push_back(std::to_string(roots.size() - 1) + " chunk(s) after the particle system ignored");
    }
    EAW_TRY_ASSIGN(const auto chunks, read_chunks(bytes, roots.front().payload, roots.front().end, "system"));
    bool has_emitters = false;
    for (const auto& chunk : chunks)
    {
        switch (chunk.type)
        {
        case 0x00:
            system.name = read_string(bytes, chunk);
            break;
        case 0x01:
            system.header_flags.assign(bytes.begin() + static_cast<std::ptrdiff_t>(chunk.payload), bytes.begin() + static_cast<std::ptrdiff_t>(chunk.end));
            break;
        case 0x02:
            if (chunk.size() != 1)
            {
                return make_error("particle ALO leave-particles chunk has " + std::to_string(chunk.size()) + " bytes, expected 1");
            }
            system.leave_particles = std::to_integer<unsigned char>(bytes[chunk.payload]) != 0;
            break;
        case 0x0800:
        {
            has_emitters = true;
            EAW_TRY_ASSIGN(const auto emitters, read_chunks(bytes, chunk.payload, chunk.end, "emitter list"));
            for (const auto& emitter : emitters)
            {
                if (emitter.type != 0x0700)
                {
                    system.warnings.push_back("emitter list: unknown chunk " + hex(emitter.type) + " ignored");
                    continue;
                }
                EAW_TRY_ASSIGN(auto decoded, read_emitter(bytes, emitter, system.emitters.size(), system.warnings));
                system.emitters.push_back(std::move(decoded));
            }
            break;
        }
        default:
            system.warnings.push_back("system: unknown chunk " + hex(chunk.type) + " ignored");
            break;
        }
    }
    if (!has_emitters)
    {
        return make_error("particle ALO has no emitter list (chunk 0x0800)");
    }
    const auto count = static_cast<std::int32_t>(system.emitters.size());
    for (std::size_t index = 0; index < system.emitters.size(); ++index)
    {
        auto& emitter = system.emitters[index];
        for (auto* link : {&emitter.death_child, &emitter.life_child})
        {
            if (*link != -1 && (*link < 0 || *link >= count || *link == static_cast<std::int32_t>(index)))
            {
                system.warnings.push_back("emitter " + std::to_string(index) + ": child link " + std::to_string(*link) + " is invalid; dropped");
                *link = -1;
            }
        }
    }
    return system;
}

} // namespace eaw
