// The simulation core: ids, components, commands, ticks, and what snapshots report.
#include "test_support.hpp"

#include "simulation/component_pool.hpp"
#include "simulation/formation.hpp"

namespace {

using namespace eaw_test;

void test_simulation_core()
{
    // Movement: 3 units/s at 10 ticks/s reaches a point 1 unit away in four ticks (0.3 per tick).
    eaw::SimulationWorld world(0.1);
    const auto mover = expect_ok(test_spawn(world, {.type_name = "Mover", .owner = 1, .position = {0.0F, 0.0F, 0.0F}, .max_speed = 3.0F, .visual = 7}), "spawning the mover");
    const auto statue = expect_ok(test_spawn(world, {.type_name = "Statue", .position = {5.0F, 5.0F, 0.0F}}), "spawning the statue");
    world.submit({eaw::SimCommandKind::move, {mover, statue}, {1.0F, 0.0F, 0.0F}});
    world.step();
    const auto& first = world.snapshot();
    require(first.tick == 1 && first.entities.size() == 2 && first.entities[0].id == mover && std::fabs(first.entities[0].position.x - 0.3F) < 1.0e-5F && first.entities[0].moving &&
                first.entities[0].visual == 7 && first.entities[0].owner == 1,
            "a moving entity must advance by speed times tick length");
    require(!first.entities[1].moving && first.entities[1].position == eaw::Vector3{5.0F, 5.0F, 0.0F}, "an entity without speed must ignore move commands");
    for (int tick = 0; tick < 3; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[0].position == eaw::Vector3{1.0F, 0.0F, 0.0F} && !world.snapshot().entities[0].moving, "an entity must stop exactly at its target");

    // Destruction: ids are generation-checked, stale references are counted, slots are reused.
    world.submit({eaw::SimCommandKind::destroy, {mover}, {}});
    world.step();
    require(!world.alive(mover) && world.alive(statue) && world.snapshot().entities.size() == 1, "a destroyed entity must leave the world");
    world.submit({eaw::SimCommandKind::move, {mover}, {2.0F, 0.0F, 0.0F}});
    world.step();
    require(world.stats().stale_references == 1, "a command naming a destroyed entity must count as stale");
    const auto reused = expect_ok(test_spawn(world, {.type_name = "Mover", .max_speed = 1.0F}), "spawning into a reused slot");
    require(reused.index == mover.index && reused.generation != mover.generation && !world.alive(mover) && world.alive(reused), "a reused slot must not revive the old id");

    // Determinism: identical histories give identical state; a different command does not.
    const auto run = [](const float target_x)
    {
        eaw::SimulationWorld sim(1.0 / 30.0);
        std::vector<eaw::EntityId> ids;
        for (int index = 0; index < 20; ++index)
        {
            ids.push_back(expect_ok(test_spawn(sim, {.type_name = "Unit", .position = {static_cast<float>(index), 0.0F, 0.0F}, .heading = 0.1F * static_cast<float>(index), .max_speed = 12.5F}),
                                    "spawning a unit"));
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

    // Subject hashes name the part of the state that differs: a stance is combat state only.
    const auto subjects = [](const bool hold_fire)
    {
        eaw::SimulationWorld sim(1.0 / 30.0);
        const auto id = expect_ok(test_spawn(sim, {.type_name = "Unit", .position = {0.0F, 0.0F, 0.0F}, .max_speed = 10.0F}), "spawning a unit");
        if (hold_fire)
        {
            sim.submit({.kind = eaw::SimCommandKind::set_stance, .entities = {id}, .stance = eaw::Stance::hold_fire});
        }
        sim.step();
        return std::pair{sim.state_hash(), sim.state_hashes()};
    };
    const auto [plain_total, plain] = subjects(false);
    const auto [held_total, held] = subjects(true);
    const auto at = [](const eaw::StateHashes& hashes, const eaw::StateSubject subject)
    {
        return hashes[static_cast<std::size_t>(subject)];
    };
    require(plain == subjects(false).second && plain_total != held_total && at(plain, eaw::StateSubject::combat) != at(held, eaw::StateSubject::combat) &&
                at(plain, eaw::StateSubject::bodies) == at(held, eaw::StateSubject::bodies) && at(plain, eaw::StateSubject::routes) == at(held, eaw::StateSubject::routes) &&
                eaw::state_subject_names[static_cast<std::size_t>(eaw::StateSubject::combat)] == "combat",
            "subject hashes must agree on equal state and differ only where the state does");

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
    const auto rejected = test_spawn(world, {.type_name = "Bad", .position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}});
    require(!rejected && rejected.error().message.find("'Bad'") != std::string::npos && world.stats().entities == entities_before, "a non-finite spawn must be rejected");

    // Recipes: an equal recipe is the same one, a variant its own; a bad recipe is refused when it is
    // registered, and a spawn must name a registered recipe. Entities of one recipe share it.
    eaw::SimulationWorld shared(1.0 / 30.0);
    const auto tank = expect_ok(shared.add_recipe({.type_name = "Tank", .max_speed = 10.0F}), "registering a recipe");
    const auto again = expect_ok(shared.add_recipe({.type_name = "Tank", .max_speed = 10.0F}), "registering it again");
    const auto faster = expect_ok(shared.add_recipe({.type_name = "Tank", .max_speed = 12.0F}), "registering a variant");
    const auto negative = shared.add_recipe({.type_name = "Broken", .radius = -1.0F});
    require(tank == again && faster != tank && shared.recipe_count() == 2 && !negative && negative.error().message.find("'Broken'") != std::string::npos && shared.recipe(faster).max_speed == 12.0F,
            "equal recipes must be one, variants their own, and bad recipes refused");
    const auto first_tank = expect_ok(shared.spawn({.recipe = tank, .position = {0.0F, 0.0F, 0.0F}}), "spawning from a recipe");
    const auto second_tank = expect_ok(shared.spawn({.recipe = tank, .position = {10.0F, 0.0F, 0.0F}}), "spawning from it again");
    require(shared.type_name(first_tank) == "Tank" && shared.type_name(second_tank) == "Tank" && !shared.spawn({.recipe = 7}),
            "entities of a recipe must share it, and a spawn must name a registered one");
}

// Snapshots report how fast each entity actually moved in the last tick.
void test_snapshot_speed()
{
    eaw::SimulationWorld world(0.1);
    const auto walker = test_spawn(world, {.type_name = "Walker", .max_speed = 10.0F});
    const auto fighter = test_spawn(world, {.type_name = "Fighter", .position = {0.0F, 500.0F, 0.0F}, .max_speed = 120.0F, .turn_rate = 1.5F, .min_speed = 75.0F});
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
    (void)test_spawn(world, {.type_name = "Gun", .owner = 1, .combat = gun});
    const auto victim = test_spawn(world, {.type_name = "Victim", .owner = 2, .position = {100.0F, 20.0F, 0.0F}, .heading = 1.0F, .combat = frail});
    const auto bystander = test_spawn(world, {.type_name = "Bystander", .owner = 1, .position = {-50.0F, 0.0F, 0.0F}});
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

// Two columns of a test component.
struct PoolTestColumns
{
    std::vector<float> values;
    std::vector<std::uint32_t> counts;
    template <class Visit> void for_each_column(Visit&& visit)
    {
        visit(values);
        visit(counts);
    }
};

// A component pool: rows by slot, defaults on add, the last row filling a removed one's place
// with its data intact, and a slot that can join again after leaving.
void test_component_pool()
{
    eaw::ComponentPool<PoolTestColumns> pool;
    for (const std::uint32_t slot : {7U, 2U, 40U})
    {
        const auto row = pool.add(slot);
        require(pool.columns.values[row] == 0.0F && pool.columns.counts[row] == 0U, "a new member must start from the columns' defaults");
        pool.columns.values[row] = static_cast<float>(slot);
        pool.columns.counts[row] = slot * 10U;
    }
    require(pool.size() == 3 && !pool.find(3) && !pool.find(1000) && pool.find(2) == std::optional<std::size_t>{1}, "find must return a member's row and nothing for others");
    pool.remove(7);
    pool.remove(5);
    const auto moved = pool.find(40);
    require(pool.size() == 2 && !pool.find(7) && moved == std::optional<std::size_t>{0} && pool.columns.values[*moved] == 40.0F && pool.columns.counts[*moved] == 400U && pool.slots()[0] == 40U &&
                pool.slots()[1] == 2U,
            "removal must move the last row into the gap with its data and ignore non-members");
    require(pool.find(7, &PoolTestColumns::values) == nullptr && *pool.find(40, &PoolTestColumns::values) == 40.0F && pool.get(2, &PoolTestColumns::counts) == 20U,
            "a column's value must be found by slot, and nothing for a non-member");
    const auto again = pool.add(7);
    require(again == 2 && pool.columns.values[again] == 0.0F && pool.slots()[again] == 7U, "a slot that left must be able to join again, from the defaults");
}

const ModuleRegistrar registrar("simulation", {
                                                  {"test_simulation_core", &test_simulation_core},
                                                  {"test_snapshot_speed", &test_snapshot_speed},
                                                  {"test_death_snapshots", &test_death_snapshots},
                                                  {"test_component_pool", &test_component_pool},
                                              });

} // namespace
