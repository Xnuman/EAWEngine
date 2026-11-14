// Combat: shields, targeting, weapons, projectiles, damage, and removal of the dead.

#include "simulation_internal.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::set_damage_table(DamageTable table)
{
    damage_table_ = std::move(table);
}

void SimulationWorld::set_hostility(const Hostility& hostility) noexcept
{
    hostility_ = hostility;
}

const Hostility& SimulationWorld::hostility() const noexcept
{
    return hostility_;
}

std::uint32_t SimulationWorld::ticks_for(const float seconds) const noexcept
{
    const auto ticks = std::lround(static_cast<double>(seconds) / tick_seconds_);
    return static_cast<std::uint32_t>(std::clamp<long>(ticks, 0L, 1L << 30));
}

bool SimulationWorld::damageable(const std::size_t index) const noexcept
{
    return combat_.states[index].profile.health > 0.0F && combat_.states[index].health > 0.0F;
}

// Shields refresh in steps: Shield_Refresh_Rate points once every refresh interval, each unit on a
// phase of its own (recorded in the FoC debug build: +10 every 90 frames, two ships 5 frames apart;
// EaW-Revised's S-15, S-51, S-52, docs/compatibility-research.md). How the original chooses the
// phase is not observed; one from the entity's stable id spreads units the same way (an engine
// choice), and keeps the refresh a function of the tick, so it adds no state to hash.
bool SimulationWorld::shield_refresh_due(const std::size_t index) const noexcept
{
    const auto interval = static_cast<std::uint64_t>(std::max(1.0, std::round(static_cast<double>(combat_.states[index].profile.shield_refresh_seconds) / tick_seconds_)));
    // A multiplicative hash (Knuth's) of the id spreads consecutive ids over the interval.
    constexpr std::uint64_t phase_multiplier = 2654435761U;
    const auto phase = (static_cast<std::uint64_t>(identity_.ids[index].index) * phase_multiplier) % interval;
    return (tick_ + phase) % interval == 0;
}

// Order within a tick: shields recharge, then every armed entity (in dense order) updates its
// target and fires, then shots fly and hit, then the dead are removed. Entities are only removed
// at the end, so dense indices stay valid throughout.
void SimulationWorld::run_combat()
{
    const auto dt = static_cast<float>(tick_seconds_);
    grid_.build(placement_.positions, grid_cell_size);
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        auto& combat = combat_.states[index];
        if (combat.shield < combat.profile.shield && combat.health > 0.0F && combat.profile.shield_refresh > 0.0F && keeps(index, HardpointProfile::Kind::shield_generator) &&
            shield_refresh_due(index))
        {
            combat.shield = std::min(combat.profile.shield, combat.shield + combat.profile.shield_refresh);
        }
    }
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        // A spinning (dying) entity no longer fights.
        if (!combat_.states[index].profile.weapons.empty() && combat_.states[index].health > 0.0F)
        {
            update_target(index);
            aim_turret(index, dt);
            run_weapon(index);
        }
    }
    run_projectiles();
    remove_dead();
}

void SimulationWorld::aim_turret(const std::size_t index, const float dt)
{
    auto& combat = combat_.states[index];
    if (!combat.profile.turret)
    {
        return;
    }
    const auto& turret = *combat.profile.turret;
    float wanted = 0.0F;
    if (const auto victim = turret_victim(index))
    {
        const auto& to = placement_.positions[*victim];
        wanted = std::clamp(wrap_angle(std::atan2(to.y - placement_.positions[index].y, to.x - placement_.positions[index].x) - placement_.headings[index]), -turret.yaw_limit, turret.yaw_limit);
    }
    const auto step = turret.rate > 0.0F ? turret.rate * dt : pi;
    combat.turret_yaw = wrap_angle(combat.turret_yaw + std::clamp(wrap_angle(wanted - combat.turret_yaw), -step, step));
}

