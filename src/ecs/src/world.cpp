#include "ecs/world.hpp"

#include "core/fatal_error.hpp"
#include "core/log.hpp"
#include "core/profile.hpp"

#include "ecs/singleton.hpp"
#include "rti/order.hpp"
#include "rti/registration.hpp"

#include <algorithm>

EAW_RTI_REGISTRATION
{
    using eaw::ecs::World;
    eaw::rti::register_type<World::Snapshot::Entity>().property("name", &World::Snapshot::Entity::name).property("components", &World::Snapshot::Entity::components);
    eaw::rti::register_type<World::Snapshot>().property("entities", &World::Snapshot::entities).property("systems", &World::Snapshot::systems).property("singletons", &World::Snapshot::singletons);
}

namespace eaw::ecs {

World::~World() = default;

Entity World::create_entity(const ComponentMask& mask, const std::string_view name)
{
    const auto entity = entities_.create(mask);
    if (entity.index >= names_.size())
    {
        names_.resize(entity.index + 1U);
    }
    names_[entity.index] = std::string(name);
    return entity;
}

void World::destroy_entity(const Entity entity)
{
    entities_.destroy(entity);
}

void World::rename_entity(const Entity entity, const std::string_view name)
{
    EAW_ASSERT_MSG(entities_.alive(entity), "a renamed entity is alive");
    names_[entity.index] = std::string(name);
}

std::string_view World::entity_name(const Entity entity) const
{
    return entities_.alive(entity) ? std::string_view(names_[entity.index]) : std::string_view{};
}

void World::add_system(const rti::TypeInfo& type)
{
    const auto* add = type.meta(ISystem::meta_add);
    EAW_ASSERT_MSG(add != nullptr, std::string("a system added by type is registered as one: ") + std::string(type.name()));
    add->get<ISystem::AddCallback>()(*this);
}

void World::add_singleton(const rti::TypeInfo& type)
{
    const auto* add = type.meta(meta_singleton_add);
    EAW_ASSERT_MSG(add != nullptr, std::string("a singleton added by type is registered as one: ") + std::string(type.name()));
    add->get<SingletonAddCallback>()(*this);
}

void World::tick(const float dt)
{
    EAW_PROFILE_ZONE("ecs tick");
    entities_.process();
    if (!new_systems_.empty())
    {
        order_systems();
    }
    for (const auto& turn : systems_)
    {
        for (const auto& system : turn)
        {
            system->execute(dt);
        }
    }
}

bool World::empty() const noexcept
{
    return entities_.empty() || systems_.empty();
}

void World::order_systems()
{
    std::vector<std::unique_ptr<ISystem>> all;
    for (auto& turn : systems_)
    {
        for (auto& system : turn)
        {
            all.push_back(std::move(system));
        }
    }
    for (auto& system : new_systems_)
    {
        all.push_back(std::move(system));
    }
    new_systems_.clear();
    std::vector<rti::Type> types;
    for (const auto& system : all)
    {
        types.push_back(system->type());
    }
    systems_.clear();
    std::string order = "Systems by turn:";
    for (const auto& turn : rti::sort_by_order<ISystem>(types))
    {
        order += "\n ";
        auto& placed = systems_.emplace_back();
        for (const auto type : turn)
        {
            const auto found = std::ranges::find_if(all,
                                                    [&](const std::unique_ptr<ISystem>& system)
                                                    {
                                                        return system != nullptr && system->type() == type;
                                                    });
            placed.push_back(std::move(*found));
            order += " " + std::string(type.name());
        }
    }
    log_info("ecs", order);
}

World::Snapshot World::snapshot() const
{
    auto& entities = const_cast<EntityManager&>(entities_);
    const auto& table = entities_.table();
    Snapshot result;
    for (std::size_t slot = 0; slot < table.slot_count(); ++slot)
    {
        const auto entity = table.entity_in(slot);
        const auto* mask = entity ? entities_.mask(*entity) : nullptr;
        if (mask == nullptr)
        {
            continue;
        }
        auto& written = result.entities.emplace_back();
        written.name = names_[slot];
        for (std::size_t id = 0; id < max_components; ++id)
        {
            if (mask->test(id))
            {
                const auto component = static_cast<ComponentId>(id);
                written.components.push_back(component_type(component).copy(entities.component(*entity, component)));
            }
        }
    }
    for (const auto& turn : systems_)
    {
        for (const auto& system : turn)
        {
            result.systems.emplace_back(system->type().name());
        }
    }
    for (const auto& singleton : singletons_)
    {
        result.singletons.push_back(singleton.copy());
    }
    return result;
}

void World::Snapshot::apply(World& world) &&
{
    for (auto& written : entities)
    {
        const auto entity = world.create_entity(ComponentMask{}, written.name);
        for (auto& component : written.components)
        {
            const auto id = component_id(component.type());
            EAW_ASSERT_MSG(id.has_value(), std::string("a snapshot's component type is a registered component: ") + std::string(component.type().name()));
            world.add_component(entity, *id) = std::move(component);
        }
    }
    for (const auto& system : systems)
    {
        const auto* type = rti::TypeInfo::get(system);
        EAW_ASSERT_MSG(type != nullptr, "a snapshot's system is registered: " + system);
        world.add_system(*type);
    }
    for (auto& singleton : singletons)
    {
        world.singletons_.push_back(std::move(singleton));
    }
}

} // namespace eaw::ecs
