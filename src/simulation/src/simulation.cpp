// World core: entity storage, spawning, the tick, state hashing, footprints, and snapshots.

#include "simulation_internal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace eaw {

using namespace simulation_detail;

FixedStepClock::FixedStepClock(const double ticks_per_second, const std::uint32_t max_ticks_per_advance) : tick_seconds_(1.0 / ticks_per_second), max_ticks_(max_ticks_per_advance)
{
    EAW_ASSERT_MSG(ticks_per_second > 0.0 && std::isfinite(ticks_per_second) && max_ticks_per_advance != 0, "fixed-step clock needs a positive tick rate and tick budget");
}

std::uint32_t FixedStepClock::advance(const double seconds)
{
    if (std::isfinite(seconds) && seconds > 0.0)
    {
        accumulator_ += seconds;
    }
    std::uint32_t ticks = 0;
    while (accumulator_ >= tick_seconds_ && ticks < max_ticks_)
    {
        accumulator_ -= tick_seconds_;
        ++ticks;
    }
    if (accumulator_ >= tick_seconds_)
    {
        const auto excess = static_cast<std::uint64_t>(accumulator_ / tick_seconds_);
        dropped_ += excess;
        accumulator_ -= static_cast<double>(excess) * tick_seconds_;
    }
    return ticks;
}

float FixedStepClock::interpolation() const noexcept
{
    return static_cast<float>(std::clamp(accumulator_ / tick_seconds_, 0.0, 1.0));
}

double FixedStepClock::tick_seconds() const noexcept
{
    return tick_seconds_;
}

std::uint64_t FixedStepClock::dropped_ticks() const noexcept
{
    return dropped_;
}

SimulationWorld::SimulationWorld(const double tick_seconds) : clock_{.tick_seconds = tick_seconds}
{
    EAW_ASSERT_MSG(tick_seconds > 0.0 && std::isfinite(tick_seconds), "simulation tick length must be positive");
    check_entity_table();
}

void SimulationWorld::check_entity_table()
{
    // Every column is a std::vector, all of one size whatever their element type,
    // so a group is exactly its columns, and the groups, declared one after
    // another, span exactly the bytes from identity_ to the end of steering_.
    constexpr auto column_bytes = sizeof(std::vector<std::uint8_t>);
    std::size_t group_bytes = 0;
    for_each_group(
        [&](auto& group)
        {
            std::size_t columns = 0;
            group.for_each_column(
                [&](auto& column)
                {
                    static_assert(sizeof(column) == column_bytes, "an entity column must be a std::vector");
                    ++columns;
                });
            EAW_ASSERT_MSG(columns * column_bytes == sizeof(group), "a component group's for_each_column must list every column");
            group_bytes += sizeof(group);
        });
    const auto first = reinterpret_cast<std::uintptr_t>(&identity_);
    const auto end = reinterpret_cast<std::uintptr_t>(&steering_) + sizeof(steering_);
    EAW_ASSERT_MSG(end - first == group_bytes, "for_each_group must list every component group of the entity table");
}

Result<RecipeId> SimulationWorld::add_recipe(EntityRecipe recipe)
{
    if (!std::isfinite(recipe.max_speed) || recipe.max_speed < 0.0F)
    {
        return make_error("entity recipe '" + recipe.type_name + "' has a non-finite or negative speed");
    }
    if (!valid_amount(recipe.radius) || !valid_amount(recipe.turn_rate) || !valid_amount(recipe.formation_spacing) || !valid_amount(recipe.min_speed))
    {
        return make_error("entity recipe '" + recipe.type_name + "' has a negative or non-finite radius, turn rate, minimum speed, or formation spacing");
    }
    if (const auto& movement = recipe.movement; !valid_amount(movement.acceleration) || !valid_amount(movement.deceleration) || !valid_amount(movement.turn_speed_fraction) ||
                                                movement.turn_speed_fraction > 1.0F || !valid_amount(movement.stopped_turn_rate) || !valid_amount(movement.turn_in_place_angle) ||
                                                !valid_amount(movement.turn_in_place_rate) || !std::ranges::all_of(movement.turn_begin_seconds, valid_amount) ||
                                                !std::ranges::all_of(movement.turn_loop_seconds, valid_amount) || !std::ranges::all_of(movement.turn_end_seconds, valid_amount) ||
                                                !std::ranges::all_of(movement.turn_half_seconds, valid_amount) || !std::ranges::all_of(movement.turn_quarter_seconds, valid_amount) ||
                                                !valid_amount(movement.walk_start_seconds) || !valid_amount(movement.walk_stride_seconds) || !valid_amount(movement.walk_stop_seconds) ||
                                                !valid_amount(movement.walk_raise_seconds) || !valid_amount(movement.walk_lower_seconds))
    {
        return make_error("entity recipe '" + recipe.type_name + "' has a negative or non-finite movement value, or a turn speed fraction above 1");
    }
    if (const auto& movement = recipe.movement;
        movement.walk_stop_parts > max_walk_stop_parts ||
        (movement.walk_stop_parts > 0 && (!(movement.walk_start_seconds > 0.0F) || !(movement.walk_stride_seconds > 0.0F) || !(movement.walk_stop_seconds > 0.0F))))
    {
        return make_error("entity recipe '" + recipe.type_name + "' walks as clips without the clips' times, or with more than " + std::to_string(max_walk_stop_parts) + " stops");
    }
    if (!valid_profile(recipe.combat))
    {
        return make_error("entity recipe '" + recipe.type_name + "' has a negative or non-finite combat value");
    }
    for (std::size_t index = 0; index < recipes_.list.size(); ++index)
    {
        if (recipes_.list[index] == recipe)
        {
            return static_cast<RecipeId>(index);
        }
    }
    if (recipes_.list.size() >= std::numeric_limits<RecipeId>::max())
    {
        return make_error("simulation recipe storage is full");
    }
    recipes_.list.push_back(std::move(recipe));
    return static_cast<RecipeId>(recipes_.list.size() - 1U);
}

