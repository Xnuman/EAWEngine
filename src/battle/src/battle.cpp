#include "battle/battle.hpp"

#include "core/log.hpp"
#include "core/string_utils.hpp"
#include "simulation/formation.hpp"
#include "simulation/pathfinding.hpp"
#include "battle/combat_content.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eaw {
namespace {

// Spawn orders are limited per order, like the --spawn option.
constexpr std::uint32_t max_spawn_count = 1000;
// Spacing of spawned single units and of spawned groups (companies, squadrons), in world units.
constexpr float unit_spacing = 40.0F;
constexpr float group_spacing = 160.0F;

bool finite_point(const Vector3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool has_whitespace(const std::string& text) noexcept
{
    return std::ranges::any_of(text,
                               [](const char character)
                               {
                                   return ascii_is_space(character);
                               });
}

} // namespace

Vector3 open_ground(const GroundMap& ground, Vector3 point, const std::uint32_t passable_types)
{
    if (!ground.valid())
    {
        return point;
    }
    point.x = std::clamp(point.x, 0.0F, static_cast<float>(ground.width - 1U) * ground.spacing);
    point.y = std::clamp(point.y, 0.0F, static_cast<float>(ground.height - 1U) * ground.spacing);
    if (const auto open = nearest_enterable(ground, {point.x, point.y}, passable_types))
    {
        point.x = open->x;
        point.y = open->y;
    }
    return point;
}

std::vector<Vector3> grid_targets(const Vector3& target, const std::size_t count, const float spacing)
{
    std::vector<Vector3> result;
    if (count == 0)
    {
        return result;
    }
    const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const auto rows = (count + columns - 1U) / columns;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto column = static_cast<float>(index % columns) - static_cast<float>(columns - 1U) * 0.5F;
        const auto row = static_cast<float>(index / columns) - static_cast<float>(rows - 1U) * 0.5F;
        result.push_back({target.x + column * spacing, target.y + row * spacing, target.z});
    }
    return result;
}

BattleMap battle_map(const BattleMapAsset& asset)
{
    BattleMap map;
    map.entities = asset.entities;
    map.spawnable = asset.spawnable;
    map.ground = &asset.ground;
    map.damage_table = asset.damage_table;
    map.hostility = provisional_map_hostility();
    return map;
}

Battle::Battle(const BattleMap& map, std::string map_name, const double tick_seconds) : world_(tick_seconds), spawnable_(map.spawnable.begin(), map.spawnable.end())
{
    log_.map = std::move(map_name);
    if (map.ground != nullptr)
    {
        world_.set_ground(*map.ground);
    }
    world_.set_damage_table(map.damage_table);
    world_.set_hostility(map.hostility);
    map_ids_.reserve(map.entities.size());
    for (const auto& entity : map.entities)
    {
        auto id = world_.spawn(entity.spawn);
        if (!id)
        {
            // Map data: one bad placement does not stop the rest of the map.
            log_warning("simulation", with_context("skipping map entity", id.error()));
            map_ids_.emplace_back();
            continue;
        }
        map_ids_.emplace_back(*id);
        if (!entity.hint.empty())
        {
            hinted_.emplace_back(*id, entity.hint);
        }
        type_info_.try_emplace(ascii_lower(entity.spawn.type_name), entity.script);
        next_team_ = std::max(next_team_, entity.spawn.formation_team.id + 1U);
        if (!entity.survivors.types.empty())
        {
            survivors_.try_emplace(game_object_name_crc(entity.spawn.type_name), entity.survivors);
        }
    }
    for (const auto& type : spawnable_)
    {
        type_info_.try_emplace(ascii_lower(type.type_name), type.script);
        if (!type.survivors.types.empty())
        {
            survivors_.try_emplace(game_object_name_crc(type.type_name), type.survivors);
        }
    }
    world_.step();
}

std::span<const std::pair<EntityId, std::string>> Battle::hinted_entities() const noexcept
{
    return hinted_;
}

const ScriptTypeInfo* Battle::type_info(const std::string_view type_name) const
{
    const auto found = type_info_.find(ascii_lower(type_name));
    return found != type_info_.end() ? &found->second : nullptr;
}

