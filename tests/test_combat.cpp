// Combat: targeting, weapons, damage, hardpoints, body aiming, and stances.
#include "test_support.hpp"

#include "simulation/formation.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <tuple>

namespace {

using namespace eaw_test;

void test_combat()
{
    // Broad phase: items within the radius, cells row by row, ascending index within a cell.
    eaw::SpatialGrid grid;
    const std::vector<eaw::Vector3> points{{0.0F, 0.0F, 0.0F}, {300.0F, 0.0F, 0.0F}, {10.0F, 5.0F, 0.0F}, {0.0F, 600.0F, 0.0F}, {std::nanf(""), 0.0F, 0.0F}};
    grid.build(points, 256.0F);
    std::vector<std::uint32_t> near;
    grid.query({0.0F, 0.0F, 0.0F}, 50.0F, near);
    require(near == std::vector<std::uint32_t>{0, 2} && grid.size() == 4, "the spatial grid must return items within the radius in a fixed order");
    near.clear();
    grid.query({150.0F, 0.0F, 0.0F}, 160.0F, near);
    require(near == std::vector<std::uint32_t>{0, 2, 1}, "queries spanning cells must visit cells in order");
    // Built with radii: a large item lies in every cell it covers and is reported once; an overlap
    // query reaches only its own radius.
    const std::vector<eaw::Vector3> sized_points{{0.0F, 0.0F, 0.0F}, {60.0F, 0.0F, 0.0F}, {200.0F, 0.0F, 0.0F}, {62.0F, 1.0F, 0.0F}};
    const std::vector<float> sized_radii{58.0F, 3.0F, 3.0F, 3.0F};
    grid.build(sized_points, sized_radii, 16.0F);
    near.clear();
    grid.query_overlapping({60.0F, 0.0F, 0.0F}, 3.0F, near);
    std::ranges::sort(near);
    require(near == std::vector<std::uint32_t>{0, 1, 3}, "an overlap query must find the large item that reaches it, once, and its small neighbours");
    near.clear();
    grid.query({10.0F, 0.0F, 0.0F}, 15.0F, near);
    require(near == std::vector<std::uint32_t>{0}, "a center query must report a large item once, by its center");
    near.clear();
    grid.query_overlapping({120.0F, 0.0F, 0.0F}, 3.0F, near);
    require(near.empty(), "an overlap query must not find items whose circles do not reach it");

    eaw::DamageTable table;
    table.set(1, 2, 0.5F);
    require(table.modifier(1, 2) == 0.5F && table.modifier(2, 1) == 1.0F, "unset damage modifiers must be 1");
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    require(hostility.hostile(2, 1) && !hostility.hostile(1, 3) && !hostility.hostile(1, 1) && !hostility.hostile(1, 200), "hostility must be symmetric and bounded");

    // A shooter (owner 1) with an instant-hit weapon: bursts of two shots 0.1 s apart every second,
    // 10 damage halved by the victim's armor.
    const auto make_world = [&]
    {
        eaw::SimulationWorld world(0.1);
        world.set_damage_table(table);
        world.set_hostility(hostility);
        return world;
    };
    eaw::CombatProfile gun;
    gun.health = 50.0F;
    gun.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 1.0F, .pulse_count = 2, .pulse_delay_seconds = 0.1F, .damage = 10.0F, .damage_type = 1}};
    eaw::CombatProfile target;
    target.health = 25.0F;
    target.armor_type = 2;
    auto world = make_world();
    const auto shooter = test_spawn(world, {.type_name = "Shooter", .owner = 1, .combat = gun});
    const auto victim = test_spawn(world, {.type_name = "Victim", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = target});
    const auto bystander = test_spawn(world, {.type_name = "Neutral", .owner = 3, .position = {20.0F, 0.0F, 0.0F}, .combat = target});
    const auto distant = test_spawn(world, {.type_name = "Distant", .owner = 2, .position = {500.0F, 0.0F, 0.0F}, .combat = target});
    require(shooter && victim && bystander && distant, "test spawns must succeed");
    world.step();
    require(world.snapshot().entities[1].health == 20.0F && world.snapshot().entities[0].target == *victim, "an armed entity must target the nearest hostile in range and fire");
    world.step();
    require(world.snapshot().entities[1].health == 15.0F, "a burst must fire its pulses at the pulse delay");
    for (int tick = 0; tick < 8; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[1].health == 15.0F, "the weapon must wait for its recharge");
    for (int tick = 0; tick < 12; ++tick)
    {
        world.step();
    }
    require(!world.alive(*victim) && world.stats().kills == 1 && world.stats().shots_fired == 5 && world.stats().hits == 5, "a victim must die when its health is spent");
    require(world.alive(*bystander) && world.alive(*distant) && !world.snapshot().entities[0].target.valid(), "neutral and out-of-range entities must not be attacked");

    // Shields absorb first; the rest reaches the hull through the armor modifier.
    {
        auto shielded_world = make_world();
        auto shielded = target;
        shielded.shield = 8.0F;
        shielded.shield_armor_type = 3;
        (void)test_spawn(shielded_world, {.type_name = "Shooter", .owner = 1, .combat = gun});
        (void)test_spawn(shielded_world, {.type_name = "Shielded", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = shielded});
        shielded_world.step();
        const auto& hit = shielded_world.snapshot().entities[1];
        require(hit.shield == 0.0F && hit.health == 24.0F, "damage beyond the shield must reach the hull");
    }

    // A held target is kept when a better-ranked one appears, unless the type names a stickiness
    // threshold (recorded in the FoC debug build: a TIE Defender, which names none, stays on its
    // X-wing for the 5 s after a nearer Y-wing, ranked above it, enters range; EaW-Revised S-02).
    {
        auto retention_world = make_world();
        eaw::CombatProfile defender;
        defender.health = 100.0F;
        defender.weapons = {eaw::WeaponProfile{.range = 700.0F, .recharge_seconds = 1.0F, .damage = 1.0F}};
        defender.targeting.ranks = {{0x2U, 0U, 2.0F}, {0x1U, 0U, 3.0F}};
        eaw::CombatProfile fighter;
        fighter.health = 1.0e6F;
        fighter.categories = 0x1U;
        auto bomber = fighter;
        bomber.categories = 0x2U;
        (void)test_spawn(retention_world, {.type_name = "Defender", .owner = 1, .combat = defender});
        const auto held = test_spawn(retention_world, {.type_name = "Fighter", .owner = 2, .position = {250.0F, 20.0F, 0.0F}, .combat = fighter});
        for (int tick = 0; tick < 20; ++tick)
        {
            retention_world.step();
        }
        const bool acquired = retention_world.snapshot().entities[0].target == *held;
        (void)test_spawn(retention_world, {.type_name = "Bomber", .owner = 2, .position = {180.0F, -25.0F, 0.0F}, .combat = bomber});
        for (int tick = 0; tick < 50; ++tick)
        {
            retention_world.step();
        }
        require(acquired && retention_world.snapshot().entities[0].target == *held, "a held target must be kept when a better one appears and no stickiness is named");
    }

    // Shields refresh in steps: Shield_Refresh_Rate points once per interval, on a phase of the
    // unit's own (any interval-long run of ticks holds exactly one refresh).
    {
        auto refresh_world = make_world();
        auto one_shot = gun;
        one_shot.weapons.front().recharge_seconds = 1000.0F;
        one_shot.weapons.front().pulse_count = 1;
        auto refreshing = target;
        refreshing.shield = 30.0F;
        refreshing.shield_refresh = 5.0F;
        refreshing.shield_refresh_seconds = 1.0F;
        (void)test_spawn(refresh_world, {.type_name = "Shooter", .owner = 1, .combat = one_shot});
        (void)test_spawn(refresh_world, {.type_name = "Refreshing", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = refreshing});
        refresh_world.step();
        const auto after_hit = refresh_world.snapshot().entities[1].shield;
        bool stepped_only = true;
        for (int tick = 0; tick < 10; ++tick)
        {
            refresh_world.step();
            const auto shield = refresh_world.snapshot().entities[1].shield;
            stepped_only = stepped_only && (shield == after_hit || shield == after_hit + 5.0F);
        }
        require(after_hit < 30.0F && stepped_only && refresh_world.snapshot().entities[1].shield == after_hit + 5.0F, "a shield must refresh by its whole amount exactly once per interval");
    }

    // Projectiles fly at their speed and hit later.
    {
        auto flight_world = make_world();
        auto cannon = gun;
        cannon.weapons[0].projectile_speed = 100.0F;
        cannon.weapons[0].pulse_count = 1;
        (void)test_spawn(flight_world, {.type_name = "Cannon", .owner = 1, .combat = cannon});
        (void)test_spawn(flight_world, {.type_name = "Victim", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = target});
        flight_world.step();
        require(flight_world.snapshot().projectiles.size() == 1 && flight_world.snapshot().projectiles[0].direction.x == 1.0F && flight_world.snapshot().entities[1].health == 25.0F,
                "a projectile must be in flight before it hits");
        // A shot keeps its serial while it flies (presentation follows it by that number).
        const auto serial = flight_world.snapshot().projectiles[0].serial;
        const auto hash = flight_world.state_hash();
        flight_world.step();
        require(serial != 0 && !flight_world.snapshot().projectiles.empty() && flight_world.snapshot().projectiles[0].serial == serial, "a shot must keep its serial in flight");
        require(hash != flight_world.state_hash(), "a flying shot must change the state");
        for (int tick = 0; tick < 5; ++tick)
        {
            flight_world.step();
        }
        require(flight_world.snapshot().projectiles.empty() && flight_world.snapshot().entities[1].health == 20.0F && flight_world.stats().hits == 1, "a projectile must hit its target");
    }

    // An attack order chases a target out of range, then holds and fires.
    {
        auto chase_world = make_world();
        auto mobile = gun;
        const auto hunter = test_spawn(chase_world, {.type_name = "Hunter", .owner = 1, .max_speed = 100.0F, .combat = mobile});
        const auto prey = test_spawn(chase_world, {.type_name = "Prey", .owner = 2, .position = {400.0F, 0.0F, 0.0F}, .combat = target});
        require(hunter && prey, "test spawns must succeed");
        chase_world.submit({.kind = eaw::SimCommandKind::attack, .entities = {*hunter}, .target_entity = *prey});
        for (int tick = 0; tick < 60; ++tick)
        {
            chase_world.step();
        }
        require(!chase_world.alive(*prey), "an attack order must chase and kill its target");
        const auto hunter_x = chase_world.snapshot().entities[0].position.x;
        require(hunter_x >= 300.0F && hunter_x < 320.0F && !chase_world.snapshot().entities[0].moving, "an attacker must stop once its target is in range");
    }

    // Identical setups and commands give identical state.
    const auto run = [&]
    {
        auto replay = make_world();
        (void)test_spawn(replay, {.type_name = "A", .owner = 1, .max_speed = 50.0F, .combat = gun});
        (void)test_spawn(replay, {.type_name = "B", .owner = 2, .position = {150.0F, 30.0F, 0.0F}, .max_speed = 50.0F, .combat = gun});
        for (int tick = 0; tick < 40; ++tick)
        {
            replay.step();
        }
        return replay.state_hash();
    };
    require(run() == run(), "combat must be deterministic");

    auto invalid = gun;
    invalid.weapons[0].range = -1.0F;
    require(!test_spawn(world, {.type_name = "Invalid", .combat = invalid}), "a negative weapon range must be rejected");
}

