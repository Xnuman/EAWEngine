#include "sandbox/movement_scenarios.hpp"

#include "math/scalar.hpp"
#include "simulation/ground_map.hpp"
#include "simulation/simulation.hpp"
#include "sandbox/movement_metrics.hpp"
#include "sandbox/text_format.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <string_view>
#include <vector>

namespace eaw {

namespace {

// Flat ground of 80 x 80 cells, 10 units apart; with a wall across x = 400, open only for a gap of
// gap_cells cells around y = 400 (one tank wide).
constexpr std::uint32_t ground_cells = 80;
constexpr float ground_spacing = 10.0F;
constexpr std::uint32_t wall_column = 40;
constexpr std::uint32_t gap_first_row = 38;
constexpr std::uint32_t gap_cells = 4;
// The longest a scenario runs (60 s at 60 ticks a second).
constexpr std::uint64_t scenario_ticks = 3600;
constexpr Vector3 center{400.0F, 400.0F, 0.0F};

GroundMap make_ground(const bool wall)
{
    GroundMap ground;
    ground.width = ground_cells;
    ground.height = ground_cells;
    ground.spacing = ground_spacing;
    ground.heights.assign(static_cast<std::size_t>(ground_cells) * ground_cells, 0.0F);
    ground.passability.assign(ground.heights.size(), passability::clear);
    if (wall)
    {
        for (std::uint32_t row = 0; row < ground_cells; ++row)
        {
            if (row < gap_first_row || row >= gap_first_row + gap_cells)
            {
                ground.passability[static_cast<std::size_t>(row) * ground_cells + wall_column] = passability::impassable;
            }
        }
    }
    return ground;
}

// A hover tank roughly like a T2-B (an approximation for comparing runs, not its converted XML
// values): a 40 x 20 footprint, quick but with a finite turn, stopping to turn at corners sharper
// than 45 degrees.
EntityRecipe tank_recipe()
{
    return {.type_name = "Tank",
            .max_speed = 100.0F,
            .follows_ground = true,
            .passable_types = passability::clear,
            .radius = 14.0F,
            .body = {.half_length = 20.0F, .half_width = 10.0F},
            .turn_rate = 3.0F,
            .movement = {.acceleration = 300.0F, .deceleration = 300.0F, .turn_speed_fraction = 0.3F, .stopped_turn_rate = 3.0F, .turn_in_place_angle = half_pi * 0.5F},
            .formation_spacing = 1.5F};
}

struct Scenario
{
    std::string_view name;
    bool wall{};
    // Spawns the tanks and gives their orders.
    std::function<void(SimulationWorld&, RecipeId)> setup;
};

EntityId spawn_tank(SimulationWorld& world, const RecipeId recipe, const Vector3& at, const float heading)
{
    const auto spawned = world.spawn({.recipe = recipe, .owner = 1, .position = at, .heading = heading});
    return spawned ? *spawned : EntityId{};
}

// `count` tanks in rows of five around `middle`, 50 units apart, facing `heading`.
std::vector<EntityId> spawn_block(SimulationWorld& world, const RecipeId recipe, const Vector3& middle, const std::size_t count, const float heading)
{
    constexpr std::size_t per_row = 5;
    constexpr float pitch = 50.0F;
    std::vector<EntityId> tanks;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto row = static_cast<float>(index / per_row);
        const auto column = static_cast<float>(index % per_row) - 2.0F;
        tanks.push_back(spawn_tank(world, recipe, {middle.x - row * pitch, middle.y + column * pitch, 0.0F}, heading));
    }
    return tanks;
}

std::vector<Scenario> scenarios()
{
    return {
        {"passage, formation", true,
         [](SimulationWorld& world, const RecipeId recipe)
         {
             world.submit({.kind = SimCommandKind::formation_move, .entities = spawn_block(world, recipe, {250.0F, 400.0F, 0.0F}, 10, 0.0F), .target = {650.0F, 400.0F, 0.0F}});
         }},
        {"passage, group move", true,
         [](SimulationWorld& world, const RecipeId recipe)
         {
             world.submit({.kind = SimCommandKind::move, .entities = spawn_block(world, recipe, {250.0F, 400.0F, 0.0F}, 10, 0.0F), .target = {650.0F, 400.0F, 0.0F}});
         }},
        {"head-on pair", false,
         [](SimulationWorld& world, const RecipeId recipe)
         {
             const Vector3 west{200.0F, 400.0F, 0.0F};
             const Vector3 east{600.0F, 400.0F, 0.0F};
             world.submit({.kind = SimCommandKind::move, .entities = {spawn_tank(world, recipe, west, 0.0F)}, .target = east});
             world.submit({.kind = SimCommandKind::move, .entities = {spawn_tank(world, recipe, east, pi)}, .target = west});
         }},
        {"three converging", false,
         [](SimulationWorld& world, const RecipeId recipe)
         {
             constexpr float reach = 200.0F;
             for (int tank = 0; tank < 3; ++tank)
             {
                 const auto angle = half_pi + two_pi * static_cast<float>(tank) / 3.0F;
                 const Vector3 from{center.x + std::cos(angle) * reach, center.y + std::sin(angle) * reach, 0.0F};
                 const Vector3 to{center.x - std::cos(angle) * reach, center.y - std::sin(angle) * reach, 0.0F};
                 world.submit({.kind = SimCommandKind::move, .entities = {spawn_tank(world, recipe, from, angle + pi)}, .target = to});
             }
         }},
        {"passing standing units", false,
         [](SimulationWorld& world, const RecipeId recipe)
         {
             // Three idle tanks of no order stand across the way of a group of ten (the owner's rule:
             // a formation moves freely past a unit until it is behind it).
             for (const float y : {350.0F, 400.0F, 450.0F})
             {
                 (void)spawn_tank(world, recipe, {450.0F, y, 0.0F}, half_pi);
             }
             world.submit({.kind = SimCommandKind::group_move, .entities = spawn_block(world, recipe, {250.0F, 400.0F, 0.0F}, 10, 0.0F), .target = {650.0F, 400.0F, 0.0F}});
         }},
        {"crossing groups", false,
         [](SimulationWorld& world, const RecipeId recipe)
         {
             const auto west = spawn_block(world, recipe, {200.0F, 400.0F, 0.0F}, 5, 0.0F);
             const auto east = spawn_block(world, recipe, {600.0F, 400.0F, 0.0F}, 5, pi);
             world.submit({.kind = SimCommandKind::formation_move, .entities = west, .target = {600.0F, 400.0F, 0.0F}});
             world.submit({.kind = SimCommandKind::formation_move, .entities = east, .target = {200.0F, 400.0F, 0.0F}});
         }},
    };
}

} // namespace

