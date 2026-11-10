#pragma once

// The interpretation layer for particle systems (docs/architecture.md, "Content contract versus
// legacy implementation"): a decoded AloParticleSystemData (the file's facts) becomes an engine-native
// description that the particle runtime and renderer consume without knowing the file format.
//
// Content contract (what the fields mean, as content was authored against it) is applied the same
// way under every profile; each rule cites its evidence in docs/compatibility-research.md
// ("Particle systems"). Look-defining quirks of the original, which content was tuned to but which
// are arguably wrong, are the only things a profile changes:
//   - additive blending ignores alpha (legacy) or scales by it (enhanced);
//   - draw order: per effect, emitters in list order (legacy) or every particle back to front
//     (enhanced), chosen per particle world.

#include "formats/alo_particle_system.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

enum class ParticleFidelity : std::uint8_t
{
    // As the original draws it; the default for content that does not know this engine.
    legacy,
    // The engine's improved behavior where authored content still works.
    enhanced,
};

enum class ParticleLinkMode : std::uint8_t
{
    // Particles stay where they were emitted.
    none,
    // Particles follow the emitter's position.
    translation,
    // Particles follow the emitter's position and rotation.
    rigid,
};

enum class ParticleGround : std::uint8_t
{
    none,
    disappear,
    bounce,
    stick,
};

enum class ParticleMeshEmission : std::uint8_t
{
    none,
    random_vertex,
    random_surface,
    every_vertex,
};

enum class ParticleDrawOrder : std::uint8_t
{
    // Effects back to front by their origin; within an effect, emitters in list order and particles
    // in emission order (the original).
    emitter_list,
    // Every particle back to front.
    back_to_front,
};

// What a profile decides.
struct ParticleInterpretation
{
    bool additive_uses_alpha{};
    ParticleLinkMode linked_particles{ParticleLinkMode::translation};
    ParticleDrawOrder draw_order{ParticleDrawOrder::emitter_list};
};

[[nodiscard]] ParticleInterpretation particle_interpretation(ParticleFidelity fidelity) noexcept;

// One emitter in engine terms: explicit units (seconds, world units, revolutions), signs, and
// intents, with every file-format detail already resolved.
struct ParticleEmitterDesc
{
    std::string name;
    std::string texture;

    // Emission: `per_spawn` particles every `interval` seconds from `initial_delay` on, at most
    // `spawn_limit` times (0: unlimited). Emitters that are only another emitter's child never emit
    // on their own.
    float initial_delay{};
    float interval{1.0F};
    std::uint32_t per_spawn{1};
    std::int64_t spawn_limit{};
    // Pre-simulated seconds at start, and the clock time at which the emitter freezes (0: never).
    float skip_time{};
    float freeze_time{};
    ParticleMeshEmission mesh_emission{ParticleMeshEmission::none};
    float mesh_offset{};

    // Spawn shapes in the emitter's frame (position and initial velocity).
    AloParticleGroup position_shape;
    AloParticleGroup velocity_shape;

    // Lifetime in seconds: between lifetime x min_fraction and lifetime. Size likewise scales by
    // min_size_fraction..1.
    float lifetime{1.0F};
    float lifetime_min_fraction{1.0F};
    float size_min_fraction{1.0F};

    // Motion: constant acceleration (in the emitter's frame when `acceleration_in_emitter_frame`),
    // plus `gravity` downward (-Z), plus speed and acceleration away from the emitter along the
    // spawn offset (negative: toward it).
    Vector3 acceleration{};
    bool acceleration_in_emitter_frame{};
    float gravity{};
    float outward_speed{};
    float outward_acceleration{};
    ParticleLinkMode link{ParticleLinkMode::none};
    // Fraction of a parent particle's velocity a child particle inherits.
    float parent_velocity{};
    ParticleGround ground{ParticleGround::none};
    float bounciness{};

    // Appearance over the particle's normalized age: red, green, blue, alpha, scale (the radius in
    // world units), texture cell, rotation speed (revolutions per second).
    std::array<AloParticleTrack, 7> tracks{};
    // Added colour: random up to these per channel (grayscale: one random value for all).
    std::array<float, 4> color_variance{};
    bool color_variance_grayscale{};
    // A fixed random angle (mean revolutions x (1 +- spread)) instead of spinning.
    bool fixed_rotation{};
    float rotation_mean{};
    float rotation_spread{};
    bool random_rotation_direction{};
    // Square texture atlas: cells per side.
    std::uint32_t atlas_side{1};
    VisualParticleShape shape{VisualParticleShape::billboard};
    // Kites: how far the trailing corner stretches (x the velocity's on-screen fraction), and
    // whether it is scaled by speed / 1000.
    float kite_tail{};
    bool kite_tail_by_speed{};
    VisualParticleBlend blend{VisualParticleBlend::additive};
    bool depth_test{true};
    // False for emitters the renderer has no pass for (heat distortion, weather): they never emit.
    bool drawn{true};

    // Child emitters (indices into the effect; -1 none): one burst at each particle's death, and a
    // trail emitter on each living particle.
    std::int32_t death_child{-1};
    std::int32_t life_child{-1};
};

// A particle effect ready to play: its emitters, whether particles live out when it stops, and per
// emitter the renderer texture of its colour texture (invalid_visual_texture draws white).
struct ParticleEffect
{
    std::string name;
    std::vector<ParticleEmitterDesc> emitters;
    bool leave_particles{true};
    std::vector<std::uint32_t> textures;
    ParticleFidelity fidelity{ParticleFidelity::legacy};
};

// Translates a decoded system under a profile. Texture indices are left for the caller.
[[nodiscard]] ParticleEmitterDesc normalize_particle_emitter(const AloParticleEmitter& emitter, ParticleFidelity fidelity);
[[nodiscard]] ParticleEffect normalize_particle_system(const AloParticleSystemData& system, ParticleFidelity fidelity);

[[nodiscard]] const char* to_string(ParticleFidelity fidelity) noexcept;

} // namespace eaw