const EntityRecipe& SimulationWorld::recipe(const RecipeId id) const
{
    EAW_ASSERT_MSG(id < recipes_.list.size(), "a recipe id must come from add_recipe");
    return recipes_.list[id];
}

std::size_t SimulationWorld::recipe_count() const noexcept
{
    return recipes_.list.size();
}

Result<EntityId> SimulationWorld::spawn(const EntitySpawn& spawn, const EntityId squad)
{
    if (spawn.recipe >= recipes_.list.size())
    {
        return make_error("entity spawn names recipe " + std::to_string(spawn.recipe) + ", which is not registered");
    }
    const auto& recipe = recipes_.list[spawn.recipe];
    if (!is_finite(spawn.position) || !std::isfinite(spawn.heading))
    {
        return make_error("entity spawn '" + recipe.type_name + "' has a non-finite position or heading");
    }
    if (!valid_amount(spawn.formation_team.spacing) || !valid_amount(spawn.formation_team.raggedness))
    {
        return make_error("entity spawn '" + recipe.type_name + "' has a negative or non-finite formation team spacing");
    }
    if (spawn.stance != Stance::aggressive && spawn.stance != Stance::hold_position && spawn.stance != Stance::hold_fire)
    {
        return make_error("entity spawn '" + recipe.type_name + "' has an unknown stance");
    }
    if (const auto& footprint = spawn.footprint;
        footprint && (!is_finite(footprint->center) || !valid_amount(footprint->half_length) || !valid_amount(footprint->half_width) || !std::isfinite(footprint->heading)))
    {
        return make_error("entity spawn '" + recipe.type_name + "' has a negative or non-finite footprint");
    }
    if (spawn.squad.has_value() != squad.valid())
    {
        return make_error("entity spawn '" + recipe.type_name + "' names a squad but was given no team, or a team without naming a squad");
    }
    if (squad.valid())
    {
        const auto team = dense_index(*this, squad);
        if (!team || !squads_.find(squad.index))
        {
            return make_error("entity spawn '" + recipe.type_name + "' joins a squad whose team is gone or is not a team");
        }
    }
    if (identity_.ids.size() >= no_dense)
    {
        return make_error("simulation entity storage is full");
    }
    std::uint32_t slot{};
    if (!slots_.free.empty())
    {
        slot = slots_.free.back();
        slots_.free.pop_back();
    }
    else
    {
        slot = static_cast<std::uint32_t>(slots_.generations.size());
        slots_.generations.push_back(1);
        slots_.dense.push_back(no_dense);
    }
    const EntityId id{slot, slots_.generations[slot]};
    slots_.dense[slot] = static_cast<std::uint32_t>(identity_.ids.size());
    if (recipe.movement.turn_begin_seconds[0] > 0.0F)
    {
        turn_clips_.add(slot);
    }
    if (recipe.movement.walk_stop_parts > 0 || recipe.movement.walk_raise_seconds > 0.0F)
    {
        walk_clips_.add(slot);
    }
    // A minimum speed above 0 makes a flyer (FlightColumns::min_speeds).
    const auto min_speed = std::min(recipe.min_speed, recipe.max_speed);
    if (min_speed > 0.0F)
    {
        const auto row = flight_.add(slot);
        flight_.columns.min_speeds[row] = min_speed;
        flight_.columns.cruise_heights[row] = spawn.position.z;
    }
    if (spawn.footprint)
    {
        footprint_.columns.shapes[footprint_.add(slot)] = *spawn.footprint;
    }
    if (recipe.crush != CrushProfile{})
    {
        crush_.columns.profiles[crush_.add(slot)] = recipe.crush;
    }
    if (spawn.formation_team != FormationTeam{})
    {
        formation_.columns.teams[formation_.add(slot)] = spawn.formation_team;
    }
    if (recipe.team)
    {
        squads_.columns.states[squads_.add(slot)].arrangement = *recipe.team;
    }
    identity_.ids.push_back(id);
    placement_.positions.push_back(spawn.position);
    placement_.headings.push_back(wrap_angle(spawn.heading));
    locomotor_.max_speeds.push_back(recipe.max_speed);
    route_.move_targets.emplace_back();
    identity_.owners.push_back(spawn.owner);
    identity_.visuals.push_back(spawn.visual);
    identity_.selectable.push_back(spawn.selectable ? 1U : 0U);
    identity_.recipes.push_back(spawn.recipe);
    ground_mover_.follows_ground.push_back(recipe.follows_ground ? 1U : 0U);
    ground_mover_.passable_types.push_back(recipe.passable_types);
    route_.paths.emplace_back();
    if (const auto& profile = recipe.combat; profile.health > 0.0F || profile.shield > 0.0F || !profile.weapons.empty() || !profile.hardpoints.empty())
    {
        auto& state = combat_.columns.states[combat_.add(slot)];
        state = {.profile = &profile, .health = profile.health, .shield = profile.shield, .stance = spawn.stance, .aims_with_body = recipe.aims_with_body};
        // The first target search at a phase from the slot, so the units of a battle do not all
        // search in its first tick.
        state.retarget_timer = slot % std::max(1U, ticks_for(*this, retarget_seconds));
        state.weapons.resize(profile.weapons.size());
        for (const auto& hardpoint : profile.hardpoints)
        {
            state.hardpoint_health.push_back(hardpoint.health);
        }
    }
    placement_.radii.push_back(recipe.radius);
    locomotor_.turn_rates.push_back(recipe.turn_rate);
    locomotor_.movements.push_back(&recipe.movement);
    locomotor_.drive_speeds.push_back(0.0F);
    route_.loiter_points.emplace_back();
    route_.remaining.push_back(0.0F);
    route_.detours.emplace_back();
    placement_.pitches.push_back(0.0F);
    motion_.speeds.push_back(0.0F);
    motion_.starts.push_back(spawn.position);
    orders_.command_serials.push_back(0);
    orders_.standing.push_back({.scan_timer = slot % std::max(1U, ticks_for(*this, retarget_seconds))});
    locomotor_.suspended.push_back(0);
    locomotor_.arriving.push_back(0);
    steering_.intents.emplace_back();
    locomotor_.speed_limits.push_back(0.0F);
    route_.pending.push_back(0);
    for_each_column(
        [&](const auto& column)
        {
            EAW_ASSERT_MSG(column.size() == identity_.ids.size(), "every entity column needs one entry per entity (a column missing from spawn or for_each_column)");
        });
    stamp_footprint(identity_.ids.size() - 1U, true);
    if (recipe.follows_ground && ground_.map.valid())
    {
        placement_.positions.back().z = ground_.map.surface_height(spawn.position.x, spawn.position.y);
    }
    if (squad.valid())
    {
        join_squad(identity_.ids.size() - 1U, squad, spawn.formation_team.raggedness);
    }
    return id;
}

