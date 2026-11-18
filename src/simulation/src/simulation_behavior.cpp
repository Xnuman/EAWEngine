// Behavior layer: orders from commands and what each unit wants to do with them each tick.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::apply(const SimCommand& given)
{
    const auto command = for_squads(given);
    ++commands_applied_;
    if ((command.kind == SimCommandKind::move || command.kind == SimCommandKind::formation_move || command.kind == SimCommandKind::group_move) && !is_finite(command.target))
    {
        ++stale_references_;
        return;
    }
    if ((command.kind == SimCommandKind::attack_move || command.kind == SimCommandKind::guard) && !is_finite(command.target))
    {
        ++stale_references_;
        return;
    }
    // A new move, stop, or standing order ends a team's attack order.
    if (command.kind == SimCommandKind::move || command.kind == SimCommandKind::stop || command.kind == SimCommandKind::formation_move || command.kind == SimCommandKind::group_move ||
        command.kind == SimCommandKind::attack_move || command.kind == SimCommandKind::guard)
    {
        for (const auto id : command.entities)
        {
            if (const auto dense = dense_index(*this, id))
            {
                clear_squad_attack(*dense);
            }
        }
    }
    if (command.kind == SimCommandKind::formation_move || command.kind == SimCommandKind::group_move)
    {
        apply_formation_move(command);
        return;
    }
    if (command.kind == SimCommandKind::attack_move || command.kind == SimCommandKind::guard)
    {
        apply_standing_order(command);
        return;
    }
    if (command.kind == SimCommandKind::spawn)
    {
        // A squad member names its team by its index in this list; teams spawn before their members.
        std::vector<EntityId> made(command.spawns.size());
        for (const bool members : {false, true})
        {
            for (std::size_t index = 0; index < command.spawns.size(); ++index)
            {
                const auto& entity = command.spawns[index];
                if (entity.squad.has_value() != members)
                {
                    continue;
                }
                const auto squad = entity.squad && *entity.squad < made.size() ? made[*entity.squad] : EntityId{};
                const auto id = entity.squad && !squad.valid() ? Result<EntityId>(make_error("its team was not spawned")) : spawn(entity, squad);
                if (!id)
                {
                    ++rejected_spawns_;
                    continue;
                }
                made[index] = *id;
            }
        }
        return;
    }
    // The units of one move order share its number (RouteColumns::orders); a stop or an attack ends it.
    const auto move_order = command.kind == SimCommandKind::move ? next_move_order() : 0U;
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(*this, id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        if (command.kind == SimCommandKind::move || command.kind == SimCommandKind::stop || command.kind == SimCommandKind::attack)
        {
            route_.orders[*dense] = move_order;
            orders_.command_serials[*dense] = command.serial;
            orders_.standing[*dense].kind = StandingOrder::none;
            orders_.standing[*dense].engaging = false;
        }
        switch (command.kind)
        {
        case SimCommandKind::formation_move:
        case SimCommandKind::group_move:
        case SimCommandKind::spawn:
        case SimCommandKind::attack_move:
        case SimCommandKind::guard:
            break;
        case SimCommandKind::set_invulnerable:
            if (auto* state = combat_of(*this, *dense))
            {
                state->invulnerable = command.flag;
            }
            break;
        case SimCommandKind::set_cannot_be_killed:
            if (auto* state = combat_of(*this, *dense))
            {
                state->cannot_be_killed = command.flag;
            }
            break;
        case SimCommandKind::suspend_locomotor:
            locomotor_.suspended[*dense] = command.flag ? 1U : 0U;
            break;
        case SimCommandKind::damage:
            // Damage only entities that can die (structures without health stay); removal is the
            // combat layer's, as for shots.
            if (auto* state = combat_of(*this, *dense); state != nullptr && state->profile->health > 0.0F && std::isfinite(command.amount))
            {
                (void)wound(*state, command.amount);
            }
            break;
        case SimCommandKind::teleport:
            if (!is_finite(command.target) || (command.heading && !std::isfinite(*command.heading)))
            {
                ++stale_references_;
                break;
            }
            teleport(*dense, command.target, command.heading);
            break;
        case SimCommandKind::move:
            drop_target(*this, *dense);
            locomotor_.speed_limits[*dense] = 0.0F;
            if (locomotor_.max_speeds[*dense] > 0.0F)
            {
                plan_move(*this, *dense, command.target);
            }
            break;
        case SimCommandKind::stop:
            locomotor_.speed_limits[*dense] = 0.0F;
            leave_group(*this, *dense);
            route_.move_targets[*dense].reset();
            route_.paths[*dense].clear();
            drop_target(*this, *dense);
            // A ground unit that brakes (MovementProfile::deceleration) coasts to a stop straight ahead
            // over its braking distance instead of halting at once (a walk played as clips walks on to
            // its stride's next part and plays its stop there: walk_straight).
            if (const auto speed = locomotor_.drive_speeds[*dense]; ground_mover_.follows_ground[*dense] != 0 && locomotor_.movements[*dense]->deceleration > 0.0F &&
                                                                    locomotor_.movements[*dense]->walk_stop_parts == 0 && speed > 0.0F && locomotor_.max_speeds[*dense] > 0.0F)
            {
                const auto distance = speed * speed / (2.0F * locomotor_.movements[*dense]->deceleration);
                const auto& here = placement_.positions[*dense];
                plan_move(*this, *dense, {here.x + std::cos(placement_.headings[*dense]) * distance, here.y + std::sin(placement_.headings[*dense]) * distance, here.z});
            }
            break;
        case SimCommandKind::attack:
        {
            const auto victim = dense_index(*this, command.target_entity);
            auto* combat = combat_of(*this, *dense);
            // A team is a target while it has members (it has no body of its own).
            const bool team = victim && squads_.find(identity_.ids[*victim].index).has_value();
            if (!victim || (!team && !damageable(*this, *victim)))
            {
                ++stale_references_;
                break;
            }
            if (*victim == *dense || !rules_.hostility.hostile(identity_.owners[*dense], identity_.owners[*victim]))
            {
                break;
            }
            if (squads_.find(identity_.ids[*dense].index))
            {
                // A team attacks with its members; it chases until they reach (pursue_squad_target).
                order_squad_attack(*dense, *victim);
                locomotor_.speed_limits[*dense] = 0.0F;
                leave_group(*this, *dense);
            }
            else if (combat != nullptr && !combat->profile->weapons.empty())
            {
                // Against a team: any of its members (update_target picks the nearest).
                combat->target = team ? EntityId{} : command.target_entity;
                combat->ordered_squad = team ? command.target_entity : EntityId{};
                combat->ordered = !team;
                locomotor_.speed_limits[*dense] = 0.0F;
                leave_group(*this, *dense);
                combat->chase_timer = 0;
            }
            break;
        }
        case SimCommandKind::destroy:
            remove_entities({*dense});
            break;
        case SimCommandKind::set_stance:
            if (command.stance != Stance::aggressive && command.stance != Stance::hold_position && command.stance != Stance::hold_fire)
            {
                ++stale_references_;
                break;
            }
            if (auto* state = combat_of(*this, *dense))
            {
                state->stance = command.stance;
                if (command.stance == Stance::hold_fire && !state->ordered)
                {
                    state->target = {};
                }
            }
            break;
        }
    }
}

