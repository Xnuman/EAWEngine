#include "ecs/component.hpp"

#include "core/fatal_error.hpp"

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>

namespace eaw::ecs {

namespace {

// The process-wide component table, part of the type registry (AGENTS.md, process-wide services).
// A deque: component_type() hands out references that stay valid while more types register.
struct ComponentTable
{
    std::mutex lock;
    std::deque<ComponentType> types;
};

ComponentTable& table()
{
    static ComponentTable instance;
    return instance;
}

} // namespace

ComponentId register_component_type(const ComponentType& type)
{
    auto& components = table();
    const std::scoped_lock guard(components.lock);
    if (const auto found = std::ranges::find(components.types, type.type, &ComponentType::type); found != components.types.end())
    {
        return static_cast<ComponentId>(found - components.types.begin());
    }
    EAW_ASSERT_MSG(components.types.size() < max_components, "more component types than max_components: " + std::string(type.type.name()));
    components.types.push_back(type);
    return static_cast<ComponentId>(components.types.size() - 1);
}

const ComponentType& component_type(const ComponentId id)
{
    auto& components = table();
    const std::scoped_lock guard(components.lock);
    EAW_ASSERT_MSG(id < components.types.size(), "an unknown component id");
    return components.types[id];
}

std::optional<ComponentId> component_id(const rti::Type type)
{
    auto& components = table();
    const std::scoped_lock guard(components.lock);
    const auto found = std::ranges::find(components.types, type, &ComponentType::type);
    return found != components.types.end() ? std::optional(static_cast<ComponentId>(found - components.types.begin())) : std::nullopt;
}

} // namespace eaw::ecs