void test_hardpoints_and_fire_cones()
{
    using Kind = eaw::HardpointProfile::Kind;
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    // A ship with a forward gun (a 90-degree arc on a weapon hardpoint), a shield generator, and an
    // engine, all destroyable.
    eaw::CombatProfile ship;
    ship.health = 1000.0F;
    ship.shield = 50.0F;
    ship.shield_refresh = 10.0F;
    ship.hardpoints = {{Kind::weapon, 40.0F, true, {20.0F, 0.0F, 0.0F}}, {Kind::shield_generator, 30.0F, true, {0.0F, 10.0F, 0.0F}}, {Kind::engine, 30.0F, true, {-20.0F, 0.0F, 0.0F}}};
    ship.weapons = {eaw::WeaponProfile{
        .range = 500.0F, .recharge_seconds = 0.5F, .damage = 5.0F, .mount = {20.0F, 0.0F, 0.0F}, .cone_direction = 0.0F, .cone_half_angle = std::numbers::pi_v<float> / 4.0F, .hardpoint = 0}};
    const auto make_world = [&]
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        return world;
    };

    // The gun only fires at targets inside its arc: a target behind the ship is safe, one ahead is not.
    {
        auto world = make_world();
        eaw::CombatProfile target;
        target.health = 100.0F;
        (void)test_spawn(world, {.type_name = "Ship", .owner = 1, .combat = ship});
        const auto behind = test_spawn(world, {.type_name = "Behind", .owner = 2, .position = {-200.0F, 0.0F, 0.0F}, .combat = target});
        require(behind.ok(), "test spawn must succeed");
        for (int tick = 0; tick < 20; ++tick)
        {
            world.step();
        }
        require(world.stats().shots_fired == 0, "a weapon must not fire outside its arc");
        (void)test_spawn(world, {.type_name = "Ahead", .owner = 2, .position = {200.0F, 0.0F, 0.0F}, .combat = target});
        for (int tick = 0; tick < 20; ++tick)
        {
            world.step();
        }
        require(world.stats().shots_fired > 0 && world.snapshot().entities[1].health == 100.0F, "a weapon must fire at a target inside its arc and only there");
    }

    // Shots aim at the nearest live hardpoint and damage it and the hull; destroyed parts take
    // their effects.
    {
        auto world = make_world();
        auto victim_profile = ship;
        victim_profile.weapons.clear();
        for (auto& part : victim_profile.hardpoints)
        {
            part.death_sound = 77U;
            part.death_effect = 88U;
        }
        eaw::CombatProfile gun;
        gun.health = 100.0F;
        gun.weapons = {eaw::WeaponProfile{.range = 500.0F, .recharge_seconds = 0.1F, .damage = 10.0F, .damages_shield = false}};
        // The victim's engine (20 behind its center) faces the attacker, which stands behind it.
        const auto victim = test_spawn(world, {.type_name = "Victim", .owner = 2, .position = {0.0F, 0.0F, 0.0F}, .max_speed = 40.0F, .combat = victim_profile});
        (void)test_spawn(world, {.type_name = "Gun", .owner = 1, .position = {-200.0F, 0.0F, 0.0F}, .combat = gun});
        require(victim.ok(), "test spawn must succeed");
        // Four shots of 10: three destroy the 30-point engine, the fourth moves on to the next part
        // (the first search waits for the gun's retarget phase).
        std::vector<eaw::HardpointDeathSnapshot> fallen;
        for (int tick = 0; tick < 20 && world.stats().shots_fired < 4; ++tick)
        {
            world.step();
            fallen.insert(fallen.end(), world.snapshot().hardpoint_deaths.begin(), world.snapshot().hardpoint_deaths.end());
        }
        require(fallen.size() == 1 && fallen[0].entity == *victim && fallen[0].sound == 77U && fallen[0].effect == 88U && fallen[0].position.x < 0.0F,
                "a destroyed hardpoint must be reported once, with its sound and effect, at its position");
        const auto& damaged = world.snapshot().entities[0];
        require(damaged.hardpoints == 3 && damaged.hardpoints_destroyed == 1 && world.stats().hardpoints_destroyed == 1, "the nearest hardpoint must be hit and destroyed");
        // The hull follows its parts: 60 of their 100 points remain, so 0.6 + 0.2 of its health.
        require(std::fabs(damaged.health - ship.health * 0.8F) < 1.0e-3F, "a hull must follow its parts' remaining share plus a fifth");
        // Without its engine the ship moves at a quarter of its speed.
        world.submit({eaw::SimCommandKind::move, {*victim}, {0.0F, 400.0F, 0.0F}});
        const auto before = world.snapshot().entities[0].position.y;
        world.step();
        require(std::fabs(world.snapshot().entities[0].position.y - before - 1.0F) < 1.0e-3F, "a ship without engines must move at a quarter speed");
    }

    // A ship dies with its last destroyable part, unless its type says otherwise.
    for (const bool dies : {true, false})
    {
        auto world = make_world();
        eaw::CombatProfile hulk;
        hulk.health = 1000.0F;
        hulk.dies_with_hardpoints = dies;
        hulk.hardpoints = {{Kind::weapon, 20.0F, true, {0.0F, 0.0F, 0.0F}}};
        eaw::CombatProfile gun;
        gun.health = 100.0F;
        gun.weapons = {eaw::WeaponProfile{.range = 500.0F, .recharge_seconds = 0.1F, .damage = 10.0F, .damages_shield = false}};
        const auto victim = test_spawn(world, {.type_name = "Hulk", .owner = 2, .combat = hulk});
        (void)test_spawn(world, {.type_name = "Gun", .owner = 1, .position = {-200.0F, 0.0F, 0.0F}, .combat = gun});
        // Two shots of 10 destroy the 20-point part (the first search waits for the gun's retarget
        // phase).
        for (int tick = 0; tick < 20 && world.stats().shots_fired < 2; ++tick)
        {
            world.step();
        }
        require(world.alive(*victim) != dies && (dies || std::fabs(world.snapshot().entities[0].health - 200.0F) < 1.0e-3F),
                "a ship must die with its last part, or keep a fifth of its hull when its type says not to");
    }

    // An ordered ship turns in place toward its target's nearest live hardpoint; an idle one keeps
    // its heading.
    for (const bool ordered : {true, false})
    {
        auto world = make_world();
        // Its 500-unit weapons put the target in range, so it stands and turns.
        const auto gunship = ship;
        eaw::CombatProfile mark;
        mark.health = 100.0F;
        mark.hardpoints = {{Kind::weapon, 20.0F, true, {0.0F, 30.0F, 0.0F}}};
        const auto shooter = test_spawn(world, {.type_name = "Gunship", .owner = 1, .combat = gunship, .turn_rate = 1.0F});
        const auto target = test_spawn(world, {.type_name = "Mark", .owner = 2, .position = {-300.0F, 0.0F, 0.0F}, .combat = mark});
        if (ordered)
        {
            world.submit({.kind = eaw::SimCommandKind::attack, .entities = {*shooter}, .target_entity = *target});
        }
        for (int tick = 0; tick < 40; ++tick)
        {
            world.step();
        }
        // The part sits 30 to the target's left, so it bears slightly off due west.
        const auto bearing = std::atan2(30.0F, -300.0F);
        const auto heading = world.snapshot().entities[0].heading;
        require(ordered ? std::fabs(std::remainder(heading - bearing, 2.0F * std::numbers::pi_v<float>)) < 1.0e-3F : heading == 0.0F,
                "an ordered ship must turn to its target's nearest hardpoint, an idle one keep its heading");
    }
}