// Each tick, before movement: every unit turns its order, stance, and target into what it asks of
// navigation (a move request) or of its locomotor (a steering intent). Combat later picks targets
// and fires; it never moves a unit.
void SimulationWorld::BehaviorSystem::run(const View& view, const float dt)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& ground_mover = view.get<GroundMoverColumns>();
    auto& route = view.get<RouteColumns>();
    auto& orders = view.get<OrderColumns>();
    auto& steering = view.get<SteeringColumns>();
    auto& ground = view.get<GroundState>();
    auto& squads = view.get<ComponentPool<SquadColumns>>();
    auto& members = view.get<ComponentPool<SquadMemberColumns>>();
    // Standing orders scan with the spatial grid; built here only in ticks where one scans (the
    // grid of the last tick's combat names entities by dense indices that spawns and removals since
    // then have shifted).
    const bool scans = std::ranges::any_of(orders.standing,
                                           [](const StandingOrderState& order)
                                           {
                                               return order.kind != StandingOrder::none && !order.engaging && order.scan_timer == 0;
                                           });
    if (scans)
    {
        grid_.build(placement.positions, grid_cell_size);
    }
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        steering.intents[index] = {};
        run_standing_order(view, index);
        if (flies(view, index))
        {
            plan_flight(view, index, dt);
        }
        else
        {
            if (const auto* squad = squads.find(identity.ids[index].index, &SquadColumns::states); squad != nullptr && squad->target.valid())
            {
                pursue_squad_target(view, index);
            }
            else if (combat_state(view, index).ordered && members.find(identity.ids[index].index) == std::nullopt)
            {
                pursue_ordered_target(view, index);
            }
            // Standing body-aimers turn to face their target (ordered or picked on their own), and so
            // do units whose limited turret cannot reach it (unless their turret does not turn them).
            if (!route.move_targets[index])
            {
                if (const auto victim = attack_victim(view, index))
                {
                    const auto& turret = combat_state(view, index).profile->turret;
                    const auto& to = placement.positions[*victim];
                    const bool beyond_turret =
                        turret && turret->turns_body && turret->yaw_limit < pi &&
                        std::fabs(wrap_angle(std::atan2(to.y - placement.positions[index].y, to.x - placement.positions[index].x) - placement.headings[index])) > turret->yaw_limit;
                    if (combat_state(view, index).aims_with_body || beyond_turret)
                    {
                        steering.intents[index].point = to;
                    }
                    else if (combat_state(view, index).ordered && ground_mover.follows_ground[index] == 0 && !ground.map.valid())
                    {
                        // An ordered space ship turns in place toward its target's nearest live
                        // hardpoint; an idle one keeps its heading (recorded in the FoC debug build:
                        // EaW-Revised S-22 to S-27, docs/compatibility-research.md).
                        steering.intents[index].point = aim_point(view, *victim, aim_hardpoint(view, *victim, placement.positions[index]));
                    }
                }
            }
        }
    }
    follow_squads(view);
}

