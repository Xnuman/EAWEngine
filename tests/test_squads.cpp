// Squads (docs/pathfinding-design.md, 4.3): a team entity takes its squad's orders and its members
// stand in a ring around it.

#include "test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {

using namespace eaw_test;

struct Squad
{
    eaw::EntityId team;
    std::vector<eaw::EntityId> members;
};

// A team of `count` armed soldiers of `owner` at `at`, in a dartboard ring; on the ground (followed,
// clear passability) when `grounded`.
Squad make_squad(eaw::SimulationWorld& world, const std::uint32_t owner, const eaw::Vector3 at, const std::size_t count, const float health = 50.0F, const bool grounded = false)
{
    Squad squad;
    const auto passable = grounded ? eaw::passability::clear : 0U;
    squad.team = expect_ok(test_spawn(world, {.type_name = "Team",
                                              .owner = owner,
                                              .position = at,
                                              .max_speed = 8.0F,
                                              .follows_ground = grounded,
                                              .passable_types = passable,
                                              .turn_rate = 10.0F,
                                              .team = eaw::TeamArrangement::dartboard}),
                           "spawning a team");
    eaw::CombatProfile soldier;
    soldier.health = health;
    soldier.weapons = {eaw::WeaponProfile{.range = 100.0F, .recharge_seconds = 0.5F, .damage = 10.0F}};
    for (std::size_t index = 0; index < count; ++index)
    {
        squad.members.push_back(expect_ok(test_spawn(world, {.type_name = "Soldier",
                                                             .owner = owner,
                                                             .position = at,
                                                             .max_speed = 10.0F,
                                                             .selectable = false,
                                                             .follows_ground = grounded,
                                                             .passable_types = passable,
                                                             .combat = soldier,
                                                             .radius = 2.0F,
                                                             .turn_rate = 10.0F,
                                                             .squad = squad.team}),
                                          "spawning a soldier"));
    }
    return squad;
}

const eaw::EntitySnapshot& entity(const eaw::SimulationWorld& world, const eaw::EntityId id)
{
    const auto& entities = world.snapshot().entities;
    return *std::ranges::find(entities, id, &eaw::EntitySnapshot::id);
}

void test_squads()
{
    // Members stand in a ring around their team and follow it; an order to a member moves its team.
    {
        eaw::SimulationWorld world(0.1);
        const auto squad = make_squad(world, 1, {0.0F, 0.0F, 0.0F}, 5);
        world.step();
        require(entity(world, squad.members[0]).squad == squad.team && !entity(world, squad.team).squad.valid(), "a member reports its team, a team none");
        world.submit({eaw::SimCommandKind::move, {squad.members[2]}, {200.0F, 0.0F, 0.0F}});
        for (int tick = 0; tick < 600; ++tick)
        {
            world.step();
        }
        const auto& team = entity(world, squad.team);
        require(std::fabs(team.position.x - 200.0F) < 0.5F && !team.moving, "an order to a member moves its team");
        float farthest = 0.0F;
        float nearest = 1.0e9F;
        for (const auto id : squad.members)
        {
            const auto& member = entity(world, id);
            const auto distance = std::hypot(member.position.x - team.position.x, member.position.y - team.position.y);
            farthest = std::max(farthest, distance);
            nearest = std::min(nearest, distance);
        }
        require(nearest < 0.6F && farthest > 3.0F && farthest < 20.0F, "the members stand in their ring around the team (one at the center, the rest around it)");
        // A stance set on the team reaches its members.
        world.submit({.kind = eaw::SimCommandKind::set_stance, .entities = {squad.team}, .stance = eaw::Stance::hold_fire});
        world.submit({eaw::SimCommandKind::move, {squad.members[0], squad.members[1]}, {0.0F, 0.0F, 0.0F}});
        world.step();
        require(world.stats().stale_references == 0, "orders to members and stances on teams refer to living entities");
    }
    // A team ordered to attack a team: its members fire at the other team's members, and a team
    // leaves with its last member.
    {
        eaw::Hostility hostility;
        hostility.set(1, 2, true);
        eaw::SimulationWorld world(0.1);
        world.set_hostility(hostility);
        auto ours = make_squad(world, 1, {0.0F, 0.0F, 0.0F}, 3);
        auto theirs = make_squad(world, 2, {60.0F, 0.0F, 0.0F}, 3, 20.0F);
        world.step();
        world.submit({.kind = eaw::SimCommandKind::attack, .entities = {ours.team}, .target_entity = theirs.members[1]});
        world.step();
        world.step();
        bool all_on_team = true;
        for (const auto id : ours.members)
        {
            const auto target = entity(world, id).target;
            all_on_team = all_on_team && std::ranges::find(theirs.members, target) != theirs.members.end();
        }
        require(all_on_team, "an attack on a member is an attack on its team: every member targets one of the team's members");
        for (int tick = 0; tick < 300 && world.alive(theirs.team); ++tick)
        {
            world.step();
        }
        require(!world.alive(theirs.team) && std::ranges::none_of(theirs.members,
                                                                  [&](const eaw::EntityId id)
                                                                  {
                                                                      return world.alive(id);
                                                                  }),
                "a team leaves with its last member");
        require(world.snapshot().deaths.empty() || std::ranges::none_of(world.snapshot().deaths,
                                                                        [&](const eaw::DeathSnapshot& death)
                                                                        {
                                                                            return death.id == theirs.team;
                                                                        }),
                "a team leaving is not a death");
    }
    // Destroying a team destroys its members; a member cannot join a team that is not one.
    {
        eaw::SimulationWorld world(0.1);
        const auto squad = make_squad(world, 1, {0.0F, 0.0F, 0.0F}, 4);
        const auto loner = expect_ok(test_spawn(world, {.type_name = "Loner", .position = {50.0F, 0.0F, 0.0F}}), "spawning a loner");
        require(!test_spawn(world, {.type_name = "Stray", .squad = loner}).ok(), "only a team takes members");
        world.submit({eaw::SimCommandKind::destroy, {squad.team}, {}});
        world.step();
        require(!world.alive(squad.team) &&
                    std::ranges::none_of(squad.members,
                                         [&](const eaw::EntityId id)
                                         {
                                             return world.alive(id);
                                         }) &&
                    world.alive(loner),
                "destroying a team destroys its members, and nothing else");
    }
}

const ModuleRegistrar registrar("squads", {{"test_squads", &test_squads}});

} // namespace