void SimulationWorld::submit(SimCommand command)
{
    pending_.push_back(std::move(command));
}

void SimulationWorld::step()
{
    // The schedule's systems in order (simulation_schedule.cpp). Each tick layer is timed
    // (Stats::phase_seconds); the times never reach the simulation's state.
    auto mark = std::chrono::steady_clock::now();
    const auto timed = [&](const TickPhase phase)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto seconds = std::chrono::duration<double>(now - mark).count();
        const auto layer = static_cast<std::size_t>(phase);
        phase_seconds_[layer] += seconds;
        slowest_phase_seconds_[layer] = std::max(slowest_phase_seconds_[layer], seconds);
        mark = now;
    };
    for (const auto& system : schedule_)
    {
        (this->*system.run)();
        timed(system.phase);
    }
}

void SimulationWorld::run_commands()
{
    auto commands = std::move(pending_);
    pending_.clear();
    for (const auto& command : commands)
    {
        apply(command);
    }
    motion_.starts = placement_.positions;
    events_.deaths.clear();
    events_.impacts.clear();
    events_.shots.clear();
    events_.hardpoint_deaths.clear();
    events_.spins.clear();
}

void SimulationWorld::PublishSystem::run(const View& view)
{
    auto& clock = view.get<Clock>();
    ++clock.tick;
    publish(view);
}

std::string_view SimulationWorld::type_name(const EntityId id) const noexcept
{
    const auto dense = dense_index(*this, id);
    return dense ? std::string_view(recipes_.list[identity_.recipes[*dense]].type_name) : std::string_view{};
}

bool SimulationWorld::alive(const EntityId id) const noexcept
{
    return dense_index(*this, id).has_value();
}

const SimSnapshot& SimulationWorld::snapshot() const noexcept
{
    return publish_.snapshot;
}

