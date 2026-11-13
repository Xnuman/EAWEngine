#pragma once

// Presentation of a battle's effects, from its snapshots: the particle effects attached to drawn
// units' bones (engine glows and other proxies), the XML Particle objects that shot impacts and
// deaths name, and shots in flight drawn as bolts. Presentation only: nothing here feeds the
// simulation, and it needs no graphics API (textures are renderer indices the caller registered).

#include "core/random.hpp"
#include "render/visual_frame.hpp"
#include "render/visual_texture.hpp"
#include "assets/surface_effects.hpp"
#include "assets/terrain_decals.hpp"
#include "simulation/simulation.hpp"
#include "presentation/attached_effects.hpp"
#include "presentation/particle_effects.hpp"
#include "presentation/projectile_visuals.hpp"
#include "presentation/unit_animation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace eaw {

// What lays footprints on a land map: the animation events (their surface triggers fire as feet
// plant), and the surface effects and decals the triggers resolve through (the default surface).
struct FootprintSource
{
    std::shared_ptr<const AnimationEventTable> events;
    SurfaceEffectCatalog surfaces;
    TerrainDecalCatalog decals;
};

// What a map or a spawn package brings: rigs parallel to its meshes, how its shots look (`atlas` is
// the bolt atlas's renderer texture, or invalid_visual_texture), and its Particle objects. Effect
// textures must already be renderer indices (renumber_effect_textures).
struct BattleEffectContent
{
    std::span<const VisualRig> rigs;
    const ProjectileVisualTable* projectile_visuals{};
    std::uint32_t atlas{invalid_visual_texture};
    const ParticleEffectObjectTable* effect_objects{};
    // The terrain decal atlas's renderer texture (scorch marks, footprints), or invalid_visual_texture.
    std::uint32_t decal_atlas{invalid_visual_texture};
    // A land map's footprints (shared with the map), or null.
    std::shared_ptr<const FootprintSource> footprints;
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
    void attach(std::span<const VisualInstance> instances, std::span<const EntityId> entities, std::span<const Matrix4> palette, std::span<const EntitySnapshot> snapshot, double seconds);
    // Keeps the effects of shots in flight: the proxies (trails) of model projectiles drawn this frame
    // (`instances` with their shots' `serials`; `palette` as for attach) and the particle systems of
    // shots whose model is one, placed where `projectiles` are drawn. Effects of shots that ended stop
    // emitting, and their particles live out.
    void follow_projectiles(std::span<const VisualInstance> instances, std::span<const std::uint32_t> serials, std::span<const Matrix4> palette, std::span<const ProjectileSnapshot> projectiles,
                            float interpolation, float tick_seconds);
    // Advances every effect by `seconds` of presentation time (0 while the world is paused).
    void update(float seconds);
    // Appends the particles, sorted as the fidelity profile says from `eye`, then a bolt for each
    // shot in `projectiles` placed `interpolation` of a tick after the previous tick.
    void build_sprites(const Vector3& eye, std::span<const ProjectileSnapshot> projectiles, float interpolation, float tick_seconds, std::vector<VisualParticle>& sprites) const;

    // Plays a Particle object once at `position`, upright (the effect's own axes: an engine choice
    // until the original's orientation of impacts and explosions is known). Unknown keys are ignored.
    // On land, an object with a scorch mark leaves it on the ground below when it plays near enough to
    // the ground (an explosion high in the air leaves none).
    void play_object(std::uint32_t key, const Vector3& position);

    // Lays the footprints of the surface events this frame's `cues` passed (UnitAnimator::cues), at
    // the event's bone when the animator finds it (else the unit), turned with the unit.
    void leave_footprints(std::span<const AnimationCue> cues, std::span<const VisualInstance> instances, std::span<const Matrix4> palette, const UnitAnimator& animator);

    // Appends this frame's marks on the terrain (scorches, footprints), faded as they age.
    void build_decals(std::vector<VisualDecal>& decals) const;
    [[nodiscard]] std::size_t decal_count() const noexcept
    {
        return decals_.size();
    }

    // The particle world, for effects the caller plays itself (sandbox --effect).
    [[nodiscard]] ParticleWorld& particles() noexcept
    {
        return particles_;
    }
    [[nodiscard]] const ParticleWorld& particles() const noexcept
    {
        return particles_;
    }
    [[nodiscard]] const ProjectileVisualTable& projectile_visuals() const noexcept
    {
        return projectile_visuals_;
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
    // Model projectiles' proxies, keyed by shot serial (apart from units, so keys never collide).
    AttachedEffects projectile_attached_;
    std::vector<EntityId> projectile_keys_;
    // Particle systems of shots whose model is one, by shot serial, and the frame each was last seen.
    struct ProjectileEffect
    {
        ParticleInstanceId instance;
        std::uint64_t frame{};
    };
    std::unordered_map<std::uint32_t, ProjectileEffect> projectile_effects_;
    std::uint64_t projectile_frame_{};
    ProjectileVisualTable projectile_visuals_;
    std::uint32_t atlas_{invalid_visual_texture};
    ParticleEffectObjectTable effect_objects_;
    // Seeds of one-shot effects, in play order (presentation only, so not simulation state).
    std::uint64_t seed_{1};
    // Marks on the terrain: each its decal, where it lies, its turn and size, and its age.
    struct LiveDecal
    {
        const TerrainDecal* decal{};
        Vector2 center{};
        float rotation{};
        float scale{1.0F};
        TerrainDecalCell cell{};
        float age{};
    };
    std::vector<LiveDecal> decals_;
    std::shared_ptr<const FootprintSource> footprints_;
    std::uint32_t decal_atlas_{invalid_visual_texture};
    ParticleGroundHeight ground_;
    Pcg32 decal_random_{1};

    void take(const BattleEffectContent& content);
};

} // namespace eaw
