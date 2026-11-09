// The simulation core: ids, components, commands, ticks, and what snapshots report.
#include "test_support.hpp"

#include "eaw/formation.hpp"

#include <tuple>

#include <memory>
#include <numbers>

namespace {

using namespace eaw_test;

void test_simulation_core()
{
    // Movement: 3 units/s at 10 ticks/s reaches a point 1 unit away in four ticks (0.3 per tick).
    eaw::SimulationWorld world(0.1);
    const auto mover = expect_ok(world.spawn({.type_name = "Mover", .owner = 1, .position = {0.0F, 0.0F, 0.0F}, .max_speed = 3.0F, .visual = 7}), "spawning the mover");
    const auto statue = expect_ok(world.spawn({.type_name = "Statue", .position = {5.0F, 5.0F, 0.0F}}), "spawning the statue");
    world.submit({eaw::SimCommandKind::move, {mover, statue}, {1.0F, 0.0F, 0.0F}});
    world.step();
    const auto& first = world.snapshot();
    require(first.tick == 1 && first.entities.size() == 2 && first.entities[0].id == mover && std::fabs(first.entities[0].position.x - 0.3F) < 1.0e-5F && first.entities[0].moving &&
                first.entities[0].visual == 7 && first.entities[0].owner == 1,
            "a moving entity must advance by speed times tick length");
    require(!first.entities[1].moving && first.entities[1].position == eaw::SimVector3{5.0F, 5.0F, 0.0F}, "an entity without speed must ignore move commands");
    for (int tick = 0; tick < 3; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[0].position == eaw::SimVector3{1.0F, 0.0F, 0.0F} && !world.snapshot().entities[0].moving, "an entity must stop exactly at its target");

    // Destruction: ids are generation-checked, stale references are counted, slots are reused.
    world.submit({eaw::SimCommandKind::destroy, {mover}, {}});
    world.step();
    require(!world.alive(mover) && world.alive(statue) && world.snapshot().entities.size() == 1, "a destroyed entity must leave the world");
    world.submit({eaw::SimCommandKind::move, {mover}, {2.0F, 0.0F, 0.0F}});
    world.step();
    require(world.stats().stale_references == 1, "a command naming a destroyed entity must count as stale");
    const auto reused = expect_ok(world.spawn({.type_name = "Mover", .max_speed = 1.0F}), "spawning into a reused slot");
    require(reused.index == mover.index && reused.generation != mover.generation && !world.alive(mover) && world.alive(reused), "a reused slot must not revive the old id");

    // Determinism: identical histories give identical state; a different command does not.
    const auto run = [](const float target_x)
    {
        eaw::SimulationWorld sim(1.0 / 30.0);
        std::vector<eaw::EntityId> ids;
        for (int index = 0; index < 20; ++index)
        {
            ids.push_back(
                expect_ok(sim.spawn({.type_name = "Unit", .position = {static_cast<float>(index), 0.0F, 0.0F}, .heading = 0.1F * static_cast<float>(index), .max_speed = 12.5F}), "spawning a unit"));
        }
        sim.submit({eaw::SimCommandKind::move, {ids.begin(), ids.begin() + 10}, {target_x, 40.0F, 0.0F}});
        for (int tick = 0; tick < 45; ++tick)
        {
            if (tick == 20)
            {
                sim.submit({eaw::SimCommandKind::destroy, {ids[3], ids[15]}, {}});
                sim.submit({eaw::SimCommandKind::stop, {ids[4]}, {}});
            }
            sim.step();
        }
        return sim.state_hash();
    };
    require(run(25.0F) == run(25.0F) && run(25.0F) != run(26.0F), "identical command histories must produce identical state");

    // Clock: whole ticks, a capped backlog, and an interpolation fraction.
    eaw::FixedStepClock clock(10.0, 4);
    require(clock.advance(0.25) == 2 && std::fabs(clock.interpolation() - 0.5F) < 1.0e-4F, "the clock must return whole ticks and the remaining fraction");
    require(clock.advance(10.0) == 4 && clock.dropped_ticks() > 90, "a long stall must be capped and the excess dropped");

    // Interpolation blends matching entities and keeps new ones at their current state.
    eaw::SimSnapshot before{1, {{{1, 1}, {0.0F, 0.0F, 0.0F}, 0.0F}}};
    eaw::SimSnapshot after{2, {{{1, 1}, {10.0F, 0.0F, 0.0F}, 1.0F}, {{2, 1}, {4.0F, 4.0F, 0.0F}, 0.0F}}};
    const auto blended = eaw::interpolate_snapshots(before, after, 0.25F);
    require(blended.size() == 2 && std::fabs(blended[0].position.x - 2.5F) < 1.0e-5F && std::fabs(blended[0].heading - 0.25F) < 1.0e-5F && blended[1].position.x == 4.0F,
            "snapshot interpolation must blend matching entities only");

    const auto entities_before = world.stats().entities;
    const auto rejected = world.spawn({.type_name = "Bad", .position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}});
    require(!rejected && rejected.error().message.find("'Bad'") != std::string::npos && world.stats().entities == entities_before, "a non-finite spawn must be rejected");
}