// Body-aiming units (infantry) turn to face their target, fire only facing it, and never while
// moving; turreted units fire on the move.
void test_body_aiming()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::CombatProfile rifle;
    rifle.health = 1000.0F;
    rifle.weapons = {eaw::WeaponProfile{.range = 300.0F, .recharge_seconds = 0.1F, .damage = 1.0F}};
    eaw::CombatProfile target;
    target.health = 1.0e6F;
    const auto make = [&](const bool body, const float heading, const eaw::Vector3 target_at)
    {
        auto world = std::make_unique<eaw::SimulationWorld>(0.1);
        world->set_hostility(hostility);
        const auto shooter =
            test_spawn(*world, {.type_name = "Trooper", .owner = 1, .heading = heading, .max_speed = 20.0F, .combat = rifle, .turn_rate = std::numbers::pi_v<float> / 2.0F, .aims_with_body = body});
        const auto victim = test_spawn(*world, {.type_name = "Target", .owner = 2, .position = target_at, .combat = target});
        require(shooter.ok() && victim.ok(), "test spawns must succeed");
        return std::pair{std::move(world), *shooter};
    };

    // Facing away: no shot until it has turned to within 20 degrees, then it fires.
    {
        auto [world, shooter] = make(true, std::numbers::pi_v<float>, {200.0F, 0.0F, 0.0F});
        for (int tick = 0; tick < 30; ++tick)
        {
            world->step();
            const auto heading = world->snapshot().entities[0].heading;
            const auto off = std::fabs(std::remainder(heading, 2.0F * std::numbers::pi_v<float>));
            if (off > eaw::body_aim_tolerance + 0.16F)
            {
                require(world->stats().shots_fired == 0, "a body-aimer must not fire before it faces its target");
            }
        }
        require(std::fabs(std::remainder(world->snapshot().entities[0].heading, 2.0F * std::numbers::pi_v<float>)) < 0.01F && world->stats().shots_fired > 0,
                "a body-aimer must turn to its target and fire");
    }
    // Moving past a target in range: a body-aimer holds fire, a turreted unit does not.
    for (const bool body : {true, false})
    {
        auto [world, shooter] = make(body, std::numbers::pi_v<float> / 2.0F, {100.0F, 0.0F, 0.0F});
        world->submit({eaw::SimCommandKind::move, {shooter}, {0.0F, 150.0F, 0.0F}});
        for (int tick = 0; tick < 40; ++tick)
        {
            world->step();
        }
        require((world->stats().shots_fired == 0) == body, body ? "a moving body-aimer must hold fire" : "a turreted unit must fire on the move");
    }
}

// Stances belong to the behavior layer: they decide whether a unit picks targets and moves for
// them, while orders always apply.
void test_stances()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::CombatProfile armed;
    armed.health = 1.0e6F;
    armed.weapons = {eaw::WeaponProfile{.range = 450.0F, .recharge_seconds = 1.0F, .damage = 1.0F}};
    const auto run = [](eaw::SimulationWorld& world, const int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
        {
            world.step();
        }
    };

    // hold_fire never engages on its own; an attack order makes it fire anyway, and set_stance
    // switches a unit back and forth.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto quiet = test_spawn(world, {.type_name = "Quiet", .owner = 1, .combat = armed, .stance = eaw::Stance::hold_fire});
        eaw::CombatProfile target = armed;
        target.weapons.clear();
        const auto victim = test_spawn(world, {.type_name = "Target", .owner = 2, .position = {200.0F, 0.0F, 0.0F}, .combat = target});
        require(quiet.ok() && victim.ok(), "test spawns must succeed");
        run(world, 30);
        require(world.stats().shots_fired == 0 && !world.snapshot().entities[0].target.valid(), "a hold_fire unit must not pick targets or fire on its own");
        world.submit({.kind = eaw::SimCommandKind::attack, .entities = {*quiet}, .target_entity = *victim});
        run(world, 30);
        const auto ordered_shots = world.stats().shots_fired;
        require(ordered_shots > 0, "an attack order must engage even under hold_fire");
        world.submit({.kind = eaw::SimCommandKind::stop, .entities = {*quiet}});
        run(world, 30);
        require(world.stats().shots_fired == ordered_shots, "after the order ends, hold_fire must hold fire again");
        world.submit({.kind = eaw::SimCommandKind::set_stance, .entities = {*quiet}, .stance = eaw::Stance::aggressive});
        run(world, 30);
        require(world.stats().shots_fired > ordered_shots, "an aggressive unit must engage on its own");
        world.submit({.kind = eaw::SimCommandKind::set_stance, .entities = {*quiet}, .stance = eaw::Stance::hold_fire});
        run(world, 2);
        const auto settled = world.stats().shots_fired;
        run(world, 30);
        require(world.stats().shots_fired == settled && !world.snapshot().entities[0].target.valid(), "switching to hold_fire must drop automatic targets");
        world.submit({.kind = eaw::SimCommandKind::set_stance, .entities = {*quiet}, .stance = static_cast<eaw::Stance>(7)});
        run(world, 1);
        require(world.stats().stale_references == 1, "an unknown stance must be rejected");
        require(!test_spawn(world, {.type_name = "Bad", .stance = static_cast<eaw::Stance>(7)}).ok(), "a spawn with an unknown stance must be rejected");
    }

    // A fighter holding position keeps circling where it is and fires at what comes in range; an
    // aggressive one leaves to fly attack runs at it.
    for (const auto stance : {eaw::Stance::hold_position, eaw::Stance::aggressive})
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto fighter =
            test_spawn(world, {.type_name = "Fighter", .owner = 1, .max_speed = 120.0F, .combat = armed, .turn_rate = std::numbers::pi_v<float> / 2.0F, .min_speed = 75.0F, .stance = stance});
        eaw::CombatProfile station = armed;
        station.weapons.clear();
        const auto target = test_spawn(world, {.type_name = "Station", .owner = 2, .position = {400.0F, 0.0F, 0.0F}, .combat = station});
        require(fighter.ok() && target.ok(), "test spawns must succeed");
        float closest = 1.0e9F;
        float farthest_from_start = 0.0F;
        for (int tick = 0; tick < 200; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            closest = std::min(closest, std::hypot(entities[0].position.x - entities[1].position.x, entities[0].position.y - entities[1].position.y));
            farthest_from_start = std::max(farthest_from_start, std::hypot(entities[0].position.x, entities[0].position.y));
        }
        require(world.stats().shots_fired > 0, "a fighter must fire at a hostile in range in either stance");
        if (stance == eaw::Stance::hold_position)
        {
            require(farthest_from_start < 150.0F, "a fighter holding position must keep circling where it is");
        }
        else
        {
            require(closest < 150.0F, "an aggressive fighter must fly attack runs at its target");
        }
    }
}

