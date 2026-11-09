#include "particle_effects.hpp"

#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eaw {
namespace {

constexpr float two_pi = 2.0F * std::numbers::pi_v<float>;
// Pre-simulation (skip time) runs in steps of this length, for at most this long.
constexpr float skip_step = 1.0F / 30.0F;
constexpr float maximum_skip = 10.0F;
constexpr float maximum_update = 0.25F;
// A trail emitter emits at most this many particles per parent per update (runaway rates).
constexpr std::size_t maximum_trail_burst = 64;

// PCG32 (O'Neill): small, fast, and fully specified, so a seed replays the same effect everywhere.
class Random
{
public:
    Random() : Random(0)
    {}

    explicit Random(const std::uint64_t seed)
    {
        state_ = 0;
        increment_ = (seed << 1U) | 1U;
        next();
        state_ += 0x853c49e6748fea9bULL ^ seed;
        next();
    }

    std::uint32_t next()
    {
        const auto old = state_;
        state_ = old * 6364136223846793005ULL + increment_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        const auto rotation = static_cast<std::uint32_t>(old >> 59U);
        return (xorshifted >> rotation) | (xorshifted << ((32U - rotation) & 31U));
    }

    // Uniform in [0, 1).
    float unit()
    {
        return static_cast<float>(next() >> 8U) * (1.0F / 16777216.0F);
    }

    float range(const float low, const float high)
    {
        return low + (high - low) * unit();
    }

private:
    std::uint64_t state_{};
    std::uint64_t increment_{};
};

// A spawn shape sample in emitter space (docs: shape group types 0..4).
Vector3 sample_group(const ParticleGroup& group, Random& random)
{
    switch (group.type)
    {
    case 1:
        return {random.range(group.minimum.x, group.maximum.x), random.range(group.minimum.y, group.maximum.y), random.range(group.minimum.z, group.maximum.z)};
    case 2:
    {
        const auto half = group.side_length * 0.5F;
        return {random.range(-half, half), random.range(-half, half), random.range(-half, half)};
    }
    case 3:
    {
        // Uniform direction; the radius is uniform in 0..R as in the reference (denser at the core).
        const auto z = random.range(-1.0F, 1.0F);
        const auto angle = random.range(0.0F, two_pi);
        const auto planar = std::sqrt(std::max(0.0F, 1.0F - z * z));
        const auto radius = group.sphere_hollow ? group.sphere_radius : random.range(0.0F, group.sphere_radius);
        return Vector3{planar * std::cos(angle), planar * std::sin(angle), z} * radius;
    }
    case 4:
    {
        const auto angle = random.range(0.0F, two_pi);
        const auto radius = group.cylinder_hollow ? group.cylinder_radius : random.range(0.0F, group.cylinder_radius);
        return {radius * std::cos(angle), radius * std::sin(angle), random.range(0.0F, group.cylinder_height)};
    }
    default:
        return group.value;
    }
}

} // namespace

float sample_particle_track(const ParticleTrack& track, const float t) noexcept
{
    const auto& keys = track.keys;
    if (keys.empty())
    {
        return 0.0F;
    }
    if (t <= keys.front().time)
    {
        return keys.front().value;
    }
    for (std::size_t index = 1; index < keys.size(); ++index)
    {
        const auto& next = keys[index];
        if (t > next.time)
        {
            continue;
        }
        const auto& previous = keys[index - 1];
        if (track.interpolation == ParticleInterpolation::step)
        {
            return t < next.time ? previous.value : next.value;
        }
        const auto span = next.time - previous.time;
        auto f = span > 1e-6F ? (t - previous.time) / span : 1.0F;
        if (track.interpolation == ParticleInterpolation::smooth)
        {
            f = f * f * (3.0F - 2.0F * f);
        }
        return previous.value + (next.value - previous.value) * f;
    }
    return keys.back().value;
}

