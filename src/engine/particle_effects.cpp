#include "particle_effects.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eaw {
namespace {

using Vec3 = std::array<float, 3>;

constexpr float two_pi = 2.0F * std::numbers::pi_v<float>;
// Pre-simulation (skip time) runs in steps of this length, for at most this long.
constexpr float skip_step = 1.0F / 30.0F;
constexpr float maximum_skip = 10.0F;
constexpr float maximum_update = 0.25F;
// A trail emitter emits at most this many particles per parent per update (runaway rates).
constexpr std::size_t maximum_trail_burst = 64;

Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 operator*(const Vec3& a, const float s)
{
    return {a[0] * s, a[1] * s, a[2] * s};
}

float length(const Vec3& a)
{
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

Vec3 normalized(const Vec3& a)
{
    const auto l = length(a);
    return l > 1e-6F ? a * (1.0F / l) : Vec3{};
}

Vec3 to_vec(const AloVector3& v)
{
    return {v.x, v.y, v.z};
}

Vec3 transform_point(const std::array<float, 16>& m, const Vec3& p)
{
    return {p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12], p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13], p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]};
}

Vec3 transform_direction(const std::array<float, 16>& m, const Vec3& d)
{
    return {d[0] * m[0] + d[1] * m[4] + d[2] * m[8], d[0] * m[1] + d[1] * m[5] + d[2] * m[9], d[0] * m[2] + d[1] * m[6] + d[2] * m[10]};
}

Vec3 origin(const std::array<float, 16>& m)
{
    return {m[12], m[13], m[14]};
}

// Inverse of an affine row-vector transform (identity when singular).
std::array<float, 16> inverse_affine(const std::array<float, 16>& m)
{
    const auto a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const auto c00 = e * i - f * h;
    const auto c01 = c * h - b * i;
    const auto c02 = b * f - c * e;
    const auto determinant = a * c00 + d * c01 + g * c02;
    if (std::abs(determinant) < 1e-12F)
    {
        return visual_identity_transform;
    }
    const auto s = 1.0F / determinant;
    std::array<float, 16> r{};
    r[0] = c00 * s;
    r[1] = c01 * s;
    r[2] = c02 * s;
    r[4] = (f * g - d * i) * s;
    r[5] = (a * i - c * g) * s;
    r[6] = (c * d - a * f) * s;
    r[8] = (d * h - e * g) * s;
    r[9] = (b * g - a * h) * s;
    r[10] = (a * e - b * d) * s;
    r[12] = -(m[12] * r[0] + m[13] * r[4] + m[14] * r[8]);
    r[13] = -(m[12] * r[1] + m[13] * r[5] + m[14] * r[9]);
    r[14] = -(m[12] * r[2] + m[13] * r[6] + m[14] * r[10]);
    r[15] = 1.0F;
    return r;
}

// Row-vector product: `left` applied first.
std::array<float, 16> multiply(const std::array<float, 16>& left, const std::array<float, 16>& right)
{
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k)
            {
                sum += left[row * 4 + k] * right[k * 4 + column];
            }
            result[row * 4 + column] = sum;
        }
    }
    return result;
}

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
Vec3 sample_group(const ParticleGroup& group, Random& random)
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
        return Vec3{planar * std::cos(angle), planar * std::sin(angle), z} * radius;
    }
    case 4:
    {
        const auto angle = random.range(0.0F, two_pi);
        const auto radius = group.cylinder_hollow ? group.cylinder_radius : random.range(0.0F, group.cylinder_radius);
        return {radius * std::cos(angle), radius * std::sin(angle), random.range(0.0F, group.cylinder_height)};
    }
    default:
        return to_vec(group.value);
    }
}

VisualParticleBlend blend_of(const std::uint32_t mode)
{
    // The legacy particle shader table (docs/compatibility-research.md); stock data uses 1, 2, and 11.
    switch (mode)
    {
    case 1:
    case 4:
        return VisualParticleBlend::additive;
    case 3:
    case 6:
    case 8:
    case 9:
        return VisualParticleBlend::modulate;
    default:
        return VisualParticleBlend::alpha;
    }
}