// Standing orders: an attack-move engages a hostile met on the way, resumes after it dies, breaks
// off a chase beyond its leash, and ends on arrival; a guard engages what comes near its anchor,
// ignores what stays away, follows a moving anchor, and ends when the anchor dies; a move clears a
// standing order.
void test_standing_orders()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::CombatProfile armed;
    armed.health = 1.0e6F;
    armed.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 0.2F, .damage = 5.0F}};
    eaw::CombatProfile weak;
    weak.health = 10.0F;
    eaw::CombatProfile tough;
    tough.health = 1.0e6F;
    const auto run = [](eaw::SimulationWorld& world, const int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
        {
            world.step();
        }
    };
    const auto entity = [](const eaw::SimulationWorld& world, const eaw::EntityId id) -> const eaw::EntitySnapshot&
    {
        return *std::ranges::find(world.snapshot().entities, id, &eaw::EntitySnapshot::id);
    };

    // Attack-move: the weak hostile beside the path dies on the way; the destination is reached.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto mover = *test_spawn(world, {.type_name = "Mover", .owner = 1, .max_speed = 50.0F, .combat = armed, .stance = eaw::Stance::hold_fire});
        const auto victim = *test_spawn(world, {.type_name = "Victim", .owner = 2, .position = {500.0F, 60.0F, 0.0F}, .combat = weak});
        world.submit({.kind = eaw::SimCommandKind::attack_move, .entities = {mover}, .target = {1000.0F, 0.0F, 0.0F}});
        run(world, 1);
        require(entity(world, mover).order == eaw::StandingOrder::attack_move, "an attack-move must become the entity's standing order");
        run(world, 400);
        require(!world.alive(victim) && world.stats().shots_fired > 0, "an attack-move must engage a hostile met on the way, even under hold_fire");
        const auto& arrived = entity(world, mover);
        require(arrived.order == eaw::StandingOrder::none && !arrived.moving && std::abs(arrived.position.x - 1000.0F) < 1.0F, "an attack-move must resume and end on arrival");
    }

    // Attack-move with XML Attack_Move_Response_Range 30: the hostile 60 beside the path is left alone.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        auto short_reach = armed;
        short_reach.attack_move_response_range = 30.0F;
        const auto mover = *test_spawn(world, {.type_name = "Mover", .owner = 1, .max_speed = 50.0F, .combat = short_reach, .stance = eaw::Stance::hold_fire});
        const auto victim = *test_spawn(world, {.type_name = "Victim", .owner = 2, .position = {500.0F, 60.0F, 0.0F}, .combat = weak});
        world.submit({.kind = eaw::SimCommandKind::attack_move, .entities = {mover}, .target = {1000.0F, 0.0F, 0.0F}});
        run(world, 400);
        require(world.alive(victim) && world.stats().shots_fired == 0, "an attack-move must react only within its response range");
    }

    // Attack-move: a fast hostile that flees is chased only to the leash, then the move resumes.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto mover = *test_spawn(world, {.type_name = "Mover", .owner = 1, .max_speed = 50.0F, .combat = armed});
        const auto runner = *test_spawn(world, {.type_name = "Runner", .owner = 2, .position = {200.0F, 0.0F, 0.0F}, .max_speed = 300.0F, .combat = tough});
        world.submit({.kind = eaw::SimCommandKind::attack_move, .entities = {mover}, .target = {600.0F, 0.0F, 0.0F}});
        run(world, 30);
        require(entity(world, mover).target == runner, "the attack-mover must engage the hostile in reach");
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {runner}, .target = {200.0F, 5000.0F, 0.0F}});
        run(world, 300);
        const auto& resumed = entity(world, mover);
        require(world.alive(runner) && !resumed.target.valid() && std::abs(resumed.position.x - 600.0F) < 1.0F && std::abs(resumed.position.y) < 1.0F,
                "a chase beyond the leash must break off, and the attack-move continue to its destination");
    }

    // Guard a point: a hostile that comes within the radius is engaged, one that stays away is not.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto guard = *test_spawn(world, {.type_name = "Guard", .owner = 1, .max_speed = 50.0F, .combat = armed, .stance = eaw::Stance::hold_fire});
        const auto distant = *test_spawn(world, {.type_name = "Distant", .owner = 2, .position = {400.0F, 0.0F, 0.0F}, .combat = tough});
        world.submit({.kind = eaw::SimCommandKind::guard, .entities = {guard}, .target = {0.0F, 0.0F, 0.0F}});
        run(world, 50);
        require(entity(world, guard).order == eaw::StandingOrder::guard && !entity(world, guard).target.valid() && world.stats().shots_fired == 0, "a guard must ignore hostiles outside its radius");
        const auto intruder = *test_spawn(world, {.type_name = "Intruder", .owner = 2, .position = {120.0F, 0.0F, 0.0F}, .combat = weak});
        run(world, 100);
        require(!world.alive(intruder) && world.alive(distant), "a guard must engage a hostile that comes within its radius");
        const auto& settled = entity(world, guard);
        require(settled.order == eaw::StandingOrder::guard && std::hypot(settled.position.x, settled.position.y) < eaw::minimum_guard_radius, "a guard must stay near its anchor");
    }

    // Guard an object: it is followed as it moves; when it dies the guard ends; a move clears a guard.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto guard = *test_spawn(world, {.type_name = "Guard", .owner = 1, .max_speed = 60.0F, .combat = armed});
        const auto ward = *test_spawn(world, {.type_name = "Ward", .owner = 1, .position = {50.0F, 0.0F, 0.0F}, .max_speed = 30.0F, .combat = weak});
        world.submit({.kind = eaw::SimCommandKind::guard, .entities = {guard}, .target_entity = ward});
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {ward}, .target = {800.0F, 0.0F, 0.0F}});
        run(world, 400);
        const auto& follower = entity(world, guard);
        require(follower.order == eaw::StandingOrder::guard && std::abs(follower.position.x - 800.0F) < eaw::minimum_guard_radius, "a guard must follow the object it guards");
        world.submit({.kind = eaw::SimCommandKind::destroy, .entities = {ward}});
        run(world, 2);
        require(entity(world, guard).order == eaw::StandingOrder::none, "a guard must end when its object is gone");
        world.submit({.kind = eaw::SimCommandKind::guard, .entities = {guard}, .target = {0.0F, 0.0F, 0.0F}});
        run(world, 1);
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {guard}, .target = {900.0F, 0.0F, 0.0F}});
        run(world, 1);
        require(entity(world, guard).order == eaw::StandingOrder::none, "a move must clear a standing order");
    }
}

