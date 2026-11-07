#include "eaw/ted_map.hpp"

#include "eaw/text_encoding.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace eaw {
namespace {

struct Chunk
{
    std::uint32_t type{};
    bool container{};
    std::size_t payload{};
    std::size_t end{};
};

struct MiniChunk
{
    std::uint8_t type{};
    std::size_t payload{};
    std::size_t end{};
};

std::uint32_t read_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size())
    {
        throw std::runtime_error("TED ended while reading a 32-bit value");
    }
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

std::uint16_t read_u16(const std::span<const std::byte> bytes, const std::size_t offset)
{
    if (offset + 2 > bytes.size())
    {
        throw std::runtime_error("TED ended while reading a 16-bit value");
    }
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset]) | (std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8U));
}

std::int32_t read_i32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::bit_cast<std::int32_t>(read_u32(bytes, offset));
}

float read_float(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::bit_cast<float>(read_u32(bytes, offset));
}

std::string read_string(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    if (begin > end || end > bytes.size())
    {
        throw std::runtime_error("TED string range is invalid");
    }
    auto finish = begin;
    while (finish < end && bytes[finish] != std::byte{})
    {
        ++finish;
    }
    return {reinterpret_cast<const char*>(bytes.data() + begin), finish - begin};
}

std::string read_wide_string(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    if (begin > end || end > bytes.size() || ((end - begin) % 2U) != 0)
    {
        throw std::runtime_error("TED wide-string range is invalid");
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

std::vector<Chunk> read_chunks(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    if (begin > end || end > bytes.size())
    {
        throw std::runtime_error("TED chunk range is invalid");
    }
    std::vector<Chunk> result;
    auto cursor = begin;
    while (cursor < end)
    {
        if (cursor + 8 > end)
        {
            throw std::runtime_error("TED contains a truncated chunk header");
        }
        const auto type = read_u32(bytes, cursor);
        const auto encoded_size = read_u32(bytes, cursor + 4);
        const auto size = static_cast<std::size_t>(encoded_size & 0x7fffffffU);
        const auto payload = cursor + 8;
        if (payload + size > end)
        {
            throw std::runtime_error("TED chunk extends past its parent");
        }
        result.push_back({type, (encoded_size & 0x80000000U) != 0, payload, payload + size});
        cursor = payload + size;
    }
    return result;
}

std::vector<MiniChunk> read_minis(const std::span<const std::byte> bytes, const Chunk& chunk)
{
    std::vector<MiniChunk> result;
    auto cursor = chunk.payload;
    while (cursor < chunk.end)
    {
        if (cursor + 2 > chunk.end)
        {
            throw std::runtime_error("TED contains a truncated mini-chunk header");
        }
        const auto type = std::to_integer<std::uint8_t>(bytes[cursor]);
        const auto size = std::to_integer<std::uint8_t>(bytes[cursor + 1]);
        const auto payload = cursor + 2;
        if (payload + size > chunk.end)
        {
            throw std::runtime_error("TED mini-chunk extends past its parent");
        }
        result.push_back({type, payload, payload + size});
        cursor = payload + size;
    }
    return result;
}

const Chunk* find_chunk(const std::vector<Chunk>& chunks, const std::uint32_t type)
{
    const auto found = std::ranges::find(chunks, type, &Chunk::type);
    return found == chunks.end() ? nullptr : &*found;
}

void parse_properties(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    for (const auto& mini : read_minis(bytes, chunk))
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
            map.name = read_wide_string(bytes, mini.payload, mini.end);
            break;
        case 9:
            map.planet = read_wide_string(bytes, mini.payload, mini.end);
            break;
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
}

void parse_environments(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    const auto children = read_chunks(bytes, chunk.payload, chunk.end);
    for (const auto& child : children)
    {
        if (child.type == 4 && child.container)
        {
            for (const auto& environment_chunk : read_chunks(bytes, child.payload, child.end))
            {
                if (environment_chunk.type != 6)
                {
                    continue;
                }
                TedEnvironment environment;
                for (const auto& mini : read_minis(bytes, environment_chunk))
                {
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
                map.environments.push_back(std::move(environment));
            }
        }
        else if (child.type == 8)
        {
            // The active-environment index is a mini-chunk (stock maps use type 0x25, size 4).
            // Reading the payload as a raw u32 returned the mini header (0x0425 = 1061) instead.
            for (const auto& mini : read_minis(bytes, child))
            {
                if (mini.end - mini.payload >= 4)
                {
                    map.active_environment = read_u32(bytes, mini.payload);
                    break;
                }
            }
        }
    }
}

void parse_terrain(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    const auto children = read_chunks(bytes, chunk.payload, chunk.end);
    if (const auto* information = find_chunk(children, 0))
    {
        std::uint32_t declared_layers = 0;
        for (const auto& mini : read_minis(bytes, *information))
        {
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
        auto minimum = std::numeric_limits<std::int32_t>::max();
        auto maximum = std::numeric_limits<std::int32_t>::min();
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto value = static_cast<std::int16_t>(read_i32(bytes, elevation->payload + index * 4U));
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
        for (const auto& layer_chunk : read_chunks(bytes, textures->payload, textures->end))
        {
            if (layer_chunk.type != 3)
            {
                continue;
            }
            TedTerrainLayer layer;
            for (const auto& mini : read_minis(bytes, layer_chunk))
            {
                if (mini.type == 12)
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
        for (const auto& decal : read_chunks(bytes, decals->payload, decals->end))
        {
            map.decal_count += decal.type == 0x10 ? 1U : 0U;
        }
    }
    if (const auto* tracks = find_chunk(children, 9); tracks != nullptr && tracks->container)
    {
        for (const auto& track : read_chunks(bytes, tracks->payload, tracks->end))
        {
            if (track.type != 0x100 || !track.container)
            {
                continue;
            }
            const auto track_chunks = read_chunks(bytes, track.payload, track.end);
            if (const auto* texture = find_chunk(track_chunks, 0x103))
            {
                map.track_textures.push_back(read_string(bytes, texture->payload, texture->end));
            }
        }
    }
    if (map.terrain_width > 0 && map.terrain_height > 0)
    {
        map.world_width = static_cast<float>(map.terrain_width - 1U) / 31.0F;
        map.world_height = static_cast<float>(map.terrain_height - 1U) / 31.0F;
    }
}

bool find_object_identity(const std::span<const std::byte> bytes, const Chunk& chunk, TedPlacedObject& object, const unsigned depth = 0)
{
    if (!chunk.container || depth > 8)
    {
        return false;
    }
    for (const auto& child : read_chunks(bytes, chunk.payload, chunk.end))
    {
        if (child.type == 0x4b0)
        {
            for (const auto& mini : read_minis(bytes, child))
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
            if (find_object_identity(bytes, child, object, depth + 1))
            {
                return true;
            }
        }
    }
    return false;
}

void find_object_chunks(const std::span<const std::byte> bytes, const Chunk& chunk, std::map<std::uint32_t, std::uint32_t>& counts, std::vector<TedPlacedObject>& objects, const unsigned depth = 0)
{
    if (!chunk.container || depth > 12)
    {
        return;
    }
    for (const auto& child : read_chunks(bytes, chunk.payload, chunk.end))
    {
        if (child.type == 0x44c && child.container)
        {
            TedPlacedObject object;
            find_object_identity(bytes, child, object);
            ++counts[object.crc];
            objects.push_back(object);
        }
        else if (child.container)
        {
            find_object_chunks(bytes, child, counts, objects, depth + 1);
        }
    }
}

void parse_zones(const std::span<const std::byte> bytes, const Chunk& chunk, TedMap& map)
{
    if (!chunk.container)
    {
        return;
    }
    for (const auto& zone : read_chunks(bytes, chunk.payload, chunk.end))
    {
        if (zone.type == 0)
        {
            ++map.zone_count;
        }
    }
    map.has_ai_data = map.zone_count != 0;
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

TedMap TedMap::parse(const std::span<const std::byte> bytes)
{
    TedMap map;
    const auto chunks = read_chunks(bytes, 0, bytes.size());
    const auto* properties = find_chunk(chunks, 0);
    const auto* root = find_chunk(chunks, 1);
    if (properties == nullptr || root == nullptr || !root->container)
    {
        throw std::runtime_error("TED is missing its properties or root chunk");
    }
    parse_properties(bytes, *properties, map);

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

    const auto root_chunks = read_chunks(bytes, root->payload, root->end);
    if (const auto* environments = find_chunk(root_chunks, 0x100))
    {
        parse_environments(bytes, *environments, map);
    }
    if (const auto* terrain = find_chunk(root_chunks, 0x101))
    {
        parse_terrain(bytes, *terrain, map);
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
        find_object_chunks(bytes, *objects, counts, map.placed_objects);
        for (const auto [crc, count] : counts)
        {
            map.object_types.push_back({crc, count});
            map.object_count += count;
        }
    }
    if (const auto* zones = find_chunk(root_chunks, 0x10b))
    {
        parse_zones(bytes, *zones, map);
    }
    if (map.type == TedMapType::land && map.terrain_width == 0)
    {
        map.warnings.emplace_back("Land map does not contain semantic terrain data");
    }
    return map;
}

} // namespace eaw
