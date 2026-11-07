#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eaw {

enum class TedMapType : std::uint32_t
{
    unknown = 0,
    land = 1,
    space = 2,
};

struct TedEnvironment
{
    std::string name;
    std::string scenario;
    std::string skybox_primary;
    std::string skybox_secondary;
    std::string clouds;
};

struct TedTerrainLayer
{
    std::string color_texture;
    std::string normal_texture;
};

struct TedObjectType
{
    std::uint32_t crc{};
    std::uint32_t count{};
};

struct TedVector3
{
    float x{};
    float y{};
    float z{};
};

struct TedPlacedObject
{
    std::uint32_t instance_id{};
    std::uint32_t crc{};
    std::uint32_t player{};
    float scale{1.0F};
    TedVector3 position;
    TedVector3 rotation;
    bool has_transform{};
};

struct TedMap
{
    TedMapType type{TedMapType::unknown};
    std::uint32_t players{};
    std::string name;
    std::string planet;
    float world_width{};
    float world_height{};

    std::uint32_t terrain_width{};
    std::uint32_t terrain_height{};
    std::int32_t minimum_elevation{};
    std::int32_t maximum_elevation{};
    std::size_t elevation_sample_count{};
    std::size_t passability_sample_count{};
    std::vector<std::int16_t> elevation_map;
    std::vector<std::uint32_t> passability_map;
    std::size_t decal_count{};
    std::vector<TedTerrainLayer> terrain_layers;
    std::array<std::string, 3> water_maps;
    std::vector<std::string> track_textures;

    std::vector<TedEnvironment> environments;
    std::uint32_t active_environment{};
    std::vector<TedObjectType> object_types;
    std::vector<TedPlacedObject> placed_objects;
    std::size_t object_count{};
    std::size_t zone_count{};
    bool has_ai_data{};

    bool has_preview{};
    std::size_t preview_bytes{};
    std::uint32_t preview_width{};
    std::uint32_t preview_height{};
    std::vector<std::string> warnings;

    [[nodiscard]] static TedMap parse(std::span<const std::byte> bytes);
};

[[nodiscard]] const char* to_string(TedMapType type) noexcept;

} // namespace eaw