struct ParticleWorld::Particle
{
    Vector3 position{};
    Vector3 velocity{};
    float age{};
    float lifetime{1.0F};
    float size_factor{1.0F};
    // Revolutions; integrated from the rotation-speed track unless fixed.
    float rotation{};
    float rotation_direction{1.0F};
    std::array<float, 4> color_add{};
    // Seconds until this particle's trail emitter emits again.
    float trail_clock{};
    bool resting{};
    // Seconds of the current update this particle lives through when it was emitted during it
    // (sub-frame emission); negative for the whole update.
    float step{-1.0F};
    // Unit direction of the spawn offset from the emitter (inward speed and acceleration act along it).
    Vector3 outward{};
};

struct ParticleWorld::EmitterState
{
    std::vector<Particle> particles;
    float clock{};
    float next_emission{};
    std::int64_t bursts{};
    // Next vertex of an every-vertex mesh emitter.
    std::size_t mesh_cursor{};
    bool finished{};
    // Spawned only by its parent emitter (a death or trail child), never on its own.
    bool child{};
    bool drawn{true};
};

struct ParticleWorld::Instance
{
    std::shared_ptr<const ParticleEffect> effect;
    std::shared_ptr<const ParticleEmitMesh> mesh;
    Matrix4 transform{Matrix4::identity()};
    Vector3 previous_origin{};
    // The transform at the previous update: linked particles move rigidly with the emitter.
    Matrix4 previous_transform{Matrix4::identity()};
    std::vector<EmitterState> emitters;
    Random random;
    std::uint32_t generation{};
    bool live{};
    float intensity{1.0F};
    float scale{1.0F};
    bool emitting{true};
    bool stopped{};
};

ParticleWorld::ParticleWorld(const std::size_t capacity) : capacity_(capacity)
{}
ParticleWorld::~ParticleWorld() = default;
ParticleWorld::ParticleWorld(ParticleWorld&&) noexcept = default;
ParticleWorld& ParticleWorld::operator=(ParticleWorld&&) noexcept = default;

ParticleWorld::Instance* ParticleWorld::find(const ParticleInstanceId id) noexcept
{
    if (id.index >= instances_.size() || !instances_[id.index].live || instances_[id.index].generation != id.generation)
    {
        return nullptr;
    }
    return &instances_[id.index];
}

const ParticleWorld::Instance* ParticleWorld::find(const ParticleInstanceId id) const noexcept
{
    return const_cast<ParticleWorld*>(this)->find(id);
}

ParticleInstanceId ParticleWorld::play(std::shared_ptr<const ParticleEffect> effect, const Matrix4& transform, const std::uint64_t seed, std::shared_ptr<const ParticleEmitMesh> mesh,
                                       const float scale)
{
    if (!effect || effect->emitters.empty())
    {
        return {};
    }
    std::uint32_t index = 0;
    if (!free_.empty())
    {
        index = free_.back();
        free_.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(instances_.size());
        instances_.emplace_back();
    }
    auto& instance = instances_[index];
    const auto generation = instance.generation + 1U;
    instance = Instance{};
    instance.generation = generation;
    instance.live = true;
    instance.effect = std::move(effect);
    if (mesh && !mesh->positions.empty() && mesh->normals.size() == mesh->positions.size())
    {
        instance.mesh = std::move(mesh);
    }
    // The scale goes into the transform (spawn shapes, speeds, accelerations in the effect's frame)
    // and into particle sizes.
    instance.scale = std::isfinite(scale) && scale > 0.0F ? scale : 1.0F;
    instance.transform = transform;
    for (std::size_t element = 0; element < 12; ++element)
    {
        instance.transform.values[element] *= instance.scale;
    }
    instance.previous_origin = translation(instance.transform);
    instance.previous_transform = instance.transform;
    instance.random = Random(seed);
    const auto& emitters = instance.effect->emitters;
    instance.emitters.resize(emitters.size());
    for (std::size_t e = 0; e < emitters.size(); ++e)
    {
        auto& state = instance.emitters[e];
        state.next_emission = std::max(0.0F, emitters[e].initial_delay);
        state.drawn = emitters[e].drawn;
        if (!state.drawn)
        {
            // Never emits, so it does not keep a one-shot effect alive.
            state.finished = true;
            ++totals_.skipped_emitters;
        }
        for (const auto link : {emitters[e].death_child, emitters[e].life_child})
        {
            if (link >= 0 && static_cast<std::size_t>(link) < emitters.size())
            {
                instance.emitters[static_cast<std::size_t>(link)].child = true;
            }
        }
    }
    // Pre-simulate the longest skip time so looping effects start in full flow.
    auto skip = 0.0F;
    for (const auto& emitter : emitters)
    {
        skip = std::max(skip, emitter.skip_time);
    }
    for (auto elapsed = 0.0F; elapsed < std::min(skip, maximum_skip); elapsed += skip_step)
    {
        step_instance(instance, skip_step);
    }
    return {index, generation};
}

