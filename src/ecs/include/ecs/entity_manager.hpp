#pragma once
// Entities and their components (Singe's ecs::EntityManager). Changes are commands that apply in
// process(): making an entity, adding and removing components, and removing an entity take effect
// together, in the order entities were first changed (Singe applied them in hash order), so a
// system never sees storage change under it. Adding a component returns its value to fill in before
// it is stored; adding one the entity has replaces its value. Views are made once per modifier list
// and bound to every archetype they accept as archetypes are made.
#include "core/fatal_error.hpp"

#include "ecs/component.hpp"
#include "ecs/entity.hpp"
#include "ecs/entity_table.hpp"
#include "ecs/view.hpp"
#include "rti/variant.hpp"

#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace eaw::ecs {

class EntityManager final
{
public:
    EntityManager() = default;
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    // A new entity with these components (default values), stored at the next process().
    [[nodiscard]] Entity create(const ComponentMask& mask = {});
    // Removes the entity at the next process().
    void destroy(Entity entity);
    // Adds a component at the next process(): its value, default now, to fill in until then.
    rti::Variant& add_component(Entity entity, ComponentId id);
    template <class T> T& add_component(const Entity entity)
    {
        return add_component(entity, component_id<T>()).template get<T>();
    }
    template <class T> T& add_component(const Entity entity, T value)
    {
        auto& added = add_component<T>(entity);
        added = std::move(value);
        return added;
    }
    // Adds components with default values at the next process().
    void add_components(Entity entity, const ComponentMask& mask);
    void remove_components(Entity entity, const ComponentMask& mask);

    [[nodiscard]] bool alive(Entity entity) const noexcept
    {
        return table_.alive(entity);
    }
    // The components the entity has now (stored), or nullptr when it is not stored.
    [[nodiscard]] const ComponentMask* mask(Entity entity) const noexcept;
    [[nodiscard]] bool has(Entity entity, const ComponentMask& mask) const noexcept;
    // The entity's stored components (asserts that it has them).
    template <class... Components> [[nodiscard]] std::tuple<Components&...> get(const Entity entity)
    {
        const auto* location = table_.location(entity);
        EAW_ASSERT_MSG(location != nullptr && location->stored() && has(entity, mask_of<Components...>()), "an entity is read for components it has");
        auto& archetype = table_.archetype(location->archetype);
        return std::tuple<Components&...>(archetype.template get<std::remove_const_t<Components>>(*archetype.column(component_id<Components>()), location->row)...);
    }
    // The value of a stored component, or nullptr.
    [[nodiscard]] void* component(Entity entity, ComponentId id) noexcept;

    template <class... Modifiers> [[nodiscard]] IterView<Modifiers...>& iter_view()
    {
        return iterable<IterView<Modifiers...>>();
    }
    template <class... Modifiers> [[nodiscard]] AccessView<Modifiers...>& access_view()
    {
        return iterable<AccessView<Modifiers...>>();
    }
    template <class... Modifiers> [[nodiscard]] InitView<Modifiers...>& init_view()
    {
        for (const auto& view : init_views_)
        {
            if (view->kind() == rti::Type::of<InitView<Modifiers...>>())
            {
                return static_cast<InitView<Modifiers...>&>(*view);
            }
        }
        init_views_.push_back(std::make_unique<InitView<Modifiers...>>(table_));
        return static_cast<InitView<Modifiers...>&>(*init_views_.back());
    }

    // Applies the commands given since the last call.
    void process();
    // Whether no entity is stored.
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const EntityTable& table() const noexcept
    {
        return table_;
    }

private:
    struct Command
    {
        Entity entity;
        // The components the entity is to have.
        ComponentMask mask;
        bool destroy{};
        PendingComponents values;
    };

    // The entity's command, started from its current components when it has none.
    Command& command_for(Entity entity);
    // The archetype of a mask, made (and bound to the views that accept it) when new.
    std::size_t archetype_for(const ComponentMask& mask);
    void apply_update(Command& command);
    void apply_destroy(const Command& command);
    // Records that the entity now in `row` of an archetype moved there.
    void moved_into(std::size_t archetype, std::size_t row, const std::optional<Entity>& moved) noexcept;

    template <class View> View& iterable()
    {
        for (const auto& view : iter_views_)
        {
            if (view->kind() == rti::Type::of<View>())
            {
                return static_cast<View&>(*view);
            }
        }
        auto view = std::make_unique<View>(table_);
        for (std::size_t index = 0; index < table_.archetype_count(); ++index)
        {
            if (view->accepts(table_.archetype(index).mask()))
            {
                view->bind(table_.archetype(index));
            }
        }
        iter_views_.push_back(std::move(view));
        return static_cast<View&>(*iter_views_.back());
    }

    EntityTable table_;
    std::vector<Command> commands_;
    // By slot: its command's index plus one (0: none).
    std::vector<std::uint32_t> pending_;
    std::vector<std::unique_ptr<IterViewBase>> iter_views_;
    std::vector<std::unique_ptr<InitViewBase>> init_views_;
};

} // namespace eaw::ecs
