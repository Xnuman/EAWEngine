// Combat: targeting, weapons, damage, hardpoints, body aiming, and stances.
#include "test_support.hpp"

#include "eaw/formation.hpp"

#include <tuple>

#include <memory>
#include <numbers>

namespace {

using namespace eaw_test;

void test_combat()
{
    // Broad phase: items within the radius, cells row by row, ascending index within a cell.
    eaw::SpatialGrid grid;
    const std::vector<eaw::SimVector3> points{{0.0F, 0.0F, 0.0F}, {300.0F, 0.0F, 0.0F}, {10.0F, 5.0F, 0.0F}, {0.0F, 600.0F, 0.0F}, {std::nanf(""), 0.0F, 0.0F}};
    grid.build(points, 256.0F);
    std::vector<std::uint32_t> near;
    grid.query({0.0F, 0.0F, 0.0F}, 50.0F, near);
    require(near == std::vector<std::uint32_t>{0, 2} && grid.size() == 4, "the spatial grid must return items within the radius in a fixed order");
    near.clear();
    grid.query({150.0F, 0.0F, 0.0F}, 160.0F, near);
    require(near == std::vector<std::uint32_t>{0, 2, 1}, "queries spanning cells must visit cells in order");

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
    const auto shooter = world.spawn({.type_name = "Shooter", .owner = 1, .combat = gun});
    const auto victim = world.spawn({.type_name = "Victim", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = target});
    const auto bystander = world.spawn({.type_name = "Neutral", .owner = 3, .position = {20.0F, 0.0F, 0.0F}, .combat = target});
    const auto distant = world.spawn({.type_name = "Distant", .owner = 2, .position = {500.0F, 0.0F, 0.0F}, .combat = target});
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
        (void)shielded_world.spawn({.type_name = "Shooter", .owner = 1, .combat = gun});
        (void)shielded_world.spawn({.type_name = "Shielded", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = shielded});
        shielded_world.step();
        const auto& hit = shielded_world.snapshot().entities[1];
        require(hit.shield == 0.0F && hit.health == 24.0F, "damage beyond the shield must reach the hull");
    }

    // Projectiles fly at their speed and hit later.
    {
        auto flight_world = make_world();
        auto cannon = gun;
        cannon.weapons[0].projectile_speed = 100.0F;
        cannon.weapons[0].pulse_count = 1;
        (void)flight_world.spawn({.type_name = "Cannon", .owner = 1, .combat = cannon});
        (void)flight_world.spawn({.type_name = "Victim", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = target});
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
        const auto hunter = chase_world.spawn({.type_name = "Hunter", .owner = 1, .max_speed = 100.0F, .combat = mobile});
        const auto prey = chase_world.spawn({.type_name = "Prey", .owner = 2, .position = {400.0F, 0.0F, 0.0F}, .combat = target});
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
        (void)replay.spawn({.type_name = "A", .owner = 1, .max_speed = 50.0F, .combat = gun});
        (void)replay.spawn({.type_name = "B", .owner = 2, .position = {150.0F, 30.0F, 0.0F}, .max_speed = 50.0F, .combat = gun});
        for (int tick = 0; tick < 40; ++tick)
        {
            replay.step();
        }
        return replay.state_hash();
    };
    require(run() == run(), "combat must be deterministic");