SimulationWorld::Stats SimulationWorld::stats() const noexcept
{
    return {.tick = clock_.tick,
            .entities = identity_.ids.size(),
            .commands_applied = commands_applied_,
            .stale_references = stale_references_,
            .blocked_moves = movement_.blocked_moves,
            .path_searches = navigation_.path_searches,
            .partial_paths = navigation_.partial_paths,
            .path_expansions = navigation_.path_expansions,
            .queued_paths = path_queue_.requests.size(),
            .shared_routes = navigation_.shared_routes,
            .shots_fired = combat_counters_.shots_fired,
            .hits = combat_counters_.hits,
            .kills = combat_counters_.kills,
            .hardpoints_destroyed = combat_counters_.hardpoints_destroyed,
            .crushed = combat_counters_.crushed,
            .projectiles = projectiles_.flying.size(),
            .separated_pairs = contact_.separated_pairs,
            .waiting_members = navigation_.waiting_members,
            .released_members = navigation_.released_members,
            .rejected_spawns = rejected_spawns_,
            .phase_seconds = phase_seconds_,
            .slowest_phase_seconds = slowest_phase_seconds_};
}

std::uint64_t SimulationWorld::state_hash() const noexcept
{
    StateHasher hasher;
    hash_state(hasher);
    return hasher.total;
}

StateHashes SimulationWorld::state_hashes() const noexcept
{
    StateHasher hasher;
    hash_state(hasher);
    return hasher.parts;
}

