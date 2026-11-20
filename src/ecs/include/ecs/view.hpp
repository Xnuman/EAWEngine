#pragma once
// Views of the entities that have some components (Singe's views), made by the entity manager once
// per modifier list and kept up to date:
// - IterView walks every entity it accepts, archetype by archetype (in the order archetypes were
//   made), row by row, as tuples of the entity and its components;
// - AccessView reads one entity's components;
// - InitView walks the entities that gained one of its React components in the last command
//   processing.
//
//     for (auto [entity, transform, velocity] : view)  // IterView<RWAccess<Transform>, ROAccess<Velocity>>
#include "core/fatal_error.hpp"

#include "ecs/archetype.hpp"
#include "ecs/component.hpp"
#include "ecs/entity.hpp"
#include "ecs/entity_table.hpp"
#include "ecs/modifiers.hpp"
#include "rti/type.hpp"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace eaw::ecs {

namespace detail {

template <class Tuple> struct ModifierComponents;
template <class... Modifiers> struct ModifierComponents<std::tuple<Modifiers...>>
{
    [[nodiscard]] static ComponentMask mask()
    {
        return mask_of<std::remove_const_t<typename Modifiers::Type>...>();
    }
    [[nodiscard]] static std::vector<ComponentId> ids()
    {
        return {component_id<std::remove_const_t<typename Modifiers::Type>>()...};
    }
};

} // namespace detail

class ViewBase
{
public:
    explicit ViewBase(const EntityTable& table) noexcept : table_(&table)
    {}
    virtual ~ViewBase() = default;
    ViewBase(const ViewBase&) = delete;
    ViewBase& operator=(const ViewBase&) = delete;

    // The view's own type: one view per type (that is, per kind and modifier list).
    [[nodiscard]] rti::Type kind() const noexcept
    {
        return kind_;
    }
    [[nodiscard]] const ComponentMask& include() const noexcept
    {
        return include_;
    }
    [[nodiscard]] const ComponentMask& exclude() const noexcept
    {
        return exclude_;
    }
    [[nodiscard]] const ComponentMask& react() const noexcept
    {
        return react_;
    }
    // Whether an entity with these components is in the view.
    [[nodiscard]] bool accepts(const ComponentMask& mask) const noexcept
    {
        return (mask & include_) == include_ && (mask & exclude_).none();
    }

protected:
    template <class View, class... Modifiers> void define()
    {
        using List = std::tuple<Modifiers...>;
        kind_ = rti::Type::of<View>();
        include_ = detail::ModifierComponents<RemoveModifiers<List, Without>>::mask();
        exclude_ = detail::ModifierComponents<GetModifiers<List, Without>>::mask();
        react_ = detail::ModifierComponents<GetModifiers<List, ReactRW, ReactRO>>::mask();
    }

    const EntityTable* table_;
    rti::Type kind_;
    ComponentMask include_;
    ComponentMask exclude_;
    ComponentMask react_;
};

class IterViewBase : public ViewBase
{
public:
    using ViewBase::ViewBase;

    // Adds an archetype the view accepts (the entity manager calls it for each one, as they are made).
    void bind(Archetype& archetype)
    {
        EAW_ASSERT_MSG(accepts(archetype.mask()), "a view binds an archetype it accepts");
        Bound bound{&archetype, {}};
        for (const auto id : order_)
        {
            bound.columns.push_back(*archetype.column(id));
        }
        bound_.push_back(std::move(bound));
    }

protected:
    struct Bound
    {
        Archetype* archetype{};
        // The columns of the components handed out, in the view's order.
        std::vector<std::size_t> columns;
    };

    // The components handed out, in the order of the view's modifiers.
    std::vector<ComponentId> order_;
    std::vector<Bound> bound_;
};

template <class... Modifiers> class IterView final : public IterViewBase
{
    using Included = RemoveModifiers<std::tuple<Modifiers...>, Without>;
    using Types = ClearModifiers<Included>;

public:
    explicit IterView(const EntityTable& table) : IterViewBase(table)
    {
        define<IterView, Modifiers...>();
        order_ = detail::ModifierComponents<Included>::ids();
    }

    class Iterator
    {
    public:
        Iterator(const std::vector<Bound>& bound, const std::size_t archetype) noexcept : bound_(&bound), archetype_(archetype)
        {
            skip_finished();
        }
        Iterator& operator++() noexcept
        {
            ++row_;
            skip_finished();
            return *this;
        }
        [[nodiscard]] bool operator==(const Iterator& other) const noexcept
        {
            return archetype_ == other.archetype_ && row_ == other.row_;
        }
        [[nodiscard]] auto operator*() const noexcept
        {
            return get(std::make_index_sequence<std::tuple_size_v<Types>>{});
        }

    private:
        template <std::size_t... Index> [[nodiscard]] auto get(std::index_sequence<Index...>) const noexcept
        {
            const auto& bound = (*bound_)[archetype_];
            return std::tuple<Entity, std::tuple_element_t<Index, Types>&...>(bound.archetype->entity(row_),
                                                                              bound.archetype->template get<std::remove_const_t<std::tuple_element_t<Index, Types>>>(bound.columns[Index], row_)...);
        }
        // Moves past archetypes whose rows are done (or empty).
        void skip_finished() noexcept
        {
            while (archetype_ < bound_->size() && row_ >= (*bound_)[archetype_].archetype->size())
            {
                ++archetype_;
                row_ = 0;
            }
        }

        const std::vector<Bound>* bound_;
        std::size_t archetype_{};
        std::size_t row_{};
    };

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator(bound_, 0);
    }
    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator(bound_, bound_.size());
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return begin() == end();
    }
};

