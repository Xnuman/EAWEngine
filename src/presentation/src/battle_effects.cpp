#include "presentation/battle_effects.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eaw {
namespace {

// Terrain marks (engine choices): a mark that is not permanent fades out over this many seconds
// once its Fadeout_Time has passed; at most this many lie at once (the oldest go first); and an
// explosion leaves one only within this height of the ground.
constexpr float decal_fade_seconds = 2.0F;
constexpr std::size_t maximum_decals = 2048;
constexpr float scorch_ground_reach = 40.0F;

} // namespace

BattleEffects::BattleEffects(const ParticleDrawOrder draw_order)
{
    particles_.set_draw_order(draw_order);
}

void BattleEffects::reset(const BattleEffectContent& content)
{
    attached_.clear(particles_);
    projectile_attached_.clear(particles_);
    projectile_effects_.clear();
    particles_.clear();
    projectile_visuals_.clear();
    atlas_ = invalid_visual_texture;
    effect_objects_.clear();
    decals_.clear();
    decal_atlas_ = invalid_visual_texture;
    footprints_.reset();
    attached_.set_rigs(content.rigs);
    projectile_attached_.set_rigs(content.rigs);
    take(content);
}

void BattleEffects::add(const BattleEffectContent& content)
{
    attached_.add_rigs(content.rigs);
    projectile_attached_.add_rigs(content.rigs);
    take(content);
}

void BattleEffects::take(const BattleEffectContent& content)
{
    if (content.projectile_visuals != nullptr)
    {
        projectile_visuals_.insert(content.projectile_visuals->begin(), content.projectile_visuals->end());
    }
    if (atlas_ == invalid_visual_texture)
    {
        atlas_ = content.atlas;
    }
    if (content.effect_objects != nullptr)
    {
        effect_objects_.insert(content.effect_objects->begin(), content.effect_objects->end());
    }
    if (decal_atlas_ == invalid_visual_texture)
    {
        decal_atlas_ = content.decal_atlas;
    }
    if (!footprints_)
    {
        footprints_ = content.footprints;
    }
}

void BattleEffects::set_ground(ParticleGroundHeight ground)
{
    ground_ = ground;
    particles_.set_ground(std::move(ground));
}

void BattleEffects::play_tick_events(const SimSnapshot& snapshot)
{
    for (const auto& death : snapshot.deaths)
    {
        play_object(death.death_effect, death.position);
    }
    for (const auto& fallen : snapshot.hardpoint_deaths)
    {
        play_object(fallen.effect, fallen.position);
    }
    for (const auto& shot : snapshot.shots)
    {
        play_object(shot.effect, shot.origin);
    }
    for (const auto& impact : snapshot.impacts)
    {
        if (const auto found = projectile_visuals_.find(impact.visual); found != projectile_visuals_.end())
        {
            play_object(impact_effect(found->second, impact.kind), impact.position);
        }
    }
}

void BattleEffects::attach(const std::span<const VisualInstance> instances, const std::span<const EntityId> entities, const std::span<const Matrix4> palette,
                           const std::span<const EntitySnapshot> snapshot, const double seconds)
{
    attached_.update(particles_, instances, entities, palette, snapshot, seconds);
}

void BattleEffects::follow_projectiles(const std::span<const VisualInstance> instances, const std::span<const std::uint32_t> serials, const std::span<const Matrix4> palette,
                                       const std::span<const ProjectileSnapshot> projectiles, const float interpolation, const float tick_seconds)
{
    // Shot serials stand in for entity ids (generation 1 marks them valid).
    projectile_keys_.clear();
    for (const auto serial : serials)
    {
        projectile_keys_.push_back(EntityId{serial, 1U});
    }
    projectile_attached_.update(particles_, instances, projectile_keys_, palette);

    ++projectile_frame_;
    for (const auto& projectile : projectiles)
    {
        const auto found = projectile_visuals_.find(projectile.visual);
        if (found == projectile_visuals_.end() || found->second.bolt || !found->second.effect)
        {
            continue;
        }
        // The effect's scale goes to the particle world, which sizes its particles too.
        auto unscaled = found->second;
        unscaled.scale = 1.0F;
        const auto transform = projectile_model_transform(projectile, unscaled, interpolation, tick_seconds);
        auto [entry, inserted] = projectile_effects_.try_emplace(projectile.serial);
        if (inserted)
        {
            entry->second.instance = particles_.play(found->second.effect, transform, seed_++, {}, found->second.scale);
        }
        else
        {
            particles_.set_transform(entry->second.instance, transform);
        }
        entry->second.frame = projectile_frame_;
    }
    for (auto entry = projectile_effects_.begin(); entry != projectile_effects_.end();)
    {
        if (entry->second.frame != projectile_frame_)
        {
            particles_.stop(entry->second.instance);
            entry = projectile_effects_.erase(entry);
        }
        else
        {
            ++entry;
        }
    }
}

void BattleEffects::update(const float seconds)
{
    // Marks age; faded ones go.
    for (auto& mark : decals_)
    {
        mark.age += seconds;
    }
    std::erase_if(decals_,
                  [](const LiveDecal& mark)
                  {
                      return !mark.decal->permanent && mark.age > mark.decal->fade_seconds + decal_fade_seconds;
                  });
    particles_.update(seconds);
}

