#include "eaw/chunk_file.hpp"
#include "eaw/ted_map.hpp"

#include "eaw/text_encoding.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace eaw {
namespace {

// The shared chunk container (eaw/chunk_file.hpp), with this format named in diagnostics.
using Chunk = ChunkRange;
using MiniChunk = MiniChunkRange;
using chunk::find_chunk;
using chunk::find_mini;
using chunk::read_float;
using chunk::read_i32;
using chunk::read_string;
using chunk::read_u16;
using chunk::read_u32;

Result<std::vector<Chunk>> read_chunks(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    return chunk::read_chunks(bytes, begin, end, "TED");
}

Result<std::vector<MiniChunk>> read_minis(const std::span<const std::byte> bytes, const Chunk& parent)
{
    return chunk::read_minis(bytes, parent, "TED");
}

Result<std::string> read_wide_string(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    if (begin > end || end > bytes.size() || ((end - begin) % 2U) != 0)
    {
        return make_error("TED wide-string range is invalid");
    }
    std::u16string value;
    value.reserve((end - begin) / 2U);
    for (auto offset = begin; offset + 1 < end; offset += 2U)
    {
        const auto character = static_cast<char16_t>(read_u16(bytes, offset));
        if (character == u'\0')
        {
            break;
        }
        value.push_back(character);
    }
    return utf16_to_utf8(value);
}

Status parse_properties(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, chunk));
    for (const auto& mini : minis)
    {
        switch (mini.type)
        {
        case 1:
            if (mini.end - mini.payload >= 4)
            {
                const auto value = read_u32(bytes, mini.payload);
                map.type = value == 1 ? TedMapType::land : value == 2 ? TedMapType::space : TedMapType::unknown;
            }
            break;
        case 2:
            if (mini.end - mini.payload >= 4)
            {
                map.players = read_u32(bytes, mini.payload);
            }
            break;
        case 8:
        {
            EAW_TRY_ASSIGN(map.name, read_wide_string(bytes, mini.payload, mini.end));
            break;
        }
        case 9:
        {
            EAW_TRY_ASSIGN(map.planet, read_wide_string(bytes, mini.payload, mini.end));
            break;
        }
        case 16:
            if (mini.end - mini.payload >= 4)
            {
                map.world_height = read_float(bytes, mini.payload);
            }
            break;
        case 17:
            if (mini.end - mini.payload >= 4)
            {
                map.world_width = read_float(bytes, mini.payload);
            }
            break;
        default:
            break;
        }
    }
    return Status::success();
}

