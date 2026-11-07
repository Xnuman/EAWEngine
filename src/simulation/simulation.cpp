#include "eaw/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace eaw {
namespace {

constexpr std::uint32_t no_dense = std::numeric_limits<std::uint32_t>::max();
// Arrival tolerance in world units.
constexpr float arrival_distance = 0.01F;

bool finite(const SimVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::uint64_t fnv1a(std::uint64_t hash, const void* data, const std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

template <class T> std::uint64_t hash_value(const std::uint64_t hash, const T& value) noexcept
{
    return fnv1a(hash, &value, sizeof(value));
}

float wrap_angle(float angle) noexcept
{
    constexpr auto two_pi = 2.0F * std::numbers::pi_v<float>;
    angle = std::fmod(angle, two_pi);
    if (angle > std::numbers::pi_v<float>)
    {
        angle -= two_pi;
    }
    else if (angle < -std::numbers::pi_v<float>)
    {
        angle += two_pi;
    }
    return angle;
}

} // namespace

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
    if (!finite(spawn.position) || !std::isfinite(spawn.heading) || !std::isfinite(spawn.max_speed) || spawn.max_speed < 0.0F)
    {
        return make_error("entity spawn '" + spawn.type_name + "' has a non-finite position, heading, or speed");
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
    // Commands first, in submission order, so the tick sees a consistent intent; then systems.
    auto commands = std::move(pending_);
    pending_.clear();
    for (const auto& command : commands)
    {
        apply(command);
    }
    run_movement();
    ++tick_;
    publish();
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
    return {tick_, ids_.size(), commands_applied_, stale_references_, blocked_moves_};
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
        hash = hash_value(hash, visuals_[index]);
        hash = hash_value(hash, selectable_[index]);
        hash = hash_value(hash, follows_ground_[index]);
        hash = hash_value(hash, passable_types_[index]);
    }
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

void SimulationWorld::apply(const SimCommand& command)
{
    ++commands_applied_;
    if (command.kind == SimCommandKind::move && !finite(command.target))
    {
        ++stale_references_;
        return;
    }
    for (const auto id : command.entities)
    {
        const auto dense = dense_index(id);
        if (!dense)
        {
            ++stale_references_;
            continue;
        }
        switch (command.kind)
        {
        case SimCommandKind::move:
            if (max_speeds_[*dense] > 0.0F)
            {
                move_targets_[*dense] = command.target;
            }
            break;
        case SimCommandKind::stop:
            move_targets_[*dense].reset();
            break;
        case SimCommandKind::destroy:
            destroy(*dense);
            break;
        }
    }
}

void SimulationWorld::destroy(const std::size_t dense)
{
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
    slot_dense_[slot] = no_dense;
    // A slot whose generation would wrap is retired instead of reused.
    if (generations_[slot] != std::numeric_limits<std::uint32_t>::max())
    {
        ++generations_[slot];
        free_slots_.push_back(slot);
    }
}

void SimulationWorld::run_movement()
{
    const auto dt = static_cast<float>(tick_seconds_);
    for (std::size_t index = 0; index < ids_.size(); ++index)
    {
        auto& target = move_targets_[index];
        if (!target)
        {
            continue;
        }
        auto& position = positions_[index];
        if (follows_ground_[index] != 0 && ground_.valid())
        {
            move_over_ground(index, dt);
            continue;
        }
        const SimVector3 delta{target->x - position.x, target->y - position.y, target->z - position.z};
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const auto step_length = max_speeds_[index] * dt;
        if (distance <= std::max(step_length, arrival_distance))
        {
            position = *target;
            target.reset();
            continue;
        }
        const auto scale = step_length / distance;
        position = {position.x + delta.x * scale, position.y + delta.y * scale, position.z + delta.z * scale};
        // Instant turn toward the direction of travel; turn rates come with the movement model.
        if (delta.x != 0.0F || delta.y != 0.0F)
        {
            headings_[index] = std::atan2(delta.y, delta.x);
        }
    }
}

void SimulationWorld::move_over_ground(const std::size_t index, const float dt)
{
    auto& target = move_targets_[index];
    auto& position = positions_[index];
    // Ground units move in the horizontal plane; their height always comes from the surface.
    const auto delta_x = target->x - position.x;
    const auto delta_y = target->y - position.y;
    const auto distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    const auto step_length = max_speeds_[index] * dt;
    const bool arrives = distance <= std::max(step_length, arrival_distance);
    const auto scale = arrives || distance <= 0.0F ? 1.0F : step_length / distance;
    const auto next_x = position.x + delta_x * scale;
    const auto next_y = position.y + delta_y * scale;
    if (const auto allowed = passable_types_[index]; allowed != 0)
    {
        const auto here = ground_.passability_at(position.x, position.y);
        const auto there = ground_.passability_at(next_x, next_y);
        const bool leaving_blocked = here && (*here & allowed) == 0;
        if (there && (*there & allowed) == 0 && !leaving_blocked)
        {
            // No pathfinding yet: the unit stops at the edge of a blocked cell.
            target.reset();
            ++blocked_moves_;
            return;
        }
    }
    position = {next_x, next_y, ground_.surface_height(next_x, next_y)};
    if (delta_x != 0.0F || delta_y != 0.0F)
    {
        headings_[index] = std::atan2(delta_y, delta_x);
    }
    if (arrives)
    {
        target.reset();
    }
}

void SimulationWorld::set_ground(GroundMap ground)
{
    ground_ = std::move(ground);
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
        snapshot_.entities.push_back({ids_[index], positions_[index], headings_[index], visuals_[index], owners_[index], move_targets_[index].has_value(), selectable_[index] != 0, max_speeds_[index],
                                      follows_ground_[index] != 0});
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
        }
        result.push_back(blended);
    }
    return result;
}

} // namespace eaw