std::optional<std::size_t> SimulationWorld::turret_victim(const std::size_t index)
{
    const auto& combat = combat_.states[index];
    const auto target = attack_victim(index);
    for (std::size_t slot = 0; slot < combat.profile.weapons.size(); ++slot)
    {
        const auto& weapon = combat.profile.weapons[slot];
        if (weapon.hardpoint >= 0)
        {
            continue;
        }
        if (const auto& state = combat.weapons[slot]; state.pulses_left > 0)
        {
            if (const auto victim = dense_index(state.target))
            {
                return victim;
            }
        }
        // As run_weapon picks: the entity's target when the weapon reaches it, else (unless the
        // target was ordered or the stance holds fire) the nearest the weapon reaches.
        if (target && in_reach(index, weapon, *target))
        {
            return target;
        }
        if ((combat.ordered && target) || combat_.stances[index] == Stance::hold_fire)
        {
            return target;
        }
        if (const auto nearest = choose_target(index, placement_.positions[index], weapon.range,
                                               [&](const std::size_t candidate)
                                               {
                                                   return in_reach(index, weapon, candidate);
                                               }))
        {
            return nearest;
        }
        break;
    }
    return target;
}

bool SimulationWorld::in_reach(const std::size_t index, const WeaponProfile& weapon, const std::size_t victim) const noexcept
{
    const auto distance = distance_xy(placement_.positions[index], placement_.positions[victim]);
    return distance <= weapon.range && distance >= weapon.min_range && (weapon.excluded_categories & combat_.states[victim].profile.categories) == 0;
}

LocalPoint SimulationWorld::weapon_mount(const std::size_t index, const WeaponProfile& weapon, const LocalPoint& mount) const noexcept
{
    const auto& turret = combat_.states[index].profile.turret;
    if (!weapon.on_turret || !turret)
    {
        return mount;
    }
    const auto yaw = combat_.states[index].turret_yaw;
    const auto c = std::cos(yaw);
    const auto s = std::sin(yaw);
    const auto forward = mount.forward - turret->pivot.forward;
    const auto left = mount.left - turret->pivot.left;
    return {turret->pivot.forward + forward * c - left * s, turret->pivot.left + forward * s + left * c, mount.up};
}

bool SimulationWorld::turret_aimed(const std::size_t index, const std::size_t victim) const noexcept
{
    const auto& combat = combat_.states[index];
    if (!combat.profile.turret)
    {
        return true;
    }
    const auto& to = placement_.positions[victim];
    const auto bearing = std::atan2(to.y - placement_.positions[index].y, to.x - placement_.positions[index].x);
    return std::fabs(wrap_angle(bearing - (placement_.headings[index] + combat.turret_yaw))) <= turret_aim_tolerance;
}

void SimulationWorld::update_target(const std::size_t index)
{
    auto& combat = combat_.states[index];
    const auto range = combat.profile.range();
    if (combat.target.valid())
    {
        const auto victim = dense_index(combat.target);
        bool keep = victim && damageable(*victim) && combat_.states[*victim].profile.targetable && hostility_.hostile(identity_.owners[index], identity_.owners[*victim]);
        if (keep && !combat.ordered)
        {
            keep = distance_xy(placement_.positions[index], placement_.positions[*victim]) <= range;
        }
        if (!keep)
        {
            combat.target = {};
            combat.ordered = false;
        }
        // A target held past the stickiness is weighed against the others again.
        else if (!combat.ordered && combat.profile.stickiness_seconds > 0.0F && ++combat.target_ticks >= ticks_for(combat.profile.stickiness_seconds))
        {
            combat.target_ticks = 0;
            if (const auto best = choose_target(index, placement_.positions[index], range,
                                                [&](const std::size_t candidate)
                                                {
                                                    return engages(index, candidate);
                                                }))
            {
                combat.target = identity_.ids[*best];
            }
        }
    }
    // Ordered targets are kept (behavior chases them); automatic ones are searched every few ticks
    // unless the stance forbids picking targets.
    if (combat.target.valid() || combat_.stances[index] == Stance::hold_fire)
    {
        return;
    }
    if (combat.retarget_timer > 0)
    {
        --combat.retarget_timer;
        return;
    }
    combat.retarget_timer = retarget_interval;
    const auto best = choose_target(index, placement_.positions[index], range,
                                    [&](const std::size_t candidate)
                                    {
                                        return engages(index, candidate);
                                    });
    if (best)
    {
        combat.target = identity_.ids[*best];
        combat.target_ticks = 0;
    }
}

