#pragma once

// Presentation-only particle effects: instances of decoded particle systems (eaw/particle_system.hpp)
// placed in the world, simulated on the frame clock, and turned into sprites for the renderer. Nothing
// here feeds the simulation. Each instance draws from its own seeded random stream, so a seed replays
// the same effect. How emitter fields become motion follows the MIT-licensed alo-viewer where it is
// clear and is otherwise an engine choice (docs/compatibility-research.md, "Particle systems").

#include "eaw/particle_system.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

// A particle system ready to play: the decoded system and, per emitter, the renderer texture of its
// colour texture (invalid_visual_texture draws white).
struct ParticleEffect
{
    std::string name;
    ParticleSystem system;
    std::vector<std::uint32_t> textures;
};

// The mesh an emitter with emit_from_mesh spawns on, in the effect's own space: for a proxy, the
// mesh on the proxy bone's parent bone. Triangles index the vertices.
struct ParticleEmitMesh
{
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::uint32_t> indices;
};

// Generation-checked handle of a playing effect.
struct ParticleInstanceId
{
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{};

    [[nodiscard]] bool valid() const noexcept
    {
        return index != std::numeric_limits<std::uint32_t>::max();
    }
    friend bool operator==(const ParticleInstanceId&, const ParticleInstanceId&) = default;
};

struct ParticleWorldStats
{
    std::size_t instances{};
    std::size_t particles{};
    // Particles not emitted because the world was at capacity (cumulative).
    std::size_t dropped_particles{};
    // Emitters skipped because their renderer is missing (heat distortion, weather; cumulative).
    std::size_t skipped_emitters{};
};

// Height of the ground under a world point, for emitters that collide with it; nullopt where there is
// no ground (space).
using ParticleGroundHeight = std::function<std::optional<float>(float x, float y)>;

class ParticleWorld
{
public:
    // `capacity`: the most particles alive at once; emission beyond it is dropped and counted.
    explicit ParticleWorld(std::size_t capacity = 65536);

    // Starts an effect at `transform` (row-vector convention, translation in 12..14, as
    // VisualInstance). One-shot effects end on their own once their emitters are done and their
    // particles have died; looping effects run until stop() or remove().
    // `mesh` serves emitters that emit from a mesh (without one they use their position shape).
    ParticleInstanceId play(std::shared_ptr<const ParticleEffect> effect, const std::array<float, 16>& transform, std::uint64_t seed, std::shared_ptr<const ParticleEmitMesh> mesh = {});
    // Moves a playing effect (attached effects follow their bone each frame). Unknown ids are ignored.
    void set_transform(ParticleInstanceId id, const std::array<float, 16>& transform);
    // Hides or shows a playing effect: hidden effects emit nothing, and their particles live out.
    void set_emitting(ParticleInstanceId id, bool emitting);
    // Stops emitting; the particles live out when the system leaves particles, else vanish now.
    void stop(ParticleInstanceId id);
    void remove(ParticleInstanceId id);
    void clear();
    [[nodiscard]] bool playing(ParticleInstanceId id) const noexcept;

    void set_ground(ParticleGroundHeight ground);

    // Advances every effect by `seconds` (clamped to a quarter second per call).
    void update(float seconds);

    // Appends every live particle as a sprite, sorted back to front from `eye`.
    void build_sprites(const std::array<float, 3>& eye, std::vector<VisualParticle>& sprites) const;

    [[nodiscard]] ParticleWorldStats stats() const noexcept;

private:
    struct Particle;
    struct EmitterState;
    struct Instance;

    std::vector<Instance> instances_;
    std::vector<std::uint32_t> free_;
    std::size_t capacity_{};
    std::size_t live_particles_{};
    ParticleWorldStats totals_;
    ParticleGroundHeight ground_;

    Instance* find(ParticleInstanceId id) noexcept;
    const Instance* find(ParticleInstanceId id) const noexcept;
    void release(std::uint32_t index);
    void step_instance(Instance& instance, float seconds);
    // `offset` moves the spawn point (where a moving emitter was at the emission time); `step` is how
    // much of the current update the particle still lives through (negative: all of it).
    bool emit(Instance& instance, std::size_t emitter, const Particle* parent, const std::array<float, 3>& offset = {}, float step = -1.0F);
    void kill(Instance& instance, std::size_t emitter, std::size_t particle);

public:
    ~ParticleWorld();
    ParticleWorld(ParticleWorld&&) noexcept;
    ParticleWorld& operator=(ParticleWorld&&) noexcept;
};

// Samples a keyframe track at normalized age `t` (0..1).
[[nodiscard]] float sample_particle_track(const ParticleTrack& track, float t) noexcept;

} // namespace eaw
