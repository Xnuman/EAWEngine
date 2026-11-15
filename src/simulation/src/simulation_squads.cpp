// Squads (docs/pathfinding-design.md, 4.3; the owner, 2026-11-15): a team entity (XML
// Create_Team_Type containers: TEAM, SELECTABLE, UNIT_AI) takes its squad's orders and plans its
// route; its members walk to their slots of a ring around it and fight on their own unless the team
// was ordered to attack. The team has no body: no footprint, no combat, never hit; it leaves with
// its last member.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

using namespace simulation_detail;

namespace {

// Engine choices, not measured from the original: a member walks to its slot when it stands more
// than this far from it; a team waits for a member farther than its extent plus the leash from its
// slot, for at most the wait limit at a stretch (a member that cannot reach its slot, inside an
// obstacle, must not hold its team for good).
constexpr float squad_follow_tolerance = 0.5F;
constexpr float squad_leash = 40.0F;
constexpr float squad_wait_limit_seconds = 2.0F;

} // namespace

void SimulationWorld::join_squad(const std::size_t member, const EntityId squad, const float raggedness)
{
    auto& members = squads_.get(squad.index, &SquadColumns::states).members;
    const auto id = identity_.ids[member];
    members.insert(std::ranges::upper_bound(members, id), id);
    squad_members_.columns.states[squad_members_.add(id.index)] = {.squad = squad, .raggedness = raggedness};
    reshape_squad(squad.index);
}

void SimulationWorld::reshape_squad(const std::uint32_t squad_slot)
{
    auto& squad = squads_.get(squad_slot, &SquadColumns::states);
    std::vector<FormationMember> shape_members;
    for (const auto id : squad.members)
    {
        const auto dense = dense_index(*this, id);
        EAW_ASSERT_MSG(dense.has_value(), "a squad lists only living members");
        const auto& recipe = recipes_.list[identity_.recipes[*dense]];
        shape_members.push_back({.id = id,
                                 .position = placement_.positions[*dense],
                                 .radius = placement_.radii[*dense],
                                 .spacing = recipe.formation_spacing,
                                 .order = recipe.formation_order,
                                 .team = {.raggedness = squad_members_.get(id.index, &SquadMemberColumns::states).raggedness}});
    }
    const auto shape = team_shape(shape_members, squad.arrangement);
    squad.extent = shape.extent;
    for (std::size_t slot = 0; slot < squad.members.size(); ++slot)
    {
        auto& member = squad_members_.get(squad.members[slot].index, &SquadMemberColumns::states);
        member.ahead = shape.offsets[slot].first;
        member.aside = shape.offsets[slot].second;
    }
}

SimCommand SimulationWorld::for_squads(const SimCommand& command) const
{
    const auto team_of = [&](const EntityId id)
    {
        const auto dense = dense_index(*this, id);
        if (!dense)
        {
            return id;
        }
        const auto* member = squad_members_.find(identity_.ids[*dense].index, &SquadMemberColumns::states);
        return member != nullptr ? member->squad : id;
    };
    auto result = command;
    switch (command.kind)
    {
    case SimCommandKind::move:
    case SimCommandKind::stop:
    case SimCommandKind::attack:
    case SimCommandKind::formation_move:
    case SimCommandKind::attack_move:
    case SimCommandKind::guard:
    {
        // Each team once, where its first member stood in the list.
        result.entities.clear();
        for (const auto id : command.entities)
        {
            const auto team = team_of(id);
            if (std::ranges::find(result.entities, team) == result.entities.end())
            {
                result.entities.push_back(team);
            }
        }
        if (command.target_entity.valid())
        {
            result.target_entity = team_of(command.target_entity);
        }
        break;
    }
    case SimCommandKind::set_stance:
        result.entities.clear();
        for (const auto id : command.entities)
        {
            const auto dense = dense_index(*this, id);
            const auto* squad = dense ? squads_.find(identity_.ids[*dense].index, &SquadColumns::states) : nullptr;
            if (squad == nullptr)
            {
                result.entities.push_back(id);
                continue;
            }
            result.entities.insert(result.entities.end(), squad->members.begin(), squad->members.end());
        }
        break;
    default:
        break;
    }
    return result;
}

void SimulationWorld::order_squad_attack(const std::size_t squad, const std::size_t victim)
{
    auto& state = squads_.get(identity_.ids[squad].index, &SquadColumns::states);
    state.target = identity_.ids[victim];
    state.chase_timer = 0;
    const bool team = squads_.find(identity_.ids[victim].index).has_value();
    for (const auto id : state.members)
    {
        auto* member = combat_of(*this, *dense_index(*this, id));
        if (member == nullptr || member->profile->weapons.empty())
        {
            continue;
        }
        member->target = team ? EntityId{} : identity_.ids[victim];
        member->ordered_squad = team ? identity_.ids[victim] : EntityId{};
        member->ordered = !team;
        member->chase_timer = 0;
    }
}

void SimulationWorld::clear_squad_attack(const std::size_t squad)
{
    auto* state = squads_.find(identity_.ids[squad].index, &SquadColumns::states);
    if (state == nullptr)
    {
        return;
    }
    state->target = {};
    for (const auto id : state->members)
    {
        const auto dense = *dense_index(*this, id);
        drop_target(*this, dense);
        if (auto* member = combat_of(*this, dense))
        {
            member->ordered_squad = {};
        }
    }
}