SimulationWorld& Battle::world() noexcept
{
    return world_;
}

const SimulationWorld& Battle::world() const noexcept
{
    return world_;
}

std::span<const std::optional<EntityId>> Battle::map_entity_ids() const noexcept
{
    return map_ids_;
}

std::span<const SpawnableType> Battle::spawnable() const noexcept
{
    return spawnable_;
}

std::size_t Battle::add_spawnable(const std::span<const SpawnableType> types)
{
    const auto first = spawnable_.size();
    for (auto type : types)
    {
        for (auto& member : type.members)
        {
            member += first;
        }
        type_info_.try_emplace(ascii_lower(type.type_name), type.script);
        if (!type.survivors.types.empty())
        {
            survivors_.try_emplace(game_object_name_crc(type.type_name), type.survivors);
        }
        spawnable_.push_back(std::move(type));
    }
    return first;
}

const BattleLog& Battle::log() const noexcept
{
    return log_;
}

Result<std::uint64_t> Battle::submit(const BattleCommand& command, const BattleOrigin origin)
{
    EAW_TRY_ASSIGN(auto translated, translate(command));
    const auto serial = next_serial_++;
    translated.serial = serial;
    world_.submit(std::move(translated));
    if (origin == BattleOrigin::player)
    {
        log_.entries.push_back({world_.stats().tick, command});
    }
    return serial;
}

void Battle::step()
{
    world_.step();
    if (survivors_.empty())
    {
        return;
    }
    // Copied: submitting does not touch the snapshot, but stay clear of aliasing it.
    const auto deaths = world_.snapshot().deaths;
    for (const auto& death : deaths)
    {
        const auto found = survivors_.find(death.type_key);
        if (found == survivors_.end())
        {
            continue;
        }
        for (const auto& type : found->second.types)
        {
            if (const auto spawned = submit({.order = BattleOrder::spawn, .point = death.position, .type = type, .count = found->second.count, .owner = death.owner}, BattleOrigin::script); !spawned)
            {
                log_warning("battle", with_context("survivor " + type + " of a destroyed object not spawned", spawned.error()));
            }
        }
    }
}

EntitySpawn Battle::spawn_of(const std::size_t type_index, Vector3 position, const std::uint32_t owner) const
{
    const auto& type = spawnable_[type_index];
    const auto& ground = world_.ground();
    const bool follows_ground = type.follows_ground && ground.valid();
    // Never inside a building, a wall, or off the map (its stamped footprints count).
    if (follows_ground)
    {
        position = open_ground(ground, position, type.passable_types);
    }
    if (ground.valid())
    {
        position.z = ground.surface_height(position.x, position.y) + (type.follows_ground ? 0.0F : type.hover_offset);
    }
    EntitySpawn spawn;
    spawn.type_name = type.type_name;
    spawn.owner = owner;
    spawn.position = position;
    spawn.max_speed = type.max_speed;
    spawn.visual = spawnable_visual(type_index);
    spawn.follows_ground = follows_ground;
    spawn.passable_types = follows_ground ? type.passable_types : 0U;
    spawn.combat = type.combat;
    spawn.radius = type.radius;
    spawn.turn_rate = type.turn_rate;
    spawn.movement = type.movement;
    spawn.min_speed = type.min_speed;
    spawn.aims_with_body = follows_ground && type.aims_with_body;
    spawn.formation_order = type.formation_order;
    spawn.formation_spacing = type.formation_spacing;
    spawn.formation_solo = type.formation_solo;
    spawn.crush = type.crush;
    return spawn;
}

