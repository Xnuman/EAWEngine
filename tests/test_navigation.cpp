// Ground movement and navigation: terrain following, paths, separation, formations, groups, footprints, budgets.
#include "test_support.hpp"

#include "jobs/job_system.hpp"
#include "simulation/formation.hpp"
#include "simulation/orca.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>

namespace {

using namespace eaw_test;

// Where the entities that still move stand, for failure messages ("" when none moves).
std::string still_moving(const std::vector<eaw::EntitySnapshot>& entities)
{
    std::string moving;
    for (const auto& entity : entities)
    {
        if (entity.moving)
        {
            moving += " (" + std::to_string(entity.position.x) + ", " + std::to_string(entity.position.y) + ")";
        }
    }
    return moving;
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
    const auto walker = test_spawn(world, {.type_name = "Walker", .position = {0.0F, 20.0F, 999.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear});
    const auto flyer = test_spawn(world, {.type_name = "Flyer", .position = {0.0F, 20.0F, 50.0F}, .max_speed = 100.0F});
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
    const auto stranded = test_spawn(world, {.type_name = "Stranded", .position = {80.0F, 0.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear});
    require(stranded.ok(), "test spawn must succeed");
    world.submit({eaw::SimCommandKind::move, {*stranded}, {100.0F, 0.0F, 0.0F}});
    for (int tick = 0; tick < 5; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[2].position.x == 100.0F && std::fabs(world.snapshot().entities[2].position.z - 25.0F) < 1.0e-4F, "leaving a blocked cell must be allowed");

    // A climbing class may enter steep cells.
    const auto climber =
        test_spawn(world, {.type_name = "Climber", .position = {60.0F, 40.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear | eaw::passability::steep});
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
    const auto walker = test_spawn(world, {.type_name = "Walker", .position = {10.0F, 10.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = clear});
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
        const auto turner = test_spawn(
            world, {.type_name = "Turner", .position = {100.0F, 50.0F, 0.0F}, .max_speed = 50.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .turn_rate = pi / 2.0F});
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

    // No pushing (docs/pathfinding-design.md, 2): units that start overlapped (spawned before the
    // ground existed, so not placed on free ground) stay where they are.
    {
        eaw::SimulationWorld world(0.1);
        const auto a = test_spawn(world, {.type_name = "A", .position = {50.0F, 50.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto b = test_spawn(world, {.type_name = "B", .position = {52.0F, 50.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(a && b, "test spawns must succeed");
        world.set_ground(ground);
        for (int tick = 0; tick < 20; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        require(entities[0].position.x == 50.0F && entities[1].position.x == 52.0F && world.stats().separated_pairs == 0, "idle ground units must never be pushed");
    }
    // A unit walking into an idle unit of another owner goes around it; the other never moves.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto walker = test_spawn(
            world, {.type_name = "Walker", .owner = 1, .position = {40.0F, 150.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto enemy = test_spawn(
            world, {.type_name = "Enemy", .owner = 2, .position = {60.0F, 150.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(walker && enemy, "test spawns must succeed");
        world.submit({eaw::SimCommandKind::move, {*walker}, {80.0F, 150.0F, 0.0F}});
        float closest = 100.0F;
        for (int tick = 0; tick < 40; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            closest = std::min(closest, std::hypot(entities[1].position.x - entities[0].position.x, entities[1].position.y - entities[0].position.y));
        }
        const auto& entities = world.snapshot().entities;
        require(entities[1].position.x == 60.0F && entities[1].position.y == 150.0F, "an idle foreign unit must stand its ground");
        require(closest >= 9.99F && entities[0].position.x == 80.0F && entities[0].position.y == 150.0F, "a unit must go around a standing unit without touching it and arrive (closest " +
                                                                                                             std::to_string(closest) + ", at " + std::to_string(entities[0].position.x) + ", " +
                                                                                                             std::to_string(entities[0].position.y) + ")");
    }
    // Two lines of five crossing head-on through each other: no pair ever overlaps, and all arrive.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        std::vector<eaw::EntityId> left;
        std::vector<eaw::EntityId> right;
        for (int row = 0; row < 5; ++row)
        {
            const auto y = 60.0F + 15.0F * static_cast<float>(row);
            const auto from_left =
                test_spawn(world, {.type_name = "Left", .position = {20.0F, y, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 4.0F});
            const auto from_right =
                test_spawn(world, {.type_name = "Right", .position = {120.0F, y, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 4.0F});
            require(from_left && from_right, "test spawns must succeed");
            left.push_back(*from_left);
            right.push_back(*from_right);
        }
        for (int row = 0; row < 5; ++row)
        {
            const auto y = 60.0F + 15.0F * static_cast<float>(row);
            world.submit({eaw::SimCommandKind::move, {left[static_cast<std::size_t>(row)]}, {120.0F, y, 0.0F}});
            world.submit({eaw::SimCommandKind::move, {right[static_cast<std::size_t>(row)]}, {20.0F, y, 0.0F}});
        }
        float closest = 100.0F;
        for (int tick = 0; tick < 120; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            for (std::size_t first = 0; first < entities.size(); ++first)
            {
                for (auto second = first + 1; second < entities.size(); ++second)
                {
                    closest = std::min(closest, std::hypot(entities[second].position.x - entities[first].position.x, entities[second].position.y - entities[first].position.y));
                }
            }
        }
        const auto& entities = world.snapshot().entities;
        const auto moving = still_moving(entities);
        require(closest >= 7.99F, "crossing units must never overlap (closest " + std::to_string(closest) + ")");
        require(moving.empty(), "crossing units must all arrive; still moving at" + moving);
    }
    // A goal a standing unit covers: the mover stops touching that unit, the point closest to its goal
    // where it overlaps no one (the owner, 2026-11-15).
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto mover =
            test_spawn(world, {.type_name = "Mover", .position = {40.0F, 30.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto stander =
            test_spawn(world, {.type_name = "Stander", .position = {100.0F, 30.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(mover && stander, "test spawns must succeed");
        world.submit({eaw::SimCommandKind::move, {*mover}, {100.0F, 30.0F, 0.0F}});
        for (int tick = 0; tick < 60; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        const auto gap = std::hypot(entities[1].position.x - entities[0].position.x, entities[1].position.y - entities[0].position.y);
        require(!entities[0].moving && gap >= 9.99F && gap <= 10.0F * (1.0F + 0.25F) + 0.01F && entities[1].position.x == 100.0F,
                "a unit whose goal is taken must stop next to the unit standing there, without overlapping it");
    }
    {
        const auto replay = [&]
        {
            eaw::SimulationWorld world(0.1);
            world.set_ground(ground);
            for (int index = 0; index < 12; ++index)
            {
                (void)test_spawn(world, {.type_name = "Crowd",
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
        require(replay() == replay(), "avoidance must be deterministic");
    }
    eaw::SimulationWorld world(0.1);
    require(!test_spawn(world, {.type_name = "Bad", .radius = -1.0F}) && !test_spawn(world, {.type_name = "Bad", .turn_rate = std::nanf("")}), "invalid radii and turn rates must be rejected");
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
    const eaw::Vector3 target{100.0F, 200.0F, 7.0F};
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
    const auto fast =
        test_spawn(world, {.type_name = "Fast", .position = {50.0F, 100.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
    const auto slow =
        test_spawn(world, {.type_name = "Slow", .position = {50.0F, 120.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
    const auto fixed = test_spawn(world, {.type_name = "Turret", .position = {50.0F, 140.0F, 0.0F}, .follows_ground = true});
    require(fast && slow && fixed, "test spawns must succeed");
    world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = {*fast, *slow, *fixed}, .target = {300.0F, 110.0F, 0.0F}});
    world.step();
    const auto& first = world.snapshot().entities;
    // The group forms up around its frame, then moves as one body at the slowest member's pace.
    require(std::hypot(first[0].position.x - 50.0F, first[0].position.y - 100.0F) <= 2.0F + 1.0e-3F && std::hypot(first[1].position.x - 50.0F, first[1].position.y - 120.0F) <= 2.0F + 1.0e-3F &&
                !first[2].moving,
            "no member of a group may move faster than its slowest member");
    for (int tick = 0; tick < 60; ++tick)
    {
        world.step();
    }
    const auto& abreast = world.snapshot().entities;
    require(abreast[0].position.x > 100.0F && abreast[1].position.x > 100.0F && std::fabs(abreast[0].position.x - abreast[1].position.x) < 5.0F,
            "a group must move as one body (" + std::to_string(abreast[0].position.x) + ", " + std::to_string(abreast[1].position.x) + ")");
    for (int tick = 0; tick < 140; ++tick)
    {
        world.step();
    }
    const auto& arrived = world.snapshot().entities;
    require(!arrived[0].moving && !arrived[1].moving && std::hypot(arrived[0].position.x - arrived[1].position.x, arrived[0].position.y - arrived[1].position.y) >= 14.0F &&
                std::fabs((arrived[0].position.x + arrived[1].position.x) * 0.5F - 300.0F) < 1.0F,
            "a group must arrive in distinct slots around the target (" + std::to_string(arrived[0].position.x) + ", " + std::to_string(arrived[0].position.y) + " / " +
                std::to_string(arrived[1].position.x) + ", " + std::to_string(arrived[1].position.y) + (arrived[0].moving || arrived[1].moving ? ", moving" : "") + ")");

    // A solo member (XML FormationGrouping Solo) leaves the group: own speed, straight to the point.
    eaw::SimulationWorld solo_world(0.1);
    solo_world.set_ground(ground);
    const auto walker = test_spawn(
        solo_world,
        {.type_name = "Walker", .position = {50.0F, 100.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F, .formation_solo = true});
    const auto tank =
        test_spawn(solo_world, {.type_name = "Slow", .position = {50.0F, 120.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
    const auto other =
        test_spawn(solo_world, {.type_name = "Slow", .position = {50.0F, 140.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
    require(walker && tank && other, "test spawns must succeed");
    solo_world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = {*walker, *tank, *other}, .target = {300.0F, 100.0F, 0.0F}});
    solo_world.step();
    require(std::fabs(solo_world.snapshot().entities[0].position.x - 60.0F) < 0.5F, "a solo member must keep its own speed");
    for (int tick = 0; tick < 200; ++tick)
    {
        solo_world.step();
    }
    const auto& solo = solo_world.snapshot().entities[0];
    require(!solo.moving && std::hypot(solo.position.x - 300.0F, solo.position.y - 100.0F) < 6.0F, "a solo member must go to the ordered point itself");
}

// Company teams: members of a team stand together by its arrangement; teams are units of the
// formation; raggedness moves members deterministically.
// The default group move (the owner, 2026-11-15): each member from where it stands to its own slot on
// a route of its own, at the slowest member's speed, without gathering first; and a unit sent past a
// standing ally in a corridor gets by it (the owner's report: companies stopped at a standing squad).
void test_group_move()
{
    eaw::GroundMap ground;
    ground.width = 40;
    ground.height = 40;
    ground.spacing = 10.0F;
    ground.heights.assign(1600, 0.0F);
    ground.passability.assign(1600, eaw::passability::clear);
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto fast =
            test_spawn(world, {.type_name = "Fast", .position = {50.0F, 50.0F, 0.0F}, .max_speed = 100.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto slow =
            test_spawn(world, {.type_name = "Slow", .position = {50.0F, 250.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(fast && slow, "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::group_move, .entities = {*fast, *slow}, .target = {300.0F, 150.0F, 0.0F}});
        world.step();
        world.step();
        // Straight from where each stands: its route is its own slot (open ground), not a shared way
        // through a gathering point.
        const auto fast_route = world.route(*fast);
        const auto slow_route = world.route(*slow);
        require(fast_route.size() == 1 && slow_route.size() == 1 && fast_route.back().x > 250.0F && slow_route.back().x > 250.0F, "each member must head straight for its own slot");
        const auto& moved = world.snapshot().entities;
        require(std::hypot(moved[0].position.x - 50.0F, moved[0].position.y - 50.0F) <= 4.0F + 1.0e-3F && moved[0].position.y < 60.0F && moved[1].position.y > 240.0F,
                "members must start from where they stand, at the slowest member's speed");
        for (int tick = 0; tick < 200; ++tick)
        {
            world.step();
        }
        const auto& arrived = world.snapshot().entities;
        require(still_moving(arrived).empty() && std::hypot(arrived[0].position.x - arrived[1].position.x, arrived[0].position.y - arrived[1].position.y) >= 9.99F && arrived[0].position.x > 280.0F &&
                    arrived[1].position.x > 280.0F,
                "members must arrive in their own slots at the target");
    }
    // A corridor three cells wide (rows 4 to 6 open); an ally stands in it; a unit is sent past it.
    {
        auto corridor = ground;
        for (std::uint32_t y = 0; y < 40; ++y)
        {
            for (std::uint32_t x = 0; x < 40; ++x)
            {
                if (y < 4 || y > 6)
                {
                    corridor.passability[y * 40 + x] = eaw::passability::steep;
                }
            }
        }
        eaw::SimulationWorld world(0.1);
        world.set_ground(corridor);
        const auto mover =
            test_spawn(world, {.type_name = "Mover", .position = {50.0F, 50.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto stander =
            test_spawn(world, {.type_name = "Stander", .position = {150.0F, 50.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(mover && stander, "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::group_move, .entities = {*mover}, .target = {300.0F, 50.0F, 0.0F}});
        float closest = 100.0F;
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            closest = std::min(closest, std::hypot(entities[1].position.x - entities[0].position.x, entities[1].position.y - entities[0].position.y));
        }
        const auto& entities = world.snapshot().entities;
        require(!entities[0].moving && std::hypot(entities[0].position.x - 300.0F, entities[0].position.y - 50.0F) < 1.0F,
                "a unit sent past a standing ally must get by it, not stop at it (at " + std::to_string(entities[0].position.x) + ", " + std::to_string(entities[0].position.y) + ")");
        require(closest >= 9.99F && std::hypot(entities[1].position.x - 150.0F, entities[1].position.y - 50.0F) < 1.0F,
                "the ally must make way without being touched and return to its place (at " + std::to_string(entities[1].position.x) + ", " + std::to_string(entities[1].position.y) + ")");
    }
    // A large unit sent through a line of standing allies packed too close to pass between (the owner's
    // report, 2026-11-15: an AT-ST stopped at a line of stormtrooper squads): the line makes way, or
    // the unit goes around it; either way it arrives, and nobody overlaps.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto walker =
            test_spawn(world, {.type_name = "Walker", .position = {60.0F, 200.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 12.0F});
        require(walker.ok(), "test spawn must succeed");
        for (int index = 0; index < 9; ++index)
        {
            require(test_spawn(world, {.type_name = "Trooper",
                                       .position = {200.0F, 120.0F + 20.0F * static_cast<float>(index), 0.0F},
                                       .max_speed = 30.0F,
                                       .follows_ground = true,
                                       .passable_types = eaw::passability::clear,
                                       .radius = 9.0F})
                        .ok(),
                    "test spawn must succeed");
        }
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {*walker}, .target = {340.0F, 200.0F, 0.0F}});
        float closest = 1000.0F;
        for (int tick = 0; tick < 600; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            for (std::size_t other = 1; other < entities.size(); ++other)
            {
                closest = std::min(closest, std::hypot(entities[other].position.x - entities[0].position.x, entities[other].position.y - entities[0].position.y));
            }
        }
        const auto& entities = world.snapshot().entities;
        require(!entities[0].moving && std::hypot(entities[0].position.x - 340.0F, entities[0].position.y - 200.0F) < 2.0F,
                "a unit blocked by a line of allies must get through or around it (at " + std::to_string(entities[0].position.x) + ", " + std::to_string(entities[0].position.y) + ")");
        require(closest >= 20.99F, "nobody may overlap the unit on its way (closest " + std::to_string(closest) + ")");
    }
    // A block of allies three deep across its way, packed so that none can step aside alone (the
    // owner, 2026-11-15: lines open): the units in its lane step on one after another, opening a lane
    // it goes straight through, and every one returns to its place.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto walker =
            test_spawn(world, {.type_name = "Walker", .position = {60.0F, 200.0F, 0.0F}, .max_speed = 20.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 12.0F});
        require(walker.ok(), "test spawn must succeed");
        std::vector<eaw::Vector3> places;
        for (int row = 0; row < 5; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                places.push_back({180.0F + 20.0F * static_cast<float>(column), 160.0F + 20.0F * static_cast<float>(row), 0.0F});
                require(
                    test_spawn(world, {.type_name = "Trooper", .position = places.back(), .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 9.0F}).ok(),
                    "test spawn must succeed");
            }
        }
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {*walker}, .target = {340.0F, 200.0F, 0.0F}});
        float widest = 0.0F;
        for (int tick = 0; tick < 900; ++tick)
        {
            world.step();
            widest = std::max(widest, std::fabs(world.snapshot().entities[0].position.y - 200.0F));
        }
        const auto& entities = world.snapshot().entities;
        require(!entities[0].moving && std::hypot(entities[0].position.x - 340.0F, entities[0].position.y - 200.0F) < 2.0F && widest < 30.0F,
                "a unit at a packed block of allies must pass through the lane they open (at " + std::to_string(entities[0].position.x) + ", " + std::to_string(entities[0].position.y) + ", widest " +
                    std::to_string(widest) + ")");
        for (std::size_t index = 0; index < places.size(); ++index)
        {
            const auto& ally = entities[index + 1];
            require(!ally.moving && std::hypot(ally.position.x - places[index].x, ally.position.y - places[index].y) < 1.0F,
                    "the allies that opened the lane must return to their places (one at " + std::to_string(ally.position.x) + ", " + std::to_string(ally.position.y) + ")");
        }
    }
    // Destinations fit where they are ordered (the owner's model, 2026-11-15): a move onto a standing
    // unit ends beside it; a group's block ordered against ground it cannot enter shifts until every
    // member's slot fits.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto stander =
            test_spawn(world, {.type_name = "Stander", .position = {200.0F, 200.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        const auto mover =
            test_spawn(world, {.type_name = "Mover", .position = {50.0F, 200.0F, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F});
        require(stander && mover, "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {*mover}, .target = {200.0F, 200.0F, 0.0F}});
        world.step();
        const auto goal = world.route(*mover);
        require(!goal.empty() && std::hypot(goal.back().x - 200.0F, goal.back().y - 200.0F) >= 10.0F, "a move onto a standing unit must end beside it");
        for (int tick = 0; tick < 120; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        require(!entities[1].moving && std::hypot(entities[1].position.x - entities[0].position.x, entities[1].position.y - entities[0].position.y) >= 9.99F && entities[0].position.x == 200.0F,
                "the mover must stop at its placed destination without touching the unit standing on the point");
    }
    {
        auto walled = ground;
        for (std::uint32_t y = 0; y < 40; ++y)
        {
            for (std::uint32_t x = 0; x < 20; ++x)
            {
                walled.passability[y * 40 + x] = eaw::passability::steep;
            }
        }
        eaw::SimulationWorld world(0.1);
        world.set_ground(walled);
        std::vector<eaw::EntityId> group;
        for (int index = 0; index < 6; ++index)
        {
            const auto id = test_spawn(world, {.type_name = "Trooper",
                                               .position = {300.0F + 16.0F * static_cast<float>(index % 3), 100.0F + 16.0F * static_cast<float>(index / 3), 0.0F},
                                               .max_speed = 30.0F,
                                               .follows_ground = true,
                                               .passable_types = eaw::passability::clear,
                                               .radius = 5.0F});
            require(id.ok(), "test spawn must succeed");
            group.push_back(*id);
        }
        // The point lies just past the impassable west half's edge (x 195): part of the block would
        // stand on it.
        world.submit({.kind = eaw::SimCommandKind::group_move, .entities = group, .target = {205.0F, 300.0F, 0.0F}});
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        float closest = 1000.0F;
        for (std::size_t first = 0; first < entities.size(); ++first)
        {
            require(!entities[first].moving && entities[first].position.x >= 200.0F,
                    "a group's block must shift to where every member fits (one stands at " + std::to_string(entities[first].position.x) + ", " + std::to_string(entities[first].position.y) + ")");
            for (auto second = first + 1; second < entities.size(); ++second)
            {
                closest = std::min(closest, std::hypot(entities[first].position.x - entities[second].position.x, entities[first].position.y - entities[second].position.y));
            }
        }
        // In the shifted block members keep their formation spacing (15 here); members whose slots lay on
        // the wall would pile up at its edge, touching.
        require(closest >= 14.0F, "the shifted block must keep its shape (closest members " + std::to_string(closest) + " apart)");
    }
    // Six in a column in a corridor five cells wide, one group move ahead: the ones whose slots lie past
    // group-mates already standing in theirs get by them (the owner's report: units stopped while others
    // had not finished giving way), and everyone reaches the target's area.
    {
        auto corridor = ground;
        for (std::uint32_t y = 0; y < 40; ++y)
        {
            for (std::uint32_t x = 0; x < 40; ++x)
            {
                if (y < 3 || y > 7)
                {
                    corridor.passability[y * 40 + x] = eaw::passability::steep;
                }
            }
        }
        eaw::SimulationWorld world(0.1);
        world.set_ground(corridor);
        std::vector<eaw::EntityId> column;
        for (int index = 0; index < 6; ++index)
        {
            const auto id = test_spawn(world, {.type_name = "Trooper",
                                               .position = {40.0F + 16.0F * static_cast<float>(index), 50.0F, 0.0F},
                                               .max_speed = 30.0F,
                                               .follows_ground = true,
                                               .passable_types = eaw::passability::clear,
                                               .radius = 5.0F});
            require(id.ok(), "test spawn must succeed");
            column.push_back(*id);
        }
        world.submit({.kind = eaw::SimCommandKind::group_move, .entities = column, .target = {300.0F, 50.0F, 0.0F}});
        for (int tick = 0; tick < 400; ++tick)
        {
            world.step();
        }
        for (const auto& entity : world.snapshot().entities)
        {
            require(!entity.moving && entity.position.x > 260.0F, "every member of a group move must reach the target's area (one stands at " + std::to_string(entity.position.x) + ", " +
                                                                      std::to_string(entity.position.y) + (entity.moving ? ", moving" : "") + ")");
        }
    }
}

void test_formation_teams()
{
    const auto layout = [](const eaw::TeamArrangement arrangement, const float raggedness)
    {
        std::vector<eaw::FormationMember> members;
        for (std::uint32_t index = 0; index < 8; ++index)
        {
            members.push_back({eaw::EntityId{index + 1U, 1}, {0.0F, 0.0F, 0.0F}, 2.0F, 0.5F, 2, {index < 4 ? 1U : 2U, arrangement, 1.0F, raggedness}});
        }
        return eaw::formation_slots(members, {100.0F, 0.0F, 0.0F}, 0.0F);
    };
    const auto centroid = [](const std::vector<eaw::FormationSlot>& slots, const std::size_t first)
    {
        float x = 0.0F;
        float y = 0.0F;
        for (std::size_t index = first; index < first + 4U; ++index)
        {
            x += slots[index].position.x / 4.0F;
            y += slots[index].position.y / 4.0F;
        }
        return std::pair{x, y};
    };
    const auto rows = layout(eaw::TeamArrangement::rows, 0.0F);
    const auto [ax, ay] = centroid(rows, 0);
    const auto [bx, by] = centroid(rows, 4);
    float spread = 0.0F;
    for (std::size_t index = 0; index < 4; ++index)
    {
        spread = std::max(spread, std::hypot(rows[index].position.x - ax, rows[index].position.y - ay));
    }
    // A member's pitch is 6 (radius 2, spacing 0.5): a 2x2 team reaches 4.25 from its center.
    require(spread < 4.5F && std::hypot(ax - bx, ay - by) > 2.0F * spread && std::fabs((ax + bx) * 0.5F - 100.0F) < 1.0e-3F,
            "a team's members must stand together, apart from the other team, the block centered on the target");
    const auto dartboard = layout(eaw::TeamArrangement::dartboard, 0.0F);
    const auto [cx, cy] = centroid(dartboard, 0);
    require(std::hypot(dartboard[0].position.x - cx, dartboard[0].position.y - cy) < std::hypot(dartboard[1].position.x - cx, dartboard[1].position.y - cy),
            "a dartboard team's first member must stand at its center, the rest around it");
    const auto ragged = layout(eaw::TeamArrangement::rows, 0.5F);
    const auto again = layout(eaw::TeamArrangement::rows, 0.5F);
    bool moved = false;
    for (std::size_t index = 0; index < 8; ++index)
    {
        moved = moved || ragged[index].position.x != rows[index].position.x || ragged[index].position.y != rows[index].position.y;
        require(ragged[index].position.x == again[index].position.x && ragged[index].position.y == again[index].position.y, "raggedness must be deterministic");
    }
    require(moved, "raggedness must move members off the grid");
}

// Clearance (docs/pathfinding-design.md, 4.1): a cell has room for a unit when its center lies at
// least the unit's radius from ground the unit may not enter and from the map's edge; a unit plans
// only through gaps its footprint fits.
void test_clearance()
{
    // A wall at column 10 with a one-cell gap at row 10 (cells 10 units apart, so the gap's clearance
    // is half a cell).
    eaw::GroundMap ground;
    ground.width = 21;
    ground.height = 21;
    ground.spacing = 10.0F;
    ground.heights.assign(441, 0.0F);
    ground.passability.assign(441, eaw::passability::clear);
    for (std::uint32_t y = 0; y < 21; ++y)
    {
        if (y != 10)
        {
            ground.passability[y * 21 + 10] = eaw::passability::steep;
        }
    }
    const auto field = eaw::clearance_field(ground, eaw::passability::clear, 65.0F);
    require(field.cells[10 * 21 + 10] == 5.0F && std::fabs(field.cells[10 * 21 + 5] - std::hypot(45.0F, 5.0F)) < 1.0e-3F && field.cells[0] == 5.0F && field.cells[5 * 21 + 10] == 0.0F,
            "clearance must measure to the nearest blocked square and to the map's edge, and be 0 on blocked cells");
    eaw::GridPathfinder pathfinder;
    const auto small = pathfinder.find(ground, {20.0F, 100.0F}, {180.0F, 100.0F}, eaw::Footing(eaw::passability::clear, &field, 4.0F));
    const auto large = pathfinder.find(ground, {20.0F, 100.0F}, {180.0F, 100.0F}, eaw::Footing(eaw::passability::clear, &field, 8.0F));
    require(small.status == eaw::PathStatus::reached && large.status == eaw::PathStatus::partial && !large.waypoints.empty() && large.waypoints.back().x < 100.0F,
            "a unit must plan through a gap its footprint fits, and a larger one stop on its side");
    require(!eaw::ground_line_passable(ground, {80.0F, 100.0F}, {120.0F, 100.0F}, eaw::Footing(eaw::passability::clear, &field, 8.0F)) &&
                eaw::ground_line_passable(ground, {80.0F, 100.0F}, {120.0F, 100.0F}, eaw::Footing(eaw::passability::clear, &field, 4.0F)),
            "a straight line must pass a gap only for a footprint that fits it");
    // A stamp changes the field where it lands, and only there.
    auto changed = field;
    ground.passability[10 * 21 + 5] = eaw::passability::steep;
    eaw::update_clearance(changed, ground, {5, 10, 5, 10});
    const auto rebuilt = eaw::clearance_field(ground, eaw::passability::clear, 65.0F);
    require(changed == rebuilt && changed.cells[10 * 21 + 5] == 0.0F, "an update must give what a full build gives");

    // Spawns land on free ground: off other ground units, and where their footprint fits.
    ground.passability[10 * 21 + 5] = eaw::passability::clear;
    eaw::SimulationWorld world(0.1);
    world.set_ground(ground);
    const TestSpawn unit{.type_name = "Unit", .position = {50.0F, 50.0F, 0.0F}, .max_speed = 10.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 8.0F};
    world.reserve_clearance(eaw::passability::clear, 8.0F);
    const auto first = expect_ok(test_spawn(world, unit), "spawning a unit");
    const auto second = expect_ok(test_spawn(world, unit), "spawning a unit on it");
    auto in_gap = unit;
    in_gap.position = {100.0F, 100.0F, 0.0F};
    const auto squeezed = expect_ok(test_spawn(world, in_gap), "spawning a unit in the gap");
    world.step();
    const auto& entities = world.snapshot().entities;
    const auto at = [&](const eaw::EntityId id)
    {
        return std::ranges::find(entities, id, &eaw::EntitySnapshot::id)->position;
    };
    require(std::hypot(at(second).x - at(first).x, at(second).y - at(first).y) >= 16.0F, "a unit spawned on another must land beside it");
    require(std::fabs(at(squeezed).x - 100.0F) + std::fabs(at(squeezed).y - 100.0F) > 1.0F && world.stats().separated_pairs == 0,
            "a unit spawned where its footprint does not fit must land where it does, and nothing overlaps");
}

// Chunks (docs/pathfinding-design.md, 4.2): per square of cells, the connected parts a footing fits
// and which neighbouring parts touch; a search held to the corridor of parts reaches what the plain
// search reaches.
void test_chunks()
{
    // 40 x 40 cells; a wall along column 20 with a gap at rows 30 to 33.
    eaw::GroundMap ground;
    ground.width = 40;
    ground.height = 40;
    ground.spacing = 10.0F;
    ground.heights.assign(1600, 0.0F);
    ground.passability.assign(1600, eaw::passability::clear);
    for (std::uint32_t y = 0; y < 40; ++y)
    {
        if (y < 30 || y > 33)
        {
            ground.passability[y * 40 + 20] = eaw::passability::steep;
        }
    }
    const auto graph = eaw::build_chunk_graph(ground, eaw::passability::clear, 16);
    // Squares of 16 (columns and rows 0-15, 16-31, 32-39): the wall splits the top middle square in two;
    // the gap joins its halves in the squares below.
    require(graph.parts() == 10 && graph.part_of[0] != 0 && graph.part_of[20] == 0 && graph.part_of[17] != graph.part_of[22], "a wall must split a square's cells into two parts");
    std::uint32_t expanded = 0;
    const auto route = eaw::chunk_route(ground, graph, graph.part_of[5 * 40 + 5], graph.part_of[5 * 40 + 35], {}, expanded);
    require(route.size() >= 4 && route.front() == graph.part_of[5 * 40 + 5] && route.back() == graph.part_of[5 * 40 + 35] && expanded > 0, "a part route must lead around the wall");
    for (const auto layout : {eaw::ChunkLayout::one_level, eaw::ChunkLayout::two_level, eaw::ChunkLayout::refined})
    {
        eaw::GridPathfinder plain;
        eaw::GridPathfinder chunked(0, layout);
        const auto direct = plain.find(ground, {50.0F, 50.0F}, {350.0F, 50.0F}, eaw::passability::clear);
        const auto held = chunked.find(ground, {50.0F, 50.0F}, {350.0F, 50.0F}, eaw::passability::clear);
        require(held.status == eaw::PathStatus::reached && direct.status == eaw::PathStatus::reached && held.expanded < direct.expanded && held.waypoints.back() == direct.waypoints.back(),
                "a search held to the chunk corridor must reach the goal, expanding less");
    }
}

// ORCA alone: without neighbours a unit keeps its preferred velocity (within its top speed); two
// units head-on pass without touching, each taking half the avoidance; a unit hemmed in on every
// side still gets a velocity within its top speed.
void test_orca()
{
    const auto alone = eaw::orca_velocity({}, 5.0F, {10.0F, 0.0F});
    require(std::fabs(alone.x - 5.0F) < 1.0e-5F && alone.y == 0.0F, "without neighbours ORCA must keep the preferred direction at top speed");

    eaw::Vector2 first{0.0F, 0.0F};
    eaw::Vector2 second{20.0F, 0.1F};
    eaw::Vector2 first_velocity{2.0F, 0.0F};
    eaw::Vector2 second_velocity{-2.0F, 0.0F};
    constexpr float reach = 2.0F;
    constexpr float step = 0.1F;
    float closest = 100.0F;
    for (int tick = 0; tick < 200; ++tick)
    {
        const std::array first_lines{eaw::orca_line(second - first, first_velocity - second_velocity, reach, 2.0F, step, 0.5F, first_velocity)};
        const std::array second_lines{eaw::orca_line(first - second, second_velocity - first_velocity, reach, 2.0F, step, 0.5F, second_velocity)};
        first_velocity = eaw::orca_velocity(first_lines, 2.0F, {2.0F, 0.0F});
        second_velocity = eaw::orca_velocity(second_lines, 2.0F, {-2.0F, 0.0F});
        first += first_velocity * step;
        second += second_velocity * step;
        closest = std::min(closest, eaw::length(second - first));
    }
    require(closest >= reach * 0.99F && first.x > 30.0F && second.x < -10.0F, "units meeting head-on must pass each other without touching");

    std::vector<eaw::OrcaLine> hemmed;
    for (int side = 0; side < 6; ++side)
    {
        const auto angle = static_cast<float>(side) * std::numbers::pi_v<float> / 3.0F;
        hemmed.push_back(eaw::orca_line({std::cos(angle) * 1.9F, std::sin(angle) * 1.9F}, {}, reach, 2.0F, step, 1.0F, {}));
    }
    const auto squeezed = eaw::orca_velocity(hemmed, 3.0F, {3.0F, 0.0F});
    require(eaw::is_finite(squeezed) && eaw::length(squeezed) <= 3.0F + 1.0e-4F, "a hemmed-in unit must still get a velocity within its top speed");
}

void test_parallel_avoidance()
{
    // Two groups of 48 crossing each other head-on, a crowd for ground avoidance, run serially and on
    // a job system (docs/ecs-design.md, 7.4): the state hashes match tick for tick.
    eaw::GroundMap ground;
    ground.width = 60;
    ground.height = 60;
    ground.spacing = 10.0F;
    ground.heights.assign(3600, 0.0F);
    ground.passability.assign(3600, eaw::passability::clear);
    auto jobs = expect_ok(eaw::JobSystem::create({.workers = 3}), "creating a job system");
    const auto run = [&](eaw::JobSystem* system)
    {
        eaw::SimulationWorld world(1.0 / 30.0);
        world.set_ground(ground);
        world.set_job_system(system);
        std::array<std::vector<eaw::EntityId>, 2> sides;
        for (std::size_t side = 0; side < 2; ++side)
        {
            for (int index = 0; index < 48; ++index)
            {
                const auto x = side == 0 ? 100.0F + static_cast<float>(index % 6) * 14.0F : 400.0F + static_cast<float>(index % 6) * 14.0F;
                const auto y = 200.0F + static_cast<float>(index / 6) * 14.0F;
                sides[side].push_back(expect_ok(
                    test_spawn(world, {.type_name = "Trooper", .position = {x, y, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 5.0F}),
                    "test spawn"));
            }
        }
        world.submit({.kind = eaw::SimCommandKind::group_move, .entities = sides[0], .target = {450.0F, 250.0F, 0.0F}});
        world.submit({.kind = eaw::SimCommandKind::group_move, .entities = sides[1], .target = {150.0F, 250.0F, 0.0F}});
        std::vector<std::uint64_t> hashes;
        for (int tick = 0; tick < 600; ++tick)
        {
            world.step();
            hashes.push_back(world.state_hash());
        }
        return hashes;
    };
    const auto serial = run(nullptr);
    const auto parallel = run(jobs.get());
    const auto first = std::ranges::mismatch(serial, parallel).in1;
    require(first == serial.end(), "a battle on the job system must hash as the serial one tick for tick (first difference at tick " + std::to_string(first - serial.begin() + 1) + ")");
}

void test_narrow_passage()
{
    // A wall at column 15 with a gap of three cells at rows 13 to 15, room for one tank-sized unit at
    // a time (radius 8: only the middle row's clearance, 15, holds it); six of them ordered through
    // it. With equal pushes they jammed at the gap (reported on Bespin); route priority makes them
    // queue.
    eaw::GroundMap ground;
    ground.width = 30;
    ground.height = 30;
    ground.spacing = 10.0F;
    ground.heights.assign(900, 0.0F);
    ground.passability.assign(900, eaw::passability::clear);
    for (std::uint32_t y = 0; y < 30; ++y)
    {
        if (y < 13 || y > 15)
        {
            ground.passability[y * 30 + 15] = eaw::passability::steep;
        }
    }
    eaw::SimulationWorld world(1.0 / 30.0);
    world.set_ground(ground);
    std::vector<eaw::EntityId> group;
    for (int index = 0; index < 6; ++index)
    {
        const auto id = test_spawn(world, {.type_name = "Trooper",
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
        require(!entity.moving && entity.position.x > 170.0F, "a group must queue through a passage one unit wide instead of jamming at its mouth; one stands at (" +
                                                                  std::to_string(entity.position.x) + ", " + std::to_string(entity.position.y) + "), blocked moves " +
                                                                  std::to_string(world.stats().blocked_moves) + ", path searches " + std::to_string(world.stats().path_searches) +
                                                                  ", still moving:" + still_moving(world.snapshot().entities));
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
        const auto id = test_spawn(world, {.type_name = "Trooper", .position = {x, y, 0.0F}, .max_speed = 30.0F, .follows_ground = true, .passable_types = eaw::passability::clear, .radius = 4.0F});
        require(id.ok(), "test spawn must succeed");
        return *id;
    };

    // A straggler 150 units behind: the formation forms on the move (no one waits at the start, the
    // owner's report of companies standing for seconds), and the group arrives together.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const std::vector<eaw::EntityId> group{spawn(world, 200.0F, 140.0F), spawn(world, 200.0F, 160.0F), spawn(world, 50.0F, 150.0F)};
        world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = group, .target = {450.0F, 150.0F, 0.0F}});
        world.step();
        std::array<int, 3> arrived{-1, -1, -1};
        for (int tick = 1; tick < 400; ++tick)
        {
            world.step();
            if (tick == 5)
            {
                // The straggler's slot starts where it stands: nobody waits for it at the start.
                require(world.stats().waiting_members == 0, "a formation must not wait at the start for members to gather");
            }
            for (std::size_t member = 0; member < 3; ++member)
            {
                if (arrived[member] < 0 && !world.snapshot().entities[member].moving && world.snapshot().entities[member].position.x > 400.0F)
                {
                    arrived[member] = tick;
                }
            }
        }
        require(arrived[0] > 0 && arrived[2] > 0 && std::abs(arrived[0] - arrived[2]) < 40 && std::abs(arrived[1] - arrived[2]) < 40,
                "a re-formed group must arrive together (ticks " + std::to_string(arrived[0]) + ", " + std::to_string(arrived[1]) + ", " + std::to_string(arrived[2]) + "; released " +
                    std::to_string(world.stats().released_members) + ")");
    }

    // A member that cannot make progress (it faces away and turns almost imperceptibly slowly) is
    // released, so the rest of the group goes on.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto stuck = test_spawn(world, {.type_name = "Stuck",
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

    // Two pairs 470 units apart ordered together are split by distance into two frames: neither waits
    // for the other, and both arrive.
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const std::vector<eaw::EntityId> group{spawn(world, 100.0F, 100.0F), spawn(world, 100.0F, 120.0F), spawn(world, 550.0F, 250.0F), spawn(world, 550.0F, 270.0F)};
        world.submit({.kind = eaw::SimCommandKind::formation_move, .entities = group, .target = {300.0F, 150.0F, 0.0F}});
        for (int tick = 0; tick < 5; ++tick)
        {
            world.step();
        }
        require(world.stats().waiting_members == 0, "parts of a selection far apart must not wait for each other");
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
        }
        require(still_moving(world.snapshot().entities).empty(), "both parts must arrive");
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
    const auto wall = test_spawn(world, {.type_name = "Empire_Wall", .footprint = eaw::GroundFootprint{{150.0F, 150.0F, 0.0F}, 5.0F, 60.0F, 0.0F, eaw::passability::empire_wall}});
    require(wall.ok(), "test spawn must succeed");
    // Footprints spawned before the ground is set are stamped when it arrives.
    world.set_ground(ground);
    require(world.ground().passability_at(150.0F, 150.0F) == eaw::passability::empire_wall && world.ground().passability_at(150.0F, 210.0F) == eaw::passability::empire_wall &&
                world.ground().passability_at(150.0F, 220.0F) == eaw::passability::clear && world.ground().passability_at(140.0F, 150.0F) == eaw::passability::clear,
            "a footprint must stamp exactly the cells inside its rectangle");
    const auto walker = test_spawn(world, {.type_name = "Walker", .position = {100.0F, 150.0F, 0.0F}, .max_speed = 50.0F, .follows_ground = true, .passable_types = eaw::passability::clear});
    const auto large_walker = test_spawn(
        world, {.type_name = "LargeWalker", .position = {100.0F, 160.0F, 0.0F}, .max_speed = 50.0F, .follows_ground = true, .passable_types = eaw::passability::clear | eaw::passability::empire_wall});
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
    require(!test_spawn(world, {.type_name = "Bad", .footprint = eaw::GroundFootprint{{0.0F, 0.0F, 0.0F}, -1.0F, 1.0F, 0.0F, eaw::passability::obstacle}}), "an invalid footprint must be rejected");
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
    const auto run = [&](const bool as_group, const int count)
    {
        eaw::SimulationWorld world(1.0 / 30.0);
        world.set_ground(ground);
        std::vector<eaw::EntityId> units;
        for (int index = 0; index < count; ++index)
        {
            const auto id = test_spawn(world, {.type_name = "Trooper",
                                               .position = {300.0F + static_cast<float>(index % 12) * 12.0F, 300.0F + static_cast<float>(index / 12) * 12.0F, 0.0F},
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
                world.submit({eaw::SimCommandKind::move, {units[index]}, {1700.0F, 300.0F + static_cast<float>(index % 24) * 10.0F, 0.0F}});
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
        // Where the units that have not arrived stand (moving or stopped short), for failure messages.
        std::string short_of;
        for (const auto& entity : world.snapshot().entities)
        {
            if (entity.moving || entity.position.x <= 1500.0F)
            {
                short_of += " (" + std::to_string(entity.position.x) + ", " + std::to_string(entity.position.y) + ")";
            }
        }
        return std::tuple{queued_after_first, searches_after_first, world.stats(), arrived, world.state_hash(), short_of};
    };
    // Individual orders: the budget spreads 150 long searches over several ticks (searches over chunks
    // are cheap enough that a few dozen fit one tick's budget).
    constexpr int many = 150;
    const auto [queued, first_searches, individual, individual_arrived, individual_hash, individual_moving] = run(false, many);
    require(queued > 0 && first_searches >= 1 && first_searches < static_cast<std::uint64_t>(many) && individual.queued_paths == 0 && individual_arrived == static_cast<std::size_t>(many),
            "the search budget must spread a large order over ticks and still plan every unit (" + std::to_string(individual_arrived) + " arrived; not arrived:" + individual_moving + ")");
    require(std::get<4>(run(false, many)) == individual_hash, "budgeted planning must be deterministic");
    // One group order: one shared search instead of 24 (its one request may finish on the next tick
    // when it costs more than one tick's budget).
    const auto [group_queued, group_first, grouped, grouped_arrived, group_hash, group_moving] = run(true, 24);
    // Searches: the frame's route, and short detours of members whose straight way to their slot runs
    // into the wall's end (37 with frames, 2026-11-15; each member walks to its slot, so one rounding
    // the corner plans around it); fewer than two per member. The cost check below is the one that
    // matters.
    require(grouped.shared_routes == 1 && grouped.path_searches < 48 && group_queued <= 1 && grouped_arrived == 24,
            "a formation must plan one shared route and short legs, and every member must arrive (" + std::to_string(grouped.path_searches) + " searches, " + std::to_string(grouped_arrived) +
                " arrived; not arrived:" + group_moving + ")");
    require(grouped.path_expansions * 4 < individual.path_expansions, "a shared route must cost far less than searches per unit");
    (void)group_first;
    (void)group_hash;
}

// Space ships in crowds (radius 100, turning at 0.4 rad/s, as capital ships): a ship through idle
// ships, a faster ship overtaking a slower one, five ships sent to one point (they stop side by
// side), and five moved as a formation from a tight cluster. No two ever overlap and all arrive.
void test_space_crowds()
{
    struct Ship
    {
        eaw::Vector3 at;
        float speed{};
    };
    const auto run = [](const char* what, const std::vector<Ship>& ships, const std::vector<std::size_t>& movers, const eaw::Vector3& target, const bool formation)
    {
        eaw::SimulationWorld world(1.0 / 30.0);
        std::vector<eaw::EntityId> ids;
        for (const auto& ship : ships)
        {
            ids.push_back(*test_spawn(world, {.type_name = "Ship", .owner = 1, .position = ship.at, .max_speed = ship.speed, .radius = 100.0F, .turn_rate = 0.4F}));
        }
        std::vector<eaw::EntityId> moving;
        for (const auto index : movers)
        {
            moving.push_back(ids[index]);
        }
        if (formation)
        {
            world.submit({eaw::SimCommandKind::formation_move, moving, target});
        }
        else
        {
            for (const auto id : moving)
            {
                world.submit({eaw::SimCommandKind::move, {id}, target});
            }
        }
        float closest = std::numeric_limits<float>::max();
        for (int tick = 0; tick < 3000; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            for (std::size_t a = 0; a < entities.size(); ++a)
            {
                for (std::size_t b = a + 1; b < entities.size(); ++b)
                {
                    const auto apart = std::hypot(entities[a].position.x - entities[b].position.x, entities[a].position.y - entities[b].position.y);
                    closest = std::min(closest, apart / (entities[a].radius + entities[b].radius));
                }
            }
        }
        // Touching is allowed (ships slide along each other), overlapping is not.
        require(closest >= 0.999F, std::string(what) + ": ships overlapped (closest " + std::to_string(closest) + " of their radii)");
        require(std::ranges::none_of(world.snapshot().entities, &eaw::EntitySnapshot::moving), std::string(what) + ": every ship must arrive");
    };
    run("through idle ships", {{{0, 0, 0}, 60}, {{1000, -60, 0}, 0}, {{1000, 180, 0}, 0}, {{1250, 60, 0}, 0}}, {0}, {3000, 0, 0}, false);
    run("overtaking", {{{0, 0, 0}, 40}, {{-250, 0, 0}, 80}}, {0, 1}, {3000, 0, 0}, false);
    const std::vector<Ship> cluster{{{0, 0, 0}, 60}, {{220, 0, 0}, 50}, {{0, 220, 0}, 70}, {{220, 220, 0}, 60}, {{110, 440, 0}, 55}};
    run("five sent to one point", cluster, {0, 1, 2, 3, 4}, {-2500, 1500, 0}, false);
    run("five as a formation", cluster, {0, 1, 2, 3, 4}, {-2500, 1500, 0}, true);
}

// Space ships avoid contact instead of pushing: a ship steers around a ship in its way and arrives;
// a ship at another height and a much smaller one are flown past directly; two ships meeting
// head-on pass without touching; overlapping idle ships stay where they are; and a move onto another
// ship stops at its clearance.
void test_space_avoidance()
{
    const auto make = [](const float speed, const eaw::Vector3& at, const float radius)
    {
        return TestSpawn{.type_name = "Ship", .owner = 1, .position = at, .max_speed = speed, .radius = radius};
    };
    const auto closest = [](eaw::SimulationWorld& world, const eaw::EntityId a, const eaw::EntityId b, const int ticks)
    {
        float nearest = std::numeric_limits<float>::max();
        for (int tick = 0; tick < ticks; ++tick)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            const auto& first = *std::ranges::find(entities, a, &eaw::EntitySnapshot::id);
            const auto& second = *std::ranges::find(entities, b, &eaw::EntitySnapshot::id);
            nearest = std::min(nearest, std::hypot(first.position.x - second.position.x, first.position.y - second.position.y));
        }
        return nearest;
    };
    const auto position = [](const eaw::SimulationWorld& world, const eaw::EntityId id)
    {
        return std::ranges::find(world.snapshot().entities, id, &eaw::EntitySnapshot::id)->position;
    };
    {
        eaw::SimulationWorld world(0.1);
        const auto mover = *test_spawn(world, make(100.0F, {0.0F, 0.0F, 0.0F}, 20.0F));
        const auto rock = *test_spawn(world, make(0.0F, {500.0F, 0.0F, 0.0F}, 20.0F));
        world.submit({eaw::SimCommandKind::move, {mover}, {1000.0F, 0.0F, 0.0F}});
        const auto nearest = closest(world, mover, rock, 200);
        require(nearest >= 40.0F && std::abs(position(world, mover).x - 1000.0F) < 1.0F && position(world, rock).x == 500.0F,
                "a ship must steer around a ship in its way without touching or pushing it, and arrive");
    }
    for (const auto& [obstacle, what] : {std::pair{make(0.0F, {500.0F, 0.0F, 200.0F}, 20.0F), "a ship at another height"}, std::pair{make(0.0F, {500.0F, 0.0F, 0.0F}, 2.0F), "a much smaller ship"}})
    {
        eaw::SimulationWorld world(0.1);
        const auto mover = *test_spawn(world, make(100.0F, {0.0F, 0.0F, 0.0F}, 20.0F));
        (void)test_spawn(world, obstacle);
        world.submit({eaw::SimCommandKind::move, {mover}, {1000.0F, 0.0F, 0.0F}});
        float widest = 0.0F;
        for (int tick = 0; tick < 200; ++tick)
        {
            world.step();
            widest = std::max(widest, std::abs(position(world, mover).y));
        }
        require(widest == 0.0F, std::string(what) + " must be flown past directly");
    }
    {
        eaw::SimulationWorld world(0.1);
        const auto east = *test_spawn(world, make(100.0F, {0.0F, 0.0F, 0.0F}, 20.0F));
        const auto west = *test_spawn(world, make(100.0F, {1000.0F, 0.0F, 0.0F}, 20.0F));
        world.submit({eaw::SimCommandKind::move, {east}, {1000.0F, 0.0F, 0.0F}});
        world.submit({eaw::SimCommandKind::move, {west}, {0.0F, 0.0F, 0.0F}});
        require(closest(world, east, west, 300) >= 40.0F && std::abs(position(world, east).x - 1000.0F) < 1.0F && std::abs(position(world, west).x) < 1.0F,
                "two ships meeting head-on must pass without touching and arrive");
    }
    {
        eaw::SimulationWorld world(0.1);
        const auto first = *test_spawn(world, make(100.0F, {0.0F, 0.0F, 0.0F}, 20.0F));
        const auto second = *test_spawn(world, make(100.0F, {10.0F, 0.0F, 0.0F}, 20.0F));
        for (int tick = 0; tick < 20; ++tick)
        {
            world.step();
        }
        require(position(world, first).x == 0.0F && position(world, second).x == 10.0F, "idle space ships must not push each other apart");
        world.submit({eaw::SimCommandKind::move, {first}, {500.0F, 0.0F, 0.0F}});
        (void)test_spawn(world, make(0.0F, {500.0F, 0.0F, 0.0F}, 20.0F));
        for (int tick = 0; tick < 100; ++tick)
        {
            world.step();
        }
        const auto& stopped = *std::ranges::find(world.snapshot().entities, first, &eaw::EntitySnapshot::id);
        require(!stopped.moving && stopped.position.x < 500.0F - 40.0F && stopped.position.x > 500.0F - 60.0F, "a move onto another ship must stop at its clearance");
    }
}

// Movement profiles: acceleration and braking, stopping to turn, and ships that need speed to turn.
void test_movement_profiles()
{
    const auto pi = std::numbers::pi_v<float>;
    const auto run = [](eaw::SimulationWorld& world, const int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick)
        {
            world.step();
        }
    };
    {
        // 20 units/s, accelerating and braking at 10 units/s^2: 0.1 units in the first tick, and a
        // stop exactly at the destination.
        eaw::SimulationWorld world(0.1);
        const auto mover = test_spawn(world, {.type_name = "Mover", .max_speed = 20.0F, .turn_rate = pi, .movement = {.acceleration = 10.0F, .deceleration = 10.0F}});
        require(mover.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*mover}, {100.0F, 0.0F, 0.0F}});
        world.step();
        require(std::fabs(world.snapshot().entities[0].position.x - 0.1F) < 1.0e-4F, "a mover must gather speed at its acceleration");
        run(world, 30);
        require(std::fabs(world.snapshot().entities[0].speed - 20.0F) < 1.0e-3F, "a mover must reach its top speed");
        run(world, 100);
        const auto& arrived = world.snapshot().entities[0];
        require(arrived.position.x == 100.0F && !arrived.moving, "a braking mover must still arrive and stop");
    }
    {
        // Facing +X, ordered to -X: it turns where it stands before moving off.
        eaw::SimulationWorld world(0.1);
        const auto turner = test_spawn(world, {.type_name = "Turner", .max_speed = 20.0F, .turn_rate = pi / 2.0F, .movement = {.turn_in_place_angle = pi / 4.0F}});
        require(turner.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*turner}, {-50.0F, 0.0F, 0.0F}});
        run(world, 14);
        require(world.snapshot().entities[0].position.x == 0.0F, "a unit far off its way must not move while it turns");
        run(world, 3);
        require(world.snapshot().entities[0].position.x < 0.0F, "a unit must move off once it is nearly facing its way");
        run(world, 60);
        require(world.snapshot().entities[0].position.x == -50.0F, "a unit that turned in place must arrive");
    }
    {
        // On the move, a new way 60 degrees off is taken while walking, not by stopping to turn.
        eaw::SimulationWorld world(0.1);
        const auto walker = test_spawn(world, {.type_name = "Walker", .max_speed = 20.0F, .turn_rate = pi / 2.0F, .movement = {.turn_in_place_angle = pi / 4.0F}});
        require(walker.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*walker}, {200.0F, 0.0F, 0.0F}});
        run(world, 5);
        const auto before = world.snapshot().entities[0].position;
        world.submit({eaw::SimCommandKind::move, {*walker}, {before.x + 100.0F, before.y + 173.2F, 0.0F}});
        run(world, 1);
        const auto& turning = world.snapshot().entities[0];
        require(turning.moving && std::hypot(turning.position.x - before.x, turning.position.y - before.y) > 1.0F, "a moving unit must turn on the move");
    }
    {
        // A ship that cannot turn below half its speed goes forward first, then turns back.
        eaw::SimulationWorld world(0.1);
        const auto ship = test_spawn(world, {.type_name = "Ship", .max_speed = 20.0F, .turn_rate = pi / 2.0F, .movement = {.acceleration = 20.0F, .deceleration = 20.0F, .turn_speed_fraction = 0.5F}});
        require(ship.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*ship}, {-200.0F, 0.0F, 0.0F}});
        run(world, 4);
        const auto& starting = world.snapshot().entities[0];
        require(starting.heading == 0.0F && starting.position.x > 0.0F, "a slow ship must go forward without turning");
        run(world, 4);
        require(world.snapshot().entities[0].heading != 0.0F, "a ship must turn once it has speed");
        run(world, 400);
        require(world.snapshot().entities[0].position.x == -200.0F, "a ship that turned under way must arrive");
    }
    eaw::SimulationWorld world(0.1);
    require(!test_spawn(world, {.type_name = "Bad", .movement = {.acceleration = -1.0F}}) && !test_spawn(world, {.type_name = "Bad", .movement = {.turn_speed_fraction = 1.5F}}),
            "negative accelerations and turn speed fractions above 1 must be rejected");
}

// A ground unit that brakes coasts to a stop over its braking distance when ordered to stop (one at
// 20 per second braking at 10 stops about 20 further on); one that does not brake halts at once.
// Turns that wind up and down end in place on the way before the unit moves off.
void test_braking_stop_and_winding_turns()
{
    eaw::GroundMap ground;
    ground.width = 40;
    ground.height = 40;
    ground.spacing = 10.0F;
    ground.heights.assign(1600, 0.0F);
    ground.passability.assign(1600, eaw::passability::clear);
    const auto coast = [&](const float deceleration)
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        const auto unit = test_spawn(world, {.type_name = "Walker",
                                             .position = {50.0F, 100.0F, 0.0F},
                                             .max_speed = 20.0F,
                                             .follows_ground = true,
                                             .passable_types = eaw::passability::clear,
                                             .radius = 2.0F,
                                             .turn_rate = 3.0F,
                                             .movement = {.acceleration = 100.0F, .deceleration = deceleration}});
        require(static_cast<bool>(unit), "test spawn must succeed");
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {*unit}, .target = {350.0F, 100.0F, 0.0F}});
        for (int tick = 0; tick < 30; ++tick)
        {
            world.step();
        }
        const auto stopped_at = world.snapshot().entities[0].position.x;
        world.submit({.kind = eaw::SimCommandKind::stop, .entities = {*unit}});
        for (int tick = 0; tick < 40; ++tick)
        {
            world.step();
        }
        return world.snapshot().entities[0].position.x - stopped_at;
    };
    const auto braking = coast(10.0F);
    require(braking > 15.0F && braking < 25.0F && coast(0.0F) < 1.0F, "a braking ground unit must coast to a stop over its braking distance, an unbraking one halt");

    // A turn in place played as clips (BEGIN 0.5 s, loop 1 s, END 0.5 s; 90 degrees at a stopped rate
    // of 1 per second takes one loop): it lasts exactly 2 s, its END starts at the loop's boundary
    // (1.5 s), and the unit leaves exactly on its way.
    eaw::SimulationWorld world(0.1);
    world.set_ground(ground);
    const auto turner = test_spawn(world, {.type_name = "Turner",
                                           .position = {100.0F, 100.0F, 0.0F},
                                           .max_speed = 20.0F,
                                           .follows_ground = true,
                                           .passable_types = eaw::passability::clear,
                                           .radius = 2.0F,
                                           .turn_rate = 1.0F,
                                           .movement = {.acceleration = 20.0F,
                                                        .deceleration = 20.0F,
                                                        .turn_speed_fraction = 0.1F,
                                                        .stopped_turn_rate = 1.0F,
                                                        .turn_in_place_angle = 0.5236F,
                                                        .turn_begin_seconds = {0.5F, 0.5F},
                                                        .turn_loop_seconds = {1.0F, 1.0F},
                                                        .turn_end_seconds = {0.5F, 0.5F}}});
    require(static_cast<bool>(turner), "test spawn must succeed");
    world.submit({.kind = eaw::SimCommandKind::move, .entities = {*turner}, .target = {100.0F, 300.0F, 0.0F}});
    int turning_ticks = 0;
    int first_end_tick = -1;
    float heading_when_moving = 0.0F;
    for (int tick = 1; tick <= 100; ++tick)
    {
        world.step();
        const auto& entity = world.snapshot().entities[0];
        turning_ticks += entity.turn_clip != eaw::TurnClip::none ? 1 : 0;
        if (entity.turn_clip == eaw::TurnClip::end && first_end_tick < 0)
        {
            first_end_tick = tick;
        }
        if (entity.speed > 0.0F && heading_when_moving == 0.0F)
        {
            heading_when_moving = entity.heading;
        }
    }
    require(turning_ticks == 19 && first_end_tick == 15 && std::fabs(heading_when_moving - 1.5707964F) < 1.0e-3F,
            "a turn in place played as clips must last its clips, end on a loop's boundary, and leave on the way");

    // With HALF (0.5 s) and QUARTER (0.5 s) cycles, a turn toward the first waypoint up and to the
    // left (125 degrees on this ground, the cell centre) at a stopped rate of 1 per second is 1.69
    // loops beyond BEGIN and END: one loop, a HALF, and a QUARTER (7 quarters), in that order, 3 s in
    // all, ending exactly on the way (the walker sets off without turning further).
    eaw::SimulationWorld composed(0.1);
    composed.set_ground(ground);
    const auto kit = test_spawn(composed, {.type_name = "Kit",
                                           .position = {100.0F, 100.0F, 0.0F},
                                           .max_speed = 20.0F,
                                           .follows_ground = true,
                                           .passable_types = eaw::passability::clear,
                                           .radius = 2.0F,
                                           .turn_rate = 1.0F,
                                           .movement = {.acceleration = 20.0F,
                                                        .deceleration = 20.0F,
                                                        .turn_speed_fraction = 0.1F,
                                                        .stopped_turn_rate = 1.0F,
                                                        .turn_in_place_angle = 0.5236F,
                                                        .turn_begin_seconds = {0.5F, 0.5F},
                                                        .turn_loop_seconds = {1.0F, 1.0F},
                                                        .turn_end_seconds = {0.5F, 0.5F},
                                                        .turn_half_seconds = {0.5F, 0.5F},
                                                        .turn_quarter_seconds = {0.5F, 0.5F}}});
    require(static_cast<bool>(kit), "test spawn must succeed");
    composed.submit({.kind = eaw::SimCommandKind::move, .entities = {*kit}, .target = {100.0F - 200.0F * 0.70710678F, 100.0F + 200.0F * 0.70710678F, 0.0F}});
    std::vector<eaw::TurnClip> clips;
    int composed_ticks = 0;
    float leaving = 0.0F;
    float turned_to = 0.0F;
    bool left = true;
    for (int tick = 1; tick <= 60; ++tick)
    {
        composed.step();
        const auto& entity = composed.snapshot().entities[0];
        if (entity.turn_clip != eaw::TurnClip::none)
        {
            ++composed_ticks;
            left = left && entity.turn_left;
            turned_to = entity.heading;
            if (clips.empty() || clips.back() != entity.turn_clip)
            {
                clips.push_back(entity.turn_clip);
            }
        }
        if (entity.speed > 0.0F && leaving == 0.0F)
        {
            leaving = entity.heading;
        }
    }
    const std::vector<eaw::TurnClip> expected{eaw::TurnClip::begin, eaw::TurnClip::loop, eaw::TurnClip::half, eaw::TurnClip::quarter, eaw::TurnClip::end};
    require(clips == expected && composed_ticks >= 29 && composed_ticks <= 30 && left && leaving > 2.0F && std::fabs(leaving - turned_to) < 1.0e-5F,
            "a turn in place must compose its cycles of loops, HALF, and QUARTER and end exactly on the way");
}

// A walk played as clips (MOVESTART 1 s, a 2 s stride of 20 units at top speed 10, four MOVE_END
// clips of 1 s): it starts over MOVESTART, stops only where a stride's part begins, brakes over a
// whole MOVE_END clip, and nothing turns or sets off meanwhile.
void test_walk_as_clips()
{
    eaw::GroundMap ground;
    ground.width = 40;
    ground.height = 40;
    ground.spacing = 10.0F;
    ground.heights.assign(1600, 0.0F);
    ground.passability.assign(1600, eaw::passability::clear);
    struct Walk
    {
        eaw::SimulationWorld world{0.1};
        eaw::EntityId unit;
    };
    const auto walker = [&](Walk& walk)
    {
        walk.world.set_ground(ground);
        const auto unit = test_spawn(
            walk.world, {.type_name = "Walker",
                         .position = {50.0F, 100.0F, 0.0F},
                         .max_speed = 10.0F,
                         .follows_ground = true,
                         .passable_types = eaw::passability::clear,
                         .radius = 2.0F,
                         .turn_rate = 3.0F,
                         .movement = {.acceleration = 10.0F, .deceleration = 10.0F, .walk_start_seconds = 1.0F, .walk_stride_seconds = 2.0F, .walk_stop_seconds = 1.0F, .walk_stop_parts = 4}});
        require(static_cast<bool>(unit), "test spawn must succeed");
        walk.unit = *unit;
    };
    // Steps until the walker stands again (at most `ticks`); returns the clips it went through, the
    // ticks it stopped for, the stride phase before its stop, and the part the stop played.
    struct Trace
    {
        std::vector<eaw::WalkClip> clips;
        int stopping_ticks{};
        float phase_before_stop{-1.0F};
        std::uint32_t part{};
    };
    const auto run = [](Walk& walk, const int ticks)
    {
        Trace trace;
        for (int tick = 0; tick < ticks; ++tick)
        {
            const auto before = walk.world.snapshot().entities.empty() ? eaw::EntitySnapshot{} : walk.world.snapshot().entities[0];
            walk.world.step();
            const auto& entity = walk.world.snapshot().entities[0];
            if (trace.clips.empty() || trace.clips.back() != entity.walk_clip)
            {
                trace.clips.push_back(entity.walk_clip);
                if (entity.walk_clip == eaw::WalkClip::stopping)
                {
                    trace.phase_before_stop = before.walk_progress;
                    trace.part = entity.walk_part;
                }
            }
            trace.stopping_ticks += entity.walk_clip == eaw::WalkClip::stopping ? 1 : 0;
            if (entity.walk_clip == eaw::WalkClip::standing && tick > 0 && !entity.moving)
            {
                break;
            }
        }
        return trace;
    };
    // The stop began within a tick's walk (0.1 of a stride part here) after the part it plays.
    const auto at_its_part = [](const Trace& trace)
    {
        const auto part = static_cast<float>(trace.part) / 4.0F;
        const auto past = std::fmod(trace.phase_before_stop - part + 1.0F, 1.0F);
        return past < 0.06F || past > 0.94F;
    };
    {
        Walk walk;
        walker(walk);
        walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {250.0F, 100.0F, 0.0F}});
        const auto trace = run(walk, 400);
        const auto& entity = walk.world.snapshot().entities[0];
        const std::vector<eaw::WalkClip> expected{eaw::WalkClip::starting, eaw::WalkClip::moving, eaw::WalkClip::stopping, eaw::WalkClip::standing};
        // It stops within half a stride's part (2.5 units) and a tick's walk of its destination.
        require(trace.clips == expected && trace.stopping_ticks >= 10 && trace.stopping_ticks <= 11 && std::fabs(entity.position.x - 250.0F) < 3.5F && !entity.moving,
                "a walk as clips must start, walk, stop over a whole MOVE_END clip, and end its move near the destination");
    }
    {
        // A stop order mid-stride walks on to the stride's next part and stops there.
        Walk walk;
        walker(walk);
        walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {350.0F, 100.0F, 0.0F}});
        for (int tick = 0; tick < 27; ++tick)
        {
            walk.world.step();
        }
        walk.world.submit({.kind = eaw::SimCommandKind::stop, .entities = {walk.unit}});
        const auto trace = run(walk, 100);
        require(trace.stopping_ticks >= 10 && trace.stopping_ticks <= 11 && at_its_part(trace) && walk.world.snapshot().entities[0].walk_clip == eaw::WalkClip::standing,
                "a stop order must walk on to the stride's next part and play its MOVE_END clip there");
    }
    {
        // A new order while it stops waits for the stop: the heading holds until it stands.
        Walk walk;
        walker(walk);
        walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {120.0F, 100.0F, 0.0F}});
        bool held = true;
        bool ordered = false;
        float heading = 0.0F;
        bool set_off_again = false;
        for (int tick = 0; tick < 300; ++tick)
        {
            walk.world.step();
            const auto& entity = walk.world.snapshot().entities[0];
            if (entity.walk_clip == eaw::WalkClip::stopping && !ordered)
            {
                ordered = true;
                heading = entity.heading;
                walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {entity.position.x, 300.0F, 0.0F}});
            }
            if (ordered && !set_off_again && entity.walk_clip == eaw::WalkClip::stopping && std::fabs(entity.heading - heading) > 1.0e-6F)
            {
                held = false;
            }
            set_off_again = set_off_again || (ordered && entity.walk_clip == eaw::WalkClip::starting);
        }
        require(ordered && held && set_off_again && walk.world.snapshot().entities[0].position.y > 250.0F, "an order given while a walk as clips stops must wait for its stop, then set off");
    }
    {
        // A step too short for the clips still plays them, slower, and ends near its destination.
        Walk walk;
        walker(walk);
        walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {56.0F, 100.0F, 0.0F}});
        const auto trace = run(walk, 100);
        const auto& entity = walk.world.snapshot().entities[0];
        require(trace.stopping_ticks >= 10 && std::fabs(entity.position.x - 56.0F) < 1.5F && !entity.moving,
                "a walk as clips too short for its clips must still play them and end near its destination");
    }
    // A walker with turn clips and a raised leg (BEGIN, loop, END of 0.5, 1, 0.5 s; the leg 1 s up
    // and 1 s down).
    const auto kit_walker = [&](Walk& walk)
    {
        walk.world.set_ground(ground);
        const auto unit = test_spawn(walk.world, {.type_name = "Kit_Walker",
                                                  .position = {150.0F, 100.0F, 0.0F},
                                                  .max_speed = 10.0F,
                                                  .follows_ground = true,
                                                  .passable_types = eaw::passability::clear,
                                                  .radius = 2.0F,
                                                  .turn_rate = 3.0F,
                                                  .movement = {.acceleration = 10.0F,
                                                               .deceleration = 10.0F,
                                                               .turn_speed_fraction = 0.1F,
                                                               .stopped_turn_rate = 1.0F,
                                                               .turn_in_place_angle = 0.5236F,
                                                               .turn_begin_seconds = {0.5F, 0.5F},
                                                               .turn_loop_seconds = {1.0F, 1.0F},
                                                               .turn_end_seconds = {0.5F, 0.5F},
                                                               .walk_start_seconds = 1.0F,
                                                               .walk_stride_seconds = 2.0F,
                                                               .walk_stop_seconds = 1.0F,
                                                               .walk_stop_parts = 4,
                                                               .walk_raise_seconds = 1.0F,
                                                               .walk_lower_seconds = 1.0F}});
        require(static_cast<bool>(unit), "test spawn must succeed");
        walk.unit = *unit;
    };
    {
        // A walk from rest raises the leg first and lowers it after the stop; an order behind the
        // walker mid-walk stops the walk at a stride's part, holding the heading, and the walker
        // turns in place (its clips) before it walks back.
        Walk walk;
        kit_walker(walk);
        walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {350.0F, 100.0F, 0.0F}});
        std::vector<eaw::WalkClip> clips;
        bool turned_in_place = false;
        bool held = true;
        bool reversed = false;
        float heading = 0.0F;
        for (int tick = 1; tick <= 600; ++tick)
        {
            walk.world.step();
            const auto& entity = walk.world.snapshot().entities[0];
            if (clips.empty() || clips.back() != entity.walk_clip)
            {
                clips.push_back(entity.walk_clip);
            }
            if (!reversed && entity.walk_clip == eaw::WalkClip::moving && entity.position.x > 200.0F)
            {
                reversed = true;
                heading = entity.heading;
                walk.world.submit({.kind = eaw::SimCommandKind::move, .entities = {walk.unit}, .target = {60.0F, 100.0F, 0.0F}});
            }
            if (reversed && !turned_in_place && entity.turn_clip == eaw::TurnClip::none && std::fabs(std::remainder(entity.heading - heading, 6.2831853F)) > 0.2F)
            {
                held = false;
            }
            turned_in_place = turned_in_place || (reversed && entity.turn_clip != eaw::TurnClip::none);
        }
        const auto& entity = walk.world.snapshot().entities[0];
        const std::vector<eaw::WalkClip> expected{eaw::WalkClip::raising,  eaw::WalkClip::raised, eaw::WalkClip::starting, eaw::WalkClip::moving, eaw::WalkClip::stopping, eaw::WalkClip::raised,
                                                  eaw::WalkClip::starting, eaw::WalkClip::moving, eaw::WalkClip::stopping, eaw::WalkClip::raised, eaw::WalkClip::lowering, eaw::WalkClip::standing};
        require(reversed && held && turned_in_place && clips == expected && std::fabs(entity.position.x - 60.0F) < 3.5F,
                "a walker must raise its leg to walk, stop and turn in place for an order behind it, and lower the leg at rest");
    }
}

void test_crushing()
{
    eaw::GroundMap ground;
    ground.width = 40;
    ground.height = 40;
    ground.spacing = 10.0F;
    ground.heights.assign(1600, 0.0F);
    ground.passability.assign(1600, eaw::passability::clear);
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    eaw::CombatProfile alive;
    alive.health = 10.0F;
    // `victim_radius`: a victim with a body (that takes part in separation) and legs (it could make
    // way, but stands idle); `speed` the crusher's (the AT-AT's 15 per second moves half a unit a
    // tick here).
    const auto run = [&](const eaw::Crusher kind, const eaw::CrushProfile victim_crush, const std::uint32_t victim_owner, const float victim_radius = 0.0F, const float speed = 50.0F)
    {
        eaw::SimulationWorld world(0.1);
        world.set_ground(ground);
        world.set_hostility(hostility);
        const auto tank = test_spawn(world, {.type_name = "Tank",
                                             .owner = 1,
                                             .position = {50.0F, 100.0F, 0.0F},
                                             .max_speed = speed,
                                             .follows_ground = true,
                                             .passable_types = eaw::passability::clear,
                                             .radius = 10.0F,
                                             .crush = {.crusher = kind}});
        const auto victim = test_spawn(world, {.type_name = "Victim",
                                               .owner = victim_owner,
                                               .position = {150.0F, 100.0F, 0.0F},
                                               .max_speed = victim_radius > 0.0F ? 20.0F : 0.0F,
                                               .follows_ground = true,
                                               .passable_types = eaw::passability::clear,
                                               .combat = alive,
                                               .radius = victim_radius,
                                               .crush = victim_crush});
        require(tank && victim, "test spawns must succeed");
        world.submit({.kind = eaw::SimCommandKind::move, .entities = {*tank}, .target = {250.0F, 100.0F, 0.0F}});
        for (int tick = 0; tick < 600; ++tick)
        {
            world.step();
            // A crushed death is reported as one (for its crushed clip).
            for (const auto& death : world.snapshot().deaths)
            {
                require(death.crushed, "a death by crushing must be reported as crushed");
            }
        }
        return world.stats().crushed;
    };
    const eaw::CrushProfile infantry{.squashable = true};
    const eaw::CrushProfile vehicle{.squashable_by_supercrusher = true};
    require(run(eaw::Crusher::crusher, infantry, 2) == 1, "a moving crusher must run over hostile infantry in its path");
    require(run(eaw::Crusher::crusher, infantry, 1) == 0 && run(eaw::Crusher::none, infantry, 2) == 0 && run(eaw::Crusher::crusher, {}, 2) == 0,
            "friends, non-crushers, and unsquashable entities must not be crushed");
    require(run(eaw::Crusher::crusher, vehicle, 2) == 0 && run(eaw::Crusher::supercrusher, vehicle, 2) == 1, "only a supercrusher must crush vehicles and walls");
    // Standing infantry with a body does not hold a crusher back (the owner's report), fast or slow.
    require(run(eaw::Crusher::crusher, infantry, 2, 3.0F) == 1 && run(eaw::Crusher::supercrusher, infantry, 2, 3.0F, 5.0F) == 1,
            "a crusher must walk over standing hostile infantry instead of being held back");
}

const ModuleRegistrar registrar("navigation", {
                                                  {"test_braking_stop_and_winding_turns", &test_braking_stop_and_winding_turns},
                                                  {"test_walk_as_clips", &test_walk_as_clips},
                                                  {"test_ground_movement", &test_ground_movement},
                                                  {"test_pathfinding", &test_pathfinding},
                                                  {"test_turning_and_separation", &test_turning_and_separation},
                                                  {"test_formations", &test_formations},
                                                  {"test_formation_teams", &test_formation_teams},
                                                  {"test_group_move", &test_group_move},
                                                  {"test_clearance", &test_clearance},
                                                  {"test_chunks", &test_chunks},
                                                  {"test_orca", &test_orca},
                                                  {"test_narrow_passage", &test_narrow_passage},
                                                  {"test_parallel_avoidance", &test_parallel_avoidance},
                                                  {"test_group_cohesion", &test_group_cohesion},
                                                  {"test_ground_footprints", &test_ground_footprints},
                                                  {"test_path_budget_and_shared_routes", &test_path_budget_and_shared_routes},
                                                  {"test_space_avoidance", &test_space_avoidance},
                                                  {"test_space_crowds", &test_space_crowds},
                                                  {"test_movement_profiles", &test_movement_profiles},
                                                  {"test_crushing", &test_crushing},
                                              });

} // namespace
