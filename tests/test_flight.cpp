// Flight: fighter locomotion, flight planes, half loops, and dogfights.
#include "test_support.hpp"

#include "simulation/formation.hpp"

#include <tuple>

#include <memory>
#include <numbers>

namespace {

using namespace eaw_test;

void test_fighter_flight()
{
    // A fighter: 120 units/s top speed, 75 minimum, a quarter turn per second (turn circle radius
    // about 48 at minimum speed).
    const eaw::EntitySpawn fighter{.type_name = "Fighter", .owner = 1, .max_speed = 120.0F, .turn_rate = std::numbers::pi_v<float> / 2.0F, .min_speed = 75.0F};
    const auto distance = [](const eaw::Vector3& a, const eaw::Vector3& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    };

    // Idle, it never stops but circles where it is; ordered away, it flies there and circles that point.
    {
        eaw::SimulationWorld world(0.1);
        const auto id = world.spawn(fighter);
        require(id.ok(), "test spawn must succeed");
        // Snapshots exist from the first tick on; the fighter starts at the origin.
        eaw::Vector3 previous{};
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

// Flyers keep the plane they cruise in, report their turning for presentation, and push squadron
// mates (same owner) apart sideways; flyers of other owners pass through each other.
void test_flight_plane_and_separation()
{
    const eaw::EntitySpawn fighter{
        .type_name = "Fighter", .owner = 1, .position = {0.0F, 0.0F, 100.0F}, .max_speed = 120.0F, .radius = 5.0F, .turn_rate = std::numbers::pi_v<float> / 2.0F, .min_speed = 75.0F};
    {
        eaw::SimulationWorld world(0.1);
        const auto id = world.spawn(fighter);
        require(id.ok(), "test spawn must succeed");
        // An order to a point on another plane: the fighter turns left toward it but stays at its height.
        world.submit({eaw::SimCommandKind::move, {*id}, {0.0F, 1000.0F, 0.0F}});
        world.step();
        require(world.snapshot().entities[0].yaw_rate > 1.0F, "a fighter turning left must report a positive yaw rate");
        for (int tick = 0; tick < 200; ++tick)
        {
            world.step();
            const auto& entity = world.snapshot().entities[0];
            require(std::fabs(entity.position.z - 100.0F) < 1.0e-3F && entity.pitch == 0.0F, "a fighter must keep the plane it cruises in");
        }
    }
    const auto separation = [&](const std::uint32_t second_owner)
    {
        eaw::SimulationWorld world(0.1);
        auto other = fighter;
        other.owner = second_owner;
        require(world.spawn(fighter).ok() && world.spawn(other).ok(), "test spawns must succeed");
        for (int tick = 0; tick < 60; ++tick)
        {
            world.step();
        }
        const auto& entities = world.snapshot().entities;
        require(std::fabs(entities[0].position.z - 100.0F) < 1.0e-3F && std::fabs(entities[1].position.z - 100.0F) < 1.0e-3F, "separation must keep flyers in their plane");
        return std::hypot(entities[0].position.x - entities[1].position.x, entities[0].position.y - entities[1].position.y);
    };
    require(separation(1) > 9.0F, "squadron mates must push each other apart");
    require(separation(2) < 1.0e-3F, "flyers of different owners must not push each other");
}

// Flyers reverse through a vertical half-loop when their goal is far behind (a flat turn otherwise),
// and flyers of any class dogfight each other above and below their plane, returning to it after.
void test_half_loops_and_dogfights()
{
    const eaw::EntitySpawn fighter{
        .type_name = "Fighter", .owner = 1, .position = {0.0F, 0.0F, 100.0F}, .max_speed = 120.0F, .radius = 5.0F, .turn_rate = std::numbers::pi_v<float> / 2.0F, .min_speed = 75.0F};
    const auto heading_error = [](const float heading, const float wanted)
    {
        return std::fabs(std::remainder(heading - wanted, 2.0F * std::numbers::pi_v<float>));
    };

    // An order far behind: the fighter climbs over the top, comes out heading back with a roll to
    // unwind, then returns to its plane and flies on.
    {
        eaw::SimulationWorld world(0.1);
        const auto id = world.spawn(fighter);
        require(id.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*id}, {-8000.0F, 0.0F, 0.0F}});
        float highest = 100.0F;
        bool rolled = false;
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
            const auto& entity = world.snapshot().entities[0];
            highest = std::max(highest, entity.position.z);
            rolled = rolled || std::fabs(entity.roll) > 2.0F;
        }
        const auto& entity = world.snapshot().entities[0];
        require(highest > 150.0F && rolled, "a reversal must climb through a half-loop and roll out of it");
        require(heading_error(entity.heading, std::numbers::pi_v<float>) < 0.1F && entity.roll == 0.0F, "after the half-loop the fighter must fly back upright");
        require(std::fabs(entity.position.z - 100.0F) < 1.0F && entity.position.x < -500.0F, "after the half-loop the fighter must return to its plane and fly on");
    }
    // A goal off to the side is a flat turn.
    {
        eaw::SimulationWorld world(0.1);
        const auto id = world.spawn(fighter);
        require(id.ok(), "test spawn must succeed");
        world.submit({eaw::SimCommandKind::move, {*id}, {0.0F, 3000.0F, 0.0F}});
        for (int tick = 0; tick < 100; ++tick)
        {
            world.step();
            require(std::fabs(world.snapshot().entities[0].position.z - 100.0F) < 1.0e-3F, "a turn to the side must stay in the plane");
        }
    }
    // Two hostile flyers (say a fighter and a bomber) dogfight: they leave the plane within the band,
    // stay close, and the survivor returns to its plane afterwards.
    {
        eaw::Hostility hostility;
        hostility.set(1, 2, true);
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        auto hunter = fighter;
        hunter.combat.health = 1.0e6F;
        hunter.combat.weapons = {eaw::WeaponProfile{.range = 450.0F, .recharge_seconds = 1.0F, .damage = 1.0F}};
        auto bomber = hunter;
        bomber.type_name = "Bomber";
        bomber.owner = 2;
        bomber.position = {300.0F, 50.0F, 100.0F};
        bomber.heading = std::numbers::pi_v<float>;
        bomber.max_speed = 100.0F;
        bomber.min_speed = 60.0F;
        bomber.combat.health = 60.0F;
        require(world.spawn(hunter).ok() && world.spawn(bomber).ok(), "test spawns must succeed");
        float lowest = 100.0F;
        float highest = 100.0F;
        float farthest = 0.0F;
        int ticks = 0;
        for (; ticks < 1200 && world.snapshot().entities.size() != 1; ++ticks)
        {
            world.step();
            const auto& entities = world.snapshot().entities;
            for (const auto& entity : entities)
            {
                lowest = std::min(lowest, entity.position.z);
                highest = std::max(highest, entity.position.z);
            }
            if (entities.size() == 2)
            {
                farthest = std::max(farthest, std::hypot(entities[0].position.x - entities[1].position.x, entities[0].position.y - entities[1].position.y));
            }
        }
        require(world.snapshot().entities.size() == 1, "the dogfight must end with the bomber shot down");
        require(highest - lowest > 30.0F, "a dogfight must leave the plane");
        require(lowest > 100.0F - 2.0F * 150.0F && highest < 100.0F + 2.0F * 150.0F, "a dogfight must stay near its band around the plane");
        require(farthest < 700.0F, "dogfighting flyers must stay close to each other");
        for (int tick = 0; tick < 300; ++tick)
        {
            world.step();
        }
        require(std::fabs(world.snapshot().entities[0].position.z - 100.0F) < 1.0F, "after a dogfight a flyer must return to its plane");
    }
}

const ModuleRegistrar registrar("flight", {
                                              {"test_fighter_flight", &test_fighter_flight},
                                              {"test_flight_plane_and_separation", &test_flight_plane_and_separation},
                                              {"test_half_loops_and_dogfights", &test_half_loops_and_dogfights},
                                          });

} // namespace