Status parse_environments(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& child : children)
    {
        if (child.type == 4 && child.container)
        {
            EAW_TRY_ASSIGN(const auto environment_chunks, read_chunks(bytes, child.payload, child.end));
            for (const auto& environment_chunk : environment_chunks)
            {
                if (environment_chunk.type != 6)
                {
                    continue;
                }
                TedEnvironment environment;
                // Lighting fields observed in all 624 stock environments: colors 0x00 (key),
                // 0x01-0x02 (fills), 0x03 (specular), 0x04 (ambient); intensities 0x05-0x07;
                // azimuths 0x08-0x0a and elevations 0x0b-0x0d in radians.
                std::array<bool, 0x0e> seen{};
                const auto read_rgb = [&](const MiniChunk& mini, std::array<float, 3>& target)
                {
                    if (mini.end - mini.payload != 12)
                    {
                        return false;
                    }
                    target = {read_float(bytes, mini.payload), read_float(bytes, mini.payload + 4), read_float(bytes, mini.payload + 8)};
                    return true;
                };
                const auto read_scalar = [&](const MiniChunk& mini, float& target)
                {
                    if (mini.end - mini.payload != 4)
                    {
                        return false;
                    }
                    target = read_float(bytes, mini.payload);
                    return true;
                };
                EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, environment_chunk));
                for (const auto& mini : minis)
                {
                    TedEnvironmentField field;
                    field.type = static_cast<std::uint8_t>(mini.type);
                    field.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(mini.payload), bytes.begin() + static_cast<std::ptrdiff_t>(mini.end));
                    environment.fields.push_back(std::move(field));
                    if (mini.type < seen.size())
                    {
                        bool valid = false;
                        if (mini.type <= 0x02)
                        {
                            valid = read_rgb(mini, environment.lights[mini.type].color);
                        }
                        else if (mini.type == 0x03)
                        {
                            valid = read_rgb(mini, environment.specular);
                        }
                        else if (mini.type == 0x04)
                        {
                            valid = read_rgb(mini, environment.ambient);
                        }
                        else if (mini.type <= 0x07)
                        {
                            valid = read_scalar(mini, environment.lights[mini.type - 0x05].intensity);
                        }
                        else if (mini.type <= 0x0a)
                        {
                            valid = read_scalar(mini, environment.lights[mini.type - 0x08].azimuth);
                        }
                        else
                        {
                            valid = read_scalar(mini, environment.lights[mini.type - 0x0b].elevation);
                        }
                        seen[mini.type] = valid;
                    }
                    switch (mini.type)
                    {
                    case 0x14:
                        environment.name = read_string(bytes, mini.payload, mini.end);
                        break;
                    case 0x29:
                        environment.scenario = read_string(bytes, mini.payload, mini.end);
                        break;
                    case 0x19:
                        environment.skybox_primary = read_string(bytes, mini.payload, mini.end);
                        break;
                    case 0x1a:
                        environment.skybox_secondary = read_string(bytes, mini.payload, mini.end);
                        break;
                    case 0x2f:
                        environment.clouds = read_string(bytes, mini.payload, mini.end);
                        break;
                    default:
                        break;
                    }
                }
                environment.has_lighting = std::ranges::all_of(seen,
                                                               [](const bool value)
                                                               {
                                                                   return value;
                                                               });
                if (!environment.has_lighting)
                {
                    map.warnings.push_back("environment '" + environment.name + "' has incomplete or malformed lighting fields");
                }
                map.environments.push_back(std::move(environment));
            }
        }
        else if (child.type == 8)
        {
            // The active-environment index is a mini-chunk (stock maps use type 0x25, size 4).
            // Reading the payload as a raw u32 returned the mini header (0x0425 = 1061) instead.
            EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, child));
            for (const auto& mini : minis)
            {
                if (mini.end - mini.payload >= 4)
                {
                    map.active_environment = read_u32(bytes, mini.payload);
                    break;
                }
            }
        }
    }
    return Status::success();
}

