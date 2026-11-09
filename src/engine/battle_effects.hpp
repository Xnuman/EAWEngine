#pragma once

// Presentation of a battle's effects, from its snapshots: the particle effects attached to drawn
// units' bones (engine glows and other proxies), the XML Particle objects that shot impacts and
// deaths name, and shots in flight drawn as bolts. Presentation only: nothing here feeds the
// simulation, and it needs no graphics API (textures are renderer indices the caller registered).

#include "attached_effects.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"

#include "eaw/simulation.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

// What a map or a spawn package brings: rigs parallel to its meshes, how its shots look (`atlas` is
// the bolt atlas's renderer texture, or invalid_visual_texture), and its Particle objects. Effect
// textures must already be renderer indices (renumber_effect_textures).
struct BattleEffectContent
{
    std::span<const VisualRig> rigs;
    const ProjectileVisualTable* projectile_visuals{};
    std::uint32_t atlas{invalid_visual_texture};
    const ParticleEffectObjectTable* effect_objects{};
};

class BattleEffects
{
public:
    explicit BattleEffects(ParticleDrawOrder draw_order = ParticleDrawOrder::emitter_list);

    // A new scene: stops every effect and replaces the content.
    void reset(const BattleEffectContent& content);
    // Runtime meshes and types join (rigs append to the existing ones).
    void add(const BattleEffectContent& content);
    void set_ground(ParticleGroundHeight ground);

    // Plays the impacts and deaths of the tick `snapshot` ends; call after every tick so none is
    // missed when a frame runs several.
    void play_tick_events(const SimSnapshot& snapshot);
    // Keeps attached effects on this frame's drawn units (see AttachedEffects::update).
    void attach(std::span<const VisualInstance> instances, std::span<const EntityId> entities, std::span<const std::array<float, 16>> palette, std::span<const EntitySnapshot> snapshot,
                double seconds);
    // Advances every effect by `seconds` of presentation time (0 while the world is paused).
    void update(float seconds);
    // Appends the particles, sorted as the fidelity profile says from `eye`, then a bolt for each
    // shot in `projectiles` placed `interpolation` of a tick after the previous tick.
    void build_sprites(const std::array<float, 3>& eye, std::span<const ProjectileSnapshot> projectiles, float interpolation, float tick_seconds, std::vector<VisualParticle>& sprites) const;

    // Plays a Particle object once at `position`, upright (the effect's own axes: an engine choice
    // until the original's orientation of impacts and explosions is known). Unknown keys are ignored.
    void play_object(std::uint32_t key, const SimVector3& position);

    // The particle world, for effects the caller plays itself (sandbox --effect).
    [[nodiscard]] ParticleWorld& particles() noexcept
    {
        return particles_;
    }
    [[nodiscard]] const ParticleWorld& particles() const noexcept
    {
        return particles_;
    }
    // Projectile visuals when bolts can be drawn (an atlas is loaded), else null.
    [[nodiscard]] const ProjectileVisualTable* bolts() const noexcept
    {
        return atlas_ != invalid_visual_texture ? &projectile_visuals_ : nullptr;
    }
    [[nodiscard]] std::size_t attached_playing() const noexcept
    {
        return attached_.playing();
    }
    // Particle objects known, and how many of them have an effect to play.
    [[nodiscard]] std::size_t effect_objects() const noexcept
    {
        return effect_objects_.size();
    }
    [[nodiscard]] std::size_t playable_effect_objects() const noexcept;

private:
    ParticleWorld particles_;
    AttachedEffects attached_;
    ProjectileVisualTable projectile_visuals_;
    std::uint32_t atlas_{invalid_visual_texture};
    ParticleEffectObjectTable effect_objects_;
    // Seeds of one-shot effects, in play order (presentation only, so not simulation state).
    std::uint64_t seed_{1};

    void take(const BattleEffectContent& content);
};

} // namespace eaw
