#pragma once

// Alamo particle systems: ALO files whose first chunk is 0x0900 (the EaW/FoC format). A system is a
// list of emitters; each emitter carries its properties (spawning, lifetime, motion, rendering),
// three shape groups (speed, lifetime, position), seven keyframe tracks over a particle's normalized
// age (red, green, blue, alpha, scale, texture cell, rotation speed), and optional child emitters
// spawned at a particle's death or during its life. Decoding is lossless: every property mini-chunk
// is also kept raw in file order, and unknown chunks or properties become diagnostics, not
// failures. The plugin format some later Alamo games use (first chunk 0x1500) is recognized and
// reported as unsupported. Field meanings follow the MIT-licensed alo-viewer (GlyphX Tools, 2017);
// see docs/compatibility-research.md, "Particle systems".

#include "formats/alo_model.hpp"
#include "core/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eaw {

enum class ParticleFileKind : std::uint8_t
{
    // Not a particle system (a mesh ALO starts with its skeleton, 0x0200).
    other,
    // 0x0900: the EaW/FoC format this decoder reads.
    classic,
    // 0x1500: the plugin format of later Alamo games; not supported.
    plugin,
};

// Which format a file's first chunk announces.
[[nodiscard]] ParticleFileKind particle_file_kind(std::span<const std::byte> bytes) noexcept;

// A spawn shape (64 bytes in the file). `type`: 0 exact (value), 1 box (minimum..maximum), 2 cube
// (+-side_length/2), 3 sphere (radius up to sphere_radius), 4 cylinder (cylinder_radius, height
// 0..cylinder_height); "hollow" spawns on the surface only.
struct ParticleGroup
{
    std::uint32_t type{};
    Vector3 minimum;
    Vector3 maximum;
    float side_length{};
    float sphere_radius{};
    bool sphere_hollow{};
    float cylinder_radius{};
    bool cylinder_hollow{};
    float cylinder_height{};
    Vector3 value;
};

enum class ParticleInterpolation : std::uint8_t
{
    linear,
    smooth,
    step,
};

struct ParticleKey
{
    // Normalized particle age, 0..1.
    float time{};
    float value{};
};

// A keyframe track: the first key at time 0, intermediate keys, and the last key at time 1. Colour
// tracks hold 0..1 (stored as 0..255).
struct ParticleTrack
{
    ParticleInterpolation interpolation{ParticleInterpolation::linear};
    std::uint32_t raw_interpolation{};
    std::vector<ParticleKey> keys;
    // The track was in the file (absent tracks keep their defaults).
    bool present{};
};

enum class ParticleTrackIndex : std::uint8_t
{
    red,
    green,
    blue,
    alpha,
    scale,
    texture_index,
    rotation_speed,
};

// A property mini-chunk as stored.
struct ParticleRawProperty
{
    std::uint8_t id{};
    std::vector<std::byte> payload;
};

struct ParticleEmitter
{
    std::string name;
    std::string texture{"p_particle_master.tga"};
    // The optional normal/depth texture (chunk 0x45; empty when absent).
    std::string normal_texture;

    // Properties (mini-chunk id in brackets), with the format's defaults.
    std::uint32_t blend_mode{1};            // [0x04] index into the legacy particle shader table
    std::uint32_t triangle_count{2};        // [0x05] stored value + 1
    bool use_bursts{};                      // [0x07]
    bool link_to_system{};                  // [0x08] particles follow the emitter's movement
    float inward_speed{};                   // [0x09] as stored
    Vector3 acceleration;                   // [0x0A]
    float inward_acceleration{};            // [0x0B] as stored
    float gravity{};                        // [0x0C] along -Z
    float lifetime{1.0F};                   // [0x0F] seconds
    std::uint32_t texture_cells{64};        // [0x10] atlas cell count (grid side ceil(sqrt))
    float random_scale{};                   // [0x12]
    float random_lifetime{};                // [0x13]
    float random_rotation_variance{};       // [0x17]
    bool random_rotation_direction{};       // [0x23]
    float initial_delay{};                  // [0x24] seconds
    float burst_delay{1.0F};                // [0x25] seconds
    std::uint32_t particles_per_burst{1};   // [0x26]
    std::int32_t burst_count{};             // [0x27] 0 or -1: unlimited
    float parent_link_strength{};           // [0x28]
    std::uint32_t particles_per_second{1};  // [0x2A]
    std::array<float, 4> random_colors{};   // [0x2C] RGBA
    bool random_color_grayscale{};          // [0x2D]
    bool world_oriented{};                  // [0x2E] lies flat on the ground plane
    std::uint32_t ground_behavior{};        // [0x2F] 0 none, 1 disappear, 2 bounce, 3 stick
    float bounciness{0.2F};                 // [0x30]
    bool affected_by_wind{};                // [0x31]
    float freeze_time{};                    // [0x32]
    float skip_time{};                      // [0x33] pre-simulation, seconds
    std::uint32_t emit_from_mesh{};         // [0x34] 0 off, 1 random vertex, 2 random surface, 3 every vertex
    bool object_space_acceleration{};       // [0x35]
    bool heat{};                            // [0x3B] distortion particle
    float emit_from_mesh_offset{0.5F};      // [0x3C]
    bool weather{};                         // [0x3D]
    float weather_cube_size{500.0F};        // [0x3E]
    float weather_fadeout_distance{100.0F}; // [0x40]
    bool has_tail{};                        // [0x41] "kite" quads stretched along the velocity
    float tail_size{50.0F};                 // [0x42]
    bool use_link_strength{};               // [0x43]
    bool no_depth_test{};                   // [0x46]
    float weather_cube_distance{};          // [0x47]
    bool random_rotation{};                 // [0x48]

    // Shape groups: speed (initial velocity), lifetime (unused by the known runtime), position.
    std::array<ParticleGroup, 3> groups{};
    std::array<ParticleTrack, 7> tracks{};
    // Child emitters (indices into the system's list; -1 for none) spawned at a particle's death
    // and during its life (one trail emitter instance per living particle).
    std::int32_t death_child{-1};
    std::int32_t life_child{-1};
    // Every property mini-chunk in file order.
    std::vector<ParticleRawProperty> properties;
};

struct ParticleSystem
{
    std::string name;
    // Chunk 0x0001 of the system (4 bytes; meaning unknown), as stored.
    std::vector<std::byte> header_flags;
    // Particles live out when the system is detached (default when the chunk is absent).
    bool leave_particles{true};
    std::vector<ParticleEmitter> emitters;
    // Unknown chunks and properties, out-of-range links, and similar oddities.
    std::vector<std::string> warnings;

    // Errors name the malformed chunk; a file of another kind is an error naming its kind.
    [[nodiscard]] static Result<ParticleSystem> parse(std::span<const std::byte> bytes);
};

} // namespace eaw