void BattleEffects::build_sprites(const Vector3& eye, const std::span<const ProjectileSnapshot> projectiles, const float interpolation, const float tick_seconds,
                                  std::vector<VisualParticle>& sprites) const
{
    particles_.build_sprites(eye, sprites);
    // Bolts are additive, so their order among the particles does not matter.
    if (atlas_ != invalid_visual_texture)
    {
        append_projectile_sprites(projectiles, projectile_visuals_, atlas_, interpolation, tick_seconds, sprites);
    }
}

void BattleEffects::play_object(const std::uint32_t key, const Vector3& position)
{
    const auto found = effect_objects_.find(key);
    if (key == 0 || found == effect_objects_.end() || !found->second.effect)
    {
        return;
    }
    auto transform = Matrix4::identity();
    set_translation(transform, position);
    (void)particles_.play(found->second.effect, transform, seed_++, {}, found->second.scale);
    // Its scorch mark, on the ground below when it played near it.
    const auto& scorch = found->second.scorch;
    if (!scorch || decal_atlas_ == invalid_visual_texture || !ground_)
    {
        return;
    }
    if (const auto ground = ground_(position.x, position.y); !ground || std::fabs(position.z - *ground) > scorch_ground_reach)
    {
        return;
    }
    LiveDecal mark;
    mark.decal = &*scorch;
    mark.center = {position.x, position.y};
    mark.rotation = scorch->random_rotate ? decal_random_.range(0.0F, 6.2831853F) : scorch->angle;
    mark.scale = 1.0F + decal_random_.range(-scorch->size_variation, scorch->size_variation);
    mark.cell = scorch->cells[decal_random_.next() % scorch->cells.size()];
    if (decals_.size() >= maximum_decals)
    {
        decals_.erase(decals_.begin());
    }
    decals_.push_back(mark);
}

void BattleEffects::leave_footprints(const std::span<const AnimationCue> cues, const std::span<const VisualInstance> instances, const std::span<const Matrix4> palette, const UnitAnimator& animator)
{
    if (!footprints_ || !footprints_->events || decal_atlas_ == invalid_visual_texture)
    {
        return;
    }
    const auto& source = *footprints_;
    visit_cue_events(*source.events, cues,
                     [&](const AnimationCue& cue, const AnimationEvent& event)
                     {
                         if (event.kind != AnimationEventKind::surface)
                         {
                             return;
                         }
                         const auto* surface = source.surfaces.find(event.name);
                         const auto* decal = surface != nullptr && !surface->decal.empty() ? source.decals.find(surface->decal) : nullptr;
                         if (decal == nullptr || decal->cells.empty())
                         {
                             return;
                         }
                         const auto position = animator.bone_position(cue, event.bone, instances, palette).value_or(cue.position);
                         // Turned with the unit: its model faces -Y, so the mark's +Y points along
                         // the unit's facing (an engine choice until compared with the original).
                         float facing = 0.0F;
                         if (cue.instance < instances.size())
                         {
                             const auto& m = instances[cue.instance].transform;
                             facing = std::atan2(-m(1, 1), -m(1, 0)) - 1.5707964F;
                         }
                         LiveDecal mark;
                         mark.decal = decal;
                         mark.center = {position.x, position.y};
                         mark.rotation = facing + (decal->random_rotate ? decal_random_.range(0.0F, 6.2831853F) : decal->angle);
                         mark.scale = 1.0F + decal_random_.range(-decal->size_variation, decal->size_variation);
                         mark.cell = decal->cells[decal_random_.next() % decal->cells.size()];
                         if (decals_.size() >= maximum_decals)
                         {
                             decals_.erase(decals_.begin());
                         }
                         decals_.push_back(mark);
                     });
}

void BattleEffects::build_decals(std::vector<VisualDecal>& decals) const
{
    constexpr float cell = 1.0F / static_cast<float>(decal_atlas_cells);
    for (const auto& mark : decals_)
    {
        const auto& decal = *mark.decal;
        float opacity = decal.intensity;
        if (!decal.permanent && mark.age > decal.fade_seconds)
        {
            opacity *= std::max(0.0F, 1.0F - (mark.age - decal.fade_seconds) / decal_fade_seconds);
        }
        const float span = static_cast<float>(decal.cell_span) * cell;
        decals.push_back({mark.center,
                          {decal.width * 0.5F * mark.scale, decal.height * 0.5F * mark.scale},
                          mark.rotation,
                          {static_cast<float>(mark.cell.column) * cell, static_cast<float>(mark.cell.row) * cell, span, span},
                          decal_atlas_,
                          decal.mode == TerrainDecalMode::bump_alpha ? VisualDecalMode::bump
                          : decal.mode == TerrainDecalMode::additive ? VisualDecalMode::additive
                                                                     : VisualDecalMode::diffuse,
                          opacity});
    }
}

std::size_t BattleEffects::playable_effect_objects() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(effect_objects_,
                                                          [](const auto& entry)
                                                          {
                                                              return entry.second.effect != nullptr;
                                                          }));
}

} // namespace eaw