Status parse_terrain(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    if (const auto* information = find_chunk(children, 0))
    {
        std::uint32_t declared_layers = 0;
        EAW_TRY_ASSIGN(const auto information_minis, read_minis(bytes, *information));
        for (const auto& mini : information_minis)
        {
            TedEnvironmentField field;
            field.type = static_cast<std::uint8_t>(mini.type);
            field.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(mini.payload), bytes.begin() + static_cast<std::ptrdiff_t>(mini.end));
            map.terrain_info_fields.push_back(std::move(field));
            if (mini.type == 0x15 && mini.end - mini.payload == 4)
            {
                const auto level = read_float(bytes, mini.payload);
                map.water.level = std::isfinite(level) ? level : 0.0F;
            }
            else if (mini.type == 0x16 && mini.end - mini.payload == 4)
            {
                map.water.raw_type = read_i32(bytes, mini.payload);
                map.water.type = map.water.raw_type >= 0 && map.water.raw_type <= 3 ? static_cast<TedWaterType>(map.water.raw_type) : TedWaterType::unknown;
            }
            else if (mini.type == 0x1a && mini.end - mini.payload == 12)
            {
                std::array<float, 3> color{read_float(bytes, mini.payload), read_float(bytes, mini.payload + 4), read_float(bytes, mini.payload + 8)};
                if (std::ranges::all_of(color,
                                        [](const float value)
                                        {
                                            return std::isfinite(value);
                                        }))
                {
                    map.water.color = color;
                }
            }
            if ((mini.type == 0 || mini.type == 1 || mini.type == 5) && mini.end - mini.payload >= 4)
            {
                const auto value = read_u32(bytes, mini.payload);
                if (mini.type == 0)
                {
                    map.terrain_width = value;
                }
                else if (mini.type == 1)
                {
                    map.terrain_height = value;
                }
                else
                {
                    declared_layers = value;
                }
            }
            else if (mini.type >= 29 && mini.type <= 31)
            {
                map.water_maps[mini.type - 29] = read_string(bytes, mini.payload, mini.end);
            }
        }
        if (declared_layers != 0)
        {
            map.terrain_layers.reserve(declared_layers);
        }
    }

    if (const auto* elevation = find_chunk(children, 5))
    {
        const auto available = (elevation->end - elevation->payload) / 4U;
        const auto expected = static_cast<std::size_t>(map.terrain_width) * map.terrain_height;
        const auto count = std::min(available, expected);
        map.elevation_map.reserve(count);
        map.terrain_samples.reserve(count);
        map.terrain_layer_map.reserve(count);
        auto minimum = std::numeric_limits<std::int32_t>::max();
        auto maximum = std::numeric_limits<std::int32_t>::min();
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto sample = read_u32(bytes, elevation->payload + index * 4U);
            map.terrain_samples.push_back(sample);
            map.terrain_layer_map.push_back(static_cast<std::uint8_t>((sample >> 16U) & 0xffU));
            const auto value = static_cast<std::int16_t>(static_cast<std::uint16_t>(sample & 0xffffU));
            map.elevation_map.push_back(value);
            minimum = std::min(minimum, static_cast<std::int32_t>(value));
            maximum = std::max(maximum, static_cast<std::int32_t>(value));
        }
        map.elevation_sample_count = map.elevation_map.size();
        if (count != 0)
        {
            map.minimum_elevation = minimum;
            map.maximum_elevation = maximum;
        }
        if (available != expected)
        {
            map.warnings.emplace_back("Terrain elevation sample count is inconsistent");
        }
    }

    if (const auto* textures = find_chunk(children, 2); textures != nullptr && textures->container)
    {
        EAW_TRY_ASSIGN(const auto layer_chunks, read_chunks(bytes, textures->payload, textures->end));
        for (const auto& layer_chunk : layer_chunks)
        {
            if (layer_chunk.type != 3)
            {
                continue;
            }
            TedTerrainLayer layer;
            EAW_TRY_ASSIGN(const auto layer_minis, read_minis(bytes, layer_chunk));
            for (const auto& mini : layer_minis)
            {
                TedEnvironmentField field;
                field.type = static_cast<std::uint8_t>(mini.type);
                field.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(mini.payload), bytes.begin() + static_cast<std::ptrdiff_t>(mini.end));
                layer.fields.push_back(std::move(field));
                if (mini.type == 6 && mini.end - mini.payload == 4)
                {
                    const auto tile_size = read_float(bytes, mini.payload);
                    layer.tile_size = std::isfinite(tile_size) && tile_size > 0.0F ? tile_size : 0.0F;
                }
                else if (mini.type == 12)
                {
                    layer.color_texture = read_string(bytes, mini.payload, mini.end);
                }
                else if (mini.type == 19)
                {
                    layer.normal_texture = read_string(bytes, mini.payload, mini.end);
                }
            }
            map.terrain_layers.push_back(std::move(layer));
        }
    }
    if (const auto* decals = find_chunk(children, 6); decals != nullptr && decals->container)
    {
        EAW_TRY_ASSIGN(const auto decal_chunks, read_chunks(bytes, decals->payload, decals->end));
        for (const auto& decal : decal_chunks)
        {
            map.decal_count += decal.type == 0x10 ? 1U : 0U;
        }
    }
    if (const auto* tracks = find_chunk(children, 9); tracks != nullptr && tracks->container)
    {
        EAW_TRY_ASSIGN(const auto track_list, read_chunks(bytes, tracks->payload, tracks->end));
        for (const auto& track : track_list)
        {
            if (track.type != 0x100 || !track.container)
            {
                continue;
            }
            EAW_TRY_ASSIGN(const auto track_chunks, read_chunks(bytes, track.payload, track.end));
            TedTrack decoded;
            if (const auto* texture = find_chunk(track_chunks, 0x103))
            {
                decoded.texture = read_string(bytes, texture->payload, texture->end);
                map.track_textures.push_back(decoded.texture);
            }
            const auto raw_fields = [&](const std::vector<MiniChunk>& minis)
            {
                std::vector<TedEnvironmentField> fields;
                for (const auto& mini : minis)
                {
                    TedEnvironmentField field;
                    field.type = static_cast<std::uint8_t>(mini.type);
                    field.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(mini.payload), bytes.begin() + static_cast<std::ptrdiff_t>(mini.end));
                    fields.push_back(std::move(field));
                }
                return fields;
            };
            const auto finite_float = [&](const MiniChunk& mini, float& target)
            {
                if (mini.end - mini.payload != 4)
                {
                    return false;
                }
                const auto value = read_float(bytes, mini.payload);
                if (!std::isfinite(value))
                {
                    return false;
                }
                target = value;
                return true;
            };
            if (const auto* header = find_chunk(track_chunks, 0x101))
            {
                EAW_TRY_ASSIGN(const auto header_minis, read_minis(bytes, *header));
                decoded.header_fields = raw_fields(header_minis);
                for (const auto& mini : header_minis)
                {
                    if (mini.type == 0x04 || mini.type == 0x05)
                    {
                        static_cast<void>(finite_float(mini, decoded.scroll[mini.type - 0x04]));
                    }
                }
            }
            if (const auto* points = find_chunk(track_chunks, 0x102))
            {
                EAW_TRY_ASSIGN(const auto point_minis, read_minis(bytes, *points));
                for (const auto& mini : point_minis)
                {
                    if (mini.type == 0x07 && mini.end - mini.payload == 12)
                    {
                        const std::array<float, 3> point{read_float(bytes, mini.payload), read_float(bytes, mini.payload + 4), read_float(bytes, mini.payload + 8)};
                        if (std::ranges::all_of(point,
                                                [](const float value)
                                                {
                                                    return std::isfinite(value);
                                                }))
                        {
                            decoded.points.push_back(point);
                        }
                    }
                }
            }
            if (const auto* widths = find_chunk(track_chunks, 0x104))
            {
                EAW_TRY_ASSIGN(const auto width_minis, read_minis(bytes, *widths));
                for (const auto& mini : width_minis)
                {
                    float width{};
                    if (mini.type == 0x08 && finite_float(mini, width))
                    {
                        decoded.widths.push_back(width);
                    }
                }
            }
            if (const auto* opacity = find_chunk(track_chunks, 0x105))
            {
                EAW_TRY_ASSIGN(const auto opacity_minis, read_minis(bytes, *opacity));
                decoded.opacity_fields = raw_fields(opacity_minis);
                std::size_t key = 0;
                for (const auto& mini : opacity_minis)
                {
                    if (mini.type == 0x09 && key < decoded.opacity.size())
                    {
                        static_cast<void>(finite_float(mini, decoded.opacity[key]));
                        ++key;
                    }
                }
            }
            if (decoded.widths.size() != decoded.points.size())
            {
                map.warnings.push_back("track '" + decoded.texture + "' has " + std::to_string(decoded.points.size()) + " points but " + std::to_string(decoded.widths.size()) + " widths");
            }
            map.tracks.push_back(std::move(decoded));
        }
    }
    if (map.terrain_width > 0 && map.terrain_height > 0)
    {
        map.world_width = static_cast<float>(map.terrain_width - 1U) / 31.0F;
        map.world_height = static_cast<float>(map.terrain_height - 1U) / 31.0F;
    }
    return Status::success();
}