void ParticleWorld::set_transform(const ParticleInstanceId id, const Matrix4& transform)
{
    if (auto* instance = find(id))
    {
        instance->transform = transform;
        for (std::size_t element = 0; element < 12; ++element)
        {
            instance->transform.values[element] *= instance->scale;
        }
    }
}

void ParticleWorld::set_emitting(const ParticleInstanceId id, const bool emitting)
{
    if (auto* instance = find(id))
    {
        instance->emitting = emitting;
    }
}

void ParticleWorld::set_intensity(const ParticleInstanceId id, const float intensity)
{
    if (auto* instance = find(id))
    {
        instance->intensity = std::clamp(intensity, 0.0F, 1.0F);
    }
}

void ParticleWorld::stop(const ParticleInstanceId id)
{
    auto* instance = find(id);
    if (instance == nullptr)
    {
        return;
    }
    instance->stopped = true;
    if (!instance->effect->leave_particles)
    {
        release(id.index);
    }
}

void ParticleWorld::remove(const ParticleInstanceId id)
{
    if (find(id) != nullptr)
    {
        release(id.index);
    }
}

void ParticleWorld::clear()
{
    for (std::uint32_t index = 0; index < instances_.size(); ++index)
    {
        if (instances_[index].live)
        {
            release(index);
        }
    }
}

bool ParticleWorld::playing(const ParticleInstanceId id) const noexcept
{
    return find(id) != nullptr;
}

void ParticleWorld::set_ground(ParticleGroundHeight ground)
{
    ground_ = std::move(ground);
}

void ParticleWorld::release(const std::uint32_t index)
{
    auto& instance = instances_[index];
    for (const auto& state : instance.emitters)
    {
        live_particles_ -= state.particles.size();
    }
    instance.emitters.clear();
    instance.effect.reset();
    instance.live = false;
    free_.push_back(index);
}

