#include "assets/particle_rules.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {
namespace {

// The legacy particle shader table (docs/compatibility-research.md); stock data uses 1, 2, and 11.
VisualParticleBlend blend_intent(const std::uint32_t mode, const bool additive_uses_alpha)
{
    switch (mode)
    {
    case 1:
    case 4:
        return additive_uses_alpha ? VisualParticleBlend::additive_alpha : VisualParticleBlend::additive;
    case 3:
    case 6:
    case 8:
    case 9:
        return VisualParticleBlend::modulate;
    default:
        return VisualParticleBlend::alpha;
    }
}

ParticleMeshEmission mesh_emission(const std::uint32_t mode)
{
    switch (mode)
    {
    case 1:
        return ParticleMeshEmission::random_vertex;
    case 2:
        return ParticleMeshEmission::random_surface;
    case 3:
        return ParticleMeshEmission::every_vertex;
    default:
        return ParticleMeshEmission::none;
    }
}

ParticleGround ground_behavior(const std::uint32_t mode)
{
    switch (mode)
    {
    case 1:
        return ParticleGround::disappear;
    case 2:
        return ParticleGround::bounce;
    case 3:
        return ParticleGround::stick;
    default:
        return ParticleGround::none;
    }
}

} // namespace

ParticleInterpretation particle_interpretation(const ParticleFidelity fidelity) noexcept
{
    // Linked particles follow their emitter's position and rotation under every profile: the
    // editors' "position only" reflects their previews, whose systems never rotate, and following
    // position only visibly detaches engine glows from turning ships.
    if (fidelity == ParticleFidelity::enhanced)
    {
        return {true, ParticleLinkMode::rigid, ParticleDrawOrder::back_to_front};
    }
    // The original (the GlyphX editor's blend setup and the fork's guide): additive ignores alpha,
    // emitters draw in list order.
    return {false, ParticleLinkMode::rigid, ParticleDrawOrder::emitter_list};
}

ParticleEmitterDesc normalize_particle_emitter(const AloParticleEmitter& emitter, const ParticleFidelity fidelity)
{
    const auto rules = particle_interpretation(fidelity);
    ParticleEmitterDesc desc;
    desc.name = emitter.name;
    desc.texture = emitter.texture;

    // Emission (the particle editor's schedule; a burst delay below 0.01 s is raised to it).
    desc.initial_delay = std::max(0.0F, emitter.initial_delay);
    desc.interval = emitter.use_bursts ? std::max(emitter.burst_delay, 0.01F) : 1.0F / static_cast<float>(std::max<std::uint32_t>(emitter.particles_per_second, 1U));
    desc.per_spawn = emitter.use_bursts ? emitter.particles_per_burst : 1U;
    desc.spawn_limit = emitter.use_bursts && emitter.burst_count > 0 ? emitter.burst_count : 0;
    desc.skip_time = std::max(0.0F, emitter.skip_time);
    // Freeze time counts only when it is not inside the skipped time.
    desc.freeze_time = emitter.freeze_time > 0.0F && emitter.freeze_time >= emitter.skip_time ? emitter.freeze_time : 0.0F;
    desc.mesh_emission = mesh_emission(emitter.emit_from_mesh);
    desc.mesh_offset = emitter.emit_from_mesh_offset;

    desc.velocity_shape = emitter.groups[0];
    desc.position_shape = emitter.groups[2];

    // Variances shorten and shrink only (the editor writes the lifetime group as exactly this range).
    desc.lifetime = std::max(0.01F, emitter.lifetime);
    desc.lifetime_min_fraction = 1.0F - std::clamp(emitter.random_lifetime, 0.0F, 1.0F);
    desc.size_min_fraction = 1.0F - std::clamp(emitter.random_scale, 0.0F, 1.0F);

    desc.acceleration = {emitter.acceleration.x, emitter.acceleration.y, emitter.acceleration.z};
    desc.acceleration_in_emitter_frame = emitter.object_space_acceleration;
    desc.gravity = emitter.gravity;
    // The stored "inward" values move particles away from the emitter when positive (the editor
    // shows them negated, as inward amounts).
    desc.outward_speed = emitter.inward_speed;
    desc.outward_acceleration = emitter.inward_acceleration;
    desc.link = emitter.link_to_system ? rules.linked_particles : ParticleLinkMode::none;
    // The decoder already zeroes the strength when its flag (0x43) is not set.
    desc.parent_velocity = emitter.parent_link_strength;
    desc.ground = ground_behavior(emitter.ground_behavior);
    desc.bounciness = emitter.bounciness;

    desc.tracks = emitter.tracks;
    desc.color_variance = emitter.random_colors;
    for (auto& channel : desc.color_variance)
    {
        channel = std::max(0.0F, channel);
    }
    desc.color_variance_grayscale = emitter.random_color_grayscale;
    desc.fixed_rotation = emitter.random_rotation;
    if (desc.fixed_rotation)
    {
        // The rotation track's first value (revolutions), varied by up to the variance (0..1).
        const auto& keys = emitter.tracks[static_cast<std::size_t>(AloParticleTrackIndex::rotation_speed)].keys;
        desc.rotation_mean = keys.empty() ? 0.0F : keys.front().value;
        desc.rotation_spread = std::clamp(emitter.random_rotation_variance, 0.0F, 1.0F);
    }
    desc.random_rotation_direction = emitter.random_rotation_direction;
    desc.atlas_side = std::max(1U, static_cast<std::uint32_t>(std::floor(std::sqrt(static_cast<float>(std::max(emitter.texture_cells, 1U))))));
    desc.shape = emitter.has_tail ? VisualParticleShape::kite : (emitter.world_oriented ? VisualParticleShape::ground : VisualParticleShape::billboard);
    desc.kite_tail = emitter.tail_size;
    desc.kite_tail_by_speed = emitter.parent_link_strength != 0.0F;
    desc.blend = blend_intent(emitter.blend_mode, rules.additive_uses_alpha);
    desc.depth_test = !emitter.no_depth_test;
    // No heat-distortion or weather pass exists yet.
    desc.drawn = !emitter.heat && !emitter.weather && emitter.blend_mode != 10;
    desc.death_child = emitter.death_child;
    desc.life_child = emitter.life_child;
    return desc;
}

ParticleEffect load_particle_effect(const AloParticleSystemData& system, const ParticleFidelity fidelity)
{
    ParticleEffect effect;
    effect.name = system.name;
    effect.leave_particles = system.leave_particles;
    effect.fidelity = fidelity;
    effect.emitters.reserve(system.emitters.size());
    for (const auto& emitter : system.emitters)
    {
        effect.emitters.push_back(normalize_particle_emitter(emitter, fidelity));
    }
    return effect;
}

const char* to_string(const ParticleFidelity fidelity) noexcept
{
    return fidelity == ParticleFidelity::enhanced ? "enhanced" : "legacy";
}

} // namespace eaw