// Scripted conditions: an invulnerable unit takes no damage, one that cannot be killed stops at the
// health floor, direct damage kills as combat does, a teleport moves and turns a unit at once and
// ends its movement, and a suspended locomotor holds a unit until it is released.
void test_scripted_conditions()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::CombatProfile gun;
    gun.health = 1.0e6F;
    gun.weapons = {eaw::WeaponProfile{.range = 200.0F, .recharge_seconds = 0.1F, .damage = 30.0F}};
    eaw::CombatProfile body;
    body.health = 100.0F;
    const auto run = [](eaw::SimulationWorld& world, const int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
        {
            world.step();
        }
    };
    const auto entity = [](const eaw::SimulationWorld& world, const eaw::EntityId id) -> const eaw::EntitySnapshot&
    {
        return *std::ranges::find(world.snapshot().entities, id, &eaw::EntitySnapshot::id);
    };
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        (void)test_spawn(world, {.type_name = "Gun", .owner = 1, .combat = gun});
        const auto victim = *test_spawn(world, {.type_name = "Victim", .owner = 2, .position = {100.0F, 0.0F, 0.0F}, .combat = body});
        world.submit({.kind = eaw::SimCommandKind::set_invulnerable, .entities = {victim}, .flag = true});
        run(world, 30);
        require(world.stats().shots_fired > 0 && entity(world, victim).health == 100.0F, "an invulnerable unit must take no damage");
        world.submit({.kind = eaw::SimCommandKind::set_cannot_be_killed, .entities = {victim}, .flag = true});
        world.submit({.kind = eaw::SimCommandKind::set_invulnerable, .entities = {victim}, .flag = false});
        run(world, 30);
        require(world.alive(victim) && entity(world, victim).health == eaw::unkillable_health_floor, "a unit that cannot be killed must stop at the health floor");
        world.submit({.kind = eaw::SimCommandKind::set_cannot_be_killed, .entities = {victim}, .flag = false});
        world.submit({.kind = eaw::SimCommandKind::damage, .entities = {victim}, .amount = 10000.0F});
        run(world, 1);
        require(!world.alive(victim) && world.snapshot().deaths.size() == 1, "direct damage must kill as combat does");
    }
    {
        eaw::SimulationWorld world(0.1);
        const auto unit = *test_spawn(world, {.type_name = "Unit", .owner = 1, .max_speed = 50.0F, .combat = body});
        world.submit({eaw::SimCommandKind::move, {unit}, {500.0F, 0.0F, 0.0F}});
        world.submit({.kind = eaw::SimCommandKind::suspend_locomotor, .entities = {unit}, .flag = true});
        run(world, 20);
        require(entity(world, unit).position.x == 0.0F && entity(world, unit).moving, "a suspended locomotor must hold the unit and keep its order");
        world.submit({.kind = eaw::SimCommandKind::suspend_locomotor, .entities = {unit}, .flag = false});
        run(world, 20);
        require(entity(world, unit).position.x > 0.0F, "a released locomotor must resume the order");
        world.submit({.kind = eaw::SimCommandKind::teleport, .entities = {unit}, .target = {-300.0F, 40.0F, 0.0F}, .heading = 1.0F});
        run(world, 1);
        const auto& moved = entity(world, unit);
        require(moved.position.x == -300.0F && moved.position.y == 40.0F && moved.heading == 1.0F && !moved.moving, "a teleport must move and turn the unit at once and end its movement");
    }
}

// Shots stray by their weapon's inaccuracy for the target's categories and miss when they land outside
// its footprint; the draws are part of the state, so a battle replays its misses.
void test_inaccuracy()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    const auto fire = [&](const float distance, const std::uint32_t categories, const bool bystander = false, const bool land = false)
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        if (land)
        {
            // Flat ground around both, sloping up beyond the target, so misses flying on meet it.
            eaw::GroundMap ground;
            ground.width = 40;
            ground.height = 40;
            ground.spacing = 20.0F;
            for (std::uint32_t y = 0; y < ground.height; ++y)
            {
                for (std::uint32_t x = 0; x < ground.width; ++x)
                {
                    ground.heights.push_back(x > 8U ? static_cast<float>(x - 8U) * 10.0F : 0.0F);
                }
            }
            world.set_ground(ground);
        }
        eaw::CombatProfile gun;
        gun.health = 50.0F;
        gun.weapons = {eaw::WeaponProfile{.range = 300.0F, .recharge_seconds = 0.1F, .damage = 1.0F, .inaccuracy = {{categories, distance}}}};
        eaw::CombatProfile target;
        target.health = 100000.0F;
        target.categories = 0x80U;
        (void)test_spawn(world, {.type_name = "Gun", .owner = 1, .combat = gun});
        (void)test_spawn(world, {.type_name = "Target", .owner = 2, .position = {100.0F, 0.0F, 0.0F}, .combat = target, .radius = 5.0F});
        if (bystander)
        {
            // A large hostile around the target catches every stray shot.
            auto large = target;
            large.categories = 0;
            (void)test_spawn(world, {.type_name = "Bystander", .owner = 2, .position = {100.0F, 0.0F, 0.0F}, .combat = large, .radius = 150.0F});
        }
        for (int tick = 0; tick < 60; ++tick)
        {
            world.step();
        }
        return std::tuple(world.stats().shots_fired, world.stats().hits, world.state_hash(), world.stats().ground_hits);
    };
    const auto [sure_shots, sure_hits, sure_hash, sure_landed] = fire(0.0F, 0x80U);
    require(sure_shots > 20 && sure_hits == sure_shots, "a weapon without inaccuracy must hit every shot");
    const auto [wide_shots, wide_hits, wide_hash, wide_landed] = fire(100.0F, 0x80U);
    require(wide_shots == sure_shots && wide_hits < wide_shots / 4U, "a wide stray against a small target must mostly miss");
    const auto [other_shots, other_hits, other_hash, other_landed] = fire(100.0F, 0x100U);
    require(other_hits == other_shots, "inaccuracy for other categories must not apply");
    const auto [again_shots, again_hits, again_hash, again_landed] = fire(100.0F, 0x80U);
    require(again_hits == wide_hits && again_hash == wide_hash, "misses must replay");
    const auto [all_shots, all_hits, all_hash, all_landed] = fire(100.0F, eaw::all_categories);
    require(all_hits < all_shots / 4U, "a fixed radius must apply to every target");
    const auto [crowd_shots, crowd_hits, crowd_hash, crowd_landed] = fire(100.0F, 0x80U, true);
    require(crowd_hits == crowd_shots, "a stray shot must hit the hostile it lands on");
    // On land a miss flies on past its aim point until it meets the ground; without ground (space)
    // it is lost at its aim point.
    require(wide_landed == 0U, "without ground no miss may land");
    const auto [land_shots, land_hits, land_hash, land_landed] = fire(100.0F, 0x80U, false, true);
    require(land_hits == wide_hits && land_landed > 0U && land_landed <= land_shots - land_hits, "on land misses must fly on and land on the ground");
    const auto [land_again_shots, land_again_hits, land_again_hash, land_again_landed] = fire(100.0F, 0x80U, false, true);
    require(land_again_hash == land_hash && land_again_landed == land_landed, "misses flying on must replay");
}

// A unit's targeting priorities pick its target before distance does; excluded targets are never
// picked on their own.
void test_target_priorities()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::SimulationWorld world(0.1);
    world.set_hostility(hostility);
    eaw::CombatProfile gun;
    gun.health = 50.0F;
    gun.weapons = {eaw::WeaponProfile{.range = 300.0F, .recharge_seconds = 1.0F, .damage = 1.0F}};
    gun.targeting.ranks = {{0x100U, 0, 1.0F}};
    gun.targeting.excluded_categories = 0x200U;
    eaw::CombatProfile soldier;
    soldier.health = 100.0F;
    soldier.categories = 0x80U;
    auto tank = soldier;
    tank.categories = 0x100U;
    auto flyer = soldier;
    flyer.categories = 0x200U;
    (void)test_spawn(world, {.type_name = "Gun", .owner = 1, .combat = gun});
    const auto near = test_spawn(world, {.type_name = "Soldier", .owner = 2, .position = {50.0F, 0.0F, 0.0F}, .combat = soldier});
    const auto far = test_spawn(world, {.type_name = "Tank", .owner = 2, .position = {200.0F, 0.0F, 0.0F}, .combat = tank});
    require(near.ok() && far.ok(), "test spawns must succeed");
    world.step();
    require(world.snapshot().entities[0].target == *far, "a ranked target must be chosen over a nearer unranked one");
    world.submit({eaw::SimCommandKind::destroy, {*far}});
    world.step();
    for (int tick = 0; tick < 20; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[0].target == *near, "an unranked target must be chosen when no ranked one is left");
    world.submit({eaw::SimCommandKind::destroy, {*near}});
    (void)test_spawn(world, {.type_name = "Flyer", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = flyer});
    for (int tick = 0; tick < 20; ++tick)
    {
        world.step();
    }
    require(!world.snapshot().entities[0].target.valid(), "an excluded target must never be chosen automatically");
}

