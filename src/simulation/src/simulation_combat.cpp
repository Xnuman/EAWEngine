// Combat: shields, targeting, weapons, projectiles, damage, and removal of the dead.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::set_damage_table(DamageTable table)
{
    rules_.damage_table = std::move(table);
}

void SimulationWorld::set_hostility(const Hostility& hostility) noexcept
{
    rules_.hostility = hostility;
}

const Hostility& SimulationWorld::hostility() const noexcept
{
    return rules_.hostility;
}

// Shields refresh in steps: Shield_Refresh_Rate points once every refresh interval, each unit on a
// phase of its own (recorded in the FoC debug build: +10 every 90 frames, two ships 5 frames apart;
// EaW-Revised's S-15, S-51, S-52, docs/compatibility-research.md). How the original chooses the
// phase is not observed; one from the entity's stable id spreads units the same way (an engine
// choice), and keeps the refresh a function of the tick, so it adds no state to hash.
bool SimulationWorld::CombatSystem::shield_refresh_due(const View& view, const std::size_t index) const noexcept
{
    auto& identity = view.get<IdentityColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& clock = view.get<Clock>();
    const auto interval = static_cast<std::uint64_t>(std::max(1.0, std::round(static_cast<double>(combat.states[index].profile->shield_refresh_seconds) / clock.tick_seconds)));
    // A multiplicative hash (Knuth's) of the id spreads consecutive ids over the interval.
    constexpr std::uint64_t phase_multiplier = 2654435761U;
    const auto phase = (static_cast<std::uint64_t>(identity.ids[index].index) * phase_multiplier) % interval;
    return (clock.tick + phase) % interval == 0;
}

// Order within a tick: shields recharge, then every armed entity (in dense order) updates its
// target and fires, then shots fly and hit, then the dead are removed. Entities are only removed
// at the end, so dense indices stay valid throughout.
void SimulationWorld::CombatSystem::run(const View& view, const float dt)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    grid_.build(placement.positions, grid_cell_size);
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        auto& state = combat.states[index];
        if (state.shield < state.profile->shield && state.health > 0.0F && state.profile->shield_refresh > 0.0F && keeps(view, index, HardpointProfile::Kind::shield_generator) &&
            shield_refresh_due(view, index))
        {
            state.shield = std::min(state.profile->shield, state.shield + state.profile->shield_refresh);
        }
    }
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        // A spinning (dying) entity no longer fights.
        if (!combat.states[index].profile->weapons.empty() && combat.states[index].health > 0.0F)
        {
            update_target(view, index);
            aim_turret(view, index, dt);
            run_weapon(view, index);
        }
    }
    run_projectiles(view);
}

void SimulationWorld::CombatSystem::aim_turret(const View& view, const std::size_t index, const float dt)
{
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& state = combat.states[index];
    if (!state.profile->turret)
    {
        return;
    }
    const auto& turret = *state.profile->turret;
    float wanted = 0.0F;
    if (const auto victim = turret_victim(view, index))
    {
        const auto& to = placement.positions[*victim];
        wanted = std::clamp(wrap_angle(std::atan2(to.y - placement.positions[index].y, to.x - placement.positions[index].x) - placement.headings[index]), -turret.yaw_limit, turret.yaw_limit);
    }
    const auto step = turret.rate > 0.0F ? turret.rate * dt : pi;
    state.turret_yaw = wrap_angle(state.turret_yaw + std::clamp(wrap_angle(wanted - state.turret_yaw), -step, step));
}