bool ParticleWorld::emit(Instance& instance, const std::size_t emitter_index, const Particle* parent, const Vector3& offset, const float step)
{
    if (live_particles_ >= capacity_)
    {
        ++totals_.dropped_particles;
        return false;
    }
    const auto& emitter = instance.effect->emitters[emitter_index];
    auto& random = instance.random;
    Particle particle;
    const Matrix4 transform{instance.transform};
    auto local_position = sample_group(emitter.position_shape, random);
    auto local_velocity = sample_group(emitter.velocity_shape, random);
    if (emitter.mesh_emission != ParticleMeshEmission::none && instance.mesh)
    {
        // On the emitter mesh: every vertex in turn (3), a random vertex (1), or a random point on a
        // random triangle (2), pushed out along the normal; the velocity's +Z turns to the normal.
        const auto& mesh = *instance.mesh;
        const auto vertices = mesh.positions.size();
        Vector3 point{};
        Vector3 normal{0.0F, 0.0F, 1.0F};
        if (emitter.mesh_emission == ParticleMeshEmission::random_surface && mesh.indices.size() >= 3)
        {
            const auto triangle = std::min<std::size_t>(static_cast<std::size_t>(random.unit() * static_cast<float>(mesh.indices.size() / 3U)), mesh.indices.size() / 3U - 1U) * 3U;
            auto w1 = random.unit();
            auto w2 = random.unit();
            if (w1 + w2 > 1.0F)
            {
                w1 = 1.0F - w1;
                w2 = 1.0F - w2;
            }
            const std::array<float, 3> weights{1.0F - w1 - w2, w1, w2};
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto vertex = std::min<std::size_t>(mesh.indices[triangle + corner], vertices - 1U);
                point = point + mesh.positions[vertex] * weights[corner];
                normal = corner == 0 ? mesh.normals[vertex] * weights[corner] : normal + mesh.normals[vertex] * weights[corner];
            }
        }
        else
        {
            auto& cursor = instance.emitters[emitter_index].mesh_cursor;
            const auto vertex = emitter.mesh_emission == ParticleMeshEmission::every_vertex
                                    ? cursor++ % vertices
                                    : std::min<std::size_t>(static_cast<std::size_t>(random.unit() * static_cast<float>(vertices)), vertices - 1U);
            point = mesh.positions[vertex];
            normal = mesh.normals[vertex];
        }
        normal = normalize_or(normal, {0.0F, 0.0F, 1.0F});
        local_position = point + normal * emitter.mesh_offset;
        // A basis whose Z is the normal.
        const Vector3 helper = std::abs(normal.z) < 0.9F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{1.0F, 0.0F, 0.0F};
        const auto side = normalize(cross(helper, normal));
        const auto up = cross(normal, side);
        local_velocity = side * local_velocity.x + up * local_velocity.y + normal * local_velocity.z;
    }
    particle.position = transform_point(local_position, transform) + offset;
    particle.step = step;
    particle.velocity = transform_direction(local_velocity, transform);
    // "Inward" speed and acceleration act along the spawn offset from the emitter, fixed at spawn. The
    // stored values move particles outward when positive (the GlyphX particle editor shows them
    // negated, as inward amounts).
    particle.outward = normalize(transform_direction(local_position, transform));
    particle.velocity = particle.velocity + particle.outward * emitter.outward_speed;
    if (parent != nullptr)
    {
        particle.position = particle.position - translation(transform) + parent->position;
        if (emitter.parent_velocity != 0.0F)
        {
            particle.velocity = particle.velocity + parent->velocity * emitter.parent_velocity;
        }
    }
    particle.lifetime = std::max(0.01F, emitter.lifetime * random.range(emitter.lifetime_min_fraction, 1.0F));
    particle.size_factor = random.range(emitter.size_min_fraction, 1.0F);
    if (emitter.fixed_rotation)
    {
        particle.rotation = emitter.rotation_mean * (1.0F + random.range(-emitter.rotation_spread, emitter.rotation_spread));
    }
    if (emitter.random_rotation_direction && random.unit() < 0.5F)
    {
        particle.rotation_direction = -1.0F;
        particle.rotation = -particle.rotation;
    }
    const auto& add = emitter.color_variance;
    particle.color_add[0] = random.range(0.0F, std::max(0.0F, add[0]));
    for (std::size_t channel = 1; channel < 4; ++channel)
    {
        particle.color_add[channel] = emitter.color_variance_grayscale ? particle.color_add[0] : random.range(0.0F, std::max(0.0F, add[channel]));
    }
    instance.emitters[emitter_index].particles.push_back(particle);
    ++live_particles_;
    return true;
}