// Each weapon starts a burst at the entity's target when that target is within its range and arc,
// otherwise at the nearest damageable hostile within its own range and arc (ties to the lower id),
// so a ship's guns engage whatever their hardpoints face. A burst stays on its victim until its
// shots are spent.
void SimulationWorld::run_weapon(const std::size_t index)
{
    auto& combat = combat_.states[index];
    const auto entity_target = combat.target.valid() ? dense_index(combat.target) : std::nullopt;
    for (std::size_t slot = 0; slot < combat.profile.weapons.size(); ++slot)
    {
        const auto& weapon = combat.profile.weapons[slot];
        auto& state = combat.weapons[slot];
        if (weapon.hardpoint >= 0 && !hardpoint_alive(index, static_cast<std::size_t>(weapon.hardpoint)))
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
        if (combat_.aims_with_body[index] != 0 && route_.move_targets[index])
        {
            continue;
        }
        if (state.pulses_left == 0)
        {
            if (state.cooldown != 0)
            {
                continue;
            }
            const auto mount = world_point(index, weapon_mount(index, weapon, weapon.mount));
            const auto cone_direction = weapon.cone_direction + (weapon.on_turret ? combat.turret_yaw : 0.0F);
            const auto covers = [&](const std::size_t victim)
            {
                if (!in_reach(index, weapon, victim))
                {
                    return false;
                }
                if (combat_.aims_with_body[index] != 0)
                {
                    const auto& to = placement_.positions[victim];
                    const auto bearing = std::atan2(to.y - placement_.positions[index].y, to.x - placement_.positions[index].x);
                    if (std::fabs(wrap_angle(bearing - placement_.headings[index])) > body_aim_tolerance)
                    {
                        return false;
                    }
                }
                if (weapon.cone_half_angle >= pi)
                {
                    return true;
                }
                const auto& to = placement_.positions[victim];
                const auto bearing = std::atan2(to.y - mount.y, to.x - mount.x);
                return std::fabs(wrap_angle(bearing - (placement_.headings[index] + cone_direction))) <= weapon.cone_half_angle;
            };
            std::optional<std::size_t> victim;
            if (entity_target && covers(*entity_target))
            {
                victim = entity_target;
            }
            else if ((!combat.ordered || !entity_target) && combat_.stances[index] != Stance::hold_fire)
            {
                victim = choose_target(index, placement_.positions[index], weapon.range, covers);
            }
            if (!victim)
            {
                continue;
            }
            // A turret's own weapon waits until the turret points at its victim.
            if (weapon.hardpoint < 0 && !turret_aimed(index, *victim))
            {
                continue;
            }
            state.target = identity_.ids[*victim];
            state.pulses_left = std::max(1U, weapon.pulse_count);
            state.pulse_timer = ticks_for(weapon.appearance_delay_seconds);
            state.cooldown = std::max(1U, ticks_for(weapon.recharge_seconds));
        }
        // A burst keeps firing at its victim, in range or not, until its shots are spent.
        const auto victim = dense_index(state.target);
        if (!victim)
        {
            state.pulses_left = 0;
            continue;
        }
        if (combat_.aims_with_body[index] != 0)
        {
            const auto& to = placement_.positions[*victim];
            const auto bearing = std::atan2(to.y - placement_.positions[index].y, to.x - placement_.positions[index].x);
            if (std::fabs(wrap_angle(bearing - placement_.headings[index])) > body_aim_tolerance)
            {
                // Turned away mid-burst: the rest of the burst waits until it faces the victim again.
                continue;
            }
        }
        if (weapon.hardpoint < 0 && !turret_aimed(index, *victim))
        {
            // Turned away mid-burst: the rest waits until the turret is back on the victim.
            continue;
        }
        while (state.pulses_left > 0 && state.pulse_timer == 0)
        {
            const auto muzzle = state.shots % (weapon.extra_mounts.size() + 1U);
            ++state.shots;
            launch(index, *victim, weapon, weapon_mount(index, weapon, muzzle == 0 ? weapon.mount : weapon.extra_mounts[muzzle - 1U]));
            --state.pulses_left;
            state.pulse_timer = ticks_for(weapon.pulse_delay_seconds);
        }
    }
}