std::optional<std::size_t> SimulationWorld::CombatSystem::turret_victim(const View& view, const std::size_t index)
{
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    const auto& fighter = combat.states[index];
    const auto target = attack_victim(view, index);
    for (std::size_t slot = 0; slot < fighter.profile->weapons.size(); ++slot)
    {
        const auto& weapon = fighter.profile->weapons[slot];
        if (weapon.hardpoint >= 0)
        {
            continue;
        }
        if (const auto& state = fighter.weapons[slot]; state.pulses_left > 0)
        {
            if (const auto victim = dense_index(view, state.target))
            {
                return victim;
            }
        }
        // As run_weapon picks: the entity's target when the weapon reaches it, else (unless the
        // target was ordered or the stance holds fire) the nearest the weapon reaches.
        if (target && in_reach(view, index, weapon, *target))
        {
            return target;
        }
        if ((fighter.ordered && target) || combat.stances[index] == Stance::hold_fire)
        {
            return target;
        }
        if (const auto nearest = choose_target(view, grid_, query_, index, placement.positions[index], weapon.range,
                                               [&](const std::size_t candidate)
                                               {
                                                   return in_reach(view, index, weapon, candidate);
                                               }))
        {
            return nearest;
        }
        break;
    }
    return target;
}

bool SimulationWorld::CombatSystem::in_reach(const View& view, const std::size_t index, const WeaponProfile& weapon, const std::size_t victim) const noexcept
{
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    const auto distance = distance_xy(placement.positions[index], placement.positions[victim]);
    return distance <= weapon.range && distance >= weapon.min_range && (weapon.excluded_categories & combat.states[victim].profile->categories) == 0;
}

LocalPoint SimulationWorld::CombatSystem::weapon_mount(const View& view, const std::size_t index, const WeaponProfile& weapon, const LocalPoint& mount) const noexcept
{
    auto& combat = view.get<CombatColumns>();
    const auto& turret = combat.states[index].profile->turret;
    if (!weapon.on_turret || !turret)
    {
        return mount;
    }
    const auto yaw = combat.states[index].turret_yaw;
    const auto c = std::cos(yaw);
    const auto s = std::sin(yaw);
    const auto forward = mount.forward - turret->pivot.forward;
    const auto left = mount.left - turret->pivot.left;
    return {turret->pivot.forward + forward * c - left * s, turret->pivot.left + forward * s + left * c, mount.up};
}

bool SimulationWorld::CombatSystem::turret_aimed(const View& view, const std::size_t index, const std::size_t victim) const noexcept
{
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    const auto& state = combat.states[index];
    if (!state.profile->turret)
    {
        return true;
    }
    const auto& to = placement.positions[victim];
    const auto bearing = std::atan2(to.y - placement.positions[index].y, to.x - placement.positions[index].x);
    return std::fabs(wrap_angle(bearing - (placement.headings[index] + state.turret_yaw))) <= turret_aim_tolerance;
}

void SimulationWorld::CombatSystem::update_target(const View& view, const std::size_t index)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& rules = view.get<Rules>();
    auto& state = combat.states[index];
    const auto range = state.profile->range();
    if (state.target.valid())
    {
        const auto victim = dense_index(view, state.target);
        bool keep = victim && damageable(view, *victim) && combat.states[*victim].profile->targetable && rules.hostility.hostile(identity.owners[index], identity.owners[*victim]);
        if (keep && !state.ordered)
        {
            keep = distance_xy(placement.positions[index], placement.positions[*victim]) <= range;
        }
        if (!keep)
        {
            state.target = {};
            state.ordered = false;
        }
        // A target held past the stickiness is weighed against the others again.
        else if (!state.ordered && state.profile->stickiness_seconds > 0.0F && ++state.target_ticks >= ticks_for(view, state.profile->stickiness_seconds))
        {
            state.target_ticks = 0;
            if (const auto best = choose_target(view, grid_, query_, index, placement.positions[index], range,
                                                [&](const std::size_t candidate)
                                                {
                                                    return engages(view, index, candidate);
                                                }))
            {
                state.target = identity.ids[*best];
            }
        }
    }
    // Ordered targets are kept (behavior chases them); automatic ones are searched every few ticks
    // unless the stance forbids picking targets.
    if (state.target.valid() || combat.stances[index] == Stance::hold_fire)
    {
        return;
    }
    if (state.retarget_timer > 0)
    {
        --state.retarget_timer;
        return;
    }
    state.retarget_timer = retarget_interval;
    const auto best = choose_target(view, grid_, query_, index, placement.positions[index], range,
                                    [&](const std::size_t candidate)
                                    {
                                        return engages(view, index, candidate);
                                    });
    if (best)
    {
        state.target = identity.ids[*best];
        state.target_ticks = 0;
    }
}

