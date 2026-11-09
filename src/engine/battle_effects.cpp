#include "battle_effects.hpp"

#include <algorithm>
#include <utility>

namespace eaw {

BattleEffects::BattleEffects(const ParticleDrawOrder draw_order)
{
    particles_.set_draw_order(draw_order);
}

void BattleEffects::reset(const BattleEffectContent& content)
{
    attached_.clear(particles_);
    particles_.clear();
    projectile_visuals_.clear();
    atlas_ = invalid_visual_texture;
    effect_objects_.clear();
    attached_.set_rigs(content.rigs);
    take(content);
}

void BattleEffects::add(const BattleEffectContent& content)
{
    attached_.add_rigs(content.rigs);
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
}

void BattleEffects::set_ground(ParticleGroundHeight ground)
{
    particles_.set_ground(std::move(ground));
}

void BattleEffects::play_tick_events(const SimSnapshot& snapshot)
{
    for (const auto& death : snapshot.deaths)
    {
        play_object(death.death_effect, death.position);
    }
    for (const auto& impact : snapshot.impacts)
    {
        if (const auto found = projectile_visuals_.find(impact.visual); found != projectile_visuals_.end())
        {
            play_object(impact_effect(found->second, impact.kind), impact.position);
        }
    }
}

void BattleEffects::attach(const std::span<const VisualInstance> instances, const std::span<const EntityId> entities, const std::span<const std::array<float, 16>> palette,
                           const std::span<const EntitySnapshot> snapshot, const double seconds)
{
    attached_.update(particles_, instances, entities, palette, snapshot, seconds);
}

void BattleEffects::update(const float seconds)
{
    particles_.update(seconds);
}

void BattleEffects::build_sprites(const std::array<float, 3>& eye, const std::span<const ProjectileSnapshot> projectiles, const float interpolation, const float tick_seconds,
                                  std::vector<VisualParticle>& sprites) const
{
    particles_.build_sprites(eye, sprites);
    // Bolts are additive, so their order among the particles does not matter.
    if (atlas_ != invalid_visual_texture)
    {
        append_projectile_sprites(projectiles, projectile_visuals_, atlas_, interpolation, tick_seconds, sprites);
    }
}

void BattleEffects::play_object(const std::uint32_t key, const SimVector3& position)
{
    const auto found = effect_objects_.find(key);
    if (key == 0 || found == effect_objects_.end() || !found->second.effect)
    {
        return;
    }
    auto transform = visual_identity_transform;
    transform[12] = position.x;
    transform[13] = position.y;
    transform[14] = position.z;
    (void)particles_.play(found->second.effect, transform, seed_++, {}, found->second.scale);
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