// Snapshots report how fast each entity actually moved in the last tick.
void test_snapshot_speed()
{
    eaw::SimulationWorld world(0.1);
    const auto walker = world.spawn({.type_name = "Walker", .max_speed = 10.0F});
    const auto fighter = world.spawn({.type_name = "Fighter", .position = {0.0F, 500.0F, 0.0F}, .max_speed = 120.0F, .turn_rate = 1.5F, .min_speed = 75.0F});
    require(walker.ok() && fighter.ok(), "test spawns must succeed");
    world.step();
    require(world.snapshot().entities[0].speed == 0.0F && world.snapshot().entities[1].speed >= 74.9F, "an idle unit must report no speed and a fighter at least its minimum");
    world.submit({eaw::SimCommandKind::move, {*walker}, {5.0F, 0.0F, 0.0F}});
    world.step();
    require(std::fabs(world.snapshot().entities[0].speed - 10.0F) < 1.0e-3F, "a moving unit must report its speed");
    for (int tick = 0; tick < 10; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[0].speed == 0.0F, "an arrived unit must report no speed");
    const auto blended = eaw::interpolate_snapshots(world.snapshot(), world.snapshot(), 0.5F);
    require(blended[1].speed == world.snapshot().entities[1].speed, "interpolation must carry the speed");
}

// Snapshots list the entities combat destroyed in their tick, and only then.
void test_death_snapshots()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::SimulationWorld world(0.1);
    world.set_hostility(hostility);
    eaw::CombatProfile gun;
    gun.health = 100.0F;
    gun.weapons = {eaw::WeaponProfile{.range = 500.0F, .recharge_seconds = 0.1F, .damage = 50.0F}};
    gun.weapons[0].projectile_visual = 9U;
    eaw::CombatProfile frail;
    frail.health = 40.0F;
    frail.death_effect = 77U;
    (void)world.spawn({.type_name = "Gun", .owner = 1, .combat = gun});
    const auto victim = world.spawn({.type_name = "Victim", .owner = 2, .position = {100.0F, 20.0F, 0.0F}, .heading = 1.0F, .combat = frail});
    const auto bystander = world.spawn({.type_name = "Bystander", .owner = 1, .position = {-50.0F, 0.0F, 0.0F}});
    require(victim.ok() && bystander.ok(), "test spawns must succeed");
    std::size_t deaths = 0;
    std::size_t hull_impacts = 0;
    for (int tick = 0; tick < 10 && world.alive(*victim); ++tick)
    {
        world.step();
        deaths += world.snapshot().deaths.size();
        for (const auto& impact : world.snapshot().impacts)
        {
            hull_impacts += impact.visual == 9U && impact.kind == eaw::ImpactKind::hull && impact.position.x == 100.0F ? 1U : 0U;
        }
    }
    require(hull_impacts >= 1, "hits must appear as impacts in their tick's snapshot");
    require(!world.alive(*victim) && deaths == 1 && world.snapshot().deaths.size() == 1, "a kill must appear in its tick's snapshot");
    const auto& death = world.snapshot().deaths.front();
    require(death.id == *victim && death.owner == 2 && death.position.x == 100.0F && death.heading == 1.0F, "a death must say who died where, facing which way");
    require(death.death_effect == 77U, "a death must carry its type's death effect");
    world.step();
    require(world.snapshot().deaths.empty() && world.snapshot().impacts.empty(), "deaths and impacts must only appear in their own tick");
    world.submit({eaw::SimCommandKind::destroy, {*bystander}});
    world.step();
    require(world.snapshot().deaths.empty() && !world.alive(*bystander), "a destroy command is not a death");
    // The gun fired in the tick the victim died; the bystander never fired.
    const auto& gun_snapshot = world.snapshot().entities.front();
    require(gun_snapshot.last_shot_tick > 0 && gun_snapshot.last_shot_tick < world.snapshot().tick, "a shooter must report the tick of its last shot");
}

const ModuleRegistrar registrar("simulation", {
                                                  {"test_simulation_core", &test_simulation_core},
                                                  {"test_snapshot_speed", &test_snapshot_speed},
                                                  {"test_death_snapshots", &test_death_snapshots},
                                              });

} // namespace