void SimulationWorld::launch(const std::size_t shooter, const std::size_t victim, const WeaponProfile& weapon, const LocalPoint& mount)
{
    ++shots_fired_;
    // The tick being run: the snapshot published at its end carries it as its tick.
    combat_.last_shot_ticks[shooter] = tick_ + 1U;
    const auto origin = world_point(shooter, mount);
    shots_.push_back({identity_.ids[shooter], origin, weapon.fire_sound, weapon.launch_effect});
    auto hardpoint = aim_hardpoint(victim, origin);
    if (weapon.random_hardpoints && hardpoint >= 0)
    {
        // Any intact, targetable part, drawn from the combat stream.
        std::vector<std::int32_t> parts;
        const auto& hardpoints = combat_.states[victim].profile.hardpoints;
        for (std::size_t part = 0; part < hardpoints.size(); ++part)
        {
            if (hardpoints[part].targetable && hardpoints[part].health > 0.0F && combat_.states[victim].hardpoint_health[part] > 0.0F)
            {
                parts.push_back(static_cast<std::int32_t>(part));
            }
        }
        hardpoint = parts[std::min(parts.size() - 1U, static_cast<std::size_t>(combat_random_.unit() * static_cast<float>(parts.size())))];
    }
    const auto offset = stray(weapon, victim);
    const bool misses = strays_off(victim, offset);
    if (weapon.projectile_speed <= 0.0F)
    {
        auto point = aim_point(victim, hardpoint);
        point = {point.x + offset.x, point.y + offset.y, point.z + offset.z};
        auto kind = ImpactKind::expired;
        EntityId struck;
        if (!misses)
        {
            ++hits_;
            kind = apply_damage(victim, weapon.damage, weapon.damage_type, weapon.damages_shield, weapon.damages_health, hardpoint);
            struck = identity_.ids[victim];
        }
        else if (const auto other = stray_victim(point, identity_.owners[shooter], victim))
        {
            ++hits_;
            kind = apply_damage(*other, weapon.damage, weapon.damage_type, weapon.damages_shield, weapon.damages_health, -1);
            struck = identity_.ids[*other];
        }
        const Vector3 delta{point.x - origin.x, point.y - origin.y, point.z - origin.z};
        const auto length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto direction = length > 0.0F ? Vector3{delta.x / length, delta.y / length, delta.z / length} : Vector3{};
        impacts_.push_back({point, direction, weapon.projectile_visual, kind, struck});
        area_blast(point, identity_.owners[shooter], weapon.blast, weapon.damage_type, weapon.damages_shield, weapon.damages_health);
        return;
    }
    Projectile projectile;
    projectile.position = origin;
    projectile.aim = aim_point(victim, hardpoint);
    projectile.target = identity_.ids[victim];
    projectile.target_hardpoint = hardpoint;
    projectile.owner = identity_.owners[shooter];
    projectile.damage = weapon.damage;
    projectile.damage_type = weapon.damage_type;
    projectile.damages_shield = weapon.damages_shield;
    projectile.damages_health = weapon.damages_health;
    projectile.blast = weapon.blast;
    projectile.lifetime_ticks = weapon.max_lifetime > 0.0F ? std::max(1U, ticks_for(weapon.max_lifetime)) : 0U;
    projectile.scan_range = weapon.scan_range;
    projectile.speed = weapon.projectile_speed;
    projectile.max_flight = weapon.max_flight_distance;
    projectile.visual = weapon.projectile_visual;
    projectile.serial = next_projectile_serial_++;
    projectile.stray = offset;
    projectile.misses = misses;
    projectile.aim = {projectile.aim.x + offset.x, projectile.aim.y + offset.y, projectile.aim.z + offset.z};
    projectiles_.push_back(projectile);
}

Vector3 SimulationWorld::stray(const WeaponProfile& weapon, const std::size_t victim)
{
    const auto categories = combat_.states[victim].profile.categories;
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
        const auto angle = combat_random_.unit() * two_pi;
        const auto radius = entry.distance * std::sqrt(combat_random_.unit());
        return {std::cos(angle) * radius, std::sin(angle) * radius, 0.0F};
    }
    return {};
}

bool SimulationWorld::strays_off(const std::size_t victim, const Vector3& stray) const noexcept
{
    // Without a size there is nothing to measure against, and the shot hits.
    const auto size = target_size(victim);
    return size > 0.0F && std::hypot(stray.x, stray.y) > size;
}

