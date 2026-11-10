#pragma once

#include "core/status.hpp"
#include "math/vector.hpp"

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

// One environment mini-chunk exactly as stored (type byte and payload).
struct TedEnvironmentField
{
    std::uint8_t type{};
    std::vector<std::byte> payload;
};

// A directional light: RGB color, intensity, and direction angles in radians. The stock corpus
// stores three per environment (a key light followed by two fill lights).
struct TedLight
{
    std::array<float, 3> color{};
    float intensity{};
    float azimuth{};
    float elevation{};
};

struct TedEnvironment
{
    std::string name;
    std::string scenario;
    std::string skybox_primary;
    std::string skybox_secondary;
    std::string clouds;
    // Lighting fields (mini types 0x00-0x0d). `has_lighting` is false when any of them is absent
    // or malformed, in which case the decoded values must not be used.
    bool has_lighting{};
    std::array<TedLight, 3> lights{};
    std::array<float, 3> specular{};
    std::array<float, 3> ambient{};
    // Every mini-chunk in source order, including fields not interpreted yet (fog, wind, clouds).
    std::vector<TedEnvironmentField> fields;
};

// Liquid surface type from terrain-info mini 0x16. The stock corpus pairs 0 with dry worlds,
// 1 with water textures, 2 with lava (Mustafar, Sullust), and 3 with ice (Ilum, Polus).
enum class TedWaterType : std::uint8_t
{
    none,
    water,
    lava,
    ice,
    unknown,
};

struct TedWater
{
    TedWaterType type{TedWaterType::none};
    std::int32_t raw_type{};
    // Surface height in world units (mini 0x15).
    float level{};
    // RGB tint (mini 0x1a); white when absent.
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
};

// A track: a textured ribbon laid along a polyline over the terrain (rivers, shoreline waves,
// lights, beams). Chunk 0x102 holds the control points (mini 0x07, world X/Y/Z with Z usually 0),
// 0x104 a width per point (mini 0x08), 0x103 the texture, 0x101 header fields, and 0x105 three
// values that the corpus suggests are opacity at the start, middle, and end ((1,1,1) for 391
// stock tracks, (0,1,0) for 151).
struct TedTrack
{
    std::string texture;
    std::vector<Vector3> points;
    std::vector<float> widths;
    // Header minis 0x04 and 0x05: texture scroll per second along U and V (inferred from wave
    // tracks); zero when absent.
    std::array<float, 2> scroll{};
    std::array<float, 3> opacity{1.0F, 1.0F, 1.0F};
    std::vector<TedEnvironmentField> header_fields;
    std::vector<TedEnvironmentField> opacity_fields;
};

// A terrain texture layer. Only the textures and the tiling scale are interpreted; every
// mini-chunk is kept in `fields` (the corpus also stores two angles, two RGB colors, flags, and an
// integer per layer whose meanings are not established yet).
struct TedTerrainLayer
{
    std::string color_texture;
    std::string normal_texture;
    // World units covered by one texture repeat (mini 0x06; 64 in most stock layers). Zero when
    // absent or malformed.
    float tile_size{};
    std::vector<TedEnvironmentField> fields;
};

struct TedObjectType
{
    std::uint32_t crc{};
    std::uint32_t count{};
};

struct TedPlacedObject
{
    std::uint32_t instance_id{};
    std::uint32_t crc{};
    std::uint32_t player{};
    float scale{1.0F};
    Vector3 position;
    Vector3 rotation;
    bool has_transform{};
};

struct TedMapData
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
    // Raw 32-bit terrain samples, one per vertex, row-major. Bits 0-15 are the signed elevation
    // (elevation_map), bits 16-23 the vertex's terrain layer index (terrain_layer_map), and bits
    // 24-31 a small signed value whose meaning is not established yet.
    std::vector<std::uint32_t> terrain_samples;
    std::vector<std::uint8_t> terrain_layer_map;
    std::vector<std::uint32_t> passability_map;
    std::size_t decal_count{};
    std::vector<TedTerrainLayer> terrain_layers;
    // Terrain-info water textures: [0] bump map (mini 0x1d), [1] surface/fallback texture (0x1e),
    // [2] an optional third texture (0x1f).
    std::array<std::string, 3> water_maps;
    TedWater water;
    // Every terrain-info mini-chunk as stored (clouds, wind, and water fields not interpreted yet).
    std::vector<TedEnvironmentField> terrain_info_fields;
    std::vector<std::string> track_textures;
    std::vector<TedTrack> tracks;

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
};

[[nodiscard]] const char* to_string(TedMapType type) noexcept;

// The error names the malformed chunk.
[[nodiscard]] Result<TedMapData> read_ted(std::span<const std::byte> bytes);

} // namespace eaw