void test_target_validity()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    // Which of two hostiles (an air unit 20 away, infantry 60 away) a shooter with `weapon` damages,
    // `near` being the air unit's profile.
    const auto damaged = [&](const eaw::WeaponProfile& weapon, const eaw::CombatProfile& near)
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        eaw::CombatProfile gun;
        gun.health = 50.0F;
        gun.weapons = {weapon};
        eaw::CombatProfile far;
        far.health = 100.0F;
        far.categories = 0x80U;
        (void)test_spawn(world, {.type_name = "Shooter", .owner = 1, .combat = gun});
        (void)test_spawn(world, {.type_name = "Near", .owner = 2, .position = {20.0F, 0.0F, 0.0F}, .combat = near});
        (void)test_spawn(world, {.type_name = "Far", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = far});
        world.step();
        const auto& entities = world.snapshot().entities;
        return std::pair{entities[1].health < near.health, entities[2].health < far.health};
    };
    const eaw::WeaponProfile gun{.range = 100.0F, .recharge_seconds = 1.0F, .damage = 10.0F};
    eaw::CombatProfile air;
    air.health = 100.0F;
    air.categories = 0x200U;
    require(damaged(gun, air) == std::pair{true, false}, "the nearer hostile must be shot");
    auto close = gun;
    close.min_range = 30.0F;
    require(damaged(close, air) == std::pair{false, true}, "a weapon must not fire inside its minimum range");
    auto no_air = gun;
    no_air.excluded_categories = 0x200U;
    require(damaged(no_air, air) == std::pair{false, true}, "a weapon must not fire at its excluded categories");
    auto hidden = air;
    hidden.targetable = false;
    require(damaged(gun, hidden) == std::pair{false, true}, "an invalid target must never be chosen");
    eaw::SimulationWorld world(0.1);
    eaw::CombatProfile pad;
    pad.health = 10.0F;
    pad.immune = true;
    const auto immune = test_spawn(world, {.type_name = "Pad", .combat = pad});
    require(immune.ok(), "test spawn must succeed");
    world.submit({.kind = eaw::SimCommandKind::damage, .entities = {*immune}, .amount = 100.0F});
    world.step();
    require(world.alive(*immune) && world.snapshot().entities[0].health == 10.0F, "an immune entity must take no damage");

    // Turrets: the weapon fires only once the turret points at the victim; a limited turret turns the
    // hull toward a target outside its arc, unless its type says it does not.
    {
        const auto engage = [&](const eaw::TurretProfile& turret, const eaw::Vector3& at, const int ticks)
        {
            eaw::SimulationWorld tanks(0.1);
            tanks.set_hostility(hostility);
            eaw::CombatProfile tank;
            tank.health = 100.0F;
            tank.weapons = {eaw::WeaponProfile{.range = 200.0F, .recharge_seconds = 10.0F, .damage = 1.0F}};
            tank.turret = turret;
            eaw::CombatProfile sturdy;
            sturdy.health = 1000.0F;
            (void)test_spawn(tanks, {.type_name = "Tank", .owner = 1, .max_speed = 10.0F, .combat = tank, .turn_rate = 3.14159265F});
            (void)test_spawn(tanks, {.type_name = "Target", .owner = 2, .position = at, .combat = sturdy});
            int first_shot = -1;
            for (int tick = 1; tick <= ticks && first_shot < 0; ++tick)
            {
                tanks.step();
                first_shot = tanks.stats().shots_fired > 0 ? tick : -1;
            }
            return std::pair{first_shot, tanks.snapshot().entities[0].heading};
        };
        // A full turret at 90 degrees per second, the target behind: it turns half a turn first.
        const auto [behind, kept] = engage({.rate = 1.5707964F}, {-100.0F, 0.0F, 0.0F}, 40);
        require(behind >= 19 && behind <= 22 && kept == 0.0F, "a turret must turn to its victim before its weapon fires, the hull staying put");
        // A 10-degree turret, the target to the side: the hull turns; or, without that, nothing fires.
        const auto [side, turned] = engage({.rate = 1.5707964F, .yaw_limit = 0.17453292F}, {0.0F, 100.0F, 0.0F}, 40);
        const auto [stuck, unturned] = engage({.rate = 1.5707964F, .yaw_limit = 0.17453292F, .turns_body = false}, {0.0F, 100.0F, 0.0F}, 40);
        require(side > 0 && turned > 1.2F && stuck < 0 && unturned == 0.0F, "a limited turret must turn its hull to a target beyond its arc, unless its type says no");
        // A turret hardpoint (HardpointProfile::turret): a 20-degree cone ahead on a turret of 90
        // degrees either way at 90 degrees per second. A target to the left waits for the cone to
        // turn onto it; one behind, beyond turret and cone, is never fired at.
        {
            const auto engage_hardpoint = [&](const eaw::Vector3& at, const std::optional<eaw::Vector3> decoy = std::nullopt)
            {
                eaw::SimulationWorld ships(0.1);
                ships.set_hostility(hostility);
                eaw::CombatProfile ship;
                ship.health = 100.0F;
                ship.hardpoints = {eaw::HardpointProfile{.kind = eaw::HardpointProfile::Kind::weapon, .turret = eaw::TurretProfile{.rate = 1.5707964F, .yaw_limit = 1.5707964F, .turns_body = false}}};
                ship.weapons = {eaw::WeaponProfile{.range = 200.0F, .recharge_seconds = 10.0F, .damage = 1.0F, .cone_half_angle = 0.17453292F, .hardpoint = 0}};
                eaw::CombatProfile sturdy;
                sturdy.health = 1000.0F;
                (void)test_spawn(ships, {.type_name = "Ship", .owner = 1, .combat = ship});
                (void)test_spawn(ships, {.type_name = "Target", .owner = 2, .position = at, .combat = sturdy});
                if (decoy)
                {
                    (void)test_spawn(ships, {.type_name = "Decoy", .owner = 2, .position = *decoy, .combat = sturdy});
                }
                int first_shot = -1;
                float yaw = 0.0F;
                for (int tick = 1; tick <= 40 && first_shot < 0; ++tick)
                {
                    ships.step();
                    first_shot = ships.stats().shots_fired > 0 ? tick : -1;
                    yaw = ships.snapshot().hardpoint_turrets.empty() ? 0.0F : ships.snapshot().hardpoint_turrets.front().yaw;
                }
                return std::pair{first_shot, yaw};
            };
            const auto [left, cone_turned] = engage_hardpoint({0.0F, 100.0F, 0.0F});
            const auto [beyond, rest] = engage_hardpoint({-100.0F, 0.0F, 0.0F});
            require(left >= 8 && left <= 11 && cone_turned > 1.3F && cone_turned < 1.58F, "a turret hardpoint must turn its cone onto its victim before it fires, and report its direction");
            require(beyond < 0, "a turret hardpoint must not fire beyond its turret's arc and cone");
            // A nearer hostile behind, out of the turret's arc, may be the ship's own target: the
            // turret still turns to the one on the left without stopping between retarget beats.
            const auto [past_decoy, decoy_turned] = engage_hardpoint({0.0F, 100.0F, 0.0F}, eaw::Vector3{-50.0F, 0.0F, 0.0F});
            require(past_decoy >= 8 && past_decoy <= 14 && decoy_turned > 1.3F, "a turret hardpoint must keep turning toward its own victim between retarget beats");
        }
        // A turret weapon fires from its muzzle turned with the turret: 10 ahead of the pivot, the
        // target to the left, the shot leaves 10 to the left.
        {
            eaw::SimulationWorld tanks(0.1);
            tanks.set_hostility(hostility);
            eaw::CombatProfile tank;
            tank.health = 100.0F;
            tank.weapons = {eaw::WeaponProfile{.range = 200.0F, .recharge_seconds = 10.0F, .damage = 1.0F, .mount = {.forward = 10.0F, .up = 5.0F}, .on_turret = true}};
            tank.turret = eaw::TurretProfile{.rate = 3.14159265F};
            eaw::CombatProfile sturdy;
            sturdy.health = 1000.0F;
            (void)test_spawn(tanks, {.type_name = "Tank", .owner = 1, .combat = tank});
            (void)test_spawn(tanks, {.type_name = "Target", .owner = 2, .position = {0.0F, 100.0F, 0.0F}, .combat = sturdy});
            std::optional<eaw::Vector3> origin;
            for (int tick = 0; tick < 20 && !origin; ++tick)
            {
                tanks.step();
                if (!tanks.snapshot().shots.empty())
                {
                    origin = tanks.snapshot().shots.front().origin;
                }
            }
            require(origin && std::fabs(origin->x) < 0.5F && std::fabs(origin->y - 10.0F) < 0.5F && origin->z == 5.0F, "a turret weapon must fire from its mount turned with the turret");
        }
        // The turret follows what its own weapon can reach: the entity's target is out of the turret
        // weapon's range (a longer hardpoint weapon picked it), a nearer hostile behind gets shot.
        {
            eaw::SimulationWorld tanks(0.1);
            tanks.set_hostility(hostility);
            eaw::CombatProfile tank;
            tank.health = 100.0F;
            tank.hardpoints = {eaw::HardpointProfile{}};
            tank.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 1.0F, .damage = 1.0F, .on_turret = true},
                            eaw::WeaponProfile{.range = 300.0F, .recharge_seconds = 100.0F, .hardpoint = 0}};
            tank.turret = eaw::TurretProfile{.rate = 3.14159265F};
            eaw::CombatProfile sturdy;
            sturdy.health = 1000.0F;
            (void)test_spawn(tanks, {.type_name = "Tank", .owner = 1, .combat = tank});
            (void)test_spawn(tanks, {.type_name = "Far", .owner = 2, .position = {250.0F, 0.0F, 0.0F}, .combat = sturdy});
            for (int tick = 0; tick < 5; ++tick)
            {
                tanks.step();
            }
            (void)test_spawn(tanks, {.type_name = "Near", .owner = 2, .position = {-80.0F, 0.0F, 0.0F}, .combat = sturdy});
            for (int tick = 0; tick < 40; ++tick)
            {
                tanks.step();
            }
            require(tanks.snapshot().entities[2].health < 1000.0F, "a turret must turn to the victim its own weapon reaches, not stay on a target beyond it");
        }
    }
    // An area blast: the victim takes shot and blast; a hostile 30 from the blast takes the second of
    // two rings' damage, one 60 away nothing, a friend beside the victim nothing.
    {
        eaw::SimulationWorld blasting(0.1);
        blasting.set_hostility(hostility);
        eaw::CombatProfile bomber;
        bomber.health = 50.0F;
        bomber.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 10.0F, .damage = 5.0F, .blast = {.damage = 20.0F, .range = 40.0F, .tiers = 2}}};
        eaw::CombatProfile sturdy;
        sturdy.health = 1000.0F;
        (void)test_spawn(blasting, {.type_name = "Bomber", .owner = 1, .combat = bomber});
        (void)test_spawn(blasting, {.type_name = "Victim", .owner = 2, .position = {50.0F, 0.0F, 0.0F}, .combat = sturdy});
        (void)test_spawn(blasting, {.type_name = "Near", .owner = 2, .position = {50.0F, 30.0F, 0.0F}, .combat = sturdy});
        (void)test_spawn(blasting, {.type_name = "Far", .owner = 2, .position = {50.0F, 60.0F, 0.0F}, .combat = sturdy});
        (void)test_spawn(blasting, {.type_name = "Friend", .owner = 1, .position = {55.0F, 0.0F, 0.0F}, .combat = sturdy});
        blasting.step();
        const auto& hurt = blasting.snapshot().entities;
        require(hurt[1].health == 975.0F && hurt[2].health == 990.0F && hurt[3].health == 1000.0F && hurt[4].health == 1000.0F,
                "a blast must damage hostiles by ring within its range, the victim besides the shot, and spare friends");
    }
    // Spinning away (chance 1): the fighter stays 0.5 s out of control, then dies with its spin
    // explosion; without the chance it dies at once.
    for (const float chance : {0.0F, 1.0F})
    {
        eaw::SimulationWorld spin_world(0.1);
        eaw::CombatProfile fighter;
        fighter.health = 10.0F;
        fighter.death_effect = 1U;
        fighter.spin_chance = chance;
        fighter.spin_seconds = 0.5F;
        fighter.spin_effect = 2U;
        const auto craft = test_spawn(spin_world, {.type_name = "Fighter", .combat = fighter});
        require(craft.ok(), "test spawn must succeed");
        spin_world.submit({.kind = eaw::SimCommandKind::damage, .entities = {*craft}, .amount = 100.0F});
        spin_world.step();
        int ticks = 1;
        std::uint32_t effect = spin_world.snapshot().deaths.empty() ? 0U : spin_world.snapshot().deaths.front().death_effect;
        while (spin_world.alive(*craft) && ticks < 20)
        {
            spin_world.step();
            ++ticks;
            if (!spin_world.snapshot().deaths.empty())
            {
                effect = spin_world.snapshot().deaths.front().death_effect;
            }
        }
        require(chance == 0.0F ? ticks == 1 && effect == 1U : ticks == 6 && effect == 2U, "a spin away must delay the death by its time and end in its explosion");
    }
    // Projectile flight rules: a lifetime ends a shot early; a scan range retargets a shot whose
    // target died; an appearance delay holds the burst's first shot; a friendly blast amount hurts
    // the shooter's own entities.
    {
        const auto shoot = [&](const eaw::WeaponProfile& weapon, const bool kill_first)
        {
            eaw::SimulationWorld flight(0.1);
            flight.set_hostility(hostility);
            eaw::CombatProfile gun;
            gun.health = 50.0F;
            gun.weapons = {weapon};
            eaw::CombatProfile sturdy;
            sturdy.health = 1000.0F;
            (void)test_spawn(flight, {.type_name = "Gun", .owner = 1, .combat = gun});
            const auto first = test_spawn(flight, {.type_name = "First", .owner = 2, .position = {80.0F, 0.0F, 0.0F}, .combat = sturdy});
            (void)test_spawn(flight, {.type_name = "Second", .owner = 2, .position = {90.0F, 30.0F, 0.0F}, .combat = sturdy});
            (void)test_spawn(flight, {.type_name = "Mate", .owner = 1, .position = {85.0F, 0.0F, 0.0F}, .combat = sturdy});
            flight.step();
            if (kill_first)
            {
                flight.submit({.kind = eaw::SimCommandKind::damage, .entities = {*first}, .amount = 5000.0F});
            }
            int fired_at = -1;
            for (int tick = 1; tick < 30; ++tick)
            {
                if (fired_at < 0 && flight.stats().shots_fired > 0)
                {
                    fired_at = tick;
                }
                flight.step();
            }
            return std::pair{flight.stats(), fired_at};
        };
        // 200 units/s: the shot at the hostile 80 away flies 0.4 s.
        const eaw::WeaponProfile slow{.range = 100.0F, .recharge_seconds = 100.0F, .damage = 1.0F, .projectile_speed = 200.0F};
        auto short_lived = slow;
        short_lived.max_lifetime = 0.2F;
        require(shoot(slow, false).first.hits == 1 && shoot(short_lived, false).first.hits == 0, "a shot must be lost when its lifetime ends");
        auto scanning = slow;
        scanning.scan_range = 100.0F;
        require(shoot(slow, true).first.hits == 0 && shoot(scanning, true).first.hits == 1, "a scanning shot must take a new target when its own dies");
        const eaw::WeaponProfile instant{.range = 100.0F, .recharge_seconds = 100.0F, .damage = 1.0F};
        auto delayed = instant;
        delayed.appearance_delay_seconds = 0.5F;
        require(shoot(delayed, false).second == shoot(instant, false).second + 5, "an appearance delay must hold the burst's first shot");
    }
    // A friendly blast amount (Friendly_Damage_Amount): the shooter's mate beside the victim is hurt.
    {
        eaw::SimulationWorld mates(0.1);
        mates.set_hostility(hostility);
        eaw::CombatProfile bomber;
        bomber.health = 50.0F;
        bomber.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 100.0F, .damage = 1.0F, .blast = {.damage = 50.0F, .range = 10.0F, .friendly_damage = 20.0F}}};
        eaw::CombatProfile sturdy;
        sturdy.health = 1000.0F;
        (void)test_spawn(mates, {.type_name = "Bomber", .owner = 1, .combat = bomber});
        (void)test_spawn(mates, {.type_name = "Victim", .owner = 2, .position = {80.0F, 0.0F, 0.0F}, .combat = sturdy});
        (void)test_spawn(mates, {.type_name = "Mate", .owner = 1, .position = {85.0F, 0.0F, 0.0F}, .combat = sturdy});
        mates.step();
        const auto& hurt = mates.snapshot().entities;
        require(hurt[1].health == 949.0F && hurt[2].health == 980.0F && hurt[0].health == 50.0F, "a blast's friendly amount must hurt the shooter's own entities in range");
    }
    // A death blast (Death_Projectiles): the dying station damages the hostile beside it.
    {
        eaw::SimulationWorld station_world(0.1);
        station_world.set_hostility(hostility);
        eaw::CombatProfile station;
        station.health = 10.0F;
        station.death_blast = {.damage = 50.0F, .range = 100.0F};
        station.death_blast_visual = 33U;
        eaw::CombatProfile sturdy;
        sturdy.health = 1000.0F;
        const auto doomed = test_spawn(station_world, {.type_name = "Station", .owner = 2, .combat = station});
        (void)test_spawn(station_world, {.type_name = "Ship", .owner = 1, .position = {60.0F, 0.0F, 0.0F}, .combat = sturdy});
        require(doomed.ok(), "test spawn must succeed");
        station_world.submit({.kind = eaw::SimCommandKind::damage, .entities = {*doomed}, .amount = 100.0F});
        station_world.step();
        require(!station_world.alive(*doomed) && station_world.snapshot().entities[0].health == 950.0F, "a death blast must damage the hostiles around the dead");
        const auto& impacts = station_world.snapshot().impacts;
        require(std::ranges::count_if(impacts,
                                      [](const eaw::ImpactSnapshot& impact)
                                      {
                                          return impact.kind == eaw::ImpactKind::detonated && impact.visual == 33U && impact.position == eaw::Vector3{};
                                      }) == 1,
                "a death blast must report its projectile's detonation where the dead lay");
    }
    // Three muzzles (MuzzleA_nn): shots cycle through them, the first from the main one.
    {
        eaw::SimulationWorld twin(0.1);
        twin.set_hostility(hostility);
        eaw::CombatProfile guns;
        guns.health = 50.0F;
        guns.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 0.1F, .damage = 1.0F, .mount = {.left = 5.0F}, .extra_mounts = {{.left = -5.0F}, {.left = 0.0F}}}};
        eaw::CombatProfile sturdy;
        sturdy.health = 1000.0F;
        (void)test_spawn(twin, {.type_name = "Twin", .owner = 1, .combat = guns});
        (void)test_spawn(twin, {.type_name = "Target", .owner = 2, .position = {50.0F, 0.0F, 0.0F}, .combat = sturdy});
        std::vector<float> sides;
        for (int tick = 0; tick < 8 && sides.size() < 4; ++tick)
        {
            twin.step();
            for (const auto& shot : twin.snapshot().shots)
            {
                sides.push_back(shot.origin.y);
            }
        }
        require(sides.size() == 4 && sides[0] > 4.0F && sides[1] < -4.0F && std::fabs(sides[2]) < 1.0F && sides[3] > 4.0F, "shots must cycle through the muzzles in order");
    }
    // Stickiness: a shooter keeps the low-priority target it found, then after 1 s looks again and
    // takes the preferred one that arrived meanwhile.
    for (const float stickiness : {0.0F, 1.0F})
    {
        eaw::SimulationWorld sticky(0.1);
        sticky.set_hostility(hostility);
        eaw::CombatProfile picky;
        picky.health = 50.0F;
        picky.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 10.0F, .damage = 1.0F}};
        picky.targeting.ranks = {{.categories = 0x200U, .rank = 1.0F}};
        picky.stickiness_seconds = stickiness;
        eaw::CombatProfile sturdy;
        sturdy.health = 1000.0F;
        auto preferred = sturdy;
        preferred.categories = 0x200U;
        (void)test_spawn(sticky, {.type_name = "Shooter", .owner = 1, .combat = picky});
        const auto first = test_spawn(sticky, {.type_name = "First", .owner = 2, .position = {50.0F, 0.0F, 0.0F}, .combat = sturdy});
        sticky.step();
        require(first.ok() && sticky.snapshot().entities[0].target == *first, "the only hostile must be targeted");
        const auto better = test_spawn(sticky, {.type_name = "Better", .owner = 2, .position = {80.0F, 0.0F, 0.0F}, .combat = preferred});
        for (int tick = 0; tick < 15; ++tick)
        {
            sticky.step();
        }
        require(better.ok() && (sticky.snapshot().entities[0].target == *better) == (stickiness > 0.0F), "a sticky target must be weighed again after the stickiness, and kept without one");
    }
}

