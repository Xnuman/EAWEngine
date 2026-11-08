// Combat: shields, targeting, weapons, projectiles, damage, and removal of the dead.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

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
    return combat_[index].profile.health > 0.0F && combat_[index].health > 0.0F;
}

// Order within a tick: shields recharge, then every armed entity (in dense order) updates its
// target and fires, then shots fly and hit, then the dead are removed. Entities are only removed
// at the end, so dense indices stay valid throughout.
void SimulationWorld::run_combat()
{
    const auto dt = static_cast<float>(tick_seconds_);
    grid_.build(positions_, grid_cell_size);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        auto& combat = combat_[index];
        if (combat.shield < combat.profile.shield && combat.health > 0.0F && keeps(index, HardpointProfile::Kind::shield_generator))
        {
            combat.shield = std::min(combat.profile.shield, combat.shield + combat.profile.shield_regen_per_second * dt);
        }
    }
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        if (!combat_[index].profile.weapons.empty())
        {
            update_target(index);
            run_weapon(index);
        }
    }
    run_projectiles();
    remove_dead();
}

void SimulationWorld::update_target(const std::size_t index)
{
    auto& combat = combat_[index];
    const auto range = combat.profile.range();
    if (combat.target.valid())
    {
        const auto victim = dense_index(combat.target);
        bool keep = victim && damageable(*victim) && hostility_.hostile(owners_[index], owners_[*victim]);
        if (keep && !combat.ordered)
        {
            keep = distance_xy(positions_[index], positions_[*victim]) <= range;
        }
        if (!keep)
        {
            combat.target = {};
            combat.ordered = false;
        }
    }
    // Ordered targets are kept (behavior chases them); automatic ones are searched every few ticks
    // unless the stance forbids picking targets.
    if (combat.target.valid() || stances_[index] == Stance::hold_fire)
    {
        return;
    }
    if (combat.retarget_timer > 0)
    {
        --combat.retarget_timer;
        return;
    }
    combat.retarget_timer = retarget_interval;
    // The nearest damageable hostile in range; equal distances go to the lower entity id.
    query_.clear();
    grid_.query(positions_[index], range, query_);
    std::optional<std::size_t> best;
    float best_distance = 0.0F;
    for (const auto candidate : query_)
    {
        if (candidate == index || !damageable(candidate) || !hostility_.hostile(owners_[index], owners_[candidate]))
        {
            continue;
        }
        const auto distance = distance_xy(positions_[index], positions_[candidate]);
        const auto lower_id = [&](const std::size_t a, const std::size_t b)
        {
            return ids_[a].index != ids_[b].index ? ids_[a].index < ids_[b].index : ids_[a].generation < ids_[b].generation;
        };
        if (!best || distance < best_distance || (distance == best_distance && lower_id(candidate, *best)))
        {
            best = candidate;
            best_distance = distance;
        }
    }
    if (best)
    {
        combat.target = ids_[*best];
    }
}

