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

SimulationWorld::SimulationWorld(const double tick_seconds) : tick_seconds_(tick_seconds)
{
    EAW_ASSERT_MSG(tick_seconds > 0.0 && std::isfinite(tick_seconds), "simulation tick length must be positive");
    check_entity_table();
}

void SimulationWorld::check_entity_table()
{
    // Every column is a std::vector, all of one size whatever their element type,
    // so a group is exactly its columns, and the groups, declared one after
    // another, span exactly the bytes from identity_ to the end of group_.
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
    const auto end = reinterpret_cast<std::uintptr_t>(&group_) + sizeof(group_);
    EAW_ASSERT_MSG(end - first == group_bytes, "for_each_group must list every component group of the entity table");
}

Result<EntityId> SimulationWorld::spawn(const EntitySpawn& spawn)
{
    if (!is_finite(spawn.position) || !std::isfinite(spawn.heading) || !std::isfinite(spawn.max_speed) || spawn.max_speed < 0.0F)
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a non-finite position, heading, or speed");
    }
    if (!valid_amount(spawn.radius) || !valid_amount(spawn.turn_rate) || !valid_amount(spawn.formation_spacing) || !valid_amount(spawn.min_speed) || !valid_amount(spawn.formation_team.spacing) ||
        !valid_amount(spawn.formation_team.raggedness))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite radius, turn rate, minimum speed, or formation spacing");
    }
    if (const auto& movement = spawn.movement; !valid_amount(movement.acceleration) || !valid_amount(movement.deceleration) || !valid_amount(movement.turn_speed_fraction) ||
                                               movement.turn_speed_fraction > 1.0F || !valid_amount(movement.stopped_turn_rate) || !valid_amount(movement.turn_in_place_angle) ||
                                               !valid_amount(movement.turn_in_place_rate) || !std::ranges::all_of(movement.turn_begin_seconds, valid_amount) ||
                                               !std::ranges::all_of(movement.turn_loop_seconds, valid_amount) || !std::ranges::all_of(movement.turn_end_seconds, valid_amount) ||
                                               !std::ranges::all_of(movement.turn_half_seconds, valid_amount) || !std::ranges::all_of(movement.turn_quarter_seconds, valid_amount) ||
                                               !valid_amount(movement.walk_start_seconds) || !valid_amount(movement.walk_stride_seconds) || !valid_amount(movement.walk_stop_seconds) ||
                                               !valid_amount(movement.walk_raise_seconds) || !valid_amount(movement.walk_lower_seconds))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite movement value, or a turn speed fraction above 1");
    }
    if (const auto& movement = spawn.movement;
        movement.walk_stop_parts > max_walk_stop_parts ||
        (movement.walk_stop_parts > 0 && (!(movement.walk_start_seconds > 0.0F) || !(movement.walk_stride_seconds > 0.0F) || !(movement.walk_stop_seconds > 0.0F))))
    {
        return make_error("entity spawn '" + spawn.type_name + "' walks as clips without the clips' times, or with more than " + std::to_string(max_walk_stop_parts) + " stops");
    }
    if (spawn.stance != Stance::aggressive && spawn.stance != Stance::hold_position && spawn.stance != Stance::hold_fire)
    {
        return make_error("entity spawn '" + spawn.type_name + "' has an unknown stance");
    }
    if (const auto& footprint = spawn.footprint;
        footprint && (!is_finite(footprint->center) || !valid_amount(footprint->half_length) || !valid_amount(footprint->half_width) || !std::isfinite(footprint->heading)))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite footprint");
    }
    if (!valid_profile(spawn.combat))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite combat value");
    }
    if (identity_.ids.size() >= no_dense)
    {
        return make_error("simulation entity storage is full");
    }
    std::uint32_t slot{};
    if (!free_slots_.empty())
    {
        slot = free_slots_.back();
        free_slots_.pop_back();
    }
    else
    {
        slot = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1);
        slot_dense_.push_back(no_dense);
    }
    const EntityId id{slot, generations_[slot]};
    slot_dense_[slot] = static_cast<std::uint32_t>(identity_.ids.size());
    if (spawn.movement.turn_begin_seconds[0] > 0.0F)
    {
        turn_clips_.add(slot);
    }
    if (spawn.movement.walk_stop_parts > 0 || spawn.movement.walk_raise_seconds > 0.0F)
    {
        walk_clips_.add(slot);
    }
    // A minimum speed above 0 makes a flyer (FlightColumns::min_speeds).
    const auto min_speed = std::min(spawn.min_speed, spawn.max_speed);
    if (min_speed > 0.0F)
    {
        flight_maneuvers_.add(slot);
    }
    if (spawn.footprint)
    {
        footprint_.columns.shapes[footprint_.add(slot)] = *spawn.footprint;
    }
    if (spawn.crush != CrushProfile{})
    {
        crush_.columns.profiles[crush_.add(slot)] = spawn.crush;
    }
    identity_.ids.push_back(id);
    placement_.positions.push_back(spawn.position);
    placement_.headings.push_back(wrap_angle(spawn.heading));
    locomotor_.max_speeds.push_back(spawn.max_speed);
    route_.move_targets.emplace_back();
    identity_.owners.push_back(spawn.owner);
    identity_.visuals.push_back(spawn.visual);
    identity_.selectable.push_back(spawn.selectable ? 1U : 0U);
    identity_.type_names.push_back(spawn.type_name);
    ground_mover_.follows_ground.push_back(spawn.follows_ground ? 1U : 0U);
    ground_mover_.passable_types.push_back(spawn.passable_types);
    route_.paths.emplace_back();
    combat_.states.push_back({.profile = spawn.combat, .health = spawn.combat.health, .shield = spawn.combat.shield});
    combat_.states.back().weapons.resize(spawn.combat.weapons.size());
    for (const auto& hardpoint : spawn.combat.hardpoints)
    {
        combat_.states.back().hardpoint_health.push_back(hardpoint.health);
    }
    placement_.radii.push_back(spawn.radius);
    locomotor_.turn_rates.push_back(spawn.turn_rate);
    flight_.min_speeds.push_back(min_speed);
    locomotor_.movements.push_back(spawn.movement);
    locomotor_.drive_speeds.push_back(0.0F);
    route_.loiter_points.emplace_back();
    combat_.stances.push_back(spawn.stance);
    combat_.aims_with_body.push_back(spawn.aims_with_body ? 1U : 0U);
    placement_.pitches.push_back(0.0F);
    flight_.cruise_heights.push_back(spawn.position.z);
    motion_.speeds.push_back(0.0F);
    combat_.last_shot_ticks.push_back(0);
    orders_.command_serials.push_back(0);
    orders_.standing.push_back({.scan_timer = slot % retarget_interval});
    locomotor_.suspended.push_back(0);
    locomotor_.arriving.push_back(0);
    steering_.intents.emplace_back();
    formation_.orders.push_back(spawn.formation_order);
    formation_.spacings.push_back(spawn.formation_spacing);
    formation_.solos.push_back(spawn.formation_solo ? 1U : 0U);
    formation_.teams.push_back(spawn.formation_team);
    locomotor_.speed_limits.push_back(0.0F);
    group_.ids.push_back(0);
    group_.best_remaining.push_back(0.0F);
    group_.stall_ticks.push_back(0);
    group_.waiting.push_back(0);
    route_.pending.push_back(0);
    for_each_column(
        [&](const auto& column)
        {
            EAW_ASSERT_MSG(column.size() == identity_.ids.size(), "every entity column needs one entry per entity (a column missing from spawn or for_each_column)");
        });
    stamp_footprint(identity_.ids.size() - 1U, true);
    if (spawn.follows_ground && ground_.valid())
    {
        placement_.positions.back().z = ground_.surface_height(spawn.position.x, spawn.position.y);
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
        run_system(system);
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
    tick_start_positions_ = placement_.positions;
    deaths_.clear();
    impacts_.clear();
    shots_.clear();
    hardpoint_deaths_.clear();
    spins_.clear();
}