    auto invalid = gun;
    invalid.weapons[0].range = -1.0F;
    require(!world.spawn({.type_name = "Invalid", .combat = invalid}), "a negative weapon range must be rejected");
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
    ship.shield_regen_per_second = 10.0F;
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
        (void)world.spawn({.type_name = "Ship", .owner = 1, .combat = ship});
        const auto behind = world.spawn({.type_name = "Behind", .owner = 2, .position = {-200.0F, 0.0F, 0.0F}, .combat = target});
        require(behind.ok(), "test spawn must succeed");
        for (int tick = 0; tick < 20; ++tick)
        {
            world.step();
        }
        require(world.stats().shots_fired == 0, "a weapon must not fire outside its arc");
        (void)world.spawn({.type_name = "Ahead", .owner = 2, .position = {200.0F, 0.0F, 0.0F}, .combat = target});
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
        eaw::CombatProfile gun;
        gun.health = 100.0F;
        gun.weapons = {eaw::WeaponProfile{.range = 500.0F, .recharge_seconds = 0.1F, .damage = 10.0F, .damages_shield = false}};
        // The victim's engine (20 behind its center) faces the attacker, which stands behind it.
        const auto victim = world.spawn({.type_name = "Victim", .owner = 2, .position = {0.0F, 0.0F, 0.0F}, .max_speed = 40.0F, .combat = victim_profile});
        (void)world.spawn({.type_name = "Gun", .owner = 1, .position = {-200.0F, 0.0F, 0.0F}, .combat = gun});
        require(victim.ok(), "test spawn must succeed");
        // Four shots of 10: three destroy the 30-point engine, the fourth moves on to the next part.
        for (int tick = 0; tick < 4; ++tick)
        {
            world.step();
        }
        const auto& damaged = world.snapshot().entities[0];
        require(damaged.health < 1000.0F && damaged.hardpoints == 3 && damaged.hardpoints_destroyed == 1 && world.stats().hardpoints_destroyed == 1, "the nearest hardpoint must be hit and destroyed");
        // Without its engine the ship moves at a quarter of its speed.
        world.submit({eaw::SimCommandKind::move, {*victim}, {0.0F, 400.0F, 0.0F}});
        const auto before = world.snapshot().entities[0].position.y;
        world.step();
        require(std::fabs(world.snapshot().entities[0].position.y - before - 1.0F) < 1.0e-3F, "a ship without engines must move at a quarter speed");
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
    const auto make = [&](const bool body, const float heading, const eaw::SimVector3 target_at)
    {
        auto world = std::make_unique<eaw::SimulationWorld>(0.1);
        world->set_hostility(hostility);
        const auto shooter =
            world->spawn({.type_name = "Trooper", .owner = 1, .heading = heading, .max_speed = 20.0F, .combat = rifle, .turn_rate = std::numbers::pi_v<float> / 2.0F, .aims_with_body = body});
        const auto victim = world->spawn({.type_name = "Target", .owner = 2, .position = target_at, .combat = target});
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
        const auto quiet = world.spawn({.type_name = "Quiet", .owner = 1, .combat = armed, .stance = eaw::Stance::hold_fire});
        eaw::CombatProfile target = armed;
        target.weapons.clear();
        const auto victim = world.spawn({.type_name = "Target", .owner = 2, .position = {200.0F, 0.0F, 0.0F}, .combat = target});
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
        require(!world.spawn({.type_name = "Bad", .stance = static_cast<eaw::Stance>(7)}).ok(), "a spawn with an unknown stance must be rejected");
    }

    // A fighter holding position keeps circling where it is and fires at what comes in range; an
    // aggressive one leaves to fly attack runs at it.
    for (const auto stance : {eaw::Stance::hold_position, eaw::Stance::aggressive})
    {
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        const auto fighter =
            world.spawn({.type_name = "Fighter", .owner = 1, .max_speed = 120.0F, .combat = armed, .turn_rate = std::numbers::pi_v<float> / 2.0F, .min_speed = 75.0F, .stance = stance});
        eaw::CombatProfile station = armed;
        station.weapons.clear();
        const auto target = world.spawn({.type_name = "Station", .owner = 2, .position = {400.0F, 0.0F, 0.0F}, .combat = station});
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

const ModuleRegistrar registrar("combat", {
                                              {"test_combat", &test_combat},
                                              {"test_hardpoints_and_fire_cones", &test_hardpoints_and_fire_cones},
                                              {"test_body_aiming", &test_body_aiming},
                                              {"test_stances", &test_stances},
                                          });

} // namespace