void ParticleWorld::kill(Instance& instance, const std::size_t emitter_index, const std::size_t particle_index)
{
    auto& particles = instance.emitters[emitter_index].particles;
    const auto parent = particles[particle_index];
    particles[particle_index] = particles.back();
    particles.pop_back();
    --live_particles_;
    const auto& emitter = instance.effect->emitters[emitter_index];
    if (emitter.death_child >= 0 && instance.emitters.size() > static_cast<std::size_t>(emitter.death_child))
    {
        // Engine choice: a death child emits one burst (or one particle when it is continuous).
        const auto child = static_cast<std::size_t>(emitter.death_child);
        const auto& child_emitter = instance.effect->emitters[child];
        const auto count = std::min<std::size_t>(child_emitter.per_spawn, maximum_trail_burst);
        for (std::size_t n = 0; n < count && emit(instance, child, &parent); ++n)
        {
        }
    }
}

void ParticleWorld::step_instance(Instance& instance, const float update_seconds)
{
    const auto& emitters = instance.effect->emitters;
    const auto current_origin = translation(instance.transform);
    const auto moved = current_origin - instance.previous_origin;
    instance.previous_origin = current_origin;
    // Rigid motion of the emitter since the last update (translation and rotation).
    // A singular previous transform carries nothing over (identity), as before.
    const auto carried = inverse_affine(instance.previous_transform).value_or(Matrix4::identity()) * instance.transform;
    instance.previous_transform = instance.transform;
    const bool may_emit = instance.emitting && !instance.stopped;
    for (std::size_t e = 0; e < emitters.size(); ++e)
    {
        const auto& emitter = emitters[e];
        auto& state = instance.emitters[e];
        // Freeze time stops the emitter's clock (no emission, particles hold still) once reached,
        // when it is not inside the skipped time (the particle editor's rule).
        auto seconds = update_seconds;
        if (emitter.freeze_time > 0.0F)
        {
            seconds = std::clamp(emitter.freeze_time - state.clock, 0.0F, update_seconds);
        }
        // Emission on the emitter's own clock.
        state.clock += seconds;
        if (!state.child && !state.finished && may_emit && state.drawn)
        {
            const auto interval = emitter.interval;
            // Each emission happens at its own time within the update, where the emitter was then
            // (its translation interpolated), so a moving emitter leaves an even trail.
            const auto update_start = state.clock - seconds;
            while (state.clock >= state.next_emission && !state.finished)
            {
                const auto fraction = seconds > 0.0F ? std::clamp((state.next_emission - update_start) / seconds, 0.0F, 1.0F) : 1.0F;
                // Linked particles spawn where the emitter is now (they move with it from then on).
                const auto behind = emitter.link != ParticleLinkMode::none ? Vector3{} : moved * -(1.0F - fraction);
                const auto remaining = seconds * (1.0F - fraction);
                // Every-vertex mesh emitters emit once per vertex per spawn.
                const auto per_vertex =
                    emitter.mesh_emission == ParticleMeshEmission::every_vertex && instance.mesh ? static_cast<std::uint32_t>(std::min<std::size_t>(instance.mesh->positions.size(), 4096U)) : 1U;
                const auto count = emitter.per_spawn * per_vertex;
                for (std::uint32_t n = 0; n < count; ++n)
                {
                    if (!emit(instance, e, nullptr, behind, remaining))
                    {
                        break;
                    }
                }
                state.next_emission += interval;
                if (emitter.spawn_limit > 0 && ++state.bursts >= emitter.spawn_limit)
                {
                    state.finished = true;
                }
            }
        }
        if (!may_emit)
        {
            // Keep the schedule from piling up while hidden.
            state.next_emission = std::max(state.next_emission, state.clock);
        }

        // Motion.
        Vector3 acceleration{0.0F, 0.0F, -emitter.gravity};
        acceleration = acceleration + (emitter.acceleration_in_emitter_frame ? transform_direction(emitter.acceleration, instance.transform) : emitter.acceleration);
        const auto& rotation_track = emitter.tracks[static_cast<std::size_t>(ParticleTrackIndex::rotation_speed)];
        for (std::size_t p = 0; p < state.particles.size();)
        {
            auto& particle = state.particles[p];
            const bool fresh = particle.step >= 0.0F;
            const auto dt = fresh ? particle.step : seconds;
            particle.step = -1.0F;
            particle.age += dt;
            if (particle.age >= particle.lifetime)
            {
                kill(instance, e, p);
                continue;
            }
            // Particles emitted in this update already sit at the current transform.
            if (!fresh && emitter.link == ParticleLinkMode::rigid)
            {
                particle.position = transform_point(particle.position, carried);
                particle.velocity = transform_direction(particle.velocity, carried);
            }
            else if (!fresh && emitter.link == ParticleLinkMode::translation)
            {
                particle.position = particle.position + moved;
            }
            if (!particle.resting)
            {
                const auto pull = acceleration + particle.outward * emitter.outward_acceleration;
                particle.velocity = particle.velocity + pull * dt;
                particle.position = particle.position + particle.velocity * dt;
            }
            if (!emitter.fixed_rotation)
            {
                particle.rotation += dt * particle.rotation_direction * sample_particle_track(rotation_track, particle.age / particle.lifetime);
            }
            if (emitter.ground != ParticleGround::none && ground_)
            {
                if (const auto ground = ground_(particle.position[0], particle.position[1]); ground && particle.position[2] < *ground)
                {
                    if (emitter.ground == ParticleGround::disappear)
                    {
                        kill(instance, e, p);
                        continue;
                    }
                    particle.position[2] = *ground;
                    if (emitter.ground == ParticleGround::bounce && std::abs(particle.velocity[2]) * emitter.bounciness > 1.0F)
                    {
                        particle.velocity[2] = -particle.velocity[2] * emitter.bounciness;
                    }
                    else
                    {
                        particle.velocity = {};
                        particle.resting = true;
                    }
                }
            }
            // A trail child emits from every living particle at its own rate.
            if (emitter.life_child >= 0 && may_emit && static_cast<std::size_t>(emitter.life_child) < emitters.size())
            {
                const auto child = static_cast<std::size_t>(emitter.life_child);
                const auto& child_emitter = emitters[child];
                const auto interval = child_emitter.interval;
                particle.trail_clock -= dt;
                std::size_t emitted = 0;
                while (particle.trail_clock <= 0.0F && emitted < maximum_trail_burst)
                {
                    const auto parent = particle;
                    const auto count = child_emitter.per_spawn;
                    for (std::uint32_t n = 0; n < count && emitted < maximum_trail_burst; ++n, ++emitted)
                    {
                        if (!emit(instance, child, &parent))
                        {
                            emitted = maximum_trail_burst;
                            break;
                        }
                    }
                    state.particles[p].trail_clock += interval;
                }
                state.particles[p].trail_clock = std::max(state.particles[p].trail_clock, -interval);
            }
            ++p;
        }
    }
}