int run_movement_scenarios()
{
    std::cout << "Movement scenarios (synthetic ground, " << scenario_ticks << " ticks at most each)\n";
    for (const auto& scenario : scenarios())
    {
        SimulationWorld world;
        world.set_ground(make_ground(scenario.wall));
        const auto recipe = world.add_recipe(tank_recipe());
        if (!recipe)
        {
            std::cout << scenario.name << ": the tank recipe was rejected: " << recipe.error().message << '\n';
            continue;
        }
        scenario.setup(world, *recipe);
        MovementMeter meter;
        std::uint64_t ticks = 0;
        std::size_t moving = 0;
        for (; ticks < scenario_ticks; ++ticks)
        {
            world.step();
            meter.observe(world.snapshot());
            moving = 0;
            for (const auto& entity : world.snapshot().entities)
            {
                moving += entity.moving ? 1U : 0U;
            }
            // Done when nothing moves and no formation is still on its way (between its members'
            // tries a formation may have none moving).
            if (moving == 0 && world.stats().land_formations == 0 && ticks > 0)
            {
                ++ticks;
                break;
            }
        }
        const auto stats = world.stats();
        std::cout << scenario.name << ": " << format_fixed(static_cast<double>(ticks) * world.tick_seconds(), 1) << " s, " << moving << " still moving | "
                  << format_movement(meter.metrics(), stats, world.tick_seconds()) << " | blocked " << stats.blocked_moves << " | hash " << format_hex(world.state_hash()) << '\n';
    }
    return 0;
}

} // namespace eaw