// Each weapon starts a burst at the entity's target when that target is within its range and arc,
// otherwise at the nearest damageable hostile within its own range and arc (ties to the lower id),
// so a ship's guns engage whatever their hardpoints face. A burst stays on its victim until its
// shots are spent.
void SimulationWorld::CombatSystem::run_weapon(const View& view, const std::size_t index)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& fighter = combat.states[index];
    const auto entity_target = fighter.target.valid() ? dense_index(view, fighter.target) : std::nullopt;
    for (std::size_t slot = 0; slot < fighter.profile->weapons.size(); ++slot)
    {
        const auto& weapon = fighter.profile->weapons[slot];
        auto& state = fighter.weapons[slot];
        if (weapon.hardpoint >= 0 && !hardpoint_alive(view, index, static_cast<std::size_t>(weapon.hardpoint)))
        {
            state.pulses_left = 0;
            continue;
        }
        if (state.cooldown > 0)
        {
            --state.cooldown;
        }
        if (state.pulse_timer > 0)
        {
            --state.pulse_timer;
        }
        // Body-aimers hold fire while moving (a burst resumes once they stand again).
        if (combat.aims_with_body[index] != 0 && route.move_targets[index])
        {
            continue;
        }
        if (state.pulses_left == 0)
        {
            if (state.cooldown != 0)
            {
                continue;
            }
            const auto mount = world_point(view, index, weapon_mount(view, index, weapon, weapon.mount));
            const auto cone_direction = weapon.cone_direction + (weapon.on_turret ? fighter.turret_yaw : 0.0F);
            const auto covers = [&](const std::size_t victim)
            {
                if (!in_reach(view, index, weapon, victim))
                {
                    return false;
                }
                if (combat.aims_with_body[index] != 0)
                {
                    const auto& to = placement.positions[victim];
                    const auto bearing = std::atan2(to.y - placement.positions[index].y, to.x - placement.positions[index].x);
                    if (std::fabs(wrap_angle(bearing - placement.headings[index])) > body_aim_tolerance)
                    {
                        return false;
                    }
                }
                if (weapon.cone_half_angle >= pi)
                {
                    return true;
                }
                const auto& to = placement.positions[victim];
                const auto bearing = std::atan2(to.y - mount.y, to.x - mount.x);
                return std::fabs(wrap_angle(bearing - (placement.headings[index] + cone_direction))) <= weapon.cone_half_angle;
            };
            std::optional<std::size_t> victim;
            if (entity_target && covers(*entity_target))
            {
                victim = entity_target;
            }
            else if ((!fighter.ordered || !entity_target) && combat.stances[index] != Stance::hold_fire)
            {
                victim = choose_target(view, grid_, query_, index, placement.positions[index], weapon.range, covers);
            }
            if (!victim)
            {
                continue;
            }
            // A turret's own weapon waits until the turret points at its victim.
            if (weapon.hardpoint < 0 && !turret_aimed(view, index, *victim))
            {
                continue;
            }
            state.target = identity.ids[*victim];
            state.pulses_left = std::max(1U, weapon.pulse_count);
            state.pulse_timer = ticks_for(view, weapon.appearance_delay_seconds);
            state.cooldown = std::max(1U, ticks_for(view, weapon.recharge_seconds));
        }
        // A burst keeps firing at its victim, in range or not, until its shots are spent.
        const auto victim = dense_index(view, state.target);
        if (!victim)
        {
            state.pulses_left = 0;
            continue;
        }
        if (combat.aims_with_body[index] != 0)
        {
            const auto& to = placement.positions[*victim];
            const auto bearing = std::atan2(to.y - placement.positions[index].y, to.x - placement.positions[index].x);
            if (std::fabs(wrap_angle(bearing - placement.headings[index])) > body_aim_tolerance)
            {
                // Turned away mid-burst: the rest of the burst waits until it faces the victim again.
                continue;
            }
        }
        if (weapon.hardpoint < 0 && !turret_aimed(view, index, *victim))
        {
            // Turned away mid-burst: the rest waits until the turret is back on the victim.
            continue;
        }
        while (state.pulses_left > 0 && state.pulse_timer == 0)
        {
            const auto muzzle = state.shots % (weapon.extra_mounts.size() + 1U);
            ++state.shots;
            launch(view, index, *victim, weapon, weapon_mount(view, index, weapon, muzzle == 0 ? weapon.mount : weapon.extra_mounts[muzzle - 1U]));
            --state.pulses_left;
            state.pulse_timer = ticks_for(view, weapon.pulse_delay_seconds);
        }
    }
}

