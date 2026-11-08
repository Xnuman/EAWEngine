#include "test_support.hpp"

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
    gun.weapon = eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 1.0F, .pulse_count = 2, .pulse_delay_seconds = 0.1F, .damage = 10.0F, .damage_type = 1};
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
        cannon.weapon->projectile_speed = 100.0F;
        cannon.weapon->pulse_count = 1;
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
    invalid.weapon->range = -1.0F;
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

const ModuleRegistrar registrar("simulation", {
                                                  {"test_simulation_core", &test_simulation_core},
                                                  {"test_ground_movement", &test_ground_movement},
                                                  {"test_pathfinding", &test_pathfinding},
                                                  {"test_combat", &test_combat},
                                                  {"test_turning_and_separation", &test_turning_and_separation},
                                              });

} // namespace
