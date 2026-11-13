// Particle effects and the effects of battles: tracks, the particle world, effect objects, bolts.
#include "test_support.hpp"

#include "assets/terrain_decals.hpp"
#include "battle/battle.hpp"
#include "battle/battle_log.hpp"
#include "presentation/attached_effects.hpp"
#include "presentation/battle_effects.hpp"
#include "presentation/particle_effects.hpp"
#include "presentation/projectile_visuals.hpp"
#include "presentation/unit_animation.hpp"
#include "scene/scene_builder_internal.hpp"
#include "sandbox/sandbox_options.hpp"

namespace {

using namespace eaw_test;

eaw::AloParticleEmitter make_test_emitter()
{
    eaw::AloParticleEmitter emitter;
    for (auto& track : emitter.tracks)
    {
        track.keys = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    }
    emitter.tracks[static_cast<std::size_t>(eaw::AloParticleTrackIndex::scale)].keys = {{0.0F, 4.0F}, {1.0F, 8.0F}};
    emitter.tracks[static_cast<std::size_t>(eaw::AloParticleTrackIndex::texture_index)].keys = {{0.0F, 0.0F}, {1.0F, 0.0F}};
    emitter.tracks[static_cast<std::size_t>(eaw::AloParticleTrackIndex::rotation_speed)].keys = {{0.0F, 0.0F}, {1.0F, 0.0F}};
    emitter.lifetime = 1.0F;
    return emitter;
}

std::shared_ptr<const eaw::ParticleEffect> make_test_effect(std::vector<eaw::AloParticleEmitter> emitters, const bool leave_particles = true,
                                                            const eaw::ParticleFidelity fidelity = eaw::ParticleFidelity::legacy)
{
    eaw::AloParticleSystemData system;
    system.name = "test";
    system.emitters = std::move(emitters);
    system.leave_particles = leave_particles;
    auto effect = std::make_shared<eaw::ParticleEffect>(eaw::load_particle_effect(system, fidelity));
    effect->textures.assign(effect->emitters.size(), 7U);
    return effect;
}

void test_particle_tracks()
{
    eaw::AloParticleTrack track;
    track.keys = {{0.0F, 0.0F}, {0.5F, 10.0F}, {1.0F, 0.0F}};
    require(eaw::sample_particle_track(track, 0.25F) == 5.0F && eaw::sample_particle_track(track, 0.75F) == 5.0F && eaw::sample_particle_track(track, 2.0F) == 0.0F,
            "linear particle track sampling failed");
    track.interpolation = eaw::AloParticleInterpolation::step;
    require(eaw::sample_particle_track(track, 0.4F) == 0.0F && eaw::sample_particle_track(track, 0.5F) == 10.0F, "step particle track sampling failed");
    track.interpolation = eaw::AloParticleInterpolation::smooth;
    require(eaw::sample_particle_track(track, 0.125F) > 0.0F && eaw::sample_particle_track(track, 0.125F) < 2.5F, "smooth particle track sampling failed");
}

// An attached effect takes its unit's scale: particles twice as large and twice as far out on a unit
// drawn at Scale_Factor 2.
void test_attached_effect_scale()
{
    auto burst = make_test_emitter();
    burst.use_bursts = true;
    burst.particles_per_burst = 1;
    burst.burst_count = 1;
    burst.lifetime = 5.0F;
    burst.groups[0].type = 0;
    burst.groups[0].value = {0.0F, 0.0F, 10.0F};
    eaw::VisualRig rig;
    rig.skeleton = std::make_shared<eaw::Skeleton>(eaw::load_skeleton(expect_ok(eaw::read_alo<eaw::AloModelData>(make_test_alo()), "test model")));
    rig.palette_size = 1;
    rig.effects.push_back({make_test_effect({burst}), 0U, "pe_test", -1, nullptr, false});
    const std::vector<eaw::VisualRig> rigs{rig};
    const std::vector<eaw::Matrix4> palette{eaw::Matrix4::identity()};
    const auto sprite_at = [&](const float scale)
    {
        eaw::ParticleWorld world;
        eaw::AttachedEffects effects;
        effects.set_rigs(rigs);
        eaw::VisualInstance unit{0};
        unit.transform.values[0] = unit.transform.values[5] = unit.transform.values[10] = scale;
        unit.transform.values[12] = 100.0F;
        const std::vector<eaw::VisualInstance> instances{unit};
        const std::vector<eaw::EntityId> ids{eaw::EntityId{1, 1}};
        effects.update(world, instances, ids, palette);
        world.update(0.01F);
        std::vector<eaw::VisualParticle> sprites;
        world.build_sprites({0.0F, 0.0F, 0.0F}, sprites);
        return sprites.size() == 1 ? sprites.front() : eaw::VisualParticle{};
    };
    const auto normal = sprite_at(1.0F);
    const auto doubled = sprite_at(2.0F);
    require(normal.half_size > 0.0F && std::fabs(doubled.half_size - 2.0F * normal.half_size) < 1e-3F && std::fabs(doubled.position[2] - 2.0F * normal.position[2]) < 1e-2F &&
                doubled.position[0] == 100.0F,
            "an attached effect must scale with its unit");

    // An _ALT<n> effect plays only while its unit draws damage state n, also on a rig whose bones
    // can be hidden (the visibility half of the palette).
    auto damaged_rig = rig;
    damaged_rig.palette_size = 2;
    damaged_rig.effects.back().state = 1U;
    const std::vector<eaw::VisualRig> damaged_rigs{damaged_rig};
    const std::vector<eaw::Matrix4> shown_palette{eaw::Matrix4::identity(), eaw::Matrix4::identity()};
    const auto sprites_in = [&](const std::uint32_t state)
    {
        eaw::ParticleWorld world;
        eaw::AttachedEffects effects;
        effects.set_rigs(damaged_rigs);
        eaw::VisualInstance unit{0};
        unit.state = state;
        const std::vector<eaw::VisualInstance> instances{unit};
        const std::vector<eaw::EntityId> ids{eaw::EntityId{1, 1}};
        effects.update(world, instances, ids, shown_palette);
        world.update(0.01F);
        std::vector<eaw::VisualParticle> sprites;
        world.build_sprites({0.0F, 0.0F, 0.0F}, sprites);
        return sprites.size();
    };
    require(sprites_in(0U) == 0U && sprites_in(1U) == 1U, "a damage state's effect must play only in that state");
}

// Model projectiles: drawn nose along the flight at their scale, between ticks; their particle
// systems start with the shot and stop when it ends.
void test_projectile_models()
{
    eaw::ProjectileVisualTable table;
    eaw::ProjectileVisual missile;
    missile.mesh = 4U;
    missile.scale = 2.0F;
    table.emplace(5U, missile);
    eaw::ProjectileVisual bolt;
    bolt.bolt = true;
    table.emplace(6U, bolt);
    auto burst = make_test_emitter();
    burst.particles_per_second = 30;
    burst.lifetime = 5.0F;
    eaw::ProjectileVisual spark;
    spark.effect = make_test_effect({burst}, false);
    table.emplace(7U, spark);

    // Flying along +X at 300 units/s, drawn half a tick (5 units at 30 Hz) behind its tick position.
    const eaw::ProjectileSnapshot shot{{100.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 300.0F, 11U};
    const auto transform = eaw::projectile_model_transform(shot, missile, 0.5F, 1.0F / 30.0F);
    // Row 1 is where the model's +Y axis goes: its -Y nose points along the flight, at scale 2.
    require(std::abs(transform.values[4] + 2.0F) < 1e-4F && std::abs(transform.values[5]) < 1e-4F && std::abs(transform.values[6]) < 1e-4F && std::abs(transform.values[12] - 95.0F) < 1e-3F,
            "a model projectile must face along its flight at its scale");
    const eaw::ProjectileSnapshot climbing{{0.0F, 0.0F, 0.0F}, {0.0F, 0.6F, 0.8F}, 1U, 5U, 0.0F, 12U};
    const auto up = eaw::projectile_model_transform(climbing, missile, 1.0F, 1.0F / 30.0F);
    require(std::abs(up.values[5] + 1.2F) < 1e-3F && std::abs(up.values[6] + 1.6F) < 1e-3F, "a climbing projectile must pitch its nose up");

    // Flight shapes: a lob 100 units at 100 units/s with g = 80 peaks g/2 (T/2)^2 = 10 high mid-way,
    // nose level there; a sway moves the shot sideways, the side by shot; a plain shot stays put.
    eaw::ProjectileVisual grenade;
    grenade.lob_gravity = 80.0F;
    const eaw::ProjectileSnapshot lobbed{{50.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 100.0F, 20U, 50.0F, 50.0F};
    const auto apex = eaw::projectile_pose(lobbed, grenade, 1.0F, 1.0F / 30.0F);
    require(std::abs(apex.position.z - 10.0F) < 1e-3F && std::abs(apex.direction.z) < 1e-4F, "a lob must peak mid-way, level there");
    const auto rising = eaw::projectile_pose({{25.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 100.0F, 20U, 25.0F, 75.0F}, grenade, 1.0F, 1.0F / 30.0F);
    require(rising.position.z > 0.0F && rising.direction.z > 0.0F, "a lob must climb in its first half");
    eaw::ProjectileVisual rocket;
    rocket.sway = 10.0F;
    rocket.sway_after = 20.0F;
    const auto left = eaw::projectile_pose({{60.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 100.0F, 20U, 60.0F, 40.0F}, rocket, 1.0F, 1.0F / 30.0F);
    const auto right = eaw::projectile_pose({{60.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 100.0F, 21U, 60.0F, 40.0F}, rocket, 1.0F, 1.0F / 30.0F);
    const auto early = eaw::projectile_pose({{10.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 100.0F, 21U, 10.0F, 90.0F}, rocket, 1.0F, 1.0F / 30.0F);
    require(std::abs(left.position.y - 10.0F) < 1e-3F && std::abs(right.position.y + 10.0F) < 1e-3F && early.position.y == 0.0F, "a rocket must sway after its straight run, the side by shot");
    const auto plain = eaw::projectile_pose(lobbed, missile, 1.0F, 1.0F / 30.0F);
    require(plain.position.x == 50.0F && plain.position.z == 0.0F && plain.direction.x == 1.0F, "a shot without a shape must fly straight");

    std::vector<eaw::ProjectileSnapshot> shots{shot, {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 6U, 300.0F, 12U}, {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 7U, 300.0F, 13U}};
    std::vector<eaw::VisualInstance> instances;
    std::vector<std::uint32_t> serials;
    eaw::append_projectile_instances(shots, table, 0.5F, 1.0F / 30.0F, instances, serials);
    require(instances.size() == 1 && instances[0].mesh == 4U && serials == std::vector<std::uint32_t>{11U}, "only mesh projectiles must become instances");

    eaw::BattleEffects effects;
    effects.reset({{}, &table, eaw::invalid_visual_texture, nullptr});
    effects.follow_projectiles({}, {}, {}, shots, 0.5F, 1.0F / 30.0F);
    require(effects.particles().stats().instances == 1, "a particle-model shot must start its effect");
    effects.update(0.1F);
    effects.follow_projectiles({}, {}, {}, shots, 0.5F, 1.0F / 30.0F);
    require(effects.particles().stats().instances == 1 && effects.particles().stats().particles > 0, "a particle-model shot must keep one effect while it flies");
    effects.follow_projectiles({}, {}, {}, {}, 0.5F, 1.0F / 30.0F);
    require(effects.particles().stats().instances == 0, "an ended shot's effect must stop");
}

void test_particle_world()
{
    // Three bursts of four, 0.5 s apart, two-second particles: the effect ends on its own.
    auto burst = make_test_emitter();
    burst.use_bursts = true;
    burst.particles_per_burst = 4;
    burst.burst_count = 3;
    burst.burst_delay = 0.5F;
    burst.lifetime = 2.0F;
    burst.groups[0].type = 0;
    burst.groups[0].value = {0.0F, 0.0F, 10.0F};
    eaw::ParticleWorld world;
    const auto effect = make_test_effect({burst});
    eaw::Matrix4 transform = eaw::Matrix4::identity();
    transform.values[12] = 100.0F;
    const auto id = world.play(effect, transform, 42);
    require(world.playing(id) && world.stats().particles == 0, "particle effect did not start");
    world.update(0.01F);
    require(world.stats().particles == 4, "first particle burst failed");
    for (int frame = 0; frame < 60; ++frame)
    {
        world.update(0.02F);
    }
    require(world.stats().particles == 12, "particle bursts did not accumulate");
    std::vector<eaw::VisualParticle> sprites;
    world.build_sprites({0.0F, 0.0F, 0.0F}, sprites);
    require(sprites.size() == 12 && sprites.front().texture == 7U && sprites.front().position[0] == 100.0F && sprites.front().position[2] > 0.0F, "particle sprites failed");
    require(sprites.front().half_size > 2.0F && sprites.front().half_size < 3.5F, "particle size track failed");
    {
        // A Scale_Factor sizes the particles and the effect's own space around its origin.
        eaw::ParticleWorld scaled;
        (void)scaled.play(effect, transform, 42, {}, 2.0F);
        scaled.update(0.01F);
        for (int frame = 0; frame < 60; ++frame)
        {
            scaled.update(0.02F);
        }
        std::vector<eaw::VisualParticle> doubled;
        scaled.build_sprites({0.0F, 0.0F, 0.0F}, doubled);
        require(doubled.size() == sprites.size() && std::abs(doubled.front().half_size - 2.0F * sprites.front().half_size) < 1e-3F &&
                    std::abs(doubled.front().position[2] - 2.0F * sprites.front().position[2]) < 1e-2F && doubled.front().position[0] == 100.0F,
                "scaled particle effect failed");
    }
    for (std::size_t index = 1; index < sprites.size(); ++index)
    {
        require(sprites[index - 1].position[2] >= sprites[index].position[2], "particle sprites are not sorted back to front");
    }
    for (int frame = 0; frame < 100; ++frame)
    {
        world.update(0.02F);
    }
    require(!world.playing(id) && world.stats().particles == 0 && world.stats().instances == 0, "finished particle effect was not released");

    // The same seed replays the same particles.
    auto spray = make_test_emitter();
    spray.particles_per_second = 50;
    spray.groups[0].type = 3;
    spray.groups[0].sphere_radius = 20.0F;
    spray.random_scale = 0.5F;
    const auto looping = make_test_effect({spray}, false);
    const auto positions = [&](const std::uint64_t seed)
    {
        eaw::ParticleWorld replay;
        replay.play(looping, eaw::Matrix4::identity(), seed);
        for (int frame = 0; frame < 30; ++frame)
        {
            replay.update(1.0F / 30.0F);
        }
        std::vector<eaw::VisualParticle> out;
        replay.build_sprites({0.0F, -100.0F, 0.0F}, out);
        std::vector<float> values;
        for (const auto& sprite : out)
        {
            values.insert(values.end(), {sprite.position[0], sprite.position[1], sprite.position[2], sprite.half_size});
        }
        return values;
    };
    require(positions(5) == positions(5) && positions(5) != positions(6), "particle seeds do not replay");

    // Looping effects run until stopped; without leave_particles they vanish at once.
    const auto loop = world.play(looping, eaw::Matrix4::identity(), 1);
    for (int frame = 0; frame < 120; ++frame)
    {
        world.update(1.0F / 30.0F);
    }
    require(world.playing(loop) && world.stats().particles > 40, "looping particle effect stopped");
    world.stop(loop);
    require(!world.playing(loop) && world.stats().particles == 0, "stopped particle effect left particles");

    // Capacity bounds the live particles and counts the rest.
    eaw::ParticleWorld small(10);
    small.play(looping, eaw::Matrix4::identity(), 3);
    for (int frame = 0; frame < 15; ++frame)
    {
        small.update(1.0F / 30.0F);
    }
    require(small.stats().particles == 10 && small.stats().dropped_particles > 0, "particle capacity failed");

    // A trail child emits from each living parent particle; a death child at each death.
    auto parent = make_test_emitter();
    parent.use_bursts = true;
    parent.particles_per_burst = 2;
    parent.burst_count = 1;
    parent.lifetime = 0.5F;
    parent.life_child = 1;
    parent.death_child = 2;
    auto trail = make_test_emitter();
    trail.particles_per_second = 10;
    trail.lifetime = 5.0F;
    auto debris = make_test_emitter();
    debris.use_bursts = true;
    debris.particles_per_burst = 3;
    debris.lifetime = 5.0F;
    eaw::ParticleWorld family;
    family.play(make_test_effect({parent, trail, debris}), eaw::Matrix4::identity(), 9);
    for (int frame = 0; frame < 30; ++frame)
    {
        family.update(1.0F / 30.0F);
    }
    // Two parents for 0.5 s at 10 per second leave about ten trail particles, then six debris.
    const auto count = family.stats().particles;
    require(count >= 14 && count <= 18, "particle child emitters failed: " + std::to_string(count));

    // A moving emitter leaves an even trail: each particle spawns where the emitter was at its
    // emission time, not where it is at the end of the update.
    auto exhaust = make_test_emitter();
    exhaust.particles_per_second = 100;
    exhaust.lifetime = 10.0F;
    eaw::ParticleWorld trail_world;
    auto moving = eaw::Matrix4::identity();
    const auto trail_id = trail_world.play(make_test_effect({exhaust}), moving, 4);
    for (int frame = 1; frame <= 10; ++frame)
    {
        moving.values[12] = 10.0F * static_cast<float>(frame);
        trail_world.set_transform(trail_id, moving);
        trail_world.update(0.1F);
    }
    std::vector<eaw::VisualParticle> trail_sprites;
    trail_world.build_sprites({0.0F, 0.0F, 100.0F}, trail_sprites);
    std::vector<float> xs;
    for (const auto& sprite : trail_sprites)
    {
        xs.push_back(sprite.position[0]);
    }
    std::ranges::sort(xs);
    require(xs.size() >= 95 && xs.front() < 1.5F && xs.back() > 98.5F, "particle trail does not cover the emitter's path");
    for (std::size_t index = 1; index < xs.size(); ++index)
    {
        require(xs[index] - xs[index - 1] < 1.5F, "particle trail has a gap at x = " + std::to_string(xs[index - 1]));
    }

    // Semantics from the GlyphX particle editor: lifetime variance only shortens, a positive stored
    // inward acceleration pushes outward along the spawn offset, and freeze time stops the emitter.
    {
        auto short_lived = make_test_emitter();
        short_lived.use_bursts = true;
        short_lived.burst_count = 1;
        short_lived.particles_per_burst = 50;
        short_lived.lifetime = 1.0F;
        short_lived.random_lifetime = 0.5F;
        eaw::ParticleWorld lifetimes;
        lifetimes.play(make_test_effect({short_lived}), eaw::Matrix4::identity(), 11);
        lifetimes.update(0.01F);
        require(lifetimes.stats().particles == 50, "lifetime test burst failed");
        for (int frame = 0; frame < 11; ++frame)
        {
            lifetimes.update(0.1F);
        }
        require(lifetimes.stats().particles == 0, "lifetime variance lengthened particles");

        auto pushed = make_test_emitter();
        pushed.use_bursts = true;
        pushed.burst_count = 1;
        pushed.lifetime = 10.0F;
        pushed.groups[2].type = 0;
        pushed.groups[2].value = {10.0F, 0.0F, 0.0F};
        pushed.inward_acceleration = 100.0F;
        eaw::ParticleWorld outward;
        outward.play(make_test_effect({pushed}), eaw::Matrix4::identity(), 12);
        outward.update(0.01F);
        for (int frame = 0; frame < 10; ++frame)
        {
            outward.update(0.05F);
        }
        std::vector<eaw::VisualParticle> pushed_sprites;
        outward.build_sprites({}, pushed_sprites);
        require(pushed_sprites.size() == 1 && pushed_sprites[0].position[0] > 20.0F && std::abs(pushed_sprites[0].position[1]) < 1e-4F, "inward acceleration did not push outward");

        auto frozen = make_test_emitter();
        frozen.particles_per_second = 10;
        frozen.lifetime = 10.0F;
        frozen.freeze_time = 0.5F;
        frozen.groups[0].type = 0;
        frozen.groups[0].value = {0.0F, 0.0F, 10.0F};
        eaw::ParticleWorld freezing;
        freezing.play(make_test_effect({frozen}), eaw::Matrix4::identity(), 13);
        for (int frame = 0; frame < 10; ++frame)
        {
            freezing.update(0.1F);
        }
        std::vector<eaw::VisualParticle> before;
        freezing.build_sprites({}, before);
        freezing.update(0.2F);
        std::vector<eaw::VisualParticle> after;
        freezing.build_sprites({}, after);
        require(before.size() == 6 && after.size() == before.size() && after.front().position == before.front().position, "freeze time did not stop the emitter");
    }

    // Intensity scales drawn size and opacity (engine glows at rest versus at full speed).
    {
        auto steady = make_test_emitter();
        steady.use_bursts = true;
        steady.burst_count = 1;
        steady.lifetime = 10.0F;
        eaw::ParticleWorld dimmed;
        const auto dim_id = dimmed.play(make_test_effect({steady}), eaw::Matrix4::identity(), 8);
        dimmed.update(0.01F);
        std::vector<eaw::VisualParticle> full;
        dimmed.build_sprites({}, full);
        dimmed.set_intensity(dim_id, 0.0F);
        std::vector<eaw::VisualParticle> idle;
        dimmed.build_sprites({}, idle);
        require(full.size() == 1 && idle.size() == 1 && std::abs(idle[0].half_size - full[0].half_size * 0.5F) < 1e-4F && std::abs(idle[0].color[3] - full[0].color[3] * 0.15F) < 1e-4F,
                "particle intensity did not scale size and opacity");
    }

    // Particles linked to the system turn with their emitter under every profile (an engine glow on
    // a turning ship).
    auto glow = make_test_emitter();
    glow.use_bursts = true;
    glow.burst_count = 1;
    glow.link_to_system = true;
    glow.lifetime = 10.0F;
    glow.groups[2].type = 0;
    glow.groups[2].value = {10.0F, 0.0F, 0.0F};
    const auto turn = [&](const eaw::ParticleFidelity fidelity)
    {
        eaw::ParticleWorld turning;
        const auto glow_id = turning.play(make_test_effect({glow}, true, fidelity), eaw::Matrix4::identity(), 3);
        turning.update(0.01F);
        // A quarter turn about +Z: X goes to Y.
        eaw::Matrix4 turned = eaw::Matrix4::identity();
        turned.values[0] = 0.0F;
        turned.values[1] = 1.0F;
        turned.values[4] = -1.0F;
        turned.values[5] = 0.0F;
        turning.set_transform(glow_id, turned);
        turning.update(0.01F);
        std::vector<eaw::VisualParticle> turned_sprites;
        turning.build_sprites({0.0F, 0.0F, 100.0F}, turned_sprites);
        require(turned_sprites.size() == 1, "linked particle test lost its particle");
        return turned_sprites.front().position;
    };
    const auto rigid = turn(eaw::ParticleFidelity::enhanced);
    require(std::abs(rigid[0]) < 1e-4F && std::abs(rigid[1] - 10.0F) < 1e-4F, "enhanced linked particle did not turn with its emitter");
    const auto legacy_turned = turn(eaw::ParticleFidelity::legacy);
    require(std::abs(legacy_turned[0]) < 1e-4F && std::abs(legacy_turned[1] - 10.0F) < 1e-4F, "legacy linked particle did not turn with its emitter");

    // Additive blending: the original ignores alpha, the enhanced profile honours it.
    {
        const auto legacy = eaw::normalize_particle_emitter(make_test_emitter(), eaw::ParticleFidelity::legacy);
        const auto enhanced = eaw::normalize_particle_emitter(make_test_emitter(), eaw::ParticleFidelity::enhanced);
        require(legacy.blend == eaw::VisualParticleBlend::additive && enhanced.blend == eaw::VisualParticleBlend::additive_alpha, "additive interpretation failed");
        require(eaw::particle_interpretation(eaw::ParticleFidelity::legacy).draw_order == eaw::ParticleDrawOrder::emitter_list &&
                    eaw::particle_interpretation(eaw::ParticleFidelity::enhanced).draw_order == eaw::ParticleDrawOrder::back_to_front,
                "draw order interpretation failed");
    }

    // Emitters that emit from a mesh spawn on it: every vertex in turn, pushed out along the normal.
    auto nozzles = make_test_emitter();
    nozzles.use_bursts = true;
    nozzles.particles_per_burst = 1;
    nozzles.burst_count = 1;
    nozzles.emit_from_mesh = 3;
    nozzles.emit_from_mesh_offset = 2.0F;
    auto emit_mesh = std::make_shared<eaw::ParticleEmitMesh>();
    emit_mesh->positions = {{-50.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {50.0F, 0.0F, 0.0F}};
    emit_mesh->normals = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    emit_mesh->indices = {0, 1, 2};
    eaw::ParticleWorld engines;
    engines.play(make_test_effect({nozzles}), eaw::Matrix4::identity(), 2, emit_mesh);
    engines.update(0.01F);
    std::vector<eaw::VisualParticle> glows;
    engines.build_sprites({0.0F, 0.0F, 100.0F}, glows);
    std::vector<float> glow_x;
    for (const auto& sprite : glows)
    {
        glow_x.push_back(sprite.position[0]);
        require(std::abs(sprite.position[1] - 2.0F) < 1e-4F, "mesh emission ignored the normal offset");
    }
    std::ranges::sort(glow_x);
    require(glow_x == std::vector<float>{-50.0F, 0.0F, 50.0F}, "every-vertex mesh emission failed");

    // Heat particles have no pass yet: they are skipped, not drawn.
    auto heat = make_test_emitter();
    heat.heat = true;
    eaw::ParticleWorld hot;
    hot.play(make_test_effect({heat}), eaw::Matrix4::identity(), 1);
    hot.update(0.1F);
    std::vector<eaw::VisualParticle> none;
    hot.build_sprites({}, none);
    require(none.empty() && hot.stats().skipped_emitters == 1, "heat particles were drawn");
}

// XML Particle objects resolve to their effect and Scale_Factor; Death_Explosions entries that name
// a Projectile play its detonation particle; objects that cannot play are kept and reported.
void test_effect_objects()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-effect-objects-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "XML");
    std::filesystem::create_directories(root / "Data" / "Art" / "Models");
    const auto write = [&](const std::filesystem::path& path, const std::vector<std::byte>& bytes)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    write(root / "Data" / "Art" / "Models" / "P_TEST.ALO", make_test_particle_system(-1));
    write(root / "Data" / "Art" / "Models" / "W_MESH.ALO", make_test_alo());
    {
        std::ofstream file(root / "Data" / "XML" / "Particles.xml", std::ios::binary);
        file << "<Particles>"
                "<Particle Name=\"Boom\"><Space_Model_Name>p_test.alo</Space_Model_Name><Scale_Factor>2</Scale_Factor></Particle>"
                "<Particle Name=\"Land_Boom\"><Land_Model_Name>p_test.alo</Land_Model_Name><Explosion_Scorch_Mark>Scorch_Mark</Explosion_Scorch_Mark></Particle>"
                "<Particle Name=\"Empty\"><Scale_Factor>1</Scale_Factor></Particle>"
                "<Particle Name=\"Mesh_Boom\"><Space_Model_Name>w_mesh.alo</Space_Model_Name></Particle>"
                "<Particle Name=\"Bad_Scale\"><Space_Model_Name>p_test.alo</Space_Model_Name><Scale_Factor>-1</Scale_Factor></Particle>"
                "</Particles>";
    }
    {
        std::ofstream file(root / "Data" / "XML" / "Projectiles.xml", std::ios::binary);
        file << "<Projectiles>"
                "<Projectile Name=\"Mine_Blast\"><Projectile_Max_Flight_Distance>0</Projectile_Max_Flight_Distance>"
                "<Projectile_Object_Detonation_Particle>Land_Boom</Projectile_Object_Detonation_Particle>"
                "<Projectile_Lifetime_Detonation_Particle>Boom</Projectile_Lifetime_Detonation_Particle></Projectile>"
                "<Projectile Name=\"Dud\"><Projectile_Max_Flight_Distance>0</Projectile_Max_Flight_Distance></Projectile>"
                "</Projectiles>";
    }
    write_file(root / "Data" / "XML" / "SurfaceFX.xml",
               "<SurfaceEffects><SurfaceFX name=\"Scorch_Mark\"><Surface_Settings name=\"default\"><Decal_Name>Scorch</Decal_Name></Surface_Settings></SurfaceFX></SurfaceEffects>");
    write_file(root / "Data" / "XML" / "TerrainDecalFX.xml",
               "<TerrainDecals><Decal Name=\"Scorch\"><Render_Mode>DIFFUSE_ALPHA</Render_Mode><Scale>10, 10</Scale><UV_Slot>0,3</UV_Slot></Decal></TerrainDecals>");
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);
    eaw::VisualScene scene;
    eaw::scene_build::SceneBuilder builder{vfs, scene};
    const auto key = [](const std::string_view name)
    {
        return eaw::game_object_name_crc(name);
    };
    const std::vector<std::uint32_t> keys{key("Boom"), key("Land_Boom"), key("Empty"), key("Mesh_Boom"), key("Bad_Scale"), key("Mine_Blast"), key("Dud"), key("Undefined"), 0U};
    eaw::ParticleEffectObjectTable table;
    std::uint32_t decal_atlas = eaw::invalid_visual_texture;
    eaw::scene_build::load_effect_objects(builder, catalog, database, keys, false, table, decal_atlas);
    const auto warned = [&](const std::string_view text)
    {
        return std::ranges::count_if(scene.warnings,
                                     [&](const std::string& warning)
                                     {
                                         return warning.find(text) != std::string::npos;
                                     });
    };
    require(table.size() == 8, "every named effect object must have an entry");
    const auto& boom = table.at(key("Boom"));
    require(boom.effect && boom.effect->emitters.size() == 2 && boom.scale == 2.0F, "a Particle object must resolve to its effect and Scale_Factor");
    require(table.at(key("Land_Boom")).effect && table.at(key("Land_Boom")).scale == 1.0F, "a space map must fall back to the land model");
    const auto& mine = table.at(key("Mine_Blast"));
    require(mine.effect == boom.effect && mine.scale == 2.0F, "a projectile named as an explosion must play its lifetime detonation Particle object");
    require(!table.at(key("Empty")).effect && warned("Empty: Particle object names no") == 1, "a Particle object without a model must be reported");
    require(!table.at(key("Mesh_Boom")).effect && warned("Mesh_Boom: Particle object model w_mesh.alo is a mesh") == 1, "a Particle object naming a mesh must be reported");
    require(!table.at(key("Dud")).effect && warned("Dud: projectile named as an explosion has no detonation") == 1, "a projectile without a detonation particle must be reported");
    require(table.at(key("Bad_Scale")).effect && table.at(key("Bad_Scale")).scale == 1.0F && warned("Bad_Scale: Scale_Factor '-1'") == 1, "a bad Scale_Factor must be reported");
    require(!table.at(key("Undefined")).effect && warned("is not defined") == 1, "an undefined effect object must be reported");
    require(!table.at(key("Land_Boom")).scorch && decal_atlas == eaw::invalid_visual_texture, "a space map must resolve no scorch marks");
    // On land the scorch mark resolves through SurfaceFX to its decal, and the atlas is asked for.
    eaw::ParticleEffectObjectTable land_table;
    eaw::scene_build::load_effect_objects(builder, catalog, database, std::vector<std::uint32_t>{key("Land_Boom")}, true, land_table, decal_atlas);
    const auto& scorched = land_table.at(key("Land_Boom"));
    require(scorched.scorch && scorched.scorch->name == "Scorch" && scorched.scorch->width == 10.0F && scorched.scorch->cells.size() == 1,
            "a land explosion must carry the decal its scorch mark names");
    std::filesystem::remove_all(root, ignored);
}

// Scorch marks: an explosion with one leaves it on the ground below when it plays near the ground
// on land (not high in the air, not without a ground), at the decal's size and cell, and it fades
// out after its fade time and goes.
void test_scorch_marks()
{
    auto burst = make_test_emitter();
    burst.lifetime = 1.0F;
    eaw::ParticleEffectObjectTable objects;
    eaw::TerrainDecal scorch;
    scorch.name = "Scorch";
    scorch.width = 10.0F;
    scorch.height = 20.0F;
    scorch.intensity = 0.5F;
    scorch.fade_seconds = 5.0F;
    scorch.cells = {{1U, 3U}};
    objects.emplace(7U, eaw::ParticleEffectObject{make_test_effect({burst}), 1.0F, scorch});
    eaw::BattleEffects effects;
    effects.reset({{}, nullptr, eaw::invalid_visual_texture, &objects, 3U});
    std::vector<eaw::VisualDecal> decals;
    effects.play_object(7U, {0.0F, 0.0F, 0.0F});
    require(effects.decal_count() == 0, "without a ground there must be no scorch");
    effects.set_ground(
        [](float, float)
        {
            return std::optional<float>(10.0F);
        });
    effects.play_object(7U, {100.0F, 50.0F, 500.0F});
    require(effects.decal_count() == 0, "an explosion high in the air must leave no scorch");
    effects.play_object(7U, {100.0F, 50.0F, 12.0F});
    effects.build_decals(decals);
    require(decals.size() == 1 && decals[0].center == eaw::Vector2{100.0F, 50.0F} && decals[0].half_size == eaw::Vector2{5.0F, 10.0F} && decals[0].texture == 3U && decals[0].uv_rect[0] == 0.25F &&
                decals[0].uv_rect[1] == 0.75F && decals[0].uv_rect[2] == 0.25F && decals[0].opacity == 0.5F && decals[0].mode == eaw::VisualDecalMode::diffuse,
            "a scorch must lie where the explosion played, at its decal's size, cell, and opacity");
    effects.update(6.0F);
    decals.clear();
    effects.build_decals(decals);
    require(decals.size() == 1 && decals[0].opacity > 0.0F && decals[0].opacity < 0.5F, "a scorch must fade once its fade time has passed");
    effects.update(2.0F);
    require(effects.decal_count() == 0, "a faded scorch must go");
}

void test_projectile_sprites()
{
    // A bolt sits between ticks like entities, its head leading, and only typed shots are drawn.
    eaw::ProjectileVisualTable table;
    eaw::ProjectileVisual bolt;
    bolt.width = 1.0F;
    bolt.length = 6.0F;
    bolt.column = 3;
    bolt.tinted = true;
    bolt.color = {0.4F, 0.7F, 0.9F, 1.0F};
    bolt.bolt = true;
    bolt.impact_effects = {11U, 12U, 0U, 14U};
    table.emplace(5U, bolt);
    // A projectile without a custom render keeps its impacts but draws no bolt.
    eaw::ProjectileVisual model_shot;
    model_shot.impact_effects = {0U, 22U, 0U, 0U};
    table.emplace(7U, model_shot);
    std::vector<eaw::ProjectileSnapshot> shots{
        {{100.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 300.0F}, {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1U, 6U, 300.0F}, {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1U, 7U, 300.0F}};
    std::vector<eaw::VisualParticle> sprites;
    eaw::append_projectile_sprites(shots, table, 9U, 0.5F, 1.0F / 30.0F, sprites);
    require(sprites.size() == 1, "untyped shots were drawn as bolts");
    const auto& sprite = sprites.front();
    // Half a tick behind (300 / 30 / 2 = 5) plus half the bolt's length (3).
    require(std::abs(sprite.position[0] - 92.0F) < 1e-4F && sprite.tail == 6.0F && sprite.half_size == 0.5F, "bolt placement failed");
    require(sprite.shape == eaw::VisualParticleShape::teardrop && sprite.texture == 9U && sprite.uv_rect[0] == 0.75F && sprite.uv_rect[2] == 0.25F && sprite.color[2] == 0.9F,
            "bolt appearance failed");
    require(eaw::impact_effect(bolt, eaw::ImpactKind::shield) == 11U && eaw::impact_effect(bolt, eaw::ImpactKind::hull) == 12U && eaw::impact_effect(bolt, eaw::ImpactKind::hull_reduced) == 0U &&
                eaw::impact_effect(bolt, eaw::ImpactKind::expired) == 14U,
            "impact effects by kind failed");
    std::vector<std::uint32_t> keys;
    eaw::collect_impact_effect_keys(table, keys);
    std::ranges::sort(keys);
    require(keys == std::vector<std::uint32_t>{11U, 12U, 14U, 22U}, "impact effect keys failed");
}

// Impacts and deaths play the Particle objects their projectile and type name, scaled; bolts draw
// only once an atlas is known; a reset stops everything.
void test_battle_effects()
{
    auto burst = make_test_emitter();
    burst.use_bursts = true;
    burst.particles_per_burst = 3;
    burst.burst_count = 1;
    burst.lifetime = 1.0F;
    eaw::ParticleEffectObjectTable objects;
    objects.emplace(21U, eaw::ParticleEffectObject{make_test_effect({burst}), 2.0F});
    objects.emplace(22U, eaw::ParticleEffectObject{make_test_effect({burst}), 1.0F});
    objects.emplace(23U, eaw::ParticleEffectObject{});
    eaw::ProjectileVisualTable visuals;
    eaw::ProjectileVisual laser;
    laser.bolt = true;
    laser.impact_effects = {0U, 21U, 0U, 23U};
    visuals.emplace(5U, laser);

    eaw::BattleEffects effects;
    effects.reset({{}, &visuals, eaw::invalid_visual_texture, &objects});
    require(effects.effect_objects() == 3 && effects.playable_effect_objects() == 2 && effects.bolts() == nullptr, "battle effect content failed");

    eaw::SimSnapshot snapshot;
    snapshot.impacts.push_back({{10.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 5U, eaw::ImpactKind::hull});
    snapshot.impacts.push_back({{20.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 5U, eaw::ImpactKind::shield});
    snapshot.impacts.push_back({{30.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 5U, eaw::ImpactKind::expired});
    snapshot.impacts.push_back({{40.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 9U, eaw::ImpactKind::hull});
    eaw::DeathSnapshot death;
    death.position = {50.0F, 0.0F, 0.0F};
    death.death_effect = 22U;
    snapshot.deaths.push_back(death);
    effects.play_tick_events(snapshot);
    // The hull impact and the death play; the shield impact names nothing, the expired one an object
    // without an effect, and projectile 9 is unknown.
    require(effects.particles().stats().instances == 2, "impacts and deaths did not play their effects");
    effects.update(0.01F);
    std::vector<eaw::VisualParticle> sprites;
    std::vector<eaw::ProjectileSnapshot> shots{{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 300.0F}};
    effects.build_sprites({0.0F, -100.0F, 0.0F}, shots, 0.5F, 1.0F / 30.0F, sprites);
    require(sprites.size() == 6, "battle effect sprites failed");
    float impact_size = 0.0F;
    float death_size = 0.0F;
    for (const auto& sprite : sprites)
    {
        (sprite.position[0] < 30.0F ? impact_size : death_size) = sprite.half_size;
    }
    require(std::abs(impact_size - 2.0F * death_size) < 1e-4F, "an effect object's Scale_Factor did not size its effect");

    // With an atlas, shots in flight draw as bolts after the particles.
    effects.add({{}, nullptr, 9U, nullptr});
    sprites.clear();
    effects.build_sprites({0.0F, -100.0F, 0.0F}, shots, 0.5F, 1.0F / 30.0F, sprites);
    require(effects.bolts() != nullptr && sprites.size() == 7 && sprites.back().texture == 9U, "bolts did not join the battle effects");

    effects.reset({});
    require(effects.particles().stats().instances == 0 && effects.effect_objects() == 0 && effects.bolts() == nullptr, "a battle effect reset kept effects");
}

// Terrain decals: modes, sizes, angles, fades, and atlas cells as written (a span limiting which
// cells fit, cells outside the atlas dropped with a warning), found by name ignoring case.
void test_terrain_decals()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-terrain-decals-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    write_file(root / "Data" / "XML" / "TerrainDecalFX.xml",
               "<TerrainDecals>"
               "<Decal Name=\"Foot\"><Render_Mode>DECAL_BUMP_ALPHA</Render_Mode><Intensity>0.65</Intensity><Scale>25, 20</Scale><UV_Slot>1,0</UV_Slot>"
               "<Fadeout_Time>40</Fadeout_Time><Permanent>false</Permanent></Decal>"
               "<Decal Name=\"Scorch\"><Render_Mode>DIFFUSE_ALPHA</Render_Mode><Intensity>.33</Intensity><Scale>10, 10</Scale><Z_Angle>90</Z_Angle>"
               "<UV_Slot>0,3,   1,3</UV_Slot><Random_Size_Offset>0.1</Random_Size_Offset><Random_Rotate>Yes</Random_Rotate><Permanent>true</Permanent></Decal>"
               "<Decal Name=\"Massive\"><Render_Mode>DECAL_BUMP_ALPHA</Render_Mode><Scale>50, 50</Scale><UV_Slot>2,1, 3,1</UV_Slot><Slot_Scale>2</Slot_Scale></Decal>"
               "<Decal Name=\"Off_Atlas\"><Render_Mode>GLOWING</Render_Mode><UV_Slot>0,7</UV_Slot></Decal>"
               "</TerrainDecals>");
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    std::vector<std::string> warnings;
    const auto decals = eaw::TerrainDecalCatalog::build(eaw::XmlAssetDatabase::build(vfs), &warnings);
    const auto* foot = decals.find("FOOT");
    const auto* scorch = decals.find("scorch");
    const auto* massive = decals.find("Massive");
    const auto* off = decals.find("Off_Atlas");
    require(decals.size() == 4 && foot != nullptr && scorch != nullptr && massive != nullptr && off != nullptr && decals.find("missing") == nullptr, "decals must be found by name");
    require(foot->mode == eaw::TerrainDecalMode::bump_alpha && foot->intensity == 0.65F && foot->width == 25.0F && foot->height == 20.0F && foot->fade_seconds == 40.0F && !foot->permanent &&
                foot->cells.size() == 1 && foot->cells[0].column == 1U && foot->cells[0].row == 0U,
            "a footprint must keep its mode, opacity, size, fade, and cell");
    require(scorch->mode == eaw::TerrainDecalMode::diffuse_alpha && std::fabs(scorch->angle - 1.5707964F) < 1.0e-5F && scorch->random_rotate && scorch->size_variation == 0.1F && scorch->permanent &&
                scorch->cells.size() == 2 && scorch->cells[1].column == 1U && scorch->cells[1].row == 3U,
            "a scorch must keep its angle, randomness, permanence, and both cells");
    require(massive->cell_span == 2U && massive->cells.size() == 1 && massive->cells[0].column == 2U, "a two-cell span must keep only the cells it fits from");
    require(off->cells.empty() && warnings.size() == 3, "a cell outside the atlas and an unknown mode must be reported");
    std::filesystem::remove_all(root, ignored);
}

const ModuleRegistrar registrar("effects", {
                                               {"test_particle_tracks", &test_particle_tracks},
                                               {"test_particle_world", &test_particle_world},
                                               {"test_effect_objects", &test_effect_objects},
                                               {"test_projectile_sprites", &test_projectile_sprites},
                                               {"test_battle_effects", &test_battle_effects},
                                               {"test_projectile_models", &test_projectile_models},
                                               {"test_attached_effect_scale", &test_attached_effect_scale},
                                               {"test_terrain_decals", &test_terrain_decals},
                                               {"test_scorch_marks", &test_scorch_marks},
                                           });

} // namespace