void SimulationWorld::apply_standing_order(const SimCommand& command)
{
    const bool attack_move = command.kind == SimCommandKind::attack_move;
    // An attack-move to a point moves the group as a group move; each member then keeps its own slot
    // as destination. To an object, or a guard, each entity heads for the object or point.
    if (attack_move && !command.target_entity.valid())
    {
        SimCommand move = command;
        move.kind = SimCommandKind::group_move;
        apply_formation_move(move);
    }
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(*this, id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        auto& order = orders_.standing[*dense];
        orders_.command_serials[*dense] = command.serial;
        drop_target(*this, *dense);
        order.kind = attack_move ? StandingOrder::attack_move : StandingOrder::guard;
        order.entity = command.target_entity;
        order.point = command.target;
        order.engaging = false;
        if (attack_move && !command.target_entity.valid())
        {
            // The member's formation slot, where the formation move sent it (immobile members keep
            // the ordered point).
            if (route_.move_targets[*dense])
            {
                order.point = *route_.move_targets[*dense];
            }
            continue;
        }
        locomotor_.speed_limits[*dense] = 0.0F;
        leave_group(*this, *dense);
        if (const auto anchor = command.target_entity.valid() ? dense_index(*this, command.target_entity) : std::optional<std::size_t>(); anchor)
        {
            order.point = placement_.positions[*anchor];
        }
        if (locomotor_.max_speeds[*dense] > 0.0F)
        {
            plan_move(*this, *dense, order.point);
        }
    }
}

void SimulationWorld::teleport(const std::size_t index, const Vector3& point, const std::optional<float> heading)
{
    // A placed obstacle's footprint moves with it.
    stamp_footprint(index, false);
    auto position = point;
    if (ground_mover_.follows_ground[index] != 0 && ground_.map.valid())
    {
        // Onto free ground, as a spawn.
        position = free_ground(position, placement_.headings[index], placement_.radii[index], placement_.bodies[index], ground_mover_.passable_types[index], index);
        position.z = ground_.map.surface_height(position.x, position.y);
    }
    // The occupancy holds where the unit stood; drop it rather than keep a stale place.
    occupancy_.tick.reset();
    if (auto* footprint = footprint_.find(slot_of(*this, index), &FootprintColumns::shapes))
    {
        // The footprint keeps its place relative to the entity: its offset turns with the heading.
        const float turn = heading ? *heading - placement_.headings[index] : 0.0F;
        const float offset_x = footprint->center.x - placement_.positions[index].x;
        const float offset_y = footprint->center.y - placement_.positions[index].y;
        footprint->center.x = position.x + offset_x * std::cos(turn) - offset_y * std::sin(turn);
        footprint->center.y = position.y + offset_x * std::sin(turn) + offset_y * std::cos(turn);
        footprint->heading += turn;
    }
    placement_.positions[index] = position;
    if (heading)
    {
        placement_.headings[index] = *heading;
    }
    route_.move_targets[index].reset();
    route_.paths[index].clear();
    route_.pending[index] = 0;
    locomotor_.speed_limits[index] = 0.0F;
    leave_group(*this, index);
    route_.loiter_points[index].reset();
    stamp_footprint(index, true);
}

void SimulationWorld::BehaviorSystem::disengage(const View& view, const std::size_t index)
{
    auto& orders = view.get<OrderColumns>();
    drop_target(view, index);
    orders.standing[index].engaging = false;
}