// True when the identity chunk was found (and decoded) below `chunk`.
Result<bool> find_object_identity(const std::span<const std::byte> bytes, const Chunk& chunk, TedPlacedObject& object, const unsigned depth = 0)
{
    if (!chunk.container || depth > 8)
    {
        return false;
    }
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& child : children)
    {
        if (child.type == 0x4b0)
        {
            EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, child));
            for (const auto& mini : minis)
            {
                const auto size = mini.end - mini.payload;
                if (mini.type == 0 && size >= 4)
                {
                    object.instance_id = read_u32(bytes, mini.payload);
                }
                else if (mini.type == 1 && size >= 4)
                {
                    object.crc = read_u32(bytes, mini.payload);
                }
                else if (mini.type == 2 && size >= 4)
                {
                    object.player = read_u32(bytes, mini.payload);
                }
                else if (mini.type == 3 && size >= 4)
                {
                    object.scale = read_float(bytes, mini.payload);
                }
                else if ((mini.type == 4 || mini.type == 5) && size >= 12)
                {
                    TedVector3 value{read_float(bytes, mini.payload), read_float(bytes, mini.payload + 4), read_float(bytes, mini.payload + 8)};
                    if (mini.type == 4)
                    {
                        object.position = value;
                        object.has_transform = std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
                    }
                    else
                    {
                        object.rotation = value;
                    }
                }
            }
            return true;
        }
        else if (child.container)
        {
            EAW_TRY_ASSIGN(const auto found, find_object_identity(bytes, child, object, depth + 1));
            if (found)
            {
                return true;
            }
        }
    }
    return false;
}