void SimulationWorld::expand_squad_removals(std::vector<std::size_t>& dense, std::vector<std::uint32_t>& reshaped)
{
    // A team removed takes its members (a script destroying a squad destroys its soldiers).
    for (std::size_t next = 0; next < dense.size(); ++next)
    {
        if (const auto* squad = squads_.find(identity_.ids[dense[next]].index, &SquadColumns::states))
        {
            for (const auto id : squad->members)
            {
                dense.push_back(*dense_index(*this, id));
            }
        }
    }
    std::ranges::sort(dense);
    dense.erase(std::unique(dense.begin(), dense.end()), dense.end());
    // Members leave their team; a team whose last member goes leaves with it.
    const auto size = dense.size();
    for (std::size_t next = 0; next < size; ++next)
    {
        const auto* member = squad_members_.find(identity_.ids[dense[next]].index, &SquadMemberColumns::states);
        if (member == nullptr)
        {
            continue;
        }
        const auto team = member->squad;
        auto& members = squads_.get(team.index, &SquadColumns::states).members;
        std::erase(members, identity_.ids[dense[next]]);
        const auto team_dense = *dense_index(*this, team);
        if (members.empty())
        {
            dense.push_back(team_dense);
        }
        else if (std::ranges::find(reshaped, team.index) == reshaped.end())
        {
            reshaped.push_back(team.index);
        }
    }
    std::ranges::sort(dense);
    dense.erase(std::unique(dense.begin(), dense.end()), dense.end());
    std::erase_if(reshaped,
                  [&](const std::uint32_t slot)
                  {
                      return squads_.get(slot, &SquadColumns::states).members.empty();
                  });
}

void SimulationWorld::BehaviorSystem::pursue_squad_target(const View& view, const std::size_t index)
{
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& squads = view.get<ComponentPool<SquadColumns>>();
    auto& squad = squads.get(slot_of(view, index), &SquadColumns::states);
    const auto victim = dense_index(view, squad.target);
    const auto* victim_squad = victim ? squads.find(slot_of(view, *victim), &SquadColumns::states) : nullptr;
    if (!victim || (victim_squad == nullptr && !damageable(view, *victim)))
    {
        squad.target = {};
        return;
    }
    // In reach when its members' shortest weapon reaches the target (a team's ring counts by its
    // extent).
    auto reach = std::numeric_limits<float>::max();
    for (const auto id : squad.members)
    {
        if (const auto member = dense_index(view, id); member && !combat_state(view, *member).profile->weapons.empty())
        {
            reach = std::min(reach, combat_state(view, *member).profile->range());
        }
    }
    if (reach == std::numeric_limits<float>::max())
    {
        squad.target = {};
        return;
    }
    reach += squad.extent + (victim_squad != nullptr ? victim_squad->extent : 0.0F);
    if (distance_xy(placement.positions[index], placement.positions[*victim]) > reach)
    {
        if (squad.chase_timer == 0 || !route.move_targets[index])
        {
            plan_move(view, index, placement.positions[*victim]);
            squad.chase_timer = ticks_for(view, chase_seconds);
        }
        else
        {
            --squad.chase_timer;
        }
        return;
    }
    // In reach: the team holds and its members fire.
    route.move_targets[index].reset();
    route.paths[index].clear();
    locomotor.speed_limits[index] = 0.0F;
}

void SimulationWorld::BehaviorSystem::follow_squads(const View& view)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& orders = view.get<OrderColumns>();
    auto& squads = view.get<ComponentPool<SquadColumns>>();
    auto& members = view.get<ComponentPool<SquadMemberColumns>>();
    const auto wait_limit = ticks_for(view, squad_wait_limit_seconds);
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        auto* squad = squads.find(identity.ids[index].index, &SquadColumns::states);
        if (squad == nullptr)
        {
            continue;
        }
        // Forward along the team's facing, right its clockwise perpendicular (as formation slots).
        const auto heading = placement.headings[index];
        const auto forward_x = std::cos(heading);
        const auto forward_y = std::sin(heading);
        const auto& center = placement.positions[index];
        float lag = 0.0F;
        bool fighting = false;
        for (const auto id : squad->members)
        {
            const auto member = *dense_index(view, id);
            const auto& offset = members.get(id.index, &SquadMemberColumns::states);
            const Vector3 slot{center.x + forward_x * offset.ahead + forward_y * offset.aside, center.y + forward_y * offset.ahead - forward_x * offset.aside, center.z};
            const auto& at = placement.positions[member];
            const auto away = std::hypot(slot.x - at.x, slot.y - at.y);
            lag = std::max(lag, away);
            fighting = fighting || combat_state(view, member).target.valid();
            // Straight to the slot, a few world units from the team that planned the way.
            if (away > squad_follow_tolerance && (!route.move_targets[member] || std::hypot(route.move_targets[member]->x - slot.x, route.move_targets[member]->y - slot.y) > squad_follow_tolerance))
            {
                route.move_targets[member] = slot;
                route.paths[member].clear();
                route.pending[member] = 0;
            }
        }
        // shortcut: a member walks straight to its slot; around a corner it can be blocked until the
        // team moves on. Members plan with the clearance and avoidance of design steps 4 and 7.
        const bool behind = lag > squad->extent + squad_leash;
        const bool engaged = orders.standing[index].kind != StandingOrder::none && fighting;
        const bool wait = (behind && squad->wait_ticks < wait_limit) || engaged;
        squad->wait_ticks = behind ? squad->wait_ticks + 1U : 0U;
        squad->waiting = wait;
    }
}

} // namespace eaw