bool renderable(const ParticleEmitter& emitter)
{
    // No heat-distortion or weather pass exists yet.
    return !emitter.heat && !emitter.weather && emitter.blend_mode != 10;
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
    Vec3 position{};
    Vec3 velocity{};
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
    std::array<float, 16> transform{visual_identity_transform};
    Vec3 previous_origin{};
    // The transform at the previous update: linked particles move rigidly with the emitter.
    std::array<float, 16> previous_transform{visual_identity_transform};
    std::vector<EmitterState> emitters;
    Random random;
    std::uint32_t generation{};
    bool live{};
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

ParticleInstanceId ParticleWorld::play(std::shared_ptr<const ParticleEffect> effect, const std::array<float, 16>& transform, const std::uint64_t seed, std::shared_ptr<const ParticleEmitMesh> mesh)
{
    if (!effect || effect->system.emitters.empty())
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
    instance.transform = transform;
    instance.previous_origin = origin(transform);
    instance.previous_transform = transform;
    instance.random = Random(seed);
    const auto& emitters = instance.effect->system.emitters;
    instance.emitters.resize(emitters.size());
    for (std::size_t e = 0; e < emitters.size(); ++e)
    {
        auto& state = instance.emitters[e];
        state.next_emission = std::max(0.0F, emitters[e].initial_delay);
        state.drawn = renderable(emitters[e]);
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

void ParticleWorld::set_transform(const ParticleInstanceId id, const std::array<float, 16>& transform)
{
    if (auto* instance = find(id))
    {
        instance->transform = transform;
    }
}

void ParticleWorld::set_emitting(const ParticleInstanceId id, const bool emitting)
{
    if (auto* instance = find(id))
    {
        instance->emitting = emitting;
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
    if (!instance->effect->system.leave_particles)
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

bool ParticleWorld::emit(Instance& instance, const std::size_t emitter_index, const Particle* parent, const std::array<float, 3>& offset, const float step)
{
    if (live_particles_ >= capacity_)
    {
        ++totals_.dropped_particles;
        return false;
    }
    const auto& emitter = instance.effect->system.emitters[emitter_index];
    auto& random = instance.random;
    Particle particle;
    const auto& transform = instance.transform;
    auto local_position = sample_group(emitter.groups[2], random);
    auto local_velocity = sample_group(emitter.groups[0], random);
    if (emitter.emit_from_mesh != 0 && instance.mesh)
    {
        // On the emitter mesh: every vertex in turn (3), a random vertex (1), or a random point on a
        // random triangle (2), pushed out along the normal; the velocity's +Z turns to the normal.
        const auto& mesh = *instance.mesh;
        const auto vertices = mesh.positions.size();
        Vec3 point{};
        Vec3 normal{0.0F, 0.0F, 1.0F};
        if (emitter.emit_from_mesh == 2 && mesh.indices.size() >= 3)
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
            const auto vertex = emitter.emit_from_mesh == 3 ? cursor++ % vertices : std::min<std::size_t>(static_cast<std::size_t>(random.unit() * static_cast<float>(vertices)), vertices - 1U);
            point = mesh.positions[vertex];
            normal = mesh.normals[vertex];
        }
        normal = normalized(normal);
        if (length(normal) == 0.0F)
        {
            normal = {0.0F, 0.0F, 1.0F};
        }
        local_position = point + normal * emitter.emit_from_mesh_offset;
        // A basis whose Z is the normal.
        const Vec3 helper = std::abs(normal[2]) < 0.9F ? Vec3{0.0F, 0.0F, 1.0F} : Vec3{1.0F, 0.0F, 0.0F};
        const auto side = normalized(Vec3{helper[1] * normal[2] - helper[2] * normal[1], helper[2] * normal[0] - helper[0] * normal[2], helper[0] * normal[1] - helper[1] * normal[0]});
        const Vec3 up{normal[1] * side[2] - normal[2] * side[1], normal[2] * side[0] - normal[0] * side[2], normal[0] * side[1] - normal[1] * side[0]};
        local_velocity = side * local_velocity[0] + up * local_velocity[1] + normal * local_velocity[2];
    }
    particle.position = transform_point(transform, local_position) + offset;
    particle.step = step;
    particle.velocity = transform_direction(transform, local_velocity);
    if (emitter.inward_speed != 0.0F)
    {
        // As in the reference, the stored value moves particles away from the emitter when positive.
        const auto outward = normalized(transform_direction(transform, local_position));
        if (length(outward) > 0.0F)
        {
            particle.velocity = outward * emitter.inward_speed;
        }
    }
    if (parent != nullptr)
    {
        particle.position = particle.position - origin(transform) + parent->position;
        if (emitter.parent_link_strength != 0.0F)
        {
            particle.velocity = particle.velocity + parent->velocity * emitter.parent_link_strength;
        }
    }
    particle.lifetime = std::max(0.01F, emitter.lifetime * (1.0F + random.range(0.0F, std::max(0.0F, emitter.random_lifetime))));
    const auto variance = std::max(0.0F, emitter.random_scale) * 0.5F;
    particle.size_factor = (1.0F + random.range(-variance, variance)) / (1.0F + variance);
    if (emitter.random_rotation)
    {
        // A fixed random angle: the reference derives its mean from the rotation track's first value
        // (fractional revolutions) and its spread relative to that mean.
        const auto first = emitter.tracks[static_cast<std::size_t>(ParticleTrackIndex::rotation_speed)].keys.front().value;
        const auto mean = first > 0.0F ? first - std::floor(first) : 0.0F;
        const auto spread = first > 0.0F ? emitter.random_rotation_variance / first : 0.0F;
        particle.rotation = mean * (1.0F + random.range(-spread, spread));
    }
    if (emitter.random_rotation_direction && random.unit() < 0.5F)
    {
        particle.rotation_direction = -1.0F;
        particle.rotation = -particle.rotation;
    }
    const auto& add = emitter.random_colors;
    particle.color_add[0] = random.range(0.0F, std::max(0.0F, add[0]));
    for (std::size_t channel = 1; channel < 4; ++channel)
    {
        particle.color_add[channel] = emitter.random_color_grayscale ? particle.color_add[0] : random.range(0.0F, std::max(0.0F, add[channel]));
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
    const auto& emitter = instance.effect->system.emitters[emitter_index];
    if (emitter.death_child >= 0 && instance.emitters.size() > static_cast<std::size_t>(emitter.death_child))
    {
        // Engine choice: a death child emits one burst (or one particle when it is continuous).
        const auto child = static_cast<std::size_t>(emitter.death_child);
        const auto& child_emitter = instance.effect->system.emitters[child];
        const auto count = child_emitter.use_bursts ? std::min<std::size_t>(child_emitter.particles_per_burst, maximum_trail_burst) : std::size_t{1};
        for (std::size_t n = 0; n < count && emit(instance, child, &parent); ++n)
        {
        }
    }
}

void ParticleWorld::step_instance(Instance& instance, const float seconds)
{
    const auto& emitters = instance.effect->system.emitters;
    const auto current_origin = origin(instance.transform);
    const auto moved = current_origin - instance.previous_origin;
    instance.previous_origin = current_origin;
    // Rigid motion of the emitter since the last update (translation and rotation).
    const auto carried = multiply(inverse_affine(instance.previous_transform), instance.transform);
    instance.previous_transform = instance.transform;
    const bool may_emit = instance.emitting && !instance.stopped;
    for (std::size_t e = 0; e < emitters.size(); ++e)
    {
        const auto& emitter = emitters[e];
        auto& state = instance.emitters[e];
        // Emission on the emitter's own clock.
        state.clock += seconds;
        if (!state.child && !state.finished && may_emit && state.drawn)
        {
            const auto interval = emitter.use_bursts ? std::max(emitter.burst_delay, 1.0F / 120.0F) : 1.0F / static_cast<float>(std::max<std::uint32_t>(emitter.particles_per_second, 1U));
            // Each emission happens at its own time within the update, where the emitter was then
            // (its translation interpolated), so a moving emitter leaves an even trail.
            const auto update_start = state.clock - seconds;
            while (state.clock >= state.next_emission && !state.finished)
            {
                const auto fraction = seconds > 0.0F ? std::clamp((state.next_emission - update_start) / seconds, 0.0F, 1.0F) : 1.0F;
                // Linked particles spawn where the emitter is now (they move with it from then on).
                const auto behind = emitter.link_to_system ? Vec3{} : moved * -(1.0F - fraction);
                const auto remaining = seconds * (1.0F - fraction);
                // Every-vertex mesh emitters emit once per vertex per spawn.
                const auto per_vertex = emitter.emit_from_mesh == 3 && instance.mesh ? static_cast<std::uint32_t>(std::min<std::size_t>(instance.mesh->positions.size(), 4096U)) : 1U;
                const auto count = (emitter.use_bursts ? emitter.particles_per_burst : 1U) * per_vertex;
                for (std::uint32_t n = 0; n < count; ++n)
                {
                    if (!emit(instance, e, nullptr, behind, remaining))
                    {
                        break;
                    }
                }
                state.next_emission += interval;
                if (emitter.use_bursts && emitter.burst_count > 0 && ++state.bursts >= emitter.burst_count)
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
        Vec3 acceleration{0.0F, 0.0F, -emitter.gravity};
        acceleration = acceleration + (emitter.object_space_acceleration ? transform_direction(instance.transform, to_vec(emitter.acceleration)) : to_vec(emitter.acceleration));
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
            if (emitter.link_to_system && !fresh)
            {
                particle.position = transform_point(carried, particle.position);
                particle.velocity = transform_direction(carried, particle.velocity);
            }
            if (!particle.resting)
            {
                auto pull = acceleration;
                if (emitter.inward_acceleration != 0.0F)
                {
                    pull = pull + normalized(current_origin - particle.position) * emitter.inward_acceleration;
                }
                particle.velocity = particle.velocity + pull * dt;
                particle.position = particle.position + particle.velocity * dt;
            }
            if (!emitter.random_rotation)
            {
                particle.rotation += dt * particle.rotation_direction * sample_particle_track(rotation_track, particle.age / particle.lifetime);
            }
            if (emitter.ground_behavior != 0 && ground_)
            {
                if (const auto ground = ground_(particle.position[0], particle.position[1]); ground && particle.position[2] < *ground)
                {
                    if (emitter.ground_behavior == 1)
                    {
                        kill(instance, e, p);
                        continue;
                    }
                    particle.position[2] = *ground;
                    if (emitter.ground_behavior == 2 && std::abs(particle.velocity[2]) * emitter.bounciness > 1.0F)
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
                const auto interval =
                    child_emitter.use_bursts ? std::max(child_emitter.burst_delay, 1.0F / 120.0F) : 1.0F / static_cast<float>(std::max<std::uint32_t>(child_emitter.particles_per_second, 1U));
                particle.trail_clock -= dt;
                std::size_t emitted = 0;
                while (particle.trail_clock <= 0.0F && emitted < maximum_trail_burst)
                {
                    const auto parent = particle;
                    const auto count = child_emitter.use_bursts ? child_emitter.particles_per_burst : 1U;
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

void ParticleWorld::build_sprites(const std::array<float, 3>& eye, std::vector<VisualParticle>& sprites) const
{
    const auto first = sprites.size();
    std::vector<float> depth;
    for (const auto& instance : instances_)
    {
        if (!instance.live)
        {
            continue;
        }
        const auto& emitters = instance.effect->system.emitters;
        for (std::size_t e = 0; e < emitters.size(); ++e)
        {
            const auto& state = instance.emitters[e];
            if (!state.drawn || state.particles.empty())
            {
                continue;
            }
            const auto& emitter = emitters[e];
            const auto grid = std::max(1U, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(std::max(emitter.texture_cells, 1U))))));
            const auto cell = 1.0F / static_cast<float>(grid);
            const auto texture = e < instance.effect->textures.size() ? instance.effect->textures[e] : invalid_visual_texture;
            const auto blend = blend_of(emitter.blend_mode);
            const auto shape = emitter.has_tail ? VisualParticleShape::kite : (emitter.world_oriented ? VisualParticleShape::ground : VisualParticleShape::billboard);
            for (const auto& particle : state.particles)
            {
                const auto t = std::clamp(particle.age / particle.lifetime, 0.0F, 1.0F);
                VisualParticle sprite;
                sprite.position = particle.position;
                sprite.half_size = 0.5F * sample_particle_track(emitter.tracks[static_cast<std::size_t>(ParticleTrackIndex::scale)], t) * particle.size_factor;
                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    sprite.color[channel] = std::clamp(sample_particle_track(emitter.tracks[channel], t) + particle.color_add[channel], 0.0F, 1.0F);
                }
                const auto index = static_cast<std::uint32_t>(std::max(0.0F, std::floor(sample_particle_track(emitter.tracks[static_cast<std::size_t>(ParticleTrackIndex::texture_index)], t) + 0.5F)));
                sprite.uv_rect = {static_cast<float>(index % grid) * cell, static_cast<float>((index / grid) % grid) * cell, cell, cell};
                sprite.rotation = particle.rotation * two_pi;
                sprite.texture = texture;
                sprite.shape = shape;
                sprite.blend = blend;
                sprite.depth_test = !emitter.no_depth_test;
                if (shape == VisualParticleShape::kite)
                {
                    sprite.axis = particle.velocity;
                    sprite.tail = emitter.tail_size;
                }
                sprites.push_back(sprite);
                const auto offset = particle.position - eye;
                depth.push_back(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
            }
        }
    }
    // Back to front; the stable sort keeps equal depths in emission order, so frames do not flicker.
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
