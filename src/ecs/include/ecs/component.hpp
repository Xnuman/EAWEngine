#pragma once
// Components (Singe's ecs::Component): plain data types, each with a component id given the first
// time it is asked for, up to max_components. An archetype stores a component through the
// operations of its ComponentType (Singe's TypePool, as one table entry per component instead of an
// object per chunk). Deriving from Component<T> gives T::id(); the base is empty and adds no
// virtual table, so components stay plain data. Register a component's reflection with
// register_component<T>() (ecs/component_registrator.hpp).
#include "rti/type.hpp"
#include "rti/variant.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace eaw::ecs {

using ComponentId = std::uint16_t;
// Component types a program may use at most (the width of a mask). An engine choice.
inline constexpr std::size_t max_components = 128;
using ComponentMask = std::bitset<max_components>;

// How a component type is stored.
struct ComponentType
{
    rti::Type type;
    std::size_t size{};
    std::size_t alignment{};
    // Constructs a default value at `at`.
    void (*construct)(void* at){};
    // Constructs a value at `at` from a variant of the component's type.
    void (*construct_from)(void* at, rti::Variant&& value){};
    // Moves the value at `from` to `to` (constructing it there) and destroys it at `from`.
    void (*relocate)(void* to, void* from){};
    void (*destroy)(void* at){};
    // An owned copy of the value at `at`.
    rti::Variant (*copy)(const void* at){};
    // A variant that refers to the value at `at`.
    rti::Variant (*reference)(void* at){};
    // A variant owning a default value.
    rti::Variant (*make)(){};
};

// The id of a component type with these operations, given now when it has none.
[[nodiscard]] ComponentId register_component_type(const ComponentType& type);
[[nodiscard]] const ComponentType& component_type(ComponentId id);
// The id of a registered component type, or none.
[[nodiscard]] std::optional<ComponentId> component_id(rti::Type type);

template <class T> [[nodiscard]] ComponentType component_type_of() noexcept
{
    static_assert(std::is_default_constructible_v<T> && std::is_move_constructible_v<T>, "a component is default and move constructible");
    return {rti::Type::of<T>(),
            sizeof(T),
            alignof(T),
            [](void* at)
            {
                new (at) T();
            },
            [](void* at, rti::Variant&& value)
            {
                new (at) T(std::move(value.get<T>()));
            },
            [](void* to, void* from)
            {
                new (to) T(std::move(*static_cast<T*>(from)));
                static_cast<T*>(from)->~T();
            },
            [](void* at)
            {
                static_cast<T*>(at)->~T();
            },
            [](const void* at)
            {
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    return rti::Variant(*static_cast<const T*>(at));
                }
                else
                {
                    return rti::Variant{};
                }
            },
            [](void* at)
            {
                return rti::Variant::reference(*static_cast<T*>(at));
            },
            []
            {
                return rti::Variant::make<T>();
            }};
}

template <class T> [[nodiscard]] ComponentId component_id()
{
    static const ComponentId id = register_component_type(component_type_of<std::remove_cvref_t<T>>());
    return id;
}

template <class... Components> [[nodiscard]] ComponentMask mask_of()
{
    ComponentMask mask;
    (mask.set(component_id<Components>()), ...);
    return mask;
}

template <class T> struct Component
{
    [[nodiscard]] static ComponentId id()
    {
        return component_id<T>();
    }
};

} // namespace eaw::ecs
