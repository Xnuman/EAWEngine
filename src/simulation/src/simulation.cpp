// World core: entity storage, spawning, the tick, state hashing, footprints, and snapshots.

#include "simulation_internal.hpp"

#include <algorithm>
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
                                               !valid_amount(movement.turn_in_place_rate))
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a negative or non-finite movement value, or a turn speed fraction above 1");
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
    if (ids_.size() >= no_dense)
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
    slot_dense_[slot] = static_cast<std::uint32_t>(ids_.size());
    ids_.push_back(id);
    positions_.push_back(spawn.position);
    headings_.push_back(wrap_angle(spawn.heading));
    max_speeds_.push_back(spawn.max_speed);
    move_targets_.emplace_back();
    owners_.push_back(spawn.owner);
    visuals_.push_back(spawn.visual);
    selectable_.push_back(spawn.selectable ? 1U : 0U);
    type_names_.push_back(spawn.type_name);
    follows_ground_.push_back(spawn.follows_ground ? 1U : 0U);
    passable_types_.push_back(spawn.passable_types);
    paths_.emplace_back();
    combat_.push_back({.profile = spawn.combat, .health = spawn.combat.health, .shield = spawn.combat.shield});
    combat_.back().weapons.resize(spawn.combat.weapons.size());
    for (const auto& hardpoint : spawn.combat.hardpoints)
    {
        combat_.back().hardpoint_health.push_back(hardpoint.health);
    }
    radii_.push_back(spawn.radius);
    turn_rates_.push_back(spawn.turn_rate);
    min_speeds_.push_back(std::min(spawn.min_speed, spawn.max_speed));
    movements_.push_back(spawn.movement);
    drive_speeds_.push_back(0.0F);
    loiter_points_.emplace_back();
    stances_.push_back(spawn.stance);
    aims_with_body_.push_back(spawn.aims_with_body ? 1U : 0U);
    pitches_.push_back(0.0F);
    cruise_heights_.push_back(spawn.position.z);
    yaw_rates_.push_back(0.0F);
    speeds_.push_back(0.0F);
    last_shot_ticks_.push_back(0);
    command_serials_.push_back(0);
    standing_orders_.push_back({.scan_timer = slot % retarget_interval});
    locomotor_suspended_.push_back(0);
    looping_.push_back(0);
    rolls_.push_back(0.0F);
    intents_.emplace_back();
    formation_orders_.push_back(spawn.formation_order);
    formation_spacings_.push_back(spawn.formation_spacing);
    formation_solos_.push_back(spawn.formation_solo ? 1U : 0U);
    formation_teams_.push_back(spawn.formation_team);
    crushes_.push_back(spawn.crush);
    speed_limits_.push_back(0.0F);
    group_ids_.push_back(0);
    group_best_remaining_.push_back(0.0F);
    group_stall_ticks_.push_back(0);
    group_waiting_.push_back(0);
    footprints_.push_back(spawn.footprint);
    path_pending_.push_back(0);
    footprint_stamped_.push_back(0);
    stamp_footprint(ids_.size() - 1U, true);
    if (spawn.follows_ground && ground_.valid())
    {
        positions_.back().z = ground_.surface_height(spawn.position.x, spawn.position.y);
    }
    return id;
}

void SimulationWorld::submit(SimCommand command)
{
    pending_.push_back(std::move(command));
}

void SimulationWorld::step()
{
    // Commands first, in submission order, so the tick sees a consistent intent. Then the layers in
    // order: behavior (what each unit wants from its order, stance, and target), movement
    // (navigation and locomotors carry it out), separation, and combat (targeting, weapons, damage).
    auto commands = std::move(pending_);
    pending_.clear();
    for (const auto& command : commands)
    {
        apply(command);
    }
    tick_start_positions_ = positions_;
    deaths_.clear();
    impacts_.clear();
    shots_.clear();
    hardpoint_deaths_.clear();
    spins_.clear();
    run_behavior();
    run_movement();
    run_crushing();
    run_separation();
    // Movement and separation are the only systems that move entities, and entities are only added
    // (commands) before and removed (combat) after them, so dense indices line up here.
    const auto dt = static_cast<float>(tick_seconds_);
    for (std::size_t index = 0; index < positions_.size(); ++index)
    {
        const auto& from = tick_start_positions_[index];
        const auto& to = positions_[index];
        const auto moved = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y) + (to.z - from.z) * (to.z - from.z));
        speeds_[index] = dt > 0.0F && std::isfinite(moved) ? moved / dt : 0.0F;
    }
    run_combat();
    ++tick_;
    publish();
}