void SimulationWorld::run_motion()
{
    // Movement and separation are the only systems that move entities, and entities are only added
    // (commands) before and removed (combat) after them, so dense indices line up here.
    const auto dt = static_cast<float>(tick_seconds_);
    for (std::size_t index = 0; index < placement_.positions.size(); ++index)
    {
        const auto& from = tick_start_positions_[index];
        const auto& to = placement_.positions[index];
        const auto moved = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y) + (to.z - from.z) * (to.z - from.z));
        motion_.speeds[index] = dt > 0.0F && std::isfinite(moved) ? moved / dt : 0.0F;
    }
}

void SimulationWorld::run_publish()
{
    ++tick_;
    publish();
}

std::string_view SimulationWorld::type_name(const EntityId id) const noexcept
{
    const auto dense = dense_index(id);
    return dense ? std::string_view(identity_.type_names[*dense]) : std::string_view{};
}

bool SimulationWorld::alive(const EntityId id) const noexcept
{
    return dense_index(id).has_value();
}

const SimSnapshot& SimulationWorld::snapshot() const noexcept
{
    return snapshot_;
}

SimulationWorld::Stats SimulationWorld::stats() const noexcept
{
    return {.tick = tick_,
            .entities = identity_.ids.size(),
            .commands_applied = commands_applied_,
            .stale_references = stale_references_,
            .blocked_moves = blocked_moves_,
            .path_searches = path_searches_,
            .partial_paths = partial_paths_,
            .path_expansions = path_expansions_,
            .queued_paths = path_queue_.size(),
            .shared_routes = shared_routes_,
            .shots_fired = shots_fired_,
            .hits = hits_,
            .kills = kills_,
            .hardpoints_destroyed = hardpoints_destroyed_,
            .crushed = crushed_,
            .projectiles = projectiles_.size(),
            .separated_pairs = separated_pairs_,
            .waiting_members = waiting_members_,
            .released_members = released_members_,
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
    hasher.total = hash_value(hasher.total, tick_);
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        hasher.subject = StateSubject::bodies;
        hasher.add(identity_.ids[index]);
        hasher.add(placement_.positions[index]);
        hasher.add(placement_.headings[index]);
        hasher.add(locomotor_.max_speeds[index]);
        const auto& target = route_.move_targets[index];
        hasher.add(target.has_value());
        if (target)
        {
            hasher.add(*target);
        }
        hasher.add(identity_.owners[index]);
        // Visual keys are presentation data the simulation never reads (a mesh index on maps), so
        // they stay out of the hash: the same battle hashes alike however its scene was built.
        hasher.add(identity_.selectable[index]);
        hasher.add(ground_mover_.follows_ground[index]);
        hasher.add(ground_mover_.passable_types[index]);
        hasher.subject = StateSubject::routes;
        hasher.add(route_.paths[index].size());
        for (const auto& waypoint : route_.paths[index])
        {
            hasher.add(waypoint);
        }
        hasher.subject = StateSubject::combat;
        // Field by field: the combat records have padding, whose bytes are unspecified.
        const auto& combat = combat_.states[index];
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
        hasher.subject = StateSubject::bodies;
        hasher.add(placement_.radii[index]);
        hasher.add(locomotor_.turn_rates[index]);
        hasher.add(flight_.min_speeds[index]);
        const auto& movement = locomotor_.movements[index];
        hasher.add(movement.acceleration);
        hasher.add(movement.deceleration);
        hasher.add(movement.turn_speed_fraction);
        hasher.add(movement.stopped_turn_rate);
        hasher.add(movement.turn_in_place_angle);
        hasher.add(movement.turn_in_place_rate);
        // Only entities that wind their turns (so other battles keep their hashes).
        if (movement.turn_begin_seconds[0] > 0.0F)
        {
            hasher.subject = StateSubject::clips;
            for (std::size_t side = 0; side < 2; ++side)
            {
                hasher.add(movement.turn_begin_seconds[side]);
                hasher.add(movement.turn_loop_seconds[side]);
                hasher.add(movement.turn_end_seconds[side]);
                hasher.add(movement.turn_half_seconds[side]);
                hasher.add(movement.turn_quarter_seconds[side]);
            }
            const auto& plan = turn_clips_.get(slot_of(index), &TurnClipColumns::plans);
            hasher.add(static_cast<std::uint8_t>(plan.active ? 1U : 0U));
            hasher.add(plan.elapsed);
            hasher.add(plan.start);
            hasher.add(plan.angle);
            hasher.add(plan.way);
            hasher.add(plan.scale);
            hasher.add(plan.loops);
            hasher.add(plan.halves);
            hasher.add(plan.quarters);
        }
        // Only entities that walk as clips.
        if (movement.walk_stop_parts > 0)
        {
            hasher.subject = StateSubject::clips;
            hasher.add(movement.walk_start_seconds);
            hasher.add(movement.walk_stride_seconds);
            hasher.add(movement.walk_stop_seconds);
            hasher.add(movement.walk_stop_parts);
            hasher.add(movement.walk_raise_seconds);
            hasher.add(movement.walk_lower_seconds);
            const auto& walk = walk_clips_.get(slot_of(index), &WalkClipColumns::plans);
            hasher.add(walk.clip);
            hasher.add(walk.part);
            hasher.add(walk.elapsed);
            hasher.add(walk.phase);
            hasher.add(walk.from_speed);
            hasher.add(walk.scale);
            hasher.add(static_cast<std::uint8_t>(walk.ends_move ? 1U : 0U));
            hasher.add(walk.destination.x);
            hasher.add(walk.destination.y);
            hasher.add(walk.destination.z);
            hasher.add(static_cast<std::uint8_t>(walk.turn_back ? 1U : 0U));
            hasher.add(static_cast<std::uint8_t>(walk.new_way ? 1U : 0U));
        }
        hasher.subject = StateSubject::bodies;
        hasher.add(locomotor_.drive_speeds[index]);
        hasher.subject = StateSubject::combat;
        hasher.add(combat_.stances[index]);
        hasher.subject = StateSubject::bodies;
        hasher.add(combat_.aims_with_body[index]);
        hasher.add_bytes(identity_.type_names[index].data(), identity_.type_names[index].size());
        hasher.subject = StateSubject::flight;
        hasher.add(placement_.pitches[index]);
        hasher.add(flight_.cruise_heights[index]);
        // A non-flyer's manoeuvres, absent from the pool, hash as the zeros it always had.
        const auto maneuver = flight_maneuvers_.find(slot_of(index));
        hasher.add(maneuver ? flight_maneuvers_.columns.looping[*maneuver] : std::uint8_t{0});
        hasher.add(maneuver ? flight_maneuvers_.columns.rolls[*maneuver] : 0.0F);
        hasher.add(route_.loiter_points[index].has_value());
        if (route_.loiter_points[index])
        {
            hasher.add(*route_.loiter_points[index]);
        }
        hasher.subject = StateSubject::formations;
        hasher.add(formation_.orders[index]);
        hasher.add(formation_.spacings[index]);
        hasher.add(formation_.solos[index]);
        hasher.add(formation_.teams[index].id);
        hasher.add(static_cast<std::uint8_t>(formation_.teams[index].arrangement));
        hasher.add(formation_.teams[index].spacing);
        hasher.add(formation_.teams[index].raggedness);
        hasher.subject = StateSubject::bodies;
        // Without a crush profile, the default one every such entity had.
        const auto& crush = crush_profile_of(index);
        hasher.add(static_cast<std::uint8_t>(crush.crusher));
        hasher.add(static_cast<std::uint8_t>((crush.squashable ? 1U : 0U) | (crush.squashable_by_supercrusher ? 2U : 0U)));
        hasher.add(locomotor_.speed_limits[index]);
        hasher.subject = StateSubject::routes;
        hasher.add(group_.ids[index]);
        hasher.add(group_.best_remaining[index]);
        hasher.add(group_.stall_ticks[index]);
        hasher.add(route_.pending[index]);
        hasher.subject = StateSubject::orders;
        const auto& order = orders_.standing[index];
        hasher.add(order.kind);
        hasher.add(order.point);
        hasher.add(order.entity);
        hasher.add(order.engaging);
        hasher.add(order.engage_origin);
        hasher.add(order.scan_timer);
        hasher.subject = StateSubject::combat;
        hasher.add(combat.invulnerable);
        hasher.add(combat.cannot_be_killed);
        hasher.subject = StateSubject::bodies;
        hasher.add(locomotor_.suspended[index]);
    }
    hasher.subject = StateSubject::routes;
    for (const auto& request : path_queue_)
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
    for (const auto& projectile : projectiles_)
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
    hasher.add(combat_random_.state());
}