void SimulationWorld::hash_state(StateHasher& hasher) const noexcept
{
    // The tick goes into the total only: each subject hash says whether that part of the state agrees.
    hasher.total = hash_value(hasher.total, clock_.tick);
    // Component by component, each in dense order (the spawn order); a pooled component only for
    // its members, with their dense index. Visual keys are presentation data the simulation never
    // reads (a mesh index on maps), so they stay out: the same battle hashes alike however its scene
    // was built.
    const auto count = identity_.ids.size();
    hasher.subject = StateSubject::bodies;
    // The recipes once each (append-only: every recipe ever added); entities hash their recipe id.
    hasher.add(recipes_.list.size());
    for (const auto& recipe : recipes_.list)
    {
        hasher.add_bytes(recipe.type_name.data(), recipe.type_name.size());
        const auto& movement = recipe.movement;
        hasher.add(movement.acceleration);
        hasher.add(movement.deceleration);
        hasher.add(movement.turn_speed_fraction);
        hasher.add(movement.stopped_turn_rate);
        hasher.add(movement.turn_in_place_angle);
        hasher.add(movement.turn_in_place_rate);
        for (std::size_t side = 0; side < 2; ++side)
        {
            hasher.add(movement.turn_begin_seconds[side]);
            hasher.add(movement.turn_loop_seconds[side]);
            hasher.add(movement.turn_end_seconds[side]);
            hasher.add(movement.turn_half_seconds[side]);
            hasher.add(movement.turn_quarter_seconds[side]);
        }
        hasher.add(movement.walk_start_seconds);
        hasher.add(movement.walk_stride_seconds);
        hasher.add(movement.walk_stop_seconds);
        hasher.add(movement.walk_stop_parts);
        hasher.add(movement.walk_raise_seconds);
        hasher.add(movement.walk_lower_seconds);
        hasher.add(recipe.formation_order);
        hasher.add(recipe.formation_spacing);
        hasher.add(recipe.formation_solo);
    }
    hasher.add(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        hasher.add(identity_.ids[index]);
        hasher.add(identity_.recipes[index]);
        hasher.add(identity_.owners[index]);
        hasher.add(identity_.selectable[index]);
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        hasher.add(placement_.positions[index]);
        hasher.add(placement_.headings[index]);
        hasher.add(placement_.pitches[index]);
        hasher.add(placement_.radii[index]);
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        hasher.add(locomotor_.max_speeds[index]);
        hasher.add(locomotor_.turn_rates[index]);
        hasher.add(locomotor_.drive_speeds[index]);
        hasher.add(locomotor_.speed_limits[index]);
        hasher.add(locomotor_.suspended[index]);
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        hasher.add(ground_mover_.follows_ground[index]);
        hasher.add(ground_mover_.passable_types[index]);
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto* crush = crush_.find(slot_of(*this, index), &CrushColumns::profiles))
        {
            hasher.add(index);
            hasher.add(static_cast<std::uint8_t>(crush->crusher));
            hasher.add(static_cast<std::uint8_t>((crush->squashable ? 1U : 0U) | (crush->squashable_by_supercrusher ? 2U : 0U)));
        }
    }
    hasher.subject = StateSubject::routes;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& target = route_.move_targets[index];
        hasher.add(target.has_value());
        if (target)
        {
            hasher.add(*target);
        }
        hasher.add(route_.paths[index].size());
        for (const auto& waypoint : route_.paths[index])
        {
            hasher.add(waypoint);
        }
        hasher.add(route_.pending[index]);
        hasher.add(route_.loiter_points[index].has_value());
        if (route_.loiter_points[index])
        {
            hasher.add(*route_.loiter_points[index]);
        }
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto row = group_.find(slot_of(*this, index)))
        {
            hasher.add(index);
            hasher.add(group_.columns.ids[*row]);
            hasher.add(group_.columns.best_remaining[*row]);
            hasher.add(group_.columns.stall_ticks[*row]);
        }
    }
    hasher.subject = StateSubject::combat;
    for (std::size_t index = 0; index < count; ++index)
    {
        // Field by field: the combat records have padding, whose bytes are unspecified.
        const auto* member = combat_.find(slot_of(*this, index), &CombatColumns::states);
        if (member == nullptr)
        {
            continue;
        }
        const auto& combat = *member;
        hasher.add(index);
        hasher.add(combat.health);
        hasher.add(combat.shield);
        hasher.add(combat.target);
        hasher.add(combat.ordered);
        for (const auto health : combat.hardpoint_health)
        {
            hasher.add(health);
        }
        for (const auto& weapon : combat.weapons)
        {
            hasher.add(weapon.cooldown);
            hasher.add(weapon.pulses_left);
            hasher.add(weapon.pulse_timer);
            hasher.add(weapon.target);
            hasher.add(weapon.shots);
        }
        hasher.add(combat.retarget_timer);
        hasher.add(combat.target_ticks);
        hasher.add(combat.spin_ticks);
        hasher.add(combat.turret_yaw);
        hasher.add(combat.chase_timer);
        hasher.add(combat.invulnerable);
        hasher.add(combat.cannot_be_killed);
        hasher.add(combat.ordered_squad);
        hasher.add(combat.stance);
        hasher.add(combat.aims_with_body);
    }
    hasher.subject = StateSubject::clips;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto* plan = turn_clips_.find(slot_of(*this, index), &TurnClipColumns::plans))
        {
            hasher.add(index);
            hasher.add(static_cast<std::uint8_t>(plan->active ? 1U : 0U));
            hasher.add(plan->elapsed);
            hasher.add(plan->start);
            hasher.add(plan->angle);
            hasher.add(plan->way);
            hasher.add(plan->scale);
            hasher.add(plan->loops);
            hasher.add(plan->halves);
            hasher.add(plan->quarters);
        }
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto* walk = walk_clips_.find(slot_of(*this, index), &WalkClipColumns::plans))
        {
            hasher.add(index);
            hasher.add(walk->clip);
            hasher.add(walk->part);
            hasher.add(walk->elapsed);
            hasher.add(walk->phase);
            hasher.add(walk->from_speed);
            hasher.add(walk->scale);
            hasher.add(static_cast<std::uint8_t>(walk->ends_move ? 1U : 0U));
            hasher.add(walk->destination);
            hasher.add(static_cast<std::uint8_t>(walk->turn_back ? 1U : 0U));
            hasher.add(static_cast<std::uint8_t>(walk->new_way ? 1U : 0U));
        }
    }
    hasher.subject = StateSubject::flight;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto row = flight_.find(slot_of(*this, index)))
        {
            hasher.add(index);
            hasher.add(flight_.columns.min_speeds[*row]);
            hasher.add(flight_.columns.cruise_heights[*row]);
            hasher.add(flight_.columns.looping[*row]);
            hasher.add(flight_.columns.rolls[*row]);
        }
    }
    hasher.subject = StateSubject::formations;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto* team = formation_.find(slot_of(*this, index), &FormationColumns::teams))
        {
            hasher.add(index);
            hasher.add(team->id);
            hasher.add(static_cast<std::uint8_t>(team->arrangement));
            hasher.add(team->spacing);
            hasher.add(team->raggedness);
        }
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (const auto* squad = squads_.find(slot_of(*this, index), &SquadColumns::states))
        {
            hasher.add(index);
            hasher.add(squad->members.size());
            for (const auto member : squad->members)
            {
                hasher.add(member);
            }
            hasher.add(static_cast<std::uint8_t>(squad->arrangement));
            hasher.add(squad->extent);
            hasher.add(squad->target);
            hasher.add(squad->waiting);
            hasher.add(squad->wait_ticks);
            hasher.add(squad->chase_timer);
        }
        if (const auto* member = squad_members_.find(slot_of(*this, index), &SquadMemberColumns::states))
        {
            hasher.add(index);
            hasher.add(member->squad);
            hasher.add(member->ahead);
            hasher.add(member->aside);
            hasher.add(member->raggedness);
            hasher.add(member->slot.has_value());
            if (member->slot)
            {
                hasher.add(*member->slot);
            }
        }
    }
    hasher.subject = StateSubject::orders;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& order = orders_.standing[index];
        hasher.add(order.kind);
        hasher.add(order.point);
        hasher.add(order.entity);
        hasher.add(order.engaging);
        hasher.add(order.engage_origin);
        hasher.add(order.scan_timer);
    }
    hasher.subject = StateSubject::routes;
    for (const auto& request : path_queue_.requests)
    {
        for (std::size_t member = 0; member < request.members.size(); ++member)
        {
            hasher.add(request.members[member]);
            hasher.add(request.goals[member]);
            hasher.add(request.serials[member]);
        }
        hasher.add(request.group_target);
    }
    hasher.subject = StateSubject::projectiles;
    for (const auto& projectile : projectiles_.flying)
    {
        hasher.add(projectile.position);
        hasher.add(projectile.aim);
        hasher.add(projectile.target);
        hasher.add(projectile.target_hardpoint);
        hasher.add(projectile.owner);
        hasher.add(projectile.damage);
        hasher.add(projectile.blast.damage);
        hasher.add(projectile.blast.range);
        hasher.add(projectile.lifetime_ticks);
        hasher.add(projectile.flown);
        hasher.add(projectile.stray);
        hasher.add(projectile.misses);
    }
    hasher.subject = StateSubject::random;
    hasher.add(combat_random_.stream.state());
}