void SimulationWorld::CombatSystem::launch(const View& view, const std::size_t shooter, const std::size_t victim, const WeaponProfile& weapon, const LocalPoint& mount)
{
    auto& identity = view.get<IdentityColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& projectiles = view.get<ProjectileState>();
    auto& events = view.get<TickEvents>();
    auto& combat_random = view.get<CombatRandom>();
    auto& clock = view.get<Clock>();
    auto& counters = view.get<CombatCounters>();
    auto& damage = view.get<DamageQueue>();
    ++counters.shots_fired;
    // The tick being run: the snapshot published at its end carries it as its tick.
    combat.last_shot_ticks[shooter] = clock.tick + 1U;
    const auto origin = world_point(view, shooter, mount);
    events.shots.push_back({identity.ids[shooter], origin, weapon.fire_sound, weapon.launch_effect});
    auto hardpoint = aim_hardpoint(view, victim, origin);
    if (weapon.random_hardpoints && hardpoint >= 0)
    {
        // Any intact, targetable part, drawn from the combat stream.
        std::vector<std::int32_t> parts;
        const auto& hardpoints = combat.states[victim].profile->hardpoints;
        for (std::size_t part = 0; part < hardpoints.size(); ++part)
        {
            if (hardpoints[part].targetable && hardpoints[part].health > 0.0F && combat.states[victim].hardpoint_health[part] > 0.0F)
            {
                parts.push_back(static_cast<std::int32_t>(part));
            }
        }
        hardpoint = parts[std::min(parts.size() - 1U, static_cast<std::size_t>(combat_random.stream.unit() * static_cast<float>(parts.size())))];
    }
    const auto offset = stray(view, weapon, victim);
    const bool misses = strays_off(view, victim, offset);
    if (weapon.projectile_speed <= 0.0F)
    {
        auto point = aim_point(view, victim, hardpoint);
        point = {point.x + offset.x, point.y + offset.y, point.z + offset.z};
        const Vector3 delta{point.x - origin.x, point.y - origin.y, point.z - origin.z};
        const auto length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        DamageEvent shot{.kind = DamageEvent::Kind::miss,
                         .damage = weapon.damage,
                         .damage_type = weapon.damage_type,
                         .damages_shield = weapon.damages_shield,
                         .damages_health = weapon.damages_health,
                         .point = point,
                         .direction = length > 0.0F ? Vector3{delta.x / length, delta.y / length, delta.z / length} : Vector3{},
                         .visual = weapon.projectile_visual,
                         .owner = identity.owners[shooter],
                         .blast = weapon.blast};
        if (!misses)
        {
            ++counters.hits;
            shot.kind = DamageEvent::Kind::hit;
            shot.victim = identity.ids[victim];
            shot.hardpoint = hardpoint;
        }
        else if (const auto other = stray_victim(view, point, identity.owners[shooter], victim))
        {
            ++counters.hits;
            shot.kind = DamageEvent::Kind::hit;
            shot.victim = identity.ids[*other];
        }
        damage.events.push_back(shot);
        return;
    }
    Projectile projectile;
    projectile.position = origin;
    projectile.aim = aim_point(view, victim, hardpoint);
    projectile.target = identity.ids[victim];
    projectile.target_hardpoint = hardpoint;
    projectile.owner = identity.owners[shooter];
    projectile.damage = weapon.damage;
    projectile.damage_type = weapon.damage_type;
    projectile.damages_shield = weapon.damages_shield;
    projectile.damages_health = weapon.damages_health;
    projectile.blast = weapon.blast;
    projectile.lifetime_ticks = weapon.max_lifetime > 0.0F ? std::max(1U, ticks_for(view, weapon.max_lifetime)) : 0U;
    projectile.scan_range = weapon.scan_range;
    projectile.speed = weapon.projectile_speed;
    projectile.max_flight = weapon.max_flight_distance;
    projectile.visual = weapon.projectile_visual;
    projectile.serial = projectiles.next_serial++;
    projectile.stray = offset;
    projectile.misses = misses;
    projectile.aim = {projectile.aim.x + offset.x, projectile.aim.y + offset.y, projectile.aim.z + offset.z};
    projectiles.flying.push_back(projectile);
}

