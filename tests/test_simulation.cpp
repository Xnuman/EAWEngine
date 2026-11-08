#include "test_support.hpp"

#include "eaw/formation.hpp"

#include <tuple>

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

void test_ground_movement()
{
    // A 6x3 field (20 units per cell) rising 5 units per column, with a blocked column at x = 4.
    eaw::GroundMap ground;
    ground.width = 6;
    ground.height = 3;
    ground.spacing = 20.0F;
    for (std::uint32_t y = 0; y < ground.height; ++y)
    {
        for (std::uint32_t x = 0; x < ground.width; ++x)
        {
            ground.heights.push_back(5.0F * static_cast<float>(x));
            ground.passability.push_back(x == 4 ? eaw::passability::steep : eaw::passability::clear);
        }
    }
    require(ground.passability_at(81.0F, 20.0F) == eaw::passability::steep && ground.passability_at(39.0F, 20.0F) == eaw::passability::clear && !ground.passability_at(-30.0F, 0.0F),
            "passability must be looked up at the nearest cell");

    eaw::SimulationWorld world(0.1);
    world.set_ground(ground);
    const auto walker = world.spawn({.type_name = "Walker", .position = {0.0F, 20.0F, 999.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear});
    const auto flyer = world.spawn({.type_name = "Flyer", .position = {0.0F, 20.0F, 50.0F}, .max_speed = 100.0F});
    require(walker && flyer, "test spawns must succeed");
    world.step();
    require(world.snapshot().entities[0].position.z == 0.0F && world.snapshot().entities[1].position.z == 50.0F, "a ground unit spawns on the surface; others keep their height");

    // Ordered across the slope beyond the blocked column: the walker climbs with the terrain and,
    // since the column cuts the field, ends at the closest reachable cell (x = 60); the flyer keeps
    // a straight line.
    world.submit({eaw::SimCommandKind::move, {*walker, *flyer}, {100.0F, 20.0F, 50.0F}});
    world.step();
    const auto& moved = world.snapshot().entities;
    require(std::fabs(moved[0].position.x - 10.0F) < 1.0e-4F && std::fabs(moved[0].position.z - 2.5F) < 1.0e-4F, "a ground unit must take its height from the surface");
    for (int tick = 0; tick < 20; ++tick)
    {
        world.step();
    }
    const auto& stopped = world.snapshot().entities;
    require(!stopped[0].moving && stopped[0].position.x == 60.0F && world.stats().partial_paths == 1 && world.stats().blocked_moves == 0,
            "a ground unit ordered into an unreachable region must stop at the closest reachable cell");
    require(stopped[1].position.x == 100.0F && stopped[1].position.z == 50.0F, "a unit that does not follow the ground ignores passability");

    // A unit standing on a blocked cell may leave it.
    const auto stranded = world.spawn({.type_name = "Stranded", .position = {80.0F, 0.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear});
    require(stranded.ok(), "test spawn must succeed");
    world.submit({eaw::SimCommandKind::move, {*stranded}, {100.0F, 0.0F, 0.0F}});
    for (int tick = 0; tick < 5; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[2].position.x == 100.0F && std::fabs(world.snapshot().entities[2].position.z - 25.0F) < 1.0e-4F, "leaving a blocked cell must be allowed");

    // A climbing class may enter steep cells.
    const auto climber =
        world.spawn({.type_name = "Climber", .position = {60.0F, 40.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear | eaw::passability::steep});
    require(climber.ok(), "test spawn must succeed");
    world.submit({eaw::SimCommandKind::move, {*climber}, {100.0F, 40.0F, 0.0F}});
    for (int tick = 0; tick < 5; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[3].position.x == 100.0F, "a class allowed on steep ground must cross it");

    // Movement class lists parse into masks; unknown names are reported.
    require(eaw::parse_passability_list(" Clear | Shield | InfantryOnly ").value_or(0) == (eaw::passability::clear | eaw::passability::shield | eaw::passability::infantry_only) &&
                eaw::parse_passability_list("water").value_or(0) == eaw::passability::water,
            "passability lists must parse case-insensitively");
    const auto unknown = eaw::parse_passability_list("Clear | Lava");
    require(!unknown && unknown.error().message.find("Lava") != std::string::npos, "an unknown passability type must be named");

    // The liquid surface carries ground units over lower terrain.
    eaw::GroundMap flooded = ground;
    flooded.liquid_level = 12.0F;
    require(flooded.surface_height(0.0F, 0.0F) == 12.0F && flooded.surface_height(100.0F, 0.0F) == 25.0F, "the surface is the higher of terrain and liquid");
}

void test_pathfinding()
{
    // A 12x12 clear field (10 units per cell) split by a steep wall at column 6 with a gap at row 9.
    eaw::GroundMap ground;
    ground.width = 12;
    ground.height = 12;
    ground.spacing = 10.0F;
    ground.heights.assign(144, 0.0F);
    for (std::uint32_t y = 0; y < ground.height; ++y)
    {
        for (std::uint32_t x = 0; x < ground.width; ++x)
        {
            ground.passability.push_back(x == 6 && y != 9 ? eaw::passability::steep : eaw::passability::clear);
        }
    }
    const auto clear = eaw::passability::clear;
    require(!eaw::ground_line_passable(ground, {10.0F, 10.0F}, {110.0F, 10.0F}, clear) && eaw::ground_line_passable(ground, {10.0F, 90.0F}, {110.0F, 90.0F}, clear),
            "a straight line must be checked against every cell it crosses");

    eaw::GridPathfinder pathfinder;
    const auto path = pathfinder.find(ground, {10.0F, 10.0F}, {110.0F, 12.0F}, clear);
    require(path.status == eaw::PathStatus::reached && !path.waypoints.empty() && path.waypoints.back() == eaw::PathPoint{110.0F, 12.0F}, "a reachable goal must end the path exactly");
    bool through_gap = false;
    eaw::PathPoint previous{10.0F, 10.0F};
    for (const auto& waypoint : path.waypoints)
    {
        require(eaw::ground_line_passable(ground, previous, waypoint, clear), "every path segment must stay on enterable cells");
        through_gap = through_gap || waypoint.y >= 80.0F;
        previous = waypoint;
    }
    require(through_gap && path.waypoints.size() <= 4, "the path must go through the gap with its redundant cell waypoints removed");
    const auto again = pathfinder.find(ground, {10.0F, 10.0F}, {110.0F, 12.0F}, clear);
    require(again.waypoints == path.waypoints && again.expanded == path.expanded, "pathfinding must be deterministic");

    // A goal on the wall moves to the nearest enterable cell (of two equally near, the start's side).
    const auto onto_wall = pathfinder.find(ground, {10.0F, 10.0F}, {60.0F, 10.0F}, clear);
    require(onto_wall.status == eaw::PathStatus::partial && onto_wall.waypoints.back() == eaw::PathPoint{50.0F, 10.0F}, "a goal the class may not enter must move to the nearest cell");
    // A class allowed on steep ground goes straight through.
    const auto climbing = pathfinder.find(ground, {10.0F, 10.0F}, {110.0F, 10.0F}, clear | eaw::passability::steep);
    require(climbing.status == eaw::PathStatus::reached && climbing.waypoints.size() == 1, "a class allowed on the wall must cross it directly");

    // Closing the gap leaves the goal unreachable: the path ends at the closest reachable cell.
    auto closed = ground;
    closed.passability[9 * 12 + 6] = eaw::passability::steep;
    eaw::GridPathfinder fresh;
    const auto blocked = fresh.find(closed, {10.0F, 10.0F}, {110.0F, 10.0F}, clear);
    require(blocked.status == eaw::PathStatus::partial && blocked.waypoints.back() == eaw::PathPoint{50.0F, 10.0F} && blocked.expanded <= 10,
            "an unreachable goal must end at the closest reachable cell without searching the whole region");

    // A start on the wall first leaves for the nearest enterable cell on the goal's side: with the
    // gap closed, only that side reaches the goal.
    const auto stranded = fresh.find(closed, {60.0F, 10.0F}, {110.0F, 10.0F}, clear);
    require(stranded.status == eaw::PathStatus::reached && stranded.waypoints.back() == eaw::PathPoint{110.0F, 10.0F}, "a unit on a cell its class may not enter must leave it toward the goal");
    const auto boxed = fresh.find(closed, {60.0F, 10.0F}, {10.0F, 10.0F}, clear);
    require(boxed.status == eaw::PathStatus::reached && boxed.waypoints.back() == eaw::PathPoint{10.0F, 10.0F}, "leaving a blocked cell must work in either direction");

    eaw::GridPathfinder limited(3);
    const auto budgeted = limited.find(ground, {10.0F, 10.0F}, {110.0F, 12.0F}, clear);
    require(budgeted.status == eaw::PathStatus::partial && budgeted.expanded == 3, "the search must stop at its expansion budget");

    // A diagonal through a cell corner needs both side cells.
    eaw::GroundMap corner;
    corner.width = 3;
    corner.height = 3;
    corner.spacing = 10.0F;
    corner.heights.assign(9, 0.0F);
    corner.passability.assign(9, clear);
    corner.passability[1] = eaw::passability::steep;
    require(!eaw::ground_line_passable(corner, {0.0F, 0.0F}, {20.0F, 20.0F}, clear) && eaw::ground_line_passable(corner, {0.0F, 10.0F}, {20.0F, 20.0F}, clear),
            "a line through a corner must check both side cells");
    const auto around = pathfinder.find(corner, {0.0F, 0.0F}, {20.0F, 0.0F}, clear);
    require(around.status == eaw::PathStatus::reached && around.waypoints.size() == 3 && around.waypoints[0] == eaw::PathPoint{0.0F, 10.0F}, "a path must not cut a blocked corner");

    auto no_data = ground;
    no_data.passability.clear();
    require(pathfinder.find(no_data, {0.0F, 0.0F}, {50.0F, 50.0F}, clear).status == eaw::PathStatus::unavailable, "ground without passability must not constrain paths");

    // The simulation plans ground orders and follows the waypoints to the ordered point.
    eaw::SimulationWorld world(0.1);
    world.set_ground(ground);
    const auto walker = world.spawn({.type_name = "Walker", .position = {10.0F, 10.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = clear});
    require(walker.ok(), "test spawn must succeed");
    world.submit({eaw::SimCommandKind::move, {*walker}, {110.0F, 12.0F, 0.0F}});
    world.step();
    const auto route = world.route(*walker);
    require(!route.empty() && route.back().x == 110.0F && route.back().y == 12.0F && world.stats().path_searches == 1 && world.stats().path_expansions > 0,
            "a ground order must be planned and expose its route");
    for (int tick = 0; tick < 40; ++tick)
    {
        world.step();
    }
    const auto& arrived = world.snapshot().entities[0];
    require(!arrived.moving && arrived.position.x == 110.0F && arrived.position.y == 12.0F && world.stats().blocked_moves == 0 && world.route(*walker).empty(),
            "a ground unit must follow its path around the wall");
}

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
        for (int tick = 0; tick < 6; ++tick)
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

void test_turning_and_separation()
{
    // A flat 20x20 clear field, 10 units per cell, with one steep cell at (15, 10).
    eaw::GroundMap ground;
    ground.width = 20;
    ground.height = 20;
    ground.spacing = 10.0F;
    ground.heights.assign(400, 0.0F);
    ground.passability.assign(400, eaw::passability::clear);
    ground.passability[10 * 20 + 15] = eaw::passability::steep;

    // Turning: a unit facing +X ordered to -X turns at its rate before it moves.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto pi = std::numbers::pi_v<float>;
        const auto turner =
            world.spawn({.type_name = "Turner", .position = {100.0F, 50.0F, 0.0F}, .max_speed = 50.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .turn_rate = pi / 2.0F});
        require(turner.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*turner}, {20.0F, 50.0F, 0.0F}});
        world.step();
        const auto& first = world.snapshot().entities[0];
        require(std::fabs(first.heading - pi / 20.0F) < 1.0e-4F && first.position.x == 100.0F, "a unit facing away must turn at its rate without moving");
        for (int tick = 0; tick < 19; ++tick)
        {
            world.step();
        }
        const auto& turned = world.snapshot().entities[0];
        require(std::fabs(std::fabs(turned.heading) - pi) < 1.0e-4F && turned.position.x < 100.0F, "a unit must move once it faces its way");
        for (int tick = 0; tick < 40; ++tick)
        {
            world.step();
        }
        require(world.snapshot().entities[0].position.x == 20.0F, "a turning unit must still arrive");
    }

    // Separation: overlapping idle units part, an immobile one stays put, and nobody is pushed onto
    // ground its class may not enter.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto a = world.spawn({.type_name = "A", .position = {50.0F, 50.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto b = world.spawn({.type_name = "B", .position = {52.0F, 50.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto wall = world.spawn({.type_name = "Bunker", .position = {80.0F, 80.0F, 0.0F}, .follows_ground = true, .radius = 10.0F});
        const auto c = world.spawn({.type_name = "C", .position = {82.0F, 80.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto d = world.spawn({.type_name = "D", .position = {141.0F, 100.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto e = world.spawn({.type_name = "E", .position = {139.0F, 100.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(a && b && wall && c && d && e, "test spawns must succeed");
        for (int tick = 0; tick < 20; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        const auto gap = [&](const std::size_t first, const std::size_t second)
        {
            return std::hypot(entities[second].position.x - entities[first].position.x, entities[second].position.y - entities[first].position.y);
        };
        require(gap(0, 1) >= 9.99F && std::fabs(entities[0].position.x + entities[1].position.x - 102.0F) < 1.0e-3F, "overlapping equal units must part symmetrically");
        require(entities[2].position.x == 80.0F && entities[2].position.y == 80.0F && gap(2, 3) >= 14.99F, "an immobile entity must not be pushed but must push");
        require(world.snapshot().entities[4].position.x < 145.0F, "a push must not move a unit onto a cell it may not enter");
        require(world.stats().separated_pairs == 0, "separated units must stop pushing once apart");
    }
    // A unit walking into an idle unit of another owner does not push it aside.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto walker =
            world.spawn({.type_name = "Walker", .owner = 1, .position = {40.0F, 150.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto enemy =
            world.spawn({.type_name = "Enemy", .owner = 2, .position = {60.0F, 150.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(walker && enemy, "test spawns must succeed");
        world.submit({eaw::SimCommandKind::move, {*walker}, {80.0F, 150.0F, 0.0F}});
        for (int tick = 0; tick < 30; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        require(entities[1].position.x == 60.0F && entities[1].position.y == 150.0F && entities[0].position.x < 51.0F, "an idle foreign unit must stand its ground");
    }
    {
        const auto replay = [&]
        {
            eaw::SimulationWorld world(0.1);
            world.set_ground(ground);
            for (int index = 0; index < 12; ++index)
            {
                (void)world.spawn({.type_name = "Crowd",
                                   .position = {60.0F + static_cast<float>(index % 3), 60.0F + static_cast<float>(index / 3), 0.0F},
                                   .max_speed = 30.0F,
                                   .follows_ground = true,
                                   .passable_types = eaw::passability::clear,
                                   .radius = 4.0F,
                                   .turn_rate = 3.0F});
            }
            for (int tick = 0; tick < 30; ++tick)
            {
                world.step();
            }
            return world.state_hash();
        };
        require(replay() == replay(), "separation must be deterministic");
    }
    eaw::SimulationWorld world(0.1);
    require(!world.spawn({.type_name = "Bad", .radius = -1.0F}) && !world.spawn({.type_name = "Bad", .turn_rate = std::nanf("")}), "invalid radii and turn rates must be rejected");
}

void test_formations()
{
    // Six equal members (pitch 2 x 5 x 1.5 = 15): rows of four and two facing +X.
    std::vector<eaw::FormationMember> members;
    for (std::uint32_t index = 0; index < 6; ++index)
    {
        members.push_back({{index + 1, 1}, {static_cast<float>(index) * 3.0F, 0.0F, 0.0F}, 5.0F, 0.5F, 2});
    }
    members[5].order = 1;
    const eaw::SimVector3 target{100.0F, 200.0F, 7.0F};
    const auto slots = eaw::formation_slots(members, target, 0.0F);
    require(slots.size() == 6, "every member must get a slot");
    float min_x = 1.0e9F;
    float max_x = -1.0e9F;
    float min_y = 1.0e9F;
    float max_y = -1.0e9F;
    for (std::size_t first = 0; first < slots.size(); ++first)
    {
        require(slots[first].id == members[first].id && slots[first].position.z == 7.0F, "slots must follow member order and take the target height");
        min_x = std::min(min_x, slots[first].position.x);
        max_x = std::max(max_x, slots[first].position.x);
        min_y = std::min(min_y, slots[first].position.y);
        max_y = std::max(max_y, slots[first].position.y);
        for (auto second = first + 1; second < slots.size(); ++second)
        {
            require(std::hypot(slots[first].position.x - slots[second].position.x, slots[first].position.y - slots[second].position.y) >= 14.99F, "slots must keep the members' pitch");
        }
    }
    require(std::fabs((min_x + max_x) * 0.5F - target.x) < 1.0e-3F && std::fabs((min_y + max_y) * 0.5F - target.y) < 1.0e-3F && std::fabs(max_x - min_x - 15.0F) < 1.0e-3F,
            "the block must be centered on the target, two rows deep");
    require(slots[5].position.x > target.x, "the lowest order must stand in the front row");
    // Members take slots by where they stand across the facing: +X facing puts -Y on the right.
    const std::vector<eaw::FormationMember> pair{{{1, 1}, {0.0F, 50.0F, 0.0F}, 5.0F, 0.5F, 2}, {{2, 1}, {0.0F, -50.0F, 0.0F}, 5.0F, 0.5F, 2}};
    const auto paired = eaw::formation_slots(pair, {100.0F, 0.0F, 0.0F}, 0.0F);
    require(paired[0].position.y > paired[1].position.y, "members must keep their side so their paths do not cross");
    require(std::fabs(eaw::formation_facing(pair, {0.0F, 100.0F, 0.0F}) - std::numbers::pi_v<float> / 2.0F) < 1.0e-5F, "the facing must point from the centroid to the target");

    // The world moves the group at its slowest member's speed into distinct slots.
    eaw::GroundMap ground;
    ground.width = 40;
    ground.height = 40;
    ground.spacing = 10.0F;
    ground.heights.assign(1600, 0.0F);
    ground.passability.assign(1600, eaw::passability::clear);
    eaw::SimulationWorld world(0.1);
    world.set_ground(ground);
    const auto fast = world.spawn({.type_name = "Fast", .position = {50.0F, 100.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
    const auto slow = world.spawn({.type_name = "Slow", .position = {50.0F, 120.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
    const auto fixed = world.spawn({.type_name = "Turret", .position = {50.0F, 140.0F, 0.0F}, .follows_ground = true});
    require(fast && slow && fixed, "test spawns must succeed");
    world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = {*fast, *slow, *fixed}, .target = {300.0F, 110.0F, 0.0F}});
    world.step();
    const auto& moving = world.snapshot().entities;
    require(std::fabs(moving[0].position.x - 52.0F) < 0.5F && std::fabs(moving[1].position.x - 52.0F) < 0.5F && !moving[2].moving, "a group must move at its slowest member's speed");
    for (int tick = 0; tick < 200; ++tick)
    {
        world.step();
    }
    const auto& arrived = world.snapshot().entities;
    require(!arrived[0].moving && !arrived[1].moving && std::hypot(arrived[0].position.x - arrived[1].position.x, arrived[0].position.y - arrived[1].position.y) >= 14.0F &&
                std::fabs((arrived[0].position.x + arrived[1].position.x) * 0.5F - 300.0F) < 1.0F,
            "a group must arrive in distinct slots around the target");
}

void test_narrow_passage()
{
    // A wall at column 15 with a one-cell gap at row 14; six tank-sized units ordered through it. With
    // equal pushes they jammed at the gap (reported on Bespin); route priority makes them queue.
    eaw::GroundMap ground;
    ground.width = 30;
    ground.height = 30;
    ground.spacing = 10.0F;
    ground.heights.assign(900, 0.0F);
    ground.passability.assign(900, eaw::passability::clear);
    for (std::uint32_t y = 0; y < 30; ++y)
    {
        if (y != 14)
        {
            ground.passability[y * 30 + 15] = eaw::passability::steep;
        }
    }
    eaw::SimulationWorld world(1.0 / 30.0);
    world.set_ground(ground);
    std::vector<eaw::EntityId> group;
    for (int index = 0; index < 6; ++index)
    {
        const auto id = world.spawn({.type_name = "Trooper",
                                     .position = {40.0F + static_cast<float>(index % 3) * 20.0F, 100.0F + static_cast<float>(index / 3) * 20.0F, 0.0F},
                                     .max_speed = 30.0F,
                                     .follows_ground = true,
                                     .passable_types = eaw::passability::clear,
                                     .radius = 8.0F,
                                     .turn_rate = 8.0F});
        require(id.ok(), "test spawn must succeed");
        group.push_back(*id);
    }
    world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = group, .target = {240.0F, 150.0F, 0.0F}});
    for (int tick = 0; tick < 900; ++tick)
    {
        world.step();
    }
    for (const auto& entity : world.snapshot().entities)
    {
        require(!entity.moving && entity.position.x > 200.0F, "a group must queue through a passage one unit wide instead of jamming at its mouth");
    }
    require(world.stats().separated_pairs == 0, "units at rest must stop pushing each other");
}

void test_group_cohesion()
{
    eaw::GroundMap ground;
    ground.width = 60;
    ground.height = 30;
    ground.spacing = 10.0F;
    ground.heights.assign(1800, 0.0F);
    ground.passability.assign(1800, eaw::passability::clear);
    const auto spawn = [&](eaw::SimulationWorld& world, const float x, const float y)
    {
        const auto id = world.spawn({.type_name = "Trooper", .position = {x, y, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 4.0F});
        require(id.ok(), "test spawn must succeed");
        return *id;
    };

    // A straggler 150 units behind: the members ahead wait for it, and the group arrives together.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const std::vector<eaw::EntityId> group{spawn(world, 200.0F, 140.0F), spawn(world, 200.0F, 160.0F), spawn(world, 50.0F, 150.0F)};
        world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = group, .target = {450.0F, 150.0F, 0.0F}});
        world.step();
        require(world.stats().waiting_members == 2, "members ahead of the group must wait");
        std::array<int, 3> arrived{-1, -1, -1};
        for (int tick = 1; tick < 400; ++tick)
        {
            world.step();
            for (std::size_t member = 0; member < 3; ++member)
            {
                if (arrived[member] < 0 && !world.snapshot().entities[member].moving)
                {
                    arrived[member] = tick;
                }
            }
        }
        require(arrived[0] > 0 && arrived[2] > 0 && std::abs(arrived[0] - arrived[2]) < 40 && std::abs(arrived[1] - arrived[2]) < 40, "a re-formed group must arrive together");
    }

    // A member that cannot make progress (it faces away and turns almost imperceptibly slowly) is
    // released, so the rest of the group goes on.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto stuck = world.spawn({.type_name = "Stuck",
                                        .position = {100.0F, 150.0F, 0.0F},
                                        .heading = std::numbers::pi_v<float>,
                                        .max_speed = 30.0F,
                                        .follows_ground = true,
                                        .passable_types = eaw::passability::clear,
                                        .radius = 4.0F,
                                        .turn_rate = 1.0e-4F});
        const auto free = spawn(world, 100.0F, 100.0F);
        require(stuck.ok(), "test spawn must succeed");
        world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = {*stuck, free}, .target = {300.0F, 130.0F, 0.0F}});
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
        }
        require(world.stats().released_members >= 1 && !world.snapshot().entities[1].moving, "a stuck member must be released so the group does not wait forever");
    }
}

void test_ground_footprints()
{
    // A clear 30x30 field; a wall footprint 10 cells long across the middle blocks the straight way.
    eaw::GroundMap ground;
    ground.width = 30;
    ground.height = 30;
    ground.spacing = 10.0F;
    ground.heights.assign(900, 0.0F);
    ground.passability.assign(900, eaw::passability::clear);
    eaw::SimulationWorld world(0.1);
    const auto wall = world.spawn({.type_name = "Empire_Wall", .footprint = eaw::GroundFootprint{{150.0F, 150.0F, 0.0F}, 5.0F, 60.0F, 0.0F, eaw::passability::empire_wall}});
    require(wall.ok(), "test spawn must succeed");
    // Footprints spawned before the ground is set are stamped when it arrives.
    world.set_ground(ground);
    require(world.ground().passability_at(150.0F, 150.0F) == eaw::passability::empire_wall && world.ground().passability_at(150.0F, 210.0F) == eaw::passability::empire_wall &&
                world.ground().passability_at(150.0F, 220.0F) == eaw::passability::clear && world.ground().passability_at(140.0F, 150.0F) == eaw::passability::clear,
            "a footprint must stamp exactly the cells inside its rectangle");
    const auto walker = world.spawn({.type_name = "Walker", .position = {100.0F, 150.0F, 0.0F}, .max_speed = 50.0F, .follows_ground = true, .passable_types = eaw::passability::clear});
    const auto large_walker = world.spawn(
        {.type_name = "LargeWalker", .position = {100.0F, 160.0F, 0.0F}, .max_speed = 50.0F, .follows_ground = true, .passable_types = eaw::passability::clear | eaw::passability::empire_wall});
    require(walker && large_walker, "test spawns must succeed");
    world.submit({eaw::SimCommandKind::move, {*walker}, {200.0F, 150.0F, 0.0F}});
    world.submit({eaw::SimCommandKind::move, {*large_walker}, {200.0F, 160.0F, 0.0F}});
    world.step();
    const auto detour = world.route(*walker);
    require(detour.size() > 1 && world.route(*large_walker).size() == 1, "a class may not cross a wall it is not allowed on; one allowed on it walks straight over");
    for (int tick = 0; tick < 200; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[1].position.x == 200.0F && world.stats().blocked_moves == 0, "the walker must arrive around the wall");

    // Destroying the wall restores the ground's own passability.
    world.submit({eaw::SimCommandKind::destroy, {*wall}, {}});
    world.step();
    require(world.ground().passability_at(150.0F, 150.0F) == eaw::passability::clear, "a destroyed footprint must restore the ground");
    require(!world.spawn({.type_name = "Bad", .footprint = eaw::GroundFootprint{{0.0F, 0.0F, 0.0F}, -1.0F, 1.0F, 0.0F, eaw::passability::obstacle}}), "an invalid footprint must be rejected");
}

void test_path_budget_and_shared_routes()
{
    // A 200x200 field with a wall across x = 100 open only at rows 180-185, so every route detours.
    eaw::GroundMap ground;
    ground.width = 200;
    ground.height = 200;
    ground.spacing = 10.0F;
    ground.heights.assign(40000, 0.0F);
    ground.passability.assign(40000, eaw::passability::clear);
    for (std::uint32_t y = 0; y < 180; ++y)
    {
        ground.passability[y * 200 + 100] = eaw::passability::steep;
    }
    const auto run = [&](const bool as_group)
    {
        eaw::SimulationWorld world(1.0 / 30.0);
        world.set_ground(ground);
        std::vector<eaw::EntityId> units;
        for (int index = 0; index < 24; ++index)
        {
            const auto id = world.spawn({.type_name = "Trooper",
                                         .position = {300.0F + static_cast<float>(index % 6) * 12.0F, 300.0F + static_cast<float>(index / 6) * 12.0F, 0.0F},
                                         .max_speed = 60.0F,
                                         .follows_ground = true,
                                         .passable_types = eaw::passability::clear,
                                         .radius = 4.0F});
            require(id.ok(), "test spawn must succeed");
            units.push_back(*id);
        }
        if (as_group)
        {
            world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = units, .target = {1700.0F, 300.0F, 0.0F}});
        }
        else
        {
            for (std::size_t index = 0; index < units.size(); ++index)
            {
                world.submit({eaw::SimCommandKind::move, {units[index]}, {1700.0F, 300.0F + static_cast<float>(index) * 10.0F, 0.0F}});
            }
        }
        world.step();
        const auto queued_after_first = world.stats().queued_paths;
        const auto searches_after_first = world.stats().path_searches;
        for (int tick = 0; tick < 4800; ++tick)
        {
            world.step();
        }
        std::size_t arrived = 0;
        for (const auto& entity : world.snapshot().entities)
        {
            arrived += !entity.moving && entity.position.x > 1500.0F ? 1U : 0U;
        }
        return std::tuple{queued_after_first, searches_after_first, world.stats(), arrived, world.state_hash()};
    };
    // Individual orders: the budget spreads the 24 long searches over several ticks.
    const auto [queued, first_searches, individual, individual_arrived, individual_hash] = run(false);
    require(queued > 0 && first_searches >= 1 && first_searches < 24 && individual.queued_paths == 0 && individual_arrived == 24,
            "the search budget must spread a large order over ticks and still plan every unit");
    require(std::get<4>(run(false)) == individual_hash, "budgeted planning must be deterministic");
    // One group order: one shared search instead of 24.
    const auto [group_queued, group_first, grouped, grouped_arrived, group_hash] = run(true);
    require(grouped.shared_routes == 1 && grouped.path_searches < 6 && group_queued == 0 && grouped_arrived == 24,
            "a formation must plan one shared route and short legs, and every member must arrive");
    require(grouped.path_expansions * 4 < individual.path_expansions, "a shared route must cost far less than searches per unit");
    (void)group_first;
    (void)group_hash;
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

void test_fighter_flight()
{
    // A fighter: 120 units/s top speed, 75 minimum, a quarter turn per second (turn circle radius
    // about 48 at minimum speed).
    const eaw::EntitySpawn fighter{.type_name = "Fighter", .owner = 1, .max_speed = 120.0F, .turn_rate = std::numbers::pi_v<float> / 2.0F, .min_speed = 75.0F};
    const auto distance = [](const eaw::SimVector3& a, const eaw::SimVector3& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    };

    // Idle, it never stops but circles where it is; ordered away, it flies there and circles that point.
    {
        eaw::SimulationWorld world(0.1);
        const auto id = world.spawn(fighter);
        require(id.ok(), "test spawn must succeed");
        // Snapshots exist from the first tick on; the fighter starts at the origin.
        eaw::SimVector3 previous{};
        for (int tick = 0; tick < 100; ++tick)
        {
            world.step();
            const auto now = world.snapshot().entities[0].position;
            require(distance(now, previous) >= 7.4F, "a fighter must keep at least its minimum speed");
            require(distance(now, {}) < 150.0F, "an idle fighter must circle near where it idles");
            previous = now;
        }
        world.submit({eaw::SimCommandKind::move, {*id}, {2000.0F, 0.0F, 0.0F}});
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
        }
        require(distance(world.snapshot().entities[0].position, {2000.0F, 0.0F, 0.0F}) < 150.0F, "a fighter must circle its destination");
    }

    // Attacking an immobile target, it makes passes: in close, past, out, and back, firing as it comes.
    {
        eaw::Hostility hostility;
        hostility.set(1, 2, true);
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        auto armed = fighter;
        armed.combat.health = 10.0F;
        armed.combat.weapons = {eaw::WeaponProfile{.range = 450.0F, .recharge_seconds = 1.0F, .damage = 1.0F}};
        eaw::CombatProfile station;
        station.health = 1.0e6F;
        const auto attacker = world.spawn(armed);
        const auto target = world.spawn({.type_name = "Station", .owner = 2, .position = {300.0F, 0.0F, 0.0F}, .combat = station});
        require(attacker.ok() && target.ok(), "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::attack, .entities = {*attacker}, .target_entity = *target});
        int passes = 0;
        bool close = false;
        float farthest = 0.0F;
        for (int tick = 0; tick < 600; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            const auto gap = distance(entities[0].position, entities[1].position);
            if (!close && gap < 60.0F)
            {
                close = true;
                ++passes;
            }
            else if (close && gap > 150.0F)
            {
                close = false;
            }
            if (passes > 0)
            {
                farthest = std::max(farthest, gap);
            }
        }
        require(passes >= 3, "an attacking fighter must make repeated passes, got " + std::to_string(passes));
        require(farthest < 450.0F, "a fighter must turn back for the next pass within its weapon range");
        require(world.stats().shots_fired > 0, "a fighter must fire during its passes");
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

const ModuleRegistrar registrar("simulation", {
                                                  {"test_simulation_core", &test_simulation_core},
                                                  {"test_ground_movement", &test_ground_movement},
                                                  {"test_pathfinding", &test_pathfinding},
                                                  {"test_combat", &test_combat},
                                                  {"test_turning_and_separation", &test_turning_and_separation},
                                                  {"test_formations", &test_formations},
                                                  {"test_narrow_passage", &test_narrow_passage},
                                                  {"test_group_cohesion", &test_group_cohesion},
                                                  {"test_ground_footprints", &test_ground_footprints},
                                                  {"test_path_budget_and_shared_routes", &test_path_budget_and_shared_routes},
                                                  {"test_hardpoints_and_fire_cones", &test_hardpoints_and_fire_cones},
                                                  {"test_fighter_flight", &test_fighter_flight},
                                                  {"test_stances", &test_stances},
                                              });

} // namespace