const SimulationWorld::CombatState& SimulationWorld::no_combat() noexcept
{
    static const CombatProfile none_profile{};
    static const CombatState none{.profile = &none_profile};
    return none;
}

const CrushProfile& SimulationWorld::crush_profile_in(const ComponentPool<CrushColumns>& pool, const std::uint32_t slot) noexcept
{
    static constexpr CrushProfile none{};
    const auto* profile = pool.find(slot, &CrushColumns::profiles);
    return profile != nullptr ? *profile : none;
}

void SimulationWorld::run_publish()
{
    publish_.run(view_of<PublishSystem::View>());
}

void SimulationWorld::run_combat()
{
    combat_system_.run(view_of<CombatSystem::View>(), static_cast<float>(clock_.tick_seconds));
}

void SimulationWorld::run_behavior()
{
    behavior_.run(view_of<BehaviorSystem::View>(), static_cast<float>(clock_.tick_seconds));
}

void SimulationWorld::run_navigation()
{
    navigation_.run(view_of<NavigationSystem::View>());
}

void SimulationWorld::run_movement()
{
    movement_.run(view_of<MovementSystem::View>(), static_cast<float>(clock_.tick_seconds));
}

void SimulationWorld::run_contact()
{
    contact_.run(view_of<ContactSystem::View>(), static_cast<float>(clock_.tick_seconds));
}

void SimulationWorld::remove_entities(std::vector<std::size_t> dense)
{
    std::vector<std::uint32_t> reshaped;
    expand_squad_removals(dense, reshaped);
    if (dense.empty())
    {
        return;
    }
    for (const auto index : dense)
    {
        stamp_footprint(index, false);
        const auto slot = identity_.ids[index].index;
        slots_.dense[slot] = no_dense;
        turn_clips_.remove(slot);
        walk_clips_.remove(slot);
        flight_.remove(slot);
        footprint_.remove(slot);
        crush_.remove(slot);
        formation_.remove(slot);
        group_.remove(slot);
        combat_.remove(slot);
        squads_.remove(slot);
        squad_members_.remove(slot);
        // A slot whose generation would wrap is retired instead of reused.
        if (slots_.generations[slot] != std::numeric_limits<std::uint32_t>::max())
        {
            ++slots_.generations[slot];
            slots_.free.push_back(slot);
        }
    }
    // One stable pass over every column: the survivors keep their order.
    for_each_column(
        [&](auto& column)
        {
            std::size_t kept = dense.front();
            std::size_t next = 0;
            for (auto index = dense.front(); index < column.size(); ++index)
            {
                if (next < dense.size() && dense[next] == index)
                {
                    ++next;
                    continue;
                }
                column[kept++] = std::move(column[index]);
            }
            column.erase(column.begin() + static_cast<std::ptrdiff_t>(kept), column.end());
        });
    for (auto index = dense.front(); index < identity_.ids.size(); ++index)
    {
        slots_.dense[identity_.ids[index].index] = static_cast<std::uint32_t>(index);
    }
    // The rings of teams that lost members close up.
    for (const auto squad_slot : reshaped)
    {
        reshape_squad(squad_slot);
    }
}

std::vector<Vector3> SimulationWorld::route(const EntityId id) const
{
    std::vector<Vector3> points;
    const auto dense = dense_index(*this, id);
    if (!dense || !route_.move_targets[*dense])
    {
        return points;
    }
    if (*dense < route_.detours.size() && route_.detours[*dense])
    {
        points.push_back(*route_.detours[*dense]);
    }
    points.insert(points.end(), route_.paths[*dense].rbegin(), route_.paths[*dense].rend());
    points.push_back(*route_.move_targets[*dense]);
    return points;
}

void SimulationWorld::set_ground(GroundMap ground)
{
    ground_.map = std::move(ground);
    ground_.own_passability = ground_.map.passability;
    ground_.footprint_cover.assign(ground_.own_passability.size(), 0);
    std::ranges::fill(footprint_.columns.stamped, std::uint8_t{0});
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        stamp_footprint(index, true);
    }
    navigation_.ground_changed();
}