const CrushProfile& SimulationWorld::crush_profile_of(const std::size_t index) const noexcept
{
    static constexpr CrushProfile none{};
    const auto* profile = crush_.find(slot_of(index), &CrushColumns::profiles);
    return profile != nullptr ? *profile : none;
}

bool SimulationWorld::turning_as_clips(const std::size_t index) const noexcept
{
    const auto* plan = turn_clips_.find(slot_of(index), &TurnClipColumns::plans);
    return plan != nullptr && plan->active;
}

std::optional<std::size_t> SimulationWorld::dense_index(const EntityId id) const noexcept
{
    if (!id.valid() || id.index >= generations_.size() || generations_[id.index] != id.generation || slot_dense_[id.index] == no_dense)
    {
        return std::nullopt;
    }
    return slot_dense_[id.index];
}

void SimulationWorld::destroy(const std::size_t dense)
{
    stamp_footprint(dense, false);
    const auto slot = identity_.ids[dense].index;
    const auto last = identity_.ids.size() - 1U;
    if (dense != last)
    {
        // Swap-remove keeps storage dense; the order change depends only on the command history.
        for_each_column(
            [&](auto& column)
            {
                column[dense] = std::move(column[last]);
            });
        slot_dense_[identity_.ids[dense].index] = static_cast<std::uint32_t>(dense);
    }
    for_each_column(
        [](auto& column)
        {
            column.pop_back();
        });
    slot_dense_[slot] = no_dense;
    turn_clips_.remove(slot);
    walk_clips_.remove(slot);
    flight_maneuvers_.remove(slot);
    footprint_.remove(slot);
    crush_.remove(slot);
    // A slot whose generation would wrap is retired instead of reused.
    if (generations_[slot] != std::numeric_limits<std::uint32_t>::max())
    {
        ++generations_[slot];
        free_slots_.push_back(slot);
    }
}