Status find_object_chunks(const std::span<const std::byte> bytes, const Chunk& chunk, std::map<std::uint32_t, std::uint32_t>& counts, std::vector<TedPlacedObject>& objects, const unsigned depth = 0)
{
    if (!chunk.container || depth > 12)
    {
        return Status::success();
    }
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& child : children)
    {
        if (child.type == 0x44c && child.container)
        {
            TedPlacedObject object;
            EAW_TRY(find_object_identity(bytes, child, object));
            ++counts[object.crc];
            objects.push_back(object);
        }
        else if (child.container)
        {
            EAW_TRY(find_object_chunks(bytes, child, counts, objects, depth + 1));
        }
    }
    return Status::success();
}

Status parse_zones(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    if (!chunk.container)
    {
        return Status::success();
    }
    EAW_TRY_ASSIGN(const auto zones, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& zone : zones)
    {
        if (zone.type == 0)
        {
            ++map.zone_count;
        }
    }
    map.has_ai_data = map.zone_count != 0;
    return Status::success();
}

} // namespace

const char* to_string(const TedMapType type) noexcept
{
    switch (type)
    {
    case TedMapType::land:
        return "land";
    case TedMapType::space:
        return "space";
    case TedMapType::unknown:
        break;
    }
    return "unknown";
}

Result<TedMap> TedMap::parse(const std::span<const std::byte> bytes)
{
    TedMap map;
    EAW_TRY_ASSIGN(const auto chunks, read_chunks(bytes, 0, bytes.size()));
    const auto* properties = find_chunk(chunks, 0);
    const auto* root = find_chunk(chunks, 1);
    if (properties == nullptr || root == nullptr || !root->container)
    {
        return make_error("TED is missing its properties or root chunk");
    }
    EAW_TRY(parse_properties(bytes, *properties, map));

    if (const auto* preview = find_chunk(chunks, 0x13))
    {
        map.has_preview = true;
        map.preview_bytes = preview->end - preview->payload;
        if (map.preview_bytes >= 18)
        {
            map.preview_width = read_u16(bytes, preview->payload + 12);
            map.preview_height = read_u16(bytes, preview->payload + 14);
        }
    }

    EAW_TRY_ASSIGN(const auto root_chunks, read_chunks(bytes, root->payload, root->end));
    if (const auto* environments = find_chunk(root_chunks, 0x100))
    {
        EAW_TRY(parse_environments(bytes, *environments, map));
    }
    if (const auto* terrain = find_chunk(root_chunks, 0x101))
    {
        EAW_TRY(parse_terrain(bytes, *terrain, map));
    }
    if (const auto* passability = find_chunk(root_chunks, 0x10a))
    {
        const auto available = (passability->end - passability->payload) / 4U;
        const auto expected = static_cast<std::size_t>(map.terrain_width) * map.terrain_height;
        const auto count = expected == 0 ? available : std::min(available, expected);
        map.passability_map.resize(count);
        if (expected != 0 && count == expected)
        {
            // TED terrain elevation is row-major, while the passability raster is
            // stored in X-major order. Normalize both to [y * width + x].
            for (std::uint32_t y = 0; y < map.terrain_height; ++y)
            {
                for (std::uint32_t x = 0; x < map.terrain_width; ++x)
                {
                    const auto source = static_cast<std::size_t>(x) * map.terrain_height + y;
                    const auto destination = static_cast<std::size_t>(y) * map.terrain_width + x;
                    map.passability_map[destination] = read_u32(bytes, passability->payload + source * 4U);
                }
            }
        }
        else
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                map.passability_map[index] = read_u32(bytes, passability->payload + index * 4U);
            }
        }
        map.passability_sample_count = map.passability_map.size();
        if (expected != 0 && available != expected)
        {
            map.warnings.emplace_back("Terrain passability sample count is inconsistent");
        }
    }
    if (const auto* objects = find_chunk(root_chunks, 0x102))
    {
        std::map<std::uint32_t, std::uint32_t> counts;
        EAW_TRY(find_object_chunks(bytes, *objects, counts, map.placed_objects));
        for (const auto [crc, count] : counts)
        {
            map.object_types.push_back({crc, count});
            map.object_count += count;
        }
    }
    if (const auto* zones = find_chunk(root_chunks, 0x10b))
    {
        EAW_TRY(parse_zones(bytes, *zones, map));
    }
    if (map.type == TedMapType::land && map.terrain_width == 0)
    {
        map.warnings.emplace_back("Land map does not contain semantic terrain data");
    }
    return map;
}

} // namespace eaw