// Cells whose sample point lies inside the footprint rectangle take its passability type while any
// footprint covers them; the last footprint removed restores the ground's own value. The cached
// region labels are dropped, so paths see the change.
void SimulationWorld::stamp_footprint(const std::size_t index, const bool add)
{
    const auto* footprint = footprint_.find(slot_of(*this, index), &FootprintColumns::shapes);
    if (footprint == nullptr || !ground_.map.valid() || ground_.map.passability.size() != ground_.map.heights.size() || ground_.footprint_cover.size() != ground_.map.passability.size() ||
        (footprint_.get(slot_of(*this, index), &FootprintColumns::stamped) != 0) == add)
    {
        return;
    }
    const auto cos_heading = std::cos(footprint->heading);
    const auto sin_heading = std::sin(footprint->heading);
    const auto reach = std::hypot(footprint->half_length, footprint->half_width);
    const auto spacing = ground_.map.spacing;
    const auto first_column = std::max(0L, static_cast<long>(std::floor((footprint->center.x - reach) / spacing)));
    const auto last_column = std::min(static_cast<long>(ground_.map.width) - 1, static_cast<long>(std::ceil((footprint->center.x + reach) / spacing)));
    const auto first_row = std::max(0L, static_cast<long>(std::floor((footprint->center.y - reach) / spacing)));
    const auto last_row = std::min(static_cast<long>(ground_.map.height) - 1, static_cast<long>(std::ceil((footprint->center.y + reach) / spacing)));
    for (auto row = first_row; row <= last_row; ++row)
    {
        for (auto column = first_column; column <= last_column; ++column)
        {
            const auto dx = static_cast<float>(column) * spacing - footprint->center.x;
            const auto dy = static_cast<float>(row) * spacing - footprint->center.y;
            const auto along = dx * cos_heading + dy * sin_heading;
            const auto across = -dx * sin_heading + dy * cos_heading;
            if (std::fabs(along) > footprint->half_length || std::fabs(across) > footprint->half_width)
            {
                continue;
            }
            const auto cell = static_cast<std::size_t>(row) * ground_.map.width + static_cast<std::size_t>(column);
            if (add)
            {
                ++ground_.footprint_cover[cell];
                ground_.map.passability[cell] = footprint->passability;
            }
            else if (ground_.footprint_cover[cell] > 0 && --ground_.footprint_cover[cell] == 0)
            {
                ground_.map.passability[cell] = ground_.own_passability[cell];
            }
        }
    }
    footprint_.get(slot_of(*this, index), &FootprintColumns::stamped) = add ? 1U : 0U;
    navigation_.ground_changed();
}

const GroundMap& SimulationWorld::ground() const noexcept
{
    return ground_.map;
}

void SimulationWorld::PublishSystem::publish(const View& view)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& motion = view.get<MotionColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& ground_mover = view.get<GroundMoverColumns>();
    auto& route = view.get<RouteColumns>();
    auto& orders = view.get<OrderColumns>();
    auto& turn_clips = view.get<ComponentPool<TurnClipColumns>>();
    auto& walk_clips = view.get<ComponentPool<WalkClipColumns>>();
    auto& flight = view.get<ComponentPool<FlightColumns>>();
    auto& squad_members = view.get<ComponentPool<SquadMemberColumns>>();
    auto& projectiles = view.get<ProjectileState>();
    auto& events = view.get<TickEvents>();
    auto& clock = view.get<Clock>();
    snapshot.tick = clock.tick;
    snapshot.entities.clear();
    snapshot.entities.reserve(identity.ids.size());
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        const auto& state = combat_state(view, index);
        std::uint32_t destroyable = 0;
        std::uint32_t destroyed = 0;
        for (std::size_t hardpoint = 0; hardpoint < state.profile->hardpoints.size(); ++hardpoint)
        {
            if (state.profile->hardpoints[hardpoint].health > 0.0F)
            {
                ++destroyable;
                destroyed += state.hardpoint_health[hardpoint] <= 0.0F ? 1U : 0U;
            }
        }
        const auto* member = squad_members.find(slot_of(view, index), &SquadMemberColumns::states);
        // A non-flyer reports no manoeuvres.
        const auto* yaw_rate = flight.find(slot_of(view, index), &FlightColumns::yaw_rates);
        const auto* roll = flight.find(slot_of(view, index), &FlightColumns::rolls);
        snapshot.entities.push_back({identity.ids[index],
                                     placement.positions[index],
                                     placement.headings[index],
                                     identity.visuals[index],
                                     identity.owners[index],
                                     route.move_targets[index].has_value(),
                                     identity.selectable[index] != 0,
                                     locomotor.max_speeds[index],
                                     ground_mover.follows_ground[index] != 0,
                                     state.health,
                                     state.profile->health,
                                     state.shield,
                                     state.profile->shield,
                                     state.target,
                                     member != nullptr ? member->squad : EntityId{},
                                     placement.radii[index],
                                     destroyable,
                                     destroyed,
                                     placement.pitches[index],
                                     (yaw_rate ? *yaw_rate : 0.0F),
                                     motion.speeds[index],
                                     (roll ? *roll : 0.0F),
                                     state.turret_yaw,
                                     combat_state(view, index).last_shot_tick,
                                     orders.command_serials[index],
                                     orders.standing[index].kind,
                                     locomotor.arriving[index] != 0});
        snapshot.entities.back().turns_as_clips = locomotor.movements[index]->turn_begin_seconds[0] > 0.0F && locomotor.movements[index]->stopped_turn_rate > 0.0F;
        if (turning_as_clips(view, index))
        {
            const auto& plan = turn_clips.get(slot_of(view, index), &TurnClipColumns::plans);
            const auto point = turn_point(view, index, plan.elapsed);
            auto& entity = snapshot.entities.back();
            entity.turn_clip = point.clip;
            entity.turn_left = plan.way > 0.0F;
            entity.turn_progress = point.progress;
        }
        if (const auto& movement = *locomotor.movements[index]; movement.walk_stop_parts > 0)
        {
            const auto& walk = walk_clips.get(slot_of(view, index), &WalkClipColumns::plans);
            auto& entity = snapshot.entities.back();
            entity.walk_clip = walk.clip;
            entity.walk_part = walk.part;
            entity.walk_progress = walk.clip == WalkClip::starting   ? std::min(walk.elapsed / movement.walk_start_seconds, 1.0F)
                                   : walk.clip == WalkClip::moving   ? walk.phase
                                   : walk.clip == WalkClip::stopping ? std::min(walk.elapsed / movement.walk_stop_seconds, 1.0F)
                                   : walk.clip == WalkClip::raising  ? std::min(walk.elapsed / std::max(movement.walk_raise_seconds, 1.0e-6F), 1.0F)
                                   : walk.clip == WalkClip::lowering ? std::min(walk.elapsed / std::max(movement.walk_lower_seconds, 1.0e-6F), 1.0F)
                                   : walk.clip == WalkClip::raised   ? 1.0F
                                                                     : 0.0F;
        }
    }
    snapshot.deaths = events.deaths;
    snapshot.impacts = events.impacts;
    snapshot.shots = events.shots;
    snapshot.hardpoint_deaths = events.hardpoint_deaths;
    snapshot.spins = events.spins;
    snapshot.projectiles.clear();
    snapshot.projectiles.reserve(projectiles.flying.size());
    for (const auto& projectile : projectiles.flying)
    {
        const Vector3 to_aim{projectile.aim.x - projectile.position.x, projectile.aim.y - projectile.position.y, projectile.aim.z - projectile.position.z};
        snapshot.projectiles.push_back({projectile.position, projectile.direction, projectile.owner, projectile.visual, projectile.speed, projectile.serial, projectile.flown,
                                        std::sqrt(to_aim.x * to_aim.x + to_aim.y * to_aim.y + to_aim.z * to_aim.z)});
    }
}

