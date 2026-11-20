#include "ecs/entity_manager.hpp"

#include "core/profile.hpp"

#include <algorithm>

namespace eaw::ecs {

Entity EntityManager::create(const ComponentMask& mask)
{
    const auto entity = table_.create();
    auto& command = command_for(entity);
    command.mask = mask;
    return entity;
}

void EntityManager::destroy(const Entity entity)
{
    EAW_ASSERT_MSG(alive(entity), "an entity removed is alive");
    command_for(entity).destroy = true;
}

rti::Variant& EntityManager::add_component(const Entity entity, const ComponentId id)
{
    EAW_ASSERT_MSG(alive(entity), "an entity given a component is alive");
    auto& command = command_for(entity);
    command.mask.set(id);
    const auto found = std::ranges::find(command.values, id, &std::pair<ComponentId, rti::Variant>::first);
    if (found != command.values.end())
    {
        return found->second;
    }
    return command.values.emplace_back(id, component_type(id).make()).second;
}

void EntityManager::add_components(const Entity entity, const ComponentMask& mask)
{
    EAW_ASSERT_MSG(alive(entity), "an entity given components is alive");
    command_for(entity).mask |= mask;
}

void EntityManager::remove_components(const Entity entity, const ComponentMask& mask)
{
    EAW_ASSERT_MSG(alive(entity), "an entity losing components is alive");
    auto& command = command_for(entity);
    command.mask &= ~mask;
    std::erase_if(command.values,
                  [&](const std::pair<ComponentId, rti::Variant>& value)
                  {
                      return mask.test(value.first);
                  });
}

const ComponentMask* EntityManager::mask(const Entity entity) const noexcept
{
    const auto* location = table_.location(entity);
    return location != nullptr && location->stored() ? &table_.archetype(location->archetype).mask() : nullptr;
}

bool EntityManager::has(const Entity entity, const ComponentMask& wanted) const noexcept
{
    const auto* current = mask(entity);
    return current != nullptr && (*current & wanted) == wanted;
}

void* EntityManager::component(const Entity entity, const ComponentId id) noexcept
{
    const auto* location = table_.location(entity);
    if (location == nullptr || !location->stored())
    {
        return nullptr;
    }
    auto& archetype = table_.archetype(location->archetype);
    const auto column = archetype.column(id);
    return column ? archetype.component(*column, location->row) : nullptr;
}

void EntityManager::process()
{
    EAW_PROFILE_ZONE("ecs commands");
    for (const auto& view : init_views_)
    {
        view->reset();
    }
    // Updates first, removals after (Singe's order), each in the order entities were first changed.
    for (auto& command : commands_)
    {
        if (!command.destroy)
        {
            apply_update(command);
        }
    }
    for (const auto& command : commands_)
    {
        if (command.destroy)
        {
            apply_destroy(command);
        }
    }
    for (const auto& command : commands_)
    {
        pending_[command.entity.index] = 0;
    }
    commands_.clear();
}

bool EntityManager::empty() const noexcept
{
    for (std::size_t index = 0; index < table_.archetype_count(); ++index)
    {
        if (!table_.archetype(index).empty())
        {
            return false;
        }
    }
    return true;
}

EntityManager::Command& EntityManager::command_for(const Entity entity)
{
    if (entity.index >= pending_.size())
    {
        pending_.resize(entity.index + 1U, 0);
    }
    if (const auto pending = pending_[entity.index]; pending != 0)
    {
        return commands_[pending - 1];
    }
    const auto* current = mask(entity);
    commands_.push_back({entity, current != nullptr ? *current : ComponentMask{}, false, {}});
    pending_[entity.index] = static_cast<std::uint32_t>(commands_.size());
    return commands_.back();
}

std::size_t EntityManager::archetype_for(const ComponentMask& mask)
{
    if (const auto found = table_.find_archetype(mask))
    {
        return *found;
    }
    const auto index = table_.add_archetype(mask);
    for (const auto& view : iter_views_)
    {
        if (view->accepts(mask))
        {
            view->bind(table_.archetype(index));
        }
    }
    return index;
}

void EntityManager::apply_update(Command& command)
{
    if (!table_.alive(command.entity))
    {
        return;
    }
    const auto location = *table_.location(command.entity);
    ComponentMask before;
    if (!location.stored())
    {
        const auto index = archetype_for(command.mask);
        const auto row = table_.archetype(index).push(command.entity, command.values);
        table_.place(command.entity, {static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(row)});
    }
    else
    {
        auto& from = table_.archetype(location.archetype);
        before = from.mask();
        if (before == command.mask)
        {
            // Same components: given values replace the stored ones.
            for (auto& [id, value] : command.values)
            {
                if (value)
                {
                    const auto& type = component_type(id);
                    auto* at = from.component(*from.column(id), location.row);
                    type.destroy(at);
                    type.construct_from(at, std::move(value));
                }
            }
            return;
        }
        const auto index = archetype_for(command.mask);
        std::optional<Entity> moved;
        const auto row = Archetype::transfer(table_.archetype(location.archetype), location.row, table_.archetype(index), command.values, moved);
        table_.place(command.entity, {static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(row)});
        moved_into(location.archetype, location.row, moved);
    }
    const auto added = command.mask & ~before;
    for (const auto& view : init_views_)
    {
        if (view->takes(command.mask, added))
        {
            view->add(command.entity);
        }
    }
}

void EntityManager::apply_destroy(const Command& command)
{
    if (!table_.alive(command.entity))
    {
        return;
    }
    const auto location = *table_.location(command.entity);
    if (location.stored())
    {
        moved_into(location.archetype, location.row, table_.archetype(location.archetype).erase(location.row));
    }
    table_.release(command.entity);
}

void EntityManager::moved_into(const std::size_t archetype, const std::size_t row, const std::optional<Entity>& moved) noexcept
{
    if (moved)
    {
        table_.place(*moved, {static_cast<std::uint32_t>(archetype), static_cast<std::uint32_t>(row)});
    }
}

} // namespace eaw::ecs
