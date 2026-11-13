#include "presentation/battle_frame.hpp"

#include "presentation/projectile_visuals.hpp"

#include <algorithm>
#include <span>

namespace eaw {

void present_tick(const BattleRuntime& runtime, BattlePresenters& presenters, const double world_seconds)
{
    const auto& world = runtime.battle().world();
    if (const auto* scripts = runtime.scripts(); scripts != nullptr && presenters.audio != nullptr)
    {
        for (const auto& request : scripts->sound_requests())
        {
            (void)presenters.audio->play_event(request.event, request.position);
        }
    }
    for (const auto& death : world.snapshot().deaths)
    {
        if (const auto corpse = presenters.presentation.corpse_instance(death); corpse && presenters.animator != nullptr)
        {
            presenters.animator->add_corpse(corpse->instance, death.id, world_seconds, corpse->style, death.crushed);
        }
    }
    // Destroyed hardpoints: their scorch marks appear and their attached models go.
    if (presenters.animator != nullptr && !world.snapshot().hardpoint_deaths.empty())
    {
        for (const auto& fallen : world.snapshot().hardpoint_deaths)
        {
            const auto& entities = world.snapshot().entities;
            const auto owner = std::ranges::find(entities, fallen.entity, &EntitySnapshot::id);
            if (owner == entities.end())
            {
                continue;
            }
            if (const auto bones = presenters.presentation.hardpoint_bones(*owner, fallen.hardpoint))
            {
                presenters.animator->destroy_hardpoint(fallen.entity, bones->mesh, bones->bones);
            }
        }
    }
    presenters.effects.play_tick_events(world.snapshot());
    // Shield hits show the projectile's absorb particle (play_tick_events); the shield mesh flash
    // (flash_shields) belongs to shield abilities, which are not implemented yet (the owner's
    // reading, 2026-11-14).
    if (presenters.audio != nullptr)
    {
        presenters.audio->play_tick_events(world.snapshot(), world);
    }
}

void build_battle_frame(const BattleRuntime& runtime, const SimSnapshot& previous, const std::unordered_set<std::uint64_t>& selected, const double world_seconds, const std::size_t max_palette,
                        BattlePresenters& presenters, BattleFrame& frame)
{
    const auto& current = runtime.battle().world().snapshot();
    const auto interpolation = runtime.interpolation();
    const auto tick_seconds = static_cast<float>(runtime.tick_seconds());
    auto entities = interpolate_snapshots(previous, current, interpolation);
    // Decorations are posed and animated like idle entities the simulation never sees.
    const auto decorations = presenters.presentation.decoration_snapshots();
    entities.insert(entities.end(), decorations.begin(), decorations.end());
    presenters.presentation.build(entities, selected, frame.instances, frame.entities, &frame.animations, world_seconds);
    // Model projectiles follow the entities, without an entity (never picked) and at rest pose.
    const auto first_projectile = frame.instances.size();
    frame.projectile_serials.clear();
    append_projectile_instances(current.projectiles, presenters.effects.projectile_visuals(), interpolation, tick_seconds, frame.instances, frame.projectile_serials);
    frame.entities.resize(frame.instances.size());
    if (presenters.animator == nullptr)
    {
        return;
    }
    presenters.animator->animate(entities, frame.entities, frame.instances, world_seconds, max_palette, frame.palette, frame.animations);
    presenters.effects.leave_footprints(presenters.animator->cues(), frame.instances, frame.palette, *presenters.animator);
    presenters.effects.lay_tracks(runtime.battle().world(), frame.instances, frame.entities, frame.palette, *presenters.animator, world_seconds);
    for (const auto& ended : presenters.animator->ended_corpses())
    {
        presenters.effects.play_object(ended.effect, ended.position);
    }
    if (presenters.audio != nullptr)
    {
        presenters.audio->play_animation_cues(presenters.animator->cues());
    }
    presenters.effects.attach(frame.instances, frame.entities, frame.palette, entities, world_seconds);
    presenters.effects.follow_projectiles(std::span(frame.instances).subspan(first_projectile), frame.projectile_serials, frame.palette, current.projectiles, interpolation, tick_seconds);
}

} // namespace eaw