void ParticleWorld::update(const float seconds)
{
    const auto step = std::clamp(seconds, 0.0F, maximum_update);
    for (std::uint32_t index = 0; index < instances_.size(); ++index)
    {
        auto& instance = instances_[index];
        if (!instance.live)
        {
            continue;
        }
        step_instance(instance, step);
        // One-shot effects end once nothing can emit and nothing is alive.
        // Hidden looping effects stay alive until shown again or stopped.
        bool active = false;
        for (const auto& state : instance.emitters)
        {
            active = active || !state.particles.empty() || (!state.child && !state.finished && !instance.stopped);
        }
        if (!active)
        {
            release(index);
        }
    }
}

void ParticleWorld::build_sprites(const Vector3& eye, std::vector<VisualParticle>& sprites) const
{
    const auto first = sprites.size();
    std::vector<float> depth;
    for (const auto& instance : instances_)
    {
        if (!instance.live)
        {
            continue;
        }
        const auto& emitters = instance.effect->emitters;
        const auto effect_offset = translation(instance.transform) - eye;
        const auto effect_depth = dot(effect_offset, effect_offset);
        for (std::size_t e = 0; e < emitters.size(); ++e)
        {
            const auto& state = instance.emitters[e];
            if (!state.drawn || state.particles.empty())
            {
                continue;
            }
            const auto& emitter = emitters[e];
            // Atlas cells indexed by the texture track rounded down, wrapping past the last.
            const auto grid = emitter.atlas_side;
            const auto cell = 1.0F / static_cast<float>(grid);
            const auto texture = e < instance.effect->textures.size() ? instance.effect->textures[e] : invalid_visual_texture;
            const auto blend = emitter.blend;
            const auto shape = emitter.shape;
            const auto size_scale = (0.5F + 0.5F * instance.intensity) * instance.scale;
            const auto alpha_scale = 0.15F + 0.85F * instance.intensity;
            for (const auto& particle : state.particles)
            {
                const auto t = std::clamp(particle.age / particle.lifetime, 0.0F, 1.0F);
                VisualParticle sprite;
                sprite.position = particle.position;
                // The scale track is the full width, as both references draw it (docs/compatibility-research.md).
                sprite.half_size = 0.5F * sample_particle_track(emitter.tracks[static_cast<std::size_t>(ParticleTrackIndex::scale)], t) * particle.size_factor * size_scale;
                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    sprite.color[channel] = std::clamp(sample_particle_track(emitter.tracks[channel], t) + particle.color_add[channel], 0.0F, 1.0F);
                }
                sprite.color[3] *= alpha_scale;
                if (blend == VisualParticleBlend::additive)
                {
                    // This additive ignores alpha: dim the colour instead.
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        sprite.color[channel] *= alpha_scale;
                    }
                }
                const auto index = static_cast<std::uint32_t>(std::max(0.0F, std::floor(sample_particle_track(emitter.tracks[static_cast<std::size_t>(ParticleTrackIndex::texture_index)], t))));
                sprite.uv_rect = {static_cast<float>(index % grid) * cell, static_cast<float>((index / grid) % grid) * cell, cell, cell};
                sprite.rotation = particle.rotation * two_pi;
                sprite.texture = texture;
                sprite.shape = shape;
                sprite.blend = blend;
                sprite.depth_test = emitter.depth_test;
                if (shape == VisualParticleShape::kite)
                {
                    sprite.axis = particle.velocity;
                    sprite.tail = emitter.kite_tail * (emitter.kite_tail_by_speed ? length(particle.velocity) / 1000.0F : 1.0F);
                }
                sprites.push_back(sprite);
                // Back to front per particle, or per effect keeping its emitters in list order and
                // its particles in emission order (the original's draw order).
                const auto offset = particle.position - eye;
                depth.push_back(draw_order_ == ParticleDrawOrder::back_to_front ? dot(offset, offset) : effect_depth);
            }
        }
    }
    // Back to front; the stable sort keeps equal depths in list and emission order, so frames do not
    // flicker.
    std::vector<std::uint32_t> order(depth.size());
    for (std::uint32_t index = 0; index < order.size(); ++index)
    {
        order[index] = index;
    }
    std::ranges::stable_sort(order,
                             [&](const std::uint32_t a, const std::uint32_t b)
                             {
                                 return depth[a] > depth[b];
                             });
    std::vector<VisualParticle> sorted;
    sorted.reserve(order.size());
    for (const auto index : order)
    {
        sorted.push_back(sprites[first + index]);
    }
    std::ranges::copy(sorted, sprites.begin() + static_cast<std::ptrdiff_t>(first));
}

ParticleWorldStats ParticleWorld::stats() const noexcept
{
    auto stats = totals_;
    stats.particles = live_particles_;
    stats.instances = instances_.size() - free_.size();
    return stats;
}

} // namespace eaw