// The tick's damage is queued and resolved after combat (docs/ecs-design.md, 6.9 rule 4).
void test_damage_resolution()
{
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::CombatProfile gun;
    gun.health = 50.0F;
    gun.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 10.0F, .damage = 10.0F}};
    eaw::CombatProfile sturdy;
    sturdy.health = 1000.0F;
    // Two shooters ordered at a target fire at once, and the first shot kills: both shots land, the
    // second on the dead body, without damage.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        eaw::CombatProfile frail;
        frail.health = 10.0F;
        const auto first = test_spawn(world, {.type_name = "First", .owner = 1, .combat = gun});
        const auto second = test_spawn(world, {.type_name = "Second", .owner = 1, .position = {0.0F, 10.0F, 0.0F}, .combat = gun});
        const auto target = test_spawn(world, {.type_name = "Target", .owner = 2, .position = {50.0F, 0.0F, 0.0F}, .combat = frail});
        require(first.ok() && second.ok() && target.ok(), "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::attack, .entities = {*first, *second}, .target_entity = *target});
        world.step();
        const auto& impacts = world.snapshot().impacts;
        require(!world.alive(*target) && world.stats().kills == 1 && world.stats().hits == 2 && impacts.size() == 2 && impacts[0].target == *target && impacts[1].target == *target &&
                    impacts[1].kind == eaw::ImpactKind::hull && world.snapshot().deaths.size() == 1,
                "two shots at once must both hit, the second on the dead body");
    }
    // A chain: the first station's death blast kills the second, whose blast reaches the ship, all
    // in one tick.
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        eaw::CombatProfile station;
        station.health = 10.0F;
        station.death_blast = {.damage = 50.0F, .range = 30.0F};
        const auto first = test_spawn(world, {.type_name = "Station", .owner = 2, .combat = station});
        const auto second = test_spawn(world, {.type_name = "Station", .owner = 1, .position = {20.0F, 0.0F, 0.0F}, .combat = station});
        (void)test_spawn(world, {.type_name = "Ship", .owner = 2, .position = {45.0F, 0.0F, 0.0F}, .combat = sturdy});
        require(first.ok() && second.ok(), "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::damage, .entities = {*first}, .amount = 100.0F});
        world.step();
        require(!world.alive(*first) && !world.alive(*second) && world.stats().kills == 2 && world.snapshot().entities.size() == 1 && world.snapshot().entities[0].health == 950.0F,
                "a death blast must kill in the tick it goes off, and the blasts it sets off too");
    }
    // Removal keeps the survivors' order: the dense order stays the spawn order.
    {
        eaw::SimulationWorld world(0.1);
        std::vector<eaw::EntityId> ids;
        for (int index = 0; index < 5; ++index)
        {
            const auto id = test_spawn(world, {.type_name = "Rock", .owner = 1, .position = {static_cast<float>(index) * 100.0F, 0.0F, 0.0F}, .combat = sturdy});
            require(id.ok(), "test spawn must succeed");
            ids.push_back(*id);
        }
        world.submit({.kind = eaw::SimCommandKind::destroy, .entities = {ids[1], ids[3]}});
        world.step();
        const auto& entities = world.snapshot().entities;
        require(entities.size() == 3 && entities[0].id == ids[0] && entities[1].id == ids[2] && entities[2].id == ids[4], "removal must keep the survivors in spawn order");
    }
}

const ModuleRegistrar registrar("combat", {
                                              {"test_combat", &test_combat},
                                              {"test_target_validity", &test_target_validity},
                                              {"test_hardpoints_and_fire_cones", &test_hardpoints_and_fire_cones},
                                              {"test_body_aiming", &test_body_aiming},
                                              {"test_stances", &test_stances},
                                              {"test_standing_orders", &test_standing_orders},
                                              {"test_scripted_conditions", &test_scripted_conditions},
                                              {"test_inaccuracy", &test_inaccuracy},
                                              {"test_target_priorities", &test_target_priorities},
                                              {"test_damage_resolution", &test_damage_resolution},
                                          });

} // namespace