// Each weapon starts a burst at the entity's target when that target is within its range and arc,
// otherwise at the nearest damageable hostile within its own range and arc (ties to the lower id),
// so a ship's guns engage whatever their hardpoints face. A burst stays on its victim until its
// shots are spent.
void SimulationWorld::run_weapon(const std::size_t index)
{
    auto& combat = combat_[index];
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
        if (aims_with_body_[index] != 0 && move_targets_[index])
        {
            continue;
        }
        if (state.pulses_left == 0)
        {
            if (state.cooldown != 0)
            {
                continue;
            }
            const auto mount = world_point(index, weapon.mount);
            const auto covers = [&](const std::size_t victim)
            {
                if (distance_xy(positions_[index], positions_[victim]) > weapon.range)
                {
                    return false;
                }
                if (aims_with_body_[index] != 0)
                {
                    const auto& to = positions_[victim];
                    const auto bearing = std::atan2(to.y - positions_[index].y, to.x - positions_[index].x);
                    if (std::fabs(wrap_angle(bearing - headings_[index])) > body_aim_tolerance)
                    {
                        return false;
                    }
                }
                if (weapon.cone_half_angle >= std::numbers::pi_v<float>)
                {
                    return true;
                }
                const auto& to = positions_[victim];
                const auto bearing = std::atan2(to.y - mount.y, to.x - mount.x);
                return std::fabs(wrap_angle(bearing - (headings_[index] + weapon.cone_direction))) <= weapon.cone_half_angle;
            };
            std::optional<std::size_t> victim;
            if (entity_target && covers(*entity_target))
            {
                victim = entity_target;
            }
            else if ((!combat.ordered || !entity_target) && stances_[index] != Stance::hold_fire)
            {
                query_.clear();
                grid_.query(positions_[index], weapon.range, query_);
                float best_distance = 0.0F;
                for (const auto candidate : query_)
                {
                    if (candidate == index || !damageable(candidate) || !hostility_.hostile(owners_[index], owners_[candidate]) || !covers(candidate))
                    {
                        continue;
                    }
                    const auto distance = distance_xy(positions_[index], positions_[candidate]);
                    const bool lower = victim && (ids_[candidate].index != ids_[*victim].index ? ids_[candidate].index < ids_[*victim].index : ids_[candidate].generation < ids_[*victim].generation);
                    if (!victim || distance < best_distance || (distance == best_distance && lower))
                    {
                        victim = candidate;
                        best_distance = distance;
                    }
                }
            }
            if (!victim)
            {
                continue;
            }
            state.target = ids_[*victim];
            state.pulses_left = std::max(1U, weapon.pulse_count);
            state.pulse_timer = 0;
            state.cooldown = std::max(1U, ticks_for(weapon.recharge_seconds));
        }
        // A burst keeps firing at its victim, in range or not, until its shots are spent.
        const auto victim = dense_index(state.target);
        if (!victim)
        {
            state.pulses_left = 0;
            continue;
        }
        if (aims_with_body_[index] != 0)
        {
            const auto& to = positions_[*victim];
            const auto bearing = std::atan2(to.y - positions_[index].y, to.x - positions_[index].x);
            if (std::fabs(wrap_angle(bearing - headings_[index])) > body_aim_tolerance)
            {
                // Turned away mid-burst: the rest of the burst waits until it faces the victim again.
                continue;
            }
        }
        while (state.pulses_left > 0 && state.pulse_timer == 0)
        {
            launch(index, *victim, weapon);
            --state.pulses_left;
            state.pulse_timer = ticks_for(weapon.pulse_delay_seconds);
        }
    }
}

void SimulationWorld::launch(const std::size_t shooter, const std::size_t victim, const WeaponProfile& weapon)
{
    ++shots_fired_;
    // The tick being run: the snapshot published at its end carries it as its tick.
    last_shot_ticks_[shooter] = tick_ + 1U;
    const auto origin = world_point(shooter, weapon.mount);
    const auto hardpoint = aim_hardpoint(victim, origin);
    if (weapon.projectile_speed <= 0.0F)
    {
        ++hits_;
        apply_damage(victim, weapon.damage, weapon.damage_type, weapon.damages_shield, weapon.damages_health, hardpoint);
        return;
    }
    Projectile projectile;
    projectile.position = origin;
    projectile.aim = aim_point(victim, hardpoint);
    projectile.target = ids_[victim];
    projectile.target_hardpoint = hardpoint;
    projectile.owner = owners_[shooter];
    projectile.damage = weapon.damage;
    projectile.damage_type = weapon.damage_type;
    projectile.damages_shield = weapon.damages_shield;
    projectile.damages_health = weapon.damages_health;
    projectile.speed = weapon.projectile_speed;
    projectile.max_flight = weapon.max_flight_distance;
    projectiles_.push_back(projectile);
}

SimVector3 SimulationWorld::world_point(const std::size_t index, const LocalPoint& point) const noexcept
{
    const auto heading = headings_[index];
    const auto cos_heading = std::cos(heading);
    const auto sin_heading = std::sin(heading);
    const auto& position = positions_[index];
    return {position.x + point.forward * cos_heading - point.left * sin_heading, position.y + point.forward * sin_heading + point.left * cos_heading, position.z + point.up};
}

bool SimulationWorld::hardpoint_alive(const std::size_t index, const std::size_t hardpoint) const noexcept
{
    const auto& combat = combat_[index];
    return combat.profile.hardpoints[hardpoint].health <= 0.0F || combat.hardpoint_health[hardpoint] > 0.0F;
}