template <class... Modifiers> class AccessView final : public IterViewBase
{
    using Included = RemoveModifiers<std::tuple<Modifiers...>, Without>;
    using Types = ClearModifiers<Included>;

public:
    explicit AccessView(const EntityTable& table) : IterViewBase(table)
    {
        define<AccessView, Modifiers...>();
        order_ = detail::ModifierComponents<Included>::ids();
    }

    // Whether the entity is stored and in the view.
    [[nodiscard]] bool has(const Entity entity) const noexcept
    {
        const auto* location = table_->location(entity);
        return location != nullptr && location->stored() && accepts(table_->archetype(location->archetype).mask());
    }
    // The entity's components (asserts that it is in the view).
    [[nodiscard]] auto get(const Entity entity) const noexcept
    {
        EAW_ASSERT_MSG(has(entity), "an access view reads an entity it has");
        const auto* location = table_->location(entity);
        return components(table_->archetype(location->archetype), location->row, std::make_index_sequence<std::tuple_size_v<Types>>{});
    }

private:
    template <std::size_t... Index> [[nodiscard]] static auto components(Archetype& archetype, const std::size_t row, std::index_sequence<Index...>) noexcept
    {
        return std::tuple<std::tuple_element_t<Index, Types>&...>(
            archetype.template get<std::remove_const_t<std::tuple_element_t<Index, Types>>>(*archetype.column(component_id<std::tuple_element_t<Index, Types>>()), row)...);
    }
};

class InitViewBase : public ViewBase
{
public:
    using ViewBase::ViewBase;

    // An entity that gained one of the view's React components (the entity manager calls it).
    void add(const Entity entity)
    {
        if (entities_.empty() || entities_.back() != entity)
        {
            entities_.push_back(entity);
        }
    }
    // Forgets the entities of the last processing (before the next one).
    void reset() noexcept
    {
        entities_.clear();
    }
    // Whether an entity whose components are now `mask` belongs here when it gained `added`.
    [[nodiscard]] bool takes(const ComponentMask& mask, const ComponentMask& added) const noexcept
    {
        return (added & react_).any() && (mask & react_) == react_ && accepts(mask);
    }

protected:
    std::vector<Entity> entities_;
};

template <class... Modifiers> class InitView final : public InitViewBase
{
    using Included = RemoveModifiers<std::tuple<Modifiers...>, Without>;
    using Types = ClearModifiers<Included>;

public:
    explicit InitView(const EntityTable& table) : InitViewBase(table)
    {
        define<InitView, Modifiers...>();
        EAW_ASSERT_MSG(react_.any(), "an init view reacts to a component");
    }

    class Iterator
    {
    public:
        Iterator(const InitView& view, const std::size_t position) noexcept : view_(&view), position_(position)
        {}
        Iterator& operator++() noexcept
        {
            ++position_;
            return *this;
        }
        [[nodiscard]] bool operator==(const Iterator& other) const noexcept
        {
            return position_ == other.position_;
        }
        [[nodiscard]] auto operator*() const noexcept
        {
            return get(std::make_index_sequence<std::tuple_size_v<Types>>{});
        }

    private:
        template <std::size_t... Index> [[nodiscard]] auto get(std::index_sequence<Index...>) const noexcept
        {
            const auto entity = view_->entities_[position_];
            const auto* location = view_->table_->location(entity);
            auto& archetype = view_->table_->archetype(location->archetype);
            return std::tuple<Entity, std::tuple_element_t<Index, Types>&...>(
                entity, archetype.template get<std::remove_const_t<std::tuple_element_t<Index, Types>>>(*archetype.column(component_id<std::tuple_element_t<Index, Types>>()), location->row)...);
        }

        const InitView* view_;
        std::size_t position_{};
    };

    [[nodiscard]] Iterator begin() const noexcept
    {
        return Iterator(*this, 0);
    }
    [[nodiscard]] Iterator end() const noexcept
    {
        return Iterator(*this, entities_.size());
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return entities_.empty();
    }
};

} // namespace eaw::ecs
