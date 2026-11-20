#pragma once
// A world (Singe's ecs::World): entities with names, their components, systems run every tick in
// turns ordered by their before and after lists, and singletons (one value per type). A tick applies
// the commands given since the last one (ecs/entity_manager.hpp), adds the systems added since, then
// runs the systems turn by turn. A snapshot holds the world as reflected values (entities with their
// components, system names, singletons) and builds a world from them.
#include "ecs/component.hpp"
#include "ecs/entity.hpp"
#include "ecs/entity_manager.hpp"
#include "ecs/isystem.hpp"
#include "ecs/view.hpp"
#include "rti/type_info.hpp"
#include "rti/variant.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace eaw::ecs {

class World final
{
public:
    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Entities (stored at the next tick).
    template <class... Components> Entity create_entity(const std::string_view name = "Entity")
    {
        return create_entity(mask_of<Components...>(), name);
    }
    Entity create_entity(const ComponentMask& mask, std::string_view name = "Entity");
    void destroy_entity(Entity entity);
    void rename_entity(Entity entity, std::string_view name);
    [[nodiscard]] std::string_view entity_name(Entity entity) const;

    // Components (added and removed at the next tick).
    template <class T> T& add_component(const Entity entity)
    {
        return entities_.add_component<T>(entity);
    }
    template <class T> T& add_component(const Entity entity, T value)
    {
        return entities_.add_component<T>(entity, std::move(value));
    }
    template <class... Components> std::tuple<Components&...> add_components(const Entity entity)
    {
        return std::tuple<Components&...>(add_component<Components>(entity)...);
    }
    rti::Variant& add_component(const Entity entity, const ComponentId id)
    {
        return entities_.add_component(entity, id);
    }
    template <class... Components> void remove_components(const Entity entity)
    {
        entities_.remove_components(entity, mask_of<Components...>());
    }
    template <class... Components> [[nodiscard]] bool has_components(const Entity entity) const noexcept
    {
        return entities_.has(entity, mask_of<Components...>());
    }
    template <class... Components> [[nodiscard]] std::tuple<Components&...> get_components(const Entity entity)
    {
        return entities_.get<Components...>(entity);
    }

    // Views.
    template <class... Modifiers> [[nodiscard]] IterView<Modifiers...>& iter_view()
    {
        return entities_.iter_view<Modifiers...>();
    }
    template <class... Modifiers> [[nodiscard]] AccessView<Modifiers...>& access_view()
    {
        return entities_.access_view<Modifiers...>();
    }
    template <class... Modifiers> [[nodiscard]] InitView<Modifiers...>& init_view()
    {
        return entities_.init_view<Modifiers...>();
    }
    // The view of a view type (IterView, AccessView, or InitView of some modifiers).
    template <class View> [[nodiscard]] View& view()
    {
        return ViewOf<View>::get(*this);
    }

    // Systems (added at the next tick).
    template <class... Systems> void add_systems()
    {
        (new_systems_.push_back(std::make_unique<Systems>(*this)), ...);
    }
    // A system by reflected type (registered with register_system).
    void add_system(const rti::TypeInfo& type);

    // Singletons.
    template <class... Singletons> void add_singletons()
    {
        (singletons_.push_back(rti::Variant::make<Singletons>()), ...);
    }
    // A singleton by reflected type (registered with register_singleton).
    void add_singleton(const rti::TypeInfo& type);
    template <class T> [[nodiscard]] T* find_singleton() noexcept
    {
        for (auto& singleton : singletons_)
        {
            if (auto* value = singleton.get_if<T>())
            {
                return value;
            }
        }
        return nullptr;
    }
    // The singleton of a type, made when the world has none.
    template <class T> [[nodiscard]] T& singleton()
    {
        if (auto* value = find_singleton<T>())
        {
            return *value;
        }
        return singletons_.emplace_back(rti::Variant::make<T>()).get<T>();
    }

    void tick(float dt);
    // Whether the world has no stored entity or no system.
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] EntityManager& entities() noexcept
    {
        return entities_;
    }

    struct Snapshot
    {
        struct Entity
        {
            std::string name;
            // Owned copies, in component id order.
            std::vector<rti::Variant> components;
        };
        // Stored entities in slot order.
        std::vector<Entity> entities;
        // In turn order.
        std::vector<std::string> systems;
        std::vector<rti::Variant> singletons;

        // Adds the snapshot's entities, systems, and singletons to `world` (entities as new ones:
        // components that name other entities are not remapped).
        void apply(World& world) &&;
    };
    // The stored entities, the systems, and the singletons, as reflected copies.
    [[nodiscard]] Snapshot snapshot() const;

private:
    template <class View> struct ViewOf;
    template <class... Modifiers> struct ViewOf<IterView<Modifiers...>>
    {
        static IterView<Modifiers...>& get(World& world)
        {
            return world.iter_view<Modifiers...>();
        }
    };
    template <class... Modifiers> struct ViewOf<AccessView<Modifiers...>>
    {
        static AccessView<Modifiers...>& get(World& world)
        {
            return world.access_view<Modifiers...>();
        }
    };
    template <class... Modifiers> struct ViewOf<InitView<Modifiers...>>
    {
        static InitView<Modifiers...>& get(World& world)
        {
            return world.init_view<Modifiers...>();
        }
    };

    // Adds the new systems and orders all into turns (rti::sort_by_order).
    void order_systems();

    EntityManager entities_;
    std::vector<rti::Variant> singletons_;
    std::vector<std::vector<std::unique_ptr<ISystem>>> systems_;
    std::vector<std::unique_ptr<ISystem>> new_systems_;
    // By entity slot.
    std::vector<std::string> names_;
};

} // namespace eaw::ecs