bool SimulationWorld::engages(const std::size_t index, const std::size_t candidate) const noexcept
{
    const auto& weapons = combat_.states[index].profile.weapons;
    const auto categories = combat_.states[candidate].profile.categories;
    return weapons.empty() || std::ranges::any_of(weapons,
                                                  [&](const WeaponProfile& weapon)
                                                  {
                                                      return (weapon.excluded_categories & categories) == 0;
                                                  });
}

float SimulationWorld::target_size(const std::size_t index) const noexcept
{
    // The separation footprint, else the obstacle footprint.
    auto size = placement_.radii[index];
    if (size <= 0.0F && footprint_.shapes[index])
    {
        size = std::max(footprint_.shapes[index]->half_length, footprint_.shapes[index]->half_width);
    }
    return size;
}

std::optional<std::size_t> SimulationWorld::stray_victim(const Vector3& point, const std::uint32_t owner, const std::size_t target) const noexcept
{
    // shortcut: a scan over every entity per stray shot; use the spatial grid when battles get big.
    std::optional<std::size_t> best;
    float best_distance = 0.0F;
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        if (index == target || !combat_.states[index].profile.collidable || !damageable(index) || !hostility_.hostile(owner, identity_.owners[index]))
        {
            continue;
        }
        const auto size = target_size(index);
        const auto distance = std::hypot(point.x - placement_.positions[index].x, point.y - placement_.positions[index].y);
        if (size > 0.0F && distance <= size && (!best || distance < best_distance))
        {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

Vector3 SimulationWorld::world_point(const std::size_t index, const LocalPoint& point) const noexcept
{
    const auto heading = placement_.headings[index];
    const auto cos_heading = std::cos(heading);
    const auto sin_heading = std::sin(heading);
    const auto& position = placement_.positions[index];
    return {position.x + point.forward * cos_heading - point.left * sin_heading, position.y + point.forward * sin_heading + point.left * cos_heading, position.z + point.up};
}

bool SimulationWorld::hardpoint_alive(const std::size_t index, const std::size_t hardpoint) const noexcept
{
    const auto& combat = combat_.states[index];
    return combat.profile.hardpoints[hardpoint].health <= 0.0F || combat.hardpoint_health[hardpoint] > 0.0F;
}

bool SimulationWorld::keeps(const std::size_t index, const HardpointProfile::Kind kind) const noexcept
{
    bool any = false;
    const auto& hardpoints = combat_.states[index].profile.hardpoints;
    for (std::size_t hardpoint = 0; hardpoint < hardpoints.size(); ++hardpoint)
    {
        if (hardpoints[hardpoint].kind == kind)
        {
            any = true;
            if (hardpoint_alive(index, hardpoint))
            {
                return true;
            }
        }
    }
    return !any;
}

std::int32_t SimulationWorld::aim_hardpoint(const std::size_t victim, const Vector3& from) const noexcept
{
    std::int32_t best = -1;
    float best_distance = 0.0F;
    const auto& hardpoints = combat_.states[victim].profile.hardpoints;
    for (std::size_t hardpoint = 0; hardpoint < hardpoints.size(); ++hardpoint)
    {
        // Indestructible parts are not aimed at; destroyed ones no longer are.
        if (!hardpoints[hardpoint].targetable || hardpoints[hardpoint].health <= 0.0F || combat_.states[victim].hardpoint_health[hardpoint] <= 0.0F)
        {
            continue;
        }
        const auto point = world_point(victim, hardpoints[hardpoint].offset);
        const auto distance = (point.x - from.x) * (point.x - from.x) + (point.y - from.y) * (point.y - from.y) + (point.z - from.z) * (point.z - from.z);
        if (best < 0 || distance < best_distance)
        {
            best = static_cast<std::int32_t>(hardpoint);
            best_distance = distance;
        }
    }
    return best;
}

Vector3 SimulationWorld::aim_point(const std::size_t victim, const std::int32_t hardpoint) const noexcept
{
    if (hardpoint >= 0)
    {
        return world_point(victim, combat_.states[victim].profile.hardpoints[static_cast<std::size_t>(hardpoint)].offset);
    }
    const auto& to = placement_.positions[victim];
    return {to.x, to.y, to.z + combat_.states[victim].profile.aim_height};
}

// The hull of an entity with destroyable parts follows them: max health x min(1, S/T + 0.2), where
// S/T is the parts' remaining share of their total health (recorded in the FoC debug build: every
// hull drop of a Nebulon-B under fire lies on this curve, and the hull never loses health to a shot
// at a part; EaW-Revised's S-45, S-49, S-50, docs/compatibility-research.md). Space ships were
// recorded; land objects with parts follow the same rule unobserved.
constexpr float hull_floor_share = 0.2F;

float SimulationWorld::hull_share_of_parts(const std::size_t index) const noexcept
{
    const auto& combat = combat_.states[index];
    float total = 0.0F;
    float remaining = 0.0F;
    for (std::size_t hardpoint = 0; hardpoint < combat.profile.hardpoints.size(); ++hardpoint)
    {
        if (combat.profile.hardpoints[hardpoint].health > 0.0F)
        {
            total += combat.profile.hardpoints[hardpoint].health;
            remaining += std::max(combat.hardpoint_health[hardpoint], 0.0F);
        }
    }
    return total > 0.0F ? std::min(1.0F, remaining / total + hull_floor_share) : 1.0F;
}

bool SimulationWorld::parts_all_destroyed(const std::size_t index) const noexcept
{
    const auto& combat = combat_.states[index];
    bool any = false;
    for (std::size_t hardpoint = 0; hardpoint < combat.profile.hardpoints.size(); ++hardpoint)
    {
        if (combat.profile.hardpoints[hardpoint].health > 0.0F)
        {
            any = true;
            if (combat.hardpoint_health[hardpoint] > 0.0F)
            {
                return false;
            }
        }
    }
    return any;
}

// Shields absorb a shot first (scaled by the shield armor type); what the shield cannot absorb,
// converted back to raw damage, is scaled by the armor type and reaches the part the shot aims at
// or, without one, the hull. A shot at a part damages only the part (recorded: 4000 scripted damage
// on a Nebulon-B laser silenced it for good and left the hull at 5400; S-19), and the hull then
// follows the parts; the entity dies with its last destroyable part unless its type says not to
// (recorded: the Nebulon-B dies with it at 0.35 or 0.23 of its hull). Damage aimed at no part
// (blasts, unaimed shots) reaches the hull directly, an engine choice: that case is not observed.
ImpactKind SimulationWorld::apply_damage(const std::size_t victim, float damage, const std::uint16_t damage_type, const bool damages_shield, const bool damages_health, const std::int32_t hardpoint)
{
    auto& combat = combat_.states[victim];
    if (damages_shield && combat.shield > 0.0F)
    {
        const auto modifier = damage_table_.modifier(damage_type, combat.profile.shield_armor_type);
        const auto dealt = damage * modifier;
        if (dealt <= combat.shield)
        {
            combat.shield -= dealt;
            return ImpactKind::shield;
        }
        damage = modifier > 0.0F ? (dealt - combat.shield) / modifier : 0.0F;
        combat.shield = 0.0F;
    }
    auto kind = ImpactKind::hull;
    if (damages_health)
    {
        const auto modifier = damage_table_.modifier(damage_type, combat.profile.armor_type);
        if (modifier < 1.0F)
        {
            kind = ImpactKind::hull_reduced;
        }
        const auto part_index = static_cast<std::size_t>(std::max(hardpoint, 0));
        const bool at_part = hardpoint >= 0 && combat.profile.hardpoints[part_index].health > 0.0F && combat.hardpoint_health[part_index] > 0.0F;
        if (!at_part)
        {
            (void)wound(victim, damage * modifier);
            return kind;
        }
        if (combat.invulnerable || combat.profile.immune)
        {
            return kind;
        }
        auto& part = combat.hardpoint_health[part_index];
        part -= damage * modifier;
        if (part <= 0.0F)
        {
            part = 0.0F;
            ++hardpoints_destroyed_;
            const auto& profile = combat.profile.hardpoints[part_index];
            hardpoint_deaths_.push_back(
                {identity_.ids[victim], static_cast<std::uint32_t>(part_index), world_point(victim, profile.offset), profile.death_sound, profile.death_effect, profile.type_key});
        }
        // The hull follows its parts, and falls with the last of them.
        const auto followed = combat.profile.health * hull_share_of_parts(victim);
        const auto lost = combat.profile.dies_with_hardpoints && parts_all_destroyed(victim) ? combat.health : std::max(combat.health - followed, 0.0F);
        (void)wound(victim, lost);
    }
    return kind;
}

float SimulationWorld::wound(const std::size_t index, const float damage)
{
    auto& combat = combat_.states[index];
    if (combat.invulnerable || combat.profile.immune || damage <= 0.0F)
    {
        return 0.0F;
    }
    auto health = combat.health - damage;
    if (combat.cannot_be_killed)
    {
        health = std::max(health, std::min(unkillable_health_floor, combat.profile.health * unkillable_health_fraction));
    }
    const auto dealt = combat.health - health;
    combat.health = health;
    return std::max(dealt, 0.0F);
}

void SimulationWorld::run_projectiles()
{
    const auto dt = static_cast<float>(tick_seconds_);
    std::size_t kept = 0;
    for (std::size_t index = 0; index < projectiles_.size(); ++index)
    {
        auto projectile = projectiles_[index];
        // Shots home on their target's current aim point; a dead target's shot flies on to the
        // last point and is lost there.
        auto victim = dense_index(projectile.target);
        if (!victim && projectile.scan_range > 0.0F)
        {
            // A guided shot takes the nearest hostile in scan range of itself (ties: the higher dense
            // index, the last one the scan meets; storage order, so the simulation rewrite settles
            // it, proposal F of the 2026-11 review; changing it changes the hash).
            float best = projectile.scan_range;
            for (std::size_t other = 0; other < identity_.ids.size(); ++other)
            {
                const auto& at = placement_.positions[other];
                const auto distance = std::sqrt((at.x - projectile.position.x) * (at.x - projectile.position.x) + (at.y - projectile.position.y) * (at.y - projectile.position.y) +
                                                (at.z - projectile.position.z) * (at.z - projectile.position.z));
                if (damageable(other) && combat_.states[other].profile.targetable && hostility_.hostile(projectile.owner, identity_.owners[other]) && distance <= best)
                {
                    victim = other;
                    best = distance;
                }
            }
            if (victim)
            {
                projectile.target = identity_.ids[*victim];
                projectile.target_hardpoint = -1;
                projectile.stray = {};
                projectile.misses = false;
            }
        }
        if (victim)
        {
            // A part destroyed meanwhile still takes the shot's aim; the hit lands on the hull.
            const auto aim = aim_point(*victim, projectile.target_hardpoint);
            projectile.aim = {aim.x + projectile.stray.x, aim.y + projectile.stray.y, aim.z + projectile.stray.z};
        }
        const Vector3 delta{projectile.aim.x - projectile.position.x, projectile.aim.y - projectile.position.y, projectile.aim.z - projectile.position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto step_length = projectile.speed * dt;
        if (distance > 0.0F)
        {
            projectile.direction = {delta.x / distance, delta.y / distance, delta.z / distance};
        }
        if (distance <= step_length)
        {
            if (victim && damageable(*victim) && !projectile.misses)
            {
                ++hits_;
                const auto kind = apply_damage(*victim, projectile.damage, projectile.damage_type, projectile.damages_shield, projectile.damages_health, projectile.target_hardpoint);
                impacts_.push_back({projectile.aim, projectile.direction, projectile.visual, kind, identity_.ids[*victim]});
            }
            else if (const auto other = projectile.misses ? stray_victim(projectile.aim, projectile.owner, victim.value_or(identity_.ids.size())) : std::nullopt)
            {
                ++hits_;
                const auto kind = apply_damage(*other, projectile.damage, projectile.damage_type, projectile.damages_shield, projectile.damages_health, -1);
                impacts_.push_back({projectile.aim, projectile.direction, projectile.visual, kind, identity_.ids[*other]});
            }
            else
            {
                // A shot that strays off its target, or whose target died, is lost at its aim point.
                impacts_.push_back({projectile.aim, projectile.direction, projectile.visual, ImpactKind::expired});
            }
            area_blast(projectile.aim, projectile.owner, projectile.blast, projectile.damage_type, projectile.damages_shield, projectile.damages_health);
            continue;
        }
        const auto scale = step_length / distance;
        projectile.position = {projectile.position.x + delta.x * scale, projectile.position.y + delta.y * scale, projectile.position.z + delta.z * scale};
        projectile.flown += step_length;
        const bool expired = projectile.lifetime_ticks > 0U && --projectile.lifetime_ticks == 0U;
        if ((projectile.max_flight > 0.0F && projectile.flown > projectile.max_flight) || expired)
        {
            impacts_.push_back({projectile.position, projectile.direction, projectile.visual, ImpactKind::expired});
            area_blast(projectile.position, projectile.owner, projectile.blast, projectile.damage_type, projectile.damages_shield, projectile.damages_health);
            continue;
        }
        projectiles_[kept++] = projectile;
    }
    projectiles_.resize(kept);
}

void SimulationWorld::area_blast(const Vector3& point, const std::uint32_t owner, const AreaBlast& blast, const std::uint16_t damage_type, const bool damages_shield, const bool damages_health)
{
    if ((blast.damage <= 0.0F && blast.friendly_damage <= 0.0F) || blast.range <= 0.0F)
    {
        return;
    }
    // shortcut: a scan over every entity per blast; use the spatial grid when battles get big.
    for (std::size_t index = 0; index < identity_.ids.size(); ++index)
    {
        const bool friendly = identity_.owners[index] == owner;
        if (!damageable(index) || (!hostility_.hostile(owner, identity_.owners[index]) && !(friendly && blast.friendly_damage > 0.0F)))
        {
            continue;
        }
        const auto& at = placement_.positions[index];
        const auto distance = std::max(0.0F, std::sqrt((at.x - point.x) * (at.x - point.x) + (at.y - point.y) * (at.y - point.y) + (at.z - point.z) * (at.z - point.z)) - target_size(index));
        if (distance > blast.range)
        {
            continue;
        }
        auto damage = friendly ? blast.friendly_damage : blast.damage;
        if (blast.tiers > 0)
        {
            const auto ring = std::min(static_cast<std::uint32_t>(distance / blast.range * static_cast<float>(blast.tiers)), blast.tiers - 1U);
            damage *= static_cast<float>(blast.tiers - ring) / static_cast<float>(blast.tiers);
        }
        (void)apply_damage(index, damage, damage_type, damages_shield, damages_health, -1);
    }
}

void SimulationWorld::remove_dead()
{
    // Back to front: swap-removal only moves already-visited entities.
    for (auto index = identity_.ids.size(); index-- > 0;)
    {
        auto& combat = combat_.states[index];
        if (combat.profile.health > 0.0F && combat.health <= 0.0F)
        {
            // Spinning away: out of control until the spin ends.
            if (combat.spin_ticks == 0 && combat.profile.spin_chance > 0.0F && combat_random_.unit() < combat.profile.spin_chance)
            {
                combat.spin_ticks = std::max(1U, ticks_for(combat.profile.spin_seconds));
                combat.target = {};
                combat.ordered = false;
                route_.move_targets[index].reset();
                route_.paths[index].clear();
                locomotor_.speed_limits[index] = 0.0F;
                group_.ids[index] = 0;
                orders_.standing[index].kind = StandingOrder::none;
                spins_.push_back({identity_.ids[index], placement_.positions[index]});
                continue;
            }
            if (combat.spin_ticks > 1U)
            {
                --combat.spin_ticks;
                continue;
            }
            const bool spun = combat.spin_ticks != 0;
            deaths_.push_back({identity_.ids[index], placement_.positions[index], placement_.headings[index], placement_.pitches[index], identity_.visuals[index], identity_.owners[index],
                               spun && combat.profile.spin_effect != 0 ? combat.profile.spin_effect : combat.profile.death_effect, combat.profile.type_key, combat.crushed});
            const auto position = placement_.positions[index];
            const auto owner = identity_.owners[index];
            const auto profile_blast = combat_.states[index].profile.death_blast;
            const auto blast_type = combat_.states[index].profile.death_blast_damage_type;
            const auto blast_shield = combat_.states[index].profile.death_blast_damages_shield;
            const auto blast_health = combat_.states[index].profile.death_blast_damages_health;
            destroy(index);
            ++kills_;
            // Entities it kills die in this pass when they come later in it, else the next tick.
            area_blast(position, owner, profile_blast, blast_type, blast_shield, blast_health);
        }
    }
}

} // namespace eaw