std::vector<EntitySnapshot> interpolate_snapshots(const SimSnapshot& previous, const SimSnapshot& current, const float alpha)
{
    const auto t = std::clamp(std::isfinite(alpha) ? alpha : 1.0F, 0.0F, 1.0F);
    std::unordered_map<std::uint64_t, const EntitySnapshot*> before;
    before.reserve(previous.entities.size());
    for (const auto& entity : previous.entities)
    {
        before.emplace((static_cast<std::uint64_t>(entity.id.generation) << 32U) | entity.id.index, &entity);
    }
    std::vector<EntitySnapshot> result;
    result.reserve(current.entities.size());
    for (const auto& entity : current.entities)
    {
        auto blended = entity;
        if (const auto found = before.find((static_cast<std::uint64_t>(entity.id.generation) << 32U) | entity.id.index); found != before.end())
        {
            const auto& old = *found->second;
            blended.position = {old.position.x + (entity.position.x - old.position.x) * t, old.position.y + (entity.position.y - old.position.y) * t,
                                old.position.z + (entity.position.z - old.position.z) * t};
            blended.heading = old.heading + wrap_angle(entity.heading - old.heading) * t;
            blended.pitch = old.pitch + (entity.pitch - old.pitch) * t;
            blended.yaw_rate = old.yaw_rate + (entity.yaw_rate - old.yaw_rate) * t;
            blended.speed = old.speed + (entity.speed - old.speed) * t;
            blended.roll = old.roll + wrap_angle(entity.roll - old.roll) * t;
            // A turn played as clips goes on through its clip, like a walk below.
            if (old.turn_clip == entity.turn_clip && old.turn_left == entity.turn_left && entity.turn_clip != TurnClip::none)
            {
                auto advance = entity.turn_progress - old.turn_progress;
                // A cycle followed by another of its kind starts again.
                if (advance < 0.0F)
                {
                    advance += 1.0F;
                }
                blended.turn_progress = old.turn_progress + advance * t;
                if (blended.turn_progress >= 1.0F)
                {
                    blended.turn_progress -= 1.0F;
                }
            }
            // A walk played as clips goes on through its clip; a new clip shows from the tick it begins.
            if (old.walk_clip == entity.walk_clip && old.walk_part == entity.walk_part)
            {
                auto advance = entity.walk_progress - old.walk_progress;
                if (entity.walk_clip == WalkClip::moving && advance < 0.0F)
                {
                    advance += 1.0F;
                }
                blended.walk_progress = old.walk_progress + advance * t;
                if (blended.walk_progress >= 1.0F && entity.walk_clip == WalkClip::moving)
                {
                    blended.walk_progress -= 1.0F;
                }
            }
        }
        result.push_back(blended);
    }
    return result;
}

} // namespace eaw