// Attack-move and guard (engine semantics; the original's are unmeasured). Both scan for hostiles
// every retarget_seconds (staggered by entity index) and engage the nearest one as an
// ordered attack, which the pursuit chases; they break off when the victim is gone or the chase
// leaves its leash, then head for the destination (attack-move) or back to the anchor (guard). An
// attack-move ends once it arrives with no engagement; a guard ends when its object dies.
void SimulationWorld::BehaviorSystem::run_standing_order(const View& view, const std::size_t index)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& orders = view.get<OrderColumns>();
    auto& order = orders.standing[index];
    if (order.kind == StandingOrder::none)
    {
        return;
    }
    if (order.entity.valid())
    {
        if (const auto followed = dense_index(view, order.entity))
        {
            order.point = placement.positions[*followed];
        }
        else
        {
            // The destination or anchor is gone: the order ends, a fight it started included.
            if (order.engaging)
            {
                disengage(view, index);
            }
            order.kind = StandingOrder::none;
            return;
        }
    }
    const auto& state = combat_state(view, index);
    const float range = state.profile->range();
    const bool guard = order.kind == StandingOrder::guard;
    const float guard_radius = std::max(range, minimum_guard_radius);
    const bool mobile = locomotor.max_speeds[index] > 0.0F;
    if (order.engaging)
    {
        const auto victim = state.ordered ? attack_victim(view, index) : std::nullopt;
        const auto& leash_center = guard ? order.point : order.engage_origin;
        const float leash = guard && state.profile->guard_chase_range > 0.0F ? state.profile->guard_chase_range : std::max(range * leash_range_factor, guard ? guard_radius : 0.0F);
        if (victim && distance_xy(placement.positions[*victim], leash_center) <= leash)
        {
            return;
        }
        disengage(view, index);
        // Resume: back on the way, or back to the anchor when out of the guard's reach.
        if (mobile && (!guard || distance_xy(placement.positions[index], order.point) > guard_radius * 0.5F))
        {
            plan_move(view, index, order.point);
        }
        return;
    }
    if (!state.profile->weapons.empty())
    {
        if (order.scan_timer > 0)
        {
            --order.scan_timer;
        }
        else
        {
            order.scan_timer = ticks_for(view, retarget_seconds);
            const auto& center = guard ? order.point : placement.positions[index];
            const float response = state.profile->attack_move_response_range;
            const float reach = guard ? guard_radius : (response > 0.0F ? response : range * acquire_range_factor);
            const auto best = choose_target(view, grid_, query_, index, center, reach,
                                            [&](const std::size_t candidate)
                                            {
                                                return engages(view, index, candidate);
                                            });
            if (best)
            {
                // Armed (the scan runs for weapons only): it has combat.
                auto& fighter = *combat_of(view, index);
                fighter.target = identity.ids[*best];
                fighter.ordered = true;
                fighter.chase_timer = 0;
                locomotor.speed_limits[index] = 0.0F;
                leave_group(view, index);
                order.engaging = true;
                order.engage_origin = placement.positions[index];
                return;
            }
        }
    }
    if (!mobile)
    {
        return;
    }
    if (guard)
    {
        // Back toward the anchor once out of its reach; a moving anchor is followed.
        if (distance_xy(placement.positions[index], order.point) > guard_radius * 0.5F && (!route.move_targets[index] || distance_xy(*route.move_targets[index], order.point) > follow_tolerance) &&
            order.scan_timer == 0)
        {
            plan_move(view, index, order.point);
        }
        return;
    }
    if (order.entity.valid() && (!route.move_targets[index] || distance_xy(*route.move_targets[index], order.point) > follow_tolerance) && order.scan_timer == 0)
    {
        plan_move(view, index, order.point);
        return;
    }
    // Arrived with nothing to fight (at the point, or within reach of the object): the attack-move
    // is done.
    if (!route.move_targets[index] && route.pending[index] == 0 && (!order.entity.valid() || distance_xy(placement.positions[index], order.point) <= std::max(follow_tolerance, range)))
    {
        order.kind = StandingOrder::none;
    }
}