Result<SimCommand> Battle::translate(const BattleCommand& command) const
{
    if (!finite_point(command.point))
    {
        return make_error("battle order has a non-finite point");
    }
    SimCommand translated;
    if (command.order == BattleOrder::spawn)
    {
        const auto found = std::ranges::find(spawnable_, command.type, &SpawnableType::type_name);
        if (found == spawnable_.end())
        {
            return make_error("spawn order names '" + command.type + "', which is not a spawnable type of this battle");
        }
        if (has_whitespace(command.type))
        {
            return make_error("spawn order names '" + command.type + "', which contains whitespace and cannot be recorded");
        }
        if (command.count == 0 || command.count > max_spawn_count)
        {
            return make_error("spawn orders need a count from 1 to " + std::to_string(max_spawn_count));
        }
        if (command.owner >= Hostility::max_owners)
        {
            return make_error("spawn order owner " + std::to_string(command.owner) + " is not below " + std::to_string(Hostility::max_owners));
        }
        const auto type_index = static_cast<std::size_t>(found - spawnable_.begin());
        translated.kind = SimCommandKind::spawn;
        if (found->members.empty())
        {
            for (const auto& spot : grid_targets(command.point, command.count, unit_spacing))
            {
                translated.spawns.push_back(spawn_of(type_index, spot, command.owner));
            }
            return translated;
        }
        // A company or squadron: its members in formation around each group center, facing +X.
        for (const auto& center : grid_targets(command.point, command.count, group_spacing))
        {
            std::vector<FormationMember> members;
            std::vector<float> raggedness;
            for (std::size_t member = 0; member < found->members.size(); ++member)
            {
                const auto& type = spawnable_[found->members[member]];
                members.push_back({EntityId{static_cast<std::uint32_t>(member + 1U), 1}, center, type.radius, type.formation_spacing, type.formation_order});
                raggedness.push_back(type.formation_raggedness);
            }
            assign_teams(members, raggedness, found->team_layout, next_team_);
            const auto slots = formation_slots(members, center, 0.0F);
            for (std::size_t member = 0; member < found->members.size(); ++member)
            {
                translated.spawns.push_back(spawn_of(found->members[member], slots[member].position, command.owner));
                apply_team_movement(found->team, translated.spawns.back());
                translated.spawns.back().formation_team = members[member].team;
            }
        }
        return translated;
    }
    if (command.entities.empty())
    {
        return make_error("battle order names no entities");
    }
    translated.entities = command.entities;
    switch (command.order)
    {
    case BattleOrder::move:
        translated.kind = SimCommandKind::formation_move;
        translated.target = command.point;
        break;
    case BattleOrder::attack:
        translated.kind = SimCommandKind::attack;
        translated.target_entity = command.target;
        break;
    case BattleOrder::stop:
        translated.kind = SimCommandKind::stop;
        break;
    case BattleOrder::set_stance:
        if (command.stance != Stance::aggressive && command.stance != Stance::hold_position && command.stance != Stance::hold_fire)
        {
            return make_error("battle order names an unknown stance");
        }
        translated.kind = SimCommandKind::set_stance;
        translated.stance = command.stance;
        break;
    case BattleOrder::remove:
        translated.kind = SimCommandKind::destroy;
        break;
    case BattleOrder::attack_move:
    case BattleOrder::guard:
        translated.kind = command.order == BattleOrder::attack_move ? SimCommandKind::attack_move : SimCommandKind::guard;
        translated.target = command.point;
        translated.target_entity = command.target;
        break;
    case BattleOrder::set_invulnerable:
    case BattleOrder::set_cannot_be_killed:
    case BattleOrder::suspend_locomotor:
        translated.kind = command.order == BattleOrder::set_invulnerable       ? SimCommandKind::set_invulnerable
                          : command.order == BattleOrder::set_cannot_be_killed ? SimCommandKind::set_cannot_be_killed
                                                                               : SimCommandKind::suspend_locomotor;
        translated.flag = command.flag;
        break;
    case BattleOrder::damage:
        if (!std::isfinite(command.amount) || command.amount < 0.0F)
        {
            return make_error("battle order damage needs a finite, non-negative amount");
        }
        translated.kind = SimCommandKind::damage;
        translated.amount = command.amount;
        break;
    case BattleOrder::teleport:
        if (command.heading && !std::isfinite(*command.heading))
        {
            return make_error("battle order teleport has a non-finite heading");
        }
        translated.kind = SimCommandKind::teleport;
        translated.target = command.point;
        translated.heading = command.heading;
        break;
    case BattleOrder::spawn:
        break;
    }
    return translated;
}

} // namespace eaw