std::vector<Vector3> SimulationWorld::route(const EntityId id) const
{
    std::vector<Vector3> points;
    const auto dense = dense_index(id);
    if (!dense || !route_.move_targets[*dense])
    {
        return points;
    }
    if (*dense < detours_.size() && detours_[*dense])
    {
        points.push_back(*detours_[*dense]);
    }
    points.insert(points.end(), route_.paths[*dense].rbegin(), route_.paths[*dense].rend());
    points.push_back(*route_.move_targets[*dense]);
    return points;
}

void SimulationWorld::set_ground(GroundMap ground)
{
    ground_ = std::move(ground);
    local_pathfinder_.reset();
    ground_passability_ = ground_.passability;
    footprint_cover_.assign(ground_passability_.size(), 0);
    std::ranges::fill(footprint_.columns.stamped, std::uint8_t{0});
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        stamp_footprint(index, true);
    }
    pathfinder_.reset();
}

// Cells whose sample point lies inside the footprint rectangle take its passability type while any
// footprint covers them; the last footprint removed restores the ground's own value. The cached
// region labels are dropped, so paths see the change.
void SimulationWorld::stamp_footprint(const std::size_t index, const bool add)
{
    const auto* footprint = footprint_.find(slot_of(index), &FootprintColumns::shapes);
    if (footprint == nullptr || !ground_.valid() || ground_.passability.size() != ground_.heights.size() || footprint_cover_.size() != ground_.passability.size() ||
        (footprint_.get(slot_of(index), &FootprintColumns::stamped) != 0) == add)
    {
        return;
    }
    const auto cos_heading = std::cos(footprint->heading);
    const auto sin_heading = std::sin(footprint->heading);
    const auto reach = std::hypot(footprint->half_length, footprint->half_width);
    const auto spacing = ground_.spacing;
    const auto first_column = std::max(0L, static_cast<long>(std::floor((footprint->center.x - reach) / spacing)));
    const auto last_column = std::min(static_cast<long>(ground_.width) - 1, static_cast<long>(std::ceil((footprint->center.x + reach) / spacing)));
    const auto first_row = std::max(0L, static_cast<long>(std::floor((footprint->center.y - reach) / spacing)));
    const auto last_row = std::min(static_cast<long>(ground_.height) - 1, static_cast<long>(std::ceil((footprint->center.y + reach) / spacing)));
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
            const auto cell = static_cast<std::size_t>(row) * ground_.width + static_cast<std::size_t>(column);
            if (add)
            {
                ++footprint_cover_[cell];
                ground_.passability[cell] = footprint->passability;
            }
            else if (footprint_cover_[cell] > 0 && --footprint_cover_[cell] == 0)
            {
                ground_.passability[cell] = ground_passability_[cell];
            }
        }
    }
    footprint_.get(slot_of(index), &FootprintColumns::stamped) = add ? 1U : 0U;
    pathfinder_.reset();
    local_pathfinder_.reset();
}