std::string_view SimulationWorld::type_name(const EntityId id) const noexcept
{
    const auto dense = dense_index(id);
    return dense ? std::string_view(type_names_[*dense]) : std::string_view{};
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
            .entities = ids_.size(),
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
            .rejected_spawns = rejected_spawns_};
}

std::uint64_t SimulationWorld::state_hash() const noexcept
{
    auto hash = hash_value(14695981039346656037ULL, tick_);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        hash = hash_value(hash, ids_[index]);
        hash = hash_value(hash, positions_[index]);
        hash = hash_value(hash, headings_[index]);
        hash = hash_value(hash, max_speeds_[index]);
        const auto& target = move_targets_[index];
        hash = hash_value(hash, target.has_value());
        if (target)
        {
            hash = hash_value(hash, *target);
        }
        hash = hash_value(hash, owners_[index]);
        // Visual keys are presentation data the simulation never reads (a mesh index on maps), so
        // they stay out of the hash: the same battle hashes alike however its scene was built.
        hash = hash_value(hash, selectable_[index]);
        hash = hash_value(hash, follows_ground_[index]);
        hash = hash_value(hash, passable_types_[index]);
        hash = hash_value(hash, paths_[index].size());
        for (const auto& waypoint : paths_[index])
        {
            hash = hash_value(hash, waypoint);
        }
        // Field by field: the combat records have padding, whose bytes are unspecified.
        const auto& combat = combat_[index];
        hash = hash_value(hash, combat.health);
        hash = hash_value(hash, combat.shield);
        hash = hash_value(hash, combat.target);
        hash = hash_value(hash, combat.ordered);
        for (const auto health : combat.hardpoint_health)
        {
            hash = hash_value(hash, health);
        }
        for (const auto& weapon : combat.weapons)
        {
            hash = hash_value(hash, weapon.cooldown);
            hash = hash_value(hash, weapon.pulses_left);
            hash = hash_value(hash, weapon.pulse_timer);
            hash = hash_value(hash, weapon.target);
            hash = hash_value(hash, weapon.shots);
        }
        hash = hash_value(hash, combat.retarget_timer);
        hash = hash_value(hash, combat.target_ticks);
        hash = hash_value(hash, combat.spin_ticks);
        hash = hash_value(hash, combat.turret_yaw);
        hash = hash_value(hash, combat.chase_timer);
        hash = hash_value(hash, radii_[index]);
        hash = hash_value(hash, turn_rates_[index]);
        hash = hash_value(hash, min_speeds_[index]);
        const auto& movement = movements_[index];
        hash = hash_value(hash, movement.acceleration);
        hash = hash_value(hash, movement.deceleration);
        hash = hash_value(hash, movement.turn_speed_fraction);
        hash = hash_value(hash, movement.stopped_turn_rate);
        hash = hash_value(hash, movement.turn_in_place_angle);
        hash = hash_value(hash, movement.turn_in_place_rate);
        hash = hash_value(hash, drive_speeds_[index]);
        hash = hash_value(hash, stances_[index]);
        hash = hash_value(hash, aims_with_body_[index]);
        hash = fnv1a(hash, type_names_[index].data(), type_names_[index].size());
        hash = hash_value(hash, pitches_[index]);
        hash = hash_value(hash, cruise_heights_[index]);
        hash = hash_value(hash, looping_[index]);
        hash = hash_value(hash, rolls_[index]);
        hash = hash_value(hash, loiter_points_[index].has_value());
        if (loiter_points_[index])
        {
            hash = hash_value(hash, *loiter_points_[index]);
        }
        hash = hash_value(hash, formation_orders_[index]);
        hash = hash_value(hash, formation_spacings_[index]);
        hash = hash_value(hash, formation_solos_[index]);
        hash = hash_value(hash, formation_teams_[index].id);
        hash = hash_value(hash, static_cast<std::uint8_t>(formation_teams_[index].arrangement));
        hash = hash_value(hash, formation_teams_[index].spacing);
        hash = hash_value(hash, formation_teams_[index].raggedness);
        hash = hash_value(hash, static_cast<std::uint8_t>(crushes_[index].crusher));
        hash = hash_value(hash, static_cast<std::uint8_t>((crushes_[index].squashable ? 1U : 0U) | (crushes_[index].squashable_by_supercrusher ? 2U : 0U)));
        hash = hash_value(hash, speed_limits_[index]);
        hash = hash_value(hash, group_ids_[index]);
        hash = hash_value(hash, group_best_remaining_[index]);
        hash = hash_value(hash, group_stall_ticks_[index]);
        hash = hash_value(hash, path_pending_[index]);
        const auto& order = standing_orders_[index];
        hash = hash_value(hash, order.kind);
        hash = hash_value(hash, order.point);
        hash = hash_value(hash, order.entity);
        hash = hash_value(hash, order.engaging);
        hash = hash_value(hash, order.engage_origin);
        hash = hash_value(hash, order.scan_timer);
        hash = hash_value(hash, combat.invulnerable);
        hash = hash_value(hash, combat.cannot_be_killed);
        hash = hash_value(hash, locomotor_suspended_[index]);
    }
    for (const auto& request : path_queue_)
    {
        for (std::size_t member = 0; member < request.members.size(); ++member)
        {
            hash = hash_value(hash, request.members[member]);
            hash = hash_value(hash, request.goals[member]);
            hash = hash_value(hash, request.serials[member]);
        }
        hash = hash_value(hash, request.group_target);
    }
    for (const auto& projectile : projectiles_)
    {
        hash = hash_value(hash, projectile.position);
        hash = hash_value(hash, projectile.aim);
        hash = hash_value(hash, projectile.target);
        hash = hash_value(hash, projectile.target_hardpoint);
        hash = hash_value(hash, projectile.owner);
        hash = hash_value(hash, projectile.damage);
        hash = hash_value(hash, projectile.blast.damage);
        hash = hash_value(hash, projectile.blast.range);
        hash = hash_value(hash, projectile.lifetime_ticks);
        hash = hash_value(hash, projectile.flown);
        hash = hash_value(hash, projectile.stray);
        hash = hash_value(hash, projectile.misses);
    }
    hash = hash_value(hash, combat_random_.state());
    return hash;
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
    const auto slot = ids_[dense].index;
    const auto last = ids_.size() - 1U;
    if (dense != last)
    {
        // Swap-remove keeps storage dense; the order change depends only on the command history.
        ids_[dense] = ids_[last];
        positions_[dense] = positions_[last];
        headings_[dense] = headings_[last];
        max_speeds_[dense] = max_speeds_[last];
        move_targets_[dense] = move_targets_[last];
        owners_[dense] = owners_[last];
        visuals_[dense] = visuals_[last];
        selectable_[dense] = selectable_[last];
        type_names_[dense] = std::move(type_names_[last]);
        follows_ground_[dense] = follows_ground_[last];
        passable_types_[dense] = passable_types_[last];
        paths_[dense] = std::move(paths_[last]);
        combat_[dense] = combat_[last];
        radii_[dense] = radii_[last];
        turn_rates_[dense] = turn_rates_[last];
        min_speeds_[dense] = min_speeds_[last];
        movements_[dense] = movements_[last];
        drive_speeds_[dense] = drive_speeds_[last];
        loiter_points_[dense] = loiter_points_[last];
        stances_[dense] = stances_[last];
        aims_with_body_[dense] = aims_with_body_[last];
        pitches_[dense] = pitches_[last];
        cruise_heights_[dense] = cruise_heights_[last];
        yaw_rates_[dense] = yaw_rates_[last];
        speeds_[dense] = speeds_[last];
        last_shot_ticks_[dense] = last_shot_ticks_[last];
        command_serials_[dense] = command_serials_[last];
        standing_orders_[dense] = standing_orders_[last];
        locomotor_suspended_[dense] = locomotor_suspended_[last];
        looping_[dense] = looping_[last];
        rolls_[dense] = rolls_[last];
        intents_[dense] = intents_[last];
        formation_orders_[dense] = formation_orders_[last];
        formation_spacings_[dense] = formation_spacings_[last];
        formation_solos_[dense] = formation_solos_[last];
        formation_teams_[dense] = formation_teams_[last];
        crushes_[dense] = crushes_[last];
        speed_limits_[dense] = speed_limits_[last];
        group_ids_[dense] = group_ids_[last];
        group_best_remaining_[dense] = group_best_remaining_[last];
        group_stall_ticks_[dense] = group_stall_ticks_[last];
        group_waiting_[dense] = group_waiting_[last];
        footprints_[dense] = footprints_[last];
        path_pending_[dense] = path_pending_[last];
        footprint_stamped_[dense] = footprint_stamped_[last];
        slot_dense_[ids_[dense].index] = static_cast<std::uint32_t>(dense);
    }
    ids_.pop_back();
    positions_.pop_back();
    headings_.pop_back();
    max_speeds_.pop_back();
    move_targets_.pop_back();
    owners_.pop_back();
    visuals_.pop_back();
    selectable_.pop_back();
    type_names_.pop_back();
    follows_ground_.pop_back();
    passable_types_.pop_back();
    paths_.pop_back();
    combat_.pop_back();
    radii_.pop_back();
    turn_rates_.pop_back();
    min_speeds_.pop_back();
    movements_.pop_back();
    drive_speeds_.pop_back();
    loiter_points_.pop_back();
    stances_.pop_back();
    aims_with_body_.pop_back();
    pitches_.pop_back();
    cruise_heights_.pop_back();
    yaw_rates_.pop_back();
    speeds_.pop_back();
    last_shot_ticks_.pop_back();
    command_serials_.pop_back();
    standing_orders_.pop_back();
    locomotor_suspended_.pop_back();
    looping_.pop_back();
    rolls_.pop_back();
    intents_.pop_back();
    formation_orders_.pop_back();
    formation_spacings_.pop_back();
    formation_solos_.pop_back();
    formation_teams_.pop_back();
    crushes_.pop_back();
    speed_limits_.pop_back();
    group_ids_.pop_back();
    group_best_remaining_.pop_back();
    group_stall_ticks_.pop_back();
    group_waiting_.pop_back();
    footprints_.pop_back();
    path_pending_.pop_back();
    footprint_stamped_.pop_back();
    slot_dense_[slot] = no_dense;
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
    if (!dense || !move_targets_[*dense])
    {
        return points;
    }
    if (*dense < detours_.size() && detours_[*dense])
    {
        points.push_back(*detours_[*dense]);
    }
    points.insert(points.end(), paths_[*dense].rbegin(), paths_[*dense].rend());
    points.push_back(*move_targets_[*dense]);
    return points;
}