Vector3 SimulationWorld::CombatSystem::stray(const View& view, const WeaponProfile& weapon, const std::size_t victim)
{
    auto& combat = view.get<CombatColumns>();
    auto& combat_random = view.get<CombatRandom>();
    const auto categories = combat.states[victim].profile->categories;
    for (const auto& entry : weapon.inaccuracy)
    {
        if (entry.categories != all_categories && (entry.categories & categories) == 0)
        {
            continue;
        }
        if (entry.distance <= 0.0F)
        {
            return {};
        }
        // Uniform over the disc of that radius.
        const auto angle = combat_random.stream.unit() * two_pi;
        const auto radius = entry.distance * std::sqrt(combat_random.stream.unit());
        return {std::cos(angle) * radius, std::sin(angle) * radius, 0.0F};
    }
    return {};
}

bool SimulationWorld::CombatSystem::strays_off(const View& view, const std::size_t victim, const Vector3& stray) const noexcept
{
    // Without a size there is nothing to measure against, and the shot hits.
    const auto size = target_size(view, victim);
    return size > 0.0F && std::hypot(stray.x, stray.y) > size;
}

std::optional<std::size_t> SimulationWorld::CombatSystem::stray_victim(const View& view, const Vector3& point, const std::uint32_t owner, const std::size_t target) const noexcept
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& rules = view.get<Rules>();
    // shortcut: a scan over every entity per stray shot; use the spatial grid when battles get big.
    std::optional<std::size_t> best;
    float best_distance = 0.0F;
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (index == target || !combat.states[index].profile->collidable || !damageable(view, index) || !rules.hostility.hostile(owner, identity.owners[index]))
        {
            continue;
        }
        const auto size = target_size(view, index);
        const auto distance = std::hypot(point.x - placement.positions[index].x, point.y - placement.positions[index].y);
        if (size > 0.0F && distance <= size && (!best || distance < best_distance))
        {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

float SimulationWorld::wound(CombatState& combat, const float damage)
{
    if (combat.invulnerable || combat.profile->immune || damage <= 0.0F)
    {
        return 0.0F;
    }
    auto health = combat.health - damage;
    if (combat.cannot_be_killed)
    {
        health = std::max(health, std::min(unkillable_health_floor, combat.profile->health * unkillable_health_fraction));
    }
    const auto dealt = combat.health - health;
    combat.health = health;
    return std::max(dealt, 0.0F);
}

void SimulationWorld::CombatSystem::run_projectiles(const View& view)
{
    auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    auto& combat = view.get<CombatColumns>();
    auto& projectiles = view.get<ProjectileState>();
    auto& rules = view.get<Rules>();
    auto& clock = view.get<Clock>();
    auto& counters = view.get<CombatCounters>();
    auto& damage = view.get<DamageQueue>();
    const auto dt = static_cast<float>(clock.tick_seconds);
    std::size_t kept = 0;
    for (std::size_t index = 0; index < projectiles.flying.size(); ++index)
    {
        auto projectile = projectiles.flying[index];
        // Shots home on their target's current aim point; a dead target's shot flies on to the
        // last point and is lost there.
        auto victim = dense_index(view, projectile.target);
        if (!victim && projectile.scan_range > 0.0F)
        {
            // A guided shot takes the nearest hostile in scan range of itself (ties: the higher dense
            // index, the last one the scan meets; storage order, so the simulation rewrite settles
            // it, proposal F of the 2026-11 review; changing it changes the hash).
            float best = projectile.scan_range;
            for (std::size_t other = 0; other < identity.ids.size(); ++other)
            {
                const auto& at = placement.positions[other];
                const auto distance = std::sqrt((at.x - projectile.position.x) * (at.x - projectile.position.x) + (at.y - projectile.position.y) * (at.y - projectile.position.y) +
                                                (at.z - projectile.position.z) * (at.z - projectile.position.z));
                if (damageable(view, other) && combat.states[other].profile->targetable && rules.hostility.hostile(projectile.owner, identity.owners[other]) && distance <= best)
                {
                    victim = other;
                    best = distance;
                }
            }
            if (victim)
            {
                projectile.target = identity.ids[*victim];
                projectile.target_hardpoint = -1;
                projectile.stray = {};
                projectile.misses = false;
            }
        }
        if (victim)
        {
            // A part destroyed meanwhile still takes the shot's aim; the hit lands on the hull.
            const auto aim = aim_point(view, *victim, projectile.target_hardpoint);
            projectile.aim = {aim.x + projectile.stray.x, aim.y + projectile.stray.y, aim.z + projectile.stray.z};
        }
        const Vector3 delta{projectile.aim.x - projectile.position.x, projectile.aim.y - projectile.position.y, projectile.aim.z - projectile.position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto step_length = projectile.speed * dt;
        if (distance > 0.0F)
        {
            projectile.direction = {delta.x / distance, delta.y / distance, delta.z / distance};
        }
        DamageEvent shot{.kind = DamageEvent::Kind::miss,
                         .damage = projectile.damage,
                         .damage_type = projectile.damage_type,
                         .damages_shield = projectile.damages_shield,
                         .damages_health = projectile.damages_health,
                         .point = projectile.aim,
                         .direction = projectile.direction,
                         .visual = projectile.visual,
                         .owner = projectile.owner,
                         .blast = projectile.blast};
        if (distance <= step_length)
        {
            // A shot that strays off its target, or whose target died, is lost at its aim point.
            if (victim && damageable(view, *victim) && !projectile.misses)
            {
                ++counters.hits;
                shot.kind = DamageEvent::Kind::hit;
                shot.victim = identity.ids[*victim];
                shot.hardpoint = projectile.target_hardpoint;
            }
            else if (const auto other = projectile.misses ? stray_victim(view, projectile.aim, projectile.owner, victim.value_or(identity.ids.size())) : std::nullopt)
            {
                ++counters.hits;
                shot.kind = DamageEvent::Kind::hit;
                shot.victim = identity.ids[*other];
            }
            damage.events.push_back(shot);
            continue;
        }
        const auto scale = step_length / distance;
        projectile.position = {projectile.position.x + delta.x * scale, projectile.position.y + delta.y * scale, projectile.position.z + delta.z * scale};
        projectile.flown += step_length;
        const bool expired = projectile.lifetime_ticks > 0U && --projectile.lifetime_ticks == 0U;
        if ((projectile.max_flight > 0.0F && projectile.flown > projectile.max_flight) || expired)
        {
            shot.point = projectile.position;
            damage.events.push_back(shot);
            continue;
        }
        projectiles.flying[kept++] = projectile;
    }
    projectiles.flying.resize(kept);
}

void SimulationWorld::run_resolution()
{
    // Each entity's death is handled once per tick; the blasts of the dead may kill more, whose
    // blasts are resolved in the next round, until a round kills nothing.
    std::vector<std::uint8_t> handled(identity_.ids.size(), 0);
    std::size_t next = 0;
    do
    {
        for (; next < damage_queue_.events.size(); ++next)
        {
            resolve(damage_queue_.events[next]);
        }
    } while (resolve_deaths(handled));
    damage_queue_.events.clear();
}

void SimulationWorld::resolve(const DamageEvent& event)
{
    const auto victim = dense_index(*this, event.victim);
    switch (event.kind)
    {
    case DamageEvent::Kind::crush:
        if (victim && wound(combat_.states[*victim], combat_.states[*victim].health) > 0.0F)
        {
            ++combat_counters_.crushed;
            combat_.states[*victim].crushed = true;
        }
        return;
    case DamageEvent::Kind::hit:
    {
        // A victim killed earlier in the tick takes the shot on its body, without damage (the
        // owner's rule: two units firing at once both hit).
        auto kind = ImpactKind::expired;
        if (victim)
        {
            kind = damageable(*this, *victim) ? apply_damage(*this, *victim, event.damage, event.damage_type, event.damages_shield, event.damages_health, event.hardpoint) : ImpactKind::hull;
        }
        events_.impacts.push_back({event.point, event.direction, event.visual, kind, victim ? event.victim : EntityId{}});
        break;
    }
    case DamageEvent::Kind::miss:
        events_.impacts.push_back({event.point, event.direction, event.visual, ImpactKind::expired});
        break;
    case DamageEvent::Kind::blast:
        break;
    }
    area_blast(*this, event.point, event.owner, event.blast, event.damage_type, event.damages_shield, event.damages_health);
}

bool SimulationWorld::resolve_deaths(std::vector<std::uint8_t>& handled)
{
    const auto queued = damage_queue_.events.size();
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        auto& combat = combat_.states[index];
        if (handled[index] != 0 || combat.profile->health <= 0.0F || combat.health > 0.0F)
        {
            continue;
        }
        handled[index] = 1;
        // Spinning away: out of control until the spin ends.
        if (combat.spin_ticks == 0 && combat.profile->spin_chance > 0.0F && combat_random_.stream.unit() < combat.profile->spin_chance)
        {
            combat.spin_ticks = std::max(1U, ticks_for(*this, combat.profile->spin_seconds));
            combat.target = {};
            combat.ordered = false;
            route_.move_targets[index].reset();
            route_.paths[index].clear();
            locomotor_.speed_limits[index] = 0.0F;
            group_.ids[index] = 0;
            orders_.standing[index].kind = StandingOrder::none;
            events_.spins.push_back({identity_.ids[index], placement_.positions[index]});
            continue;
        }
        if (combat.spin_ticks > 1U)
        {
            --combat.spin_ticks;
            continue;
        }
        const bool spun = combat.spin_ticks != 0;
        const auto& profile = *combat.profile;
        events_.deaths.push_back({identity_.ids[index], placement_.positions[index], placement_.headings[index], placement_.pitches[index], identity_.visuals[index], identity_.owners[index],
                                  spun && profile.spin_effect != 0 ? profile.spin_effect : profile.death_effect, profile.type_key, combat.crushed});
        damage_queue_.dying.push_back(index);
        ++combat_counters_.kills;
        // An empty blast is skipped where it is resolved.
        damage_queue_.events.push_back({.kind = DamageEvent::Kind::blast,
                                        .damage_type = profile.death_blast_damage_type,
                                        .damages_shield = profile.death_blast_damages_shield,
                                        .damages_health = profile.death_blast_damages_health,
                                        .point = placement_.positions[index],
                                        .owner = identity_.owners[index],
                                        .blast = profile.death_blast});
    }
    return damage_queue_.events.size() != queued;
}

void SimulationWorld::run_removal()
{
    remove_entities(std::move(damage_queue_.dying));
    damage_queue_.dying.clear();
}

} // namespace eaw