const GroundMap& SimulationWorld::ground() const noexcept
{
    return ground_;
}

void SimulationWorld::publish()
{
    snapshot_.tick = tick_;
    snapshot_.entities.clear();
    snapshot_.entities.reserve(identity_.ids.size());
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        const auto& combat = combat_.states[index];
        std::uint32_t destroyable = 0;
        std::uint32_t destroyed = 0;
        for (std::size_t hardpoint = 0; hardpoint < combat.profile.hardpoints.size(); ++hardpoint)
        {
            if (combat.profile.hardpoints[hardpoint].health > 0.0F)
            {
                ++destroyable;
                destroyed += combat.hardpoint_health[hardpoint] <= 0.0F ? 1U : 0U;
            }
        }
        // A non-flyer reports no manoeuvres.
        const auto* yaw_rate = flight_maneuvers_.find(slot_of(index), &FlightManeuverColumns::yaw_rates);
        const auto* roll = flight_maneuvers_.find(slot_of(index), &FlightManeuverColumns::rolls);
        snapshot_.entities.push_back({identity_.ids[index],
                                      placement_.positions[index],
                                      placement_.headings[index],
                                      identity_.visuals[index],
                                      identity_.owners[index],
                                      route_.move_targets[index].has_value(),
                                      identity_.selectable[index] != 0,
                                      locomotor_.max_speeds[index],
                                      ground_mover_.follows_ground[index] != 0,
                                      combat.health,
                                      combat.profile.health,
                                      combat.shield,
                                      combat.profile.shield,
                                      combat.target,
                                      placement_.radii[index],
                                      destroyable,
                                      destroyed,
                                      placement_.pitches[index],
                                      (yaw_rate ? *yaw_rate : 0.0F),
                                      motion_.speeds[index],
                                      (roll ? *roll : 0.0F),
                                      combat.turret_yaw,
                                      combat_.last_shot_ticks[index],
                                      orders_.command_serials[index],
                                      orders_.standing[index].kind,
                                      locomotor_.arriving[index] != 0});
        snapshot_.entities.back().turns_as_clips = locomotor_.movements[index].turn_begin_seconds[0] > 0.0F && locomotor_.movements[index].stopped_turn_rate > 0.0F;
        if (turning_as_clips(index))
        {
            const auto& plan = turn_clips_.get(slot_of(index), &TurnClipColumns::plans);
            const auto point = turn_point(index, plan.elapsed);
            auto& entity = snapshot_.entities.back();
            entity.turn_clip = point.clip;
            entity.turn_left = plan.way > 0.0F;
            entity.turn_progress = point.progress;
        }
        if (const auto& movement = locomotor_.movements[index]; movement.walk_stop_parts > 0)
        {
            const auto& walk = walk_clips_.get(slot_of(index), &WalkClipColumns::plans);
            auto& entity = snapshot_.entities.back();
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
    snapshot_.deaths = deaths_;
    snapshot_.impacts = impacts_;
    snapshot_.shots = shots_;
    snapshot_.hardpoint_deaths = hardpoint_deaths_;
    snapshot_.spins = spins_;
    snapshot_.projectiles.clear();
    snapshot_.projectiles.reserve(projectiles_.size());
    for (const auto& projectile : projectiles_)
    {
        const Vector3 to_aim{projectile.aim.x - projectile.position.x, projectile.aim.y - projectile.position.y, projectile.aim.z - projectile.position.z};
        snapshot_.projectiles.push_back({projectile.position, projectile.direction, projectile.owner, projectile.visual, projectile.speed, projectile.serial, projectile.flown,
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