void SimulationWorld::set_ground(GroundMap ground)
{
    ground_ = std::move(ground);
    local_pathfinder_.reset();
    ground_passability_ = ground_.passability;
    footprint_cover_.assign(ground_passability_.size(), 0);
    std::ranges::fill(footprint_stamped_, std::uint8_t{0});
    for (std::size_t index = 0; index < ids_.size(); ++index)
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
    const auto& footprint = footprints_[index];
    if (!footprint || !ground_.valid() || ground_.passability.size() != ground_.heights.size() || footprint_cover_.size() != ground_.passability.size() || (footprint_stamped_[index] != 0) == add)
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
    footprint_stamped_[index] = add ? 1U : 0U;
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
    snapshot_.entities.reserve(ids_.size());
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        const auto& combat = combat_[index];
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
        snapshot_.entities.push_back({ids_[index],
                                      positions_[index],
                                      headings_[index],
                                      visuals_[index],
                                      owners_[index],
                                      move_targets_[index].has_value(),
                                      selectable_[index] != 0,
                                      max_speeds_[index],
                                      follows_ground_[index] != 0,
                                      combat.health,
                                      combat.profile.health,
                                      combat.shield,
                                      combat.profile.shield,
                                      combat.target,
                                      radii_[index],
                                      destroyable,
                                      destroyed,
                                      pitches_[index],
                                      yaw_rates_[index],
                                      speeds_[index],
                                      rolls_[index],
                                      combat.turret_yaw,
                                      last_shot_ticks_[index],
                                      command_serials_[index],
                                      standing_orders_[index].kind});
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
        }
        result.push_back(blended);
    }
    return result;
}

} // namespace eaw