// Chase an ordered target until it is in range, then hold position (combat fires). The chase
// replans every chase_seconds while the target moves.
void SimulationWorld::BehaviorSystem::pursue_ordered_target(const View& view, const std::size_t index)
{
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& ground_mover = view.get<GroundMoverColumns>();
    auto& route = view.get<RouteColumns>();
    // Only an entity with combat is ordered to attack.
    auto& state = *combat_of(view, index);
    const auto victim = attack_victim(view, index);
    if (!victim)
    {
        // Combat drops the order this tick.
        return;
    }
    if (distance_xy(placement.positions[index], placement.positions[*victim]) > state.profile->range())
    {
        if (locomotor.max_speeds[index] > 0.0F && (state.chase_timer == 0 || !route.move_targets[index]))
        {
            // Flyers keep their altitude; ground units take theirs from the surface anyway.
            plan_move(view, index,
                      {placement.positions[*victim].x, placement.positions[*victim].y, ground_mover.follows_ground[index] != 0 ? placement.positions[*victim].z : placement.positions[index].z});
            state.chase_timer = ticks_for(view, chase_seconds);
        }
    }
    else if (route.move_targets[index])
    {
        route.move_targets[index].reset();
        route.paths[index].clear();
    }
    if (state.chase_timer > 0)
    {
        --state.chase_timer;
    }
}

// Flyers never stop (their locomotor keeps them at their minimum speed), so behavior only says
// where to steer. Observed in the original (docs/compatibility-research.md, "Fighter flight"):
// fighters fly in one plane most of the time, dogfight above and below it against other flyers,
// and reverse direction through a vertical half-loop. Here:
// - against a flyer (any class with a minimum speed: fighters, bombers, mod units), a dogfight:
//   steer at the target in 3D, weaving above and below it, within a band around the plane;
// - against anything else, attack runs in the plane: inside a third of its weapon range of the
//   target a flyer holds its heading, passes, and turns back once outside;
// - without a target, to the destination (arriving within a few ticks of flight of it), then
//   circling where it arrived or idles, in the plane;
// - a goal far behind starts a half-loop, which the locomotor finishes.
// An aggressive flyer engages any target it has; otherwise only an ordered one. The numbers are
// engine choices.
void SimulationWorld::BehaviorSystem::plan_flight(const View& view, const std::size_t index, const float dt)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& steering = view.get<SteeringColumns>();
    auto& flight = view.get<ComponentPool<FlightColumns>>();
    auto& clock = view.get<Clock>();
    const auto& position = placement.positions[index];
    auto& intent = steering.intents[index];
    if (flight.get(slot_of(view, index), &FlightColumns::looping) != 0)
    {
        intent.loop = true;
        return;
    }
    const auto& state = combat_state(view, index);
    const auto plane = flight.get(slot_of(view, index), &FlightColumns::cruise_heights);
    const auto choose = [&]
    {
        if (state.ordered || combat_state(view, index).stance == Stance::aggressive)
        {
            if (const auto victim = attack_victim(view, index))
            {
                const auto& there = placement.positions[*victim];
                if (flies(view, *victim))
                {
                    // Each flyer weaves on its own phase (from its slot), so a fight spreads vertically.
                    const auto seconds = static_cast<float>(static_cast<double>(clock.tick) * clock.tick_seconds);
                    const auto phase = static_cast<float>(identity.ids[index].index % 16U) * (pi / 8.0F);
                    const auto weave = dogfight_weave * std::sin(seconds * (two_pi / dogfight_weave_seconds) + phase);
                    intent.point = Vector3{there.x, there.y, std::clamp(there.z + weave, plane - dogfight_band, plane + dogfight_band)};
                    return;
                }
                const auto pass = std::max(state.profile->range() / 3.0F, (placement.radii[index] + placement.radii[*victim]) * 2.0F);
                intent.point = Vector3{there.x, there.y, plane};
                intent.hold_heading = distance_xy(position, there) < pass;
                return;
            }
        }
        auto& target = route.move_targets[index];
        if (target)
        {
            intent.point = Vector3{target->x, target->y, plane};
            if (distance_xy(position, *target) <= travel_speed(view, index) * dt * 3.0F)
            {
                route.loiter_points[index] = *target;
                target.reset();
                locomotor.speed_limits[index] = 0.0F;
                leave_group(view, index);
            }
            return;
        }
        if (!route.loiter_points[index])
        {
            route.loiter_points[index] = position;
        }
        intent.point = Vector3{route.loiter_points[index]->x, route.loiter_points[index]->y, plane};
    };
    choose();
    if (intent.hold_heading || !intent.point || locomotor.turn_rates[index] <= 0.0F)
    {
        return;
    }
    const auto dx = intent.point->x - position.x;
    const auto dy = intent.point->y - position.y;
    const auto turn_circle = travel_speed(view, index) / locomotor.turn_rates[index];
    if (std::hypot(dx, dy) > 4.0F * turn_circle && std::fabs(wrap_angle(std::atan2(dy, dx) - placement.headings[index])) > flight_reversal_angle)
    {
        intent.loop = true;
    }
}

} // namespace eaw