bool SimulationWorld::keeps(const std::size_t index, const HardpointProfile::Kind kind) const noexcept
{
    bool any = false;
    const auto& hardpoints = combat_[index].profile.hardpoints;
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

std::int32_t SimulationWorld::aim_hardpoint(const std::size_t victim, const SimVector3& from) const noexcept
{
    std::int32_t best = -1;
    float best_distance = 0.0F;
    const auto& hardpoints = combat_[victim].profile.hardpoints;
    for (std::size_t hardpoint = 0; hardpoint < hardpoints.size(); ++hardpoint)
    {
        // Indestructible parts are not aimed at; destroyed ones no longer are.
        if (!hardpoints[hardpoint].targetable || hardpoints[hardpoint].health <= 0.0F || combat_[victim].hardpoint_health[hardpoint] <= 0.0F)
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

SimVector3 SimulationWorld::aim_point(const std::size_t victim, const std::int32_t hardpoint) const noexcept
{
    if (hardpoint >= 0)
    {
        return world_point(victim, combat_[victim].profile.hardpoints[static_cast<std::size_t>(hardpoint)].offset);
    }
    const auto& to = positions_[victim];
    return {to.x, to.y, to.z + combat_[victim].profile.aim_height};
}

// Provisional semantics: shields absorb a shot first (scaled by the shield armor type); what the
// shield cannot absorb, converted back to raw damage, reaches the hull scaled by the armor type.
void SimulationWorld::apply_damage(const std::size_t victim, float damage, const std::uint16_t damage_type, const bool damages_shield, const bool damages_health, const std::int32_t hardpoint)
{
    auto& combat = combat_[victim];
    if (damages_shield && combat.shield > 0.0F)
    {
        const auto modifier = damage_table_.modifier(damage_type, combat.profile.shield_armor_type);
        const auto dealt = damage * modifier;
        if (dealt <= combat.shield)
        {
            combat.shield -= dealt;
            return;
        }
        damage = modifier > 0.0F ? (dealt - combat.shield) / modifier : 0.0F;
        combat.shield = 0.0F;
    }
    if (damages_health)
    {
        const auto dealt = damage * damage_table_.modifier(damage_type, combat.profile.armor_type);
        combat.health -= dealt;
        // A shot at a hardpoint damages it too (provisional: the hull and the hit part share the
        // damage; the original's split is not established).
        if (hardpoint >= 0)
        {
            auto& part = combat.hardpoint_health[static_cast<std::size_t>(hardpoint)];
            if (part > 0.0F && (part -= dealt) <= 0.0F)
            {
                part = 0.0F;
                ++hardpoints_destroyed_;
            }
        }
    }
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
        const auto victim = dense_index(projectile.target);
        if (victim)
        {
            // A part destroyed meanwhile still takes the shot's aim; the hit lands on the hull.
            projectile.aim = aim_point(*victim, projectile.target_hardpoint);
        }
        const SimVector3 delta{projectile.aim.x - projectile.position.x, projectile.aim.y - projectile.position.y, projectile.aim.z - projectile.position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto step_length = projectile.speed * dt;
        if (distance > 0.0F)
        {
            projectile.direction = {delta.x / distance, delta.y / distance, delta.z / distance};
        }
        if (distance <= step_length)
        {
            if (victim && damageable(*victim))
            {
                ++hits_;
                apply_damage(*victim, projectile.damage, projectile.damage_type, projectile.damages_shield, projectile.damages_health, projectile.target_hardpoint);
            }
            continue;
        }
        const auto scale = step_length / distance;
        projectile.position = {projectile.position.x + delta.x * scale, projectile.position.y + delta.y * scale, projectile.position.z + delta.z * scale};
        projectile.flown += step_length;
        if (projectile.max_flight > 0.0F && projectile.flown > projectile.max_flight)
        {
            continue;
        }
        projectiles_[kept++] = projectile;
    }
    projectiles_.resize(kept);
}

void SimulationWorld::remove_dead()
{
    // Back to front: swap-removal only moves already-visited entities.
    for (auto index = ids_.size(); index-- > 0;)
    {
        if (combat_[index].profile.health > 0.0F && combat_[index].health <= 0.0F)
        {
            deaths_.push_back({ids_[index], positions_[index], headings_[index], pitches_[index], visuals_[index], owners_[index]});
            destroy(index);
            ++kills_;
        }
    }
}

} // namespace eaw
