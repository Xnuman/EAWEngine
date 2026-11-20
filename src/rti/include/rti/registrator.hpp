#pragma once
// Registering a type (Singe's TypeInfo::Registrator): register_type<T>() enters T with its size and
// kind, its default constructor, and its copy constructor (else its move constructor); the returned
// registrator adds constructors, properties, metadata, and parents. Types a property or a constructor
// uses are entered too. Registering a type twice extends its entry.
//
//     EAW_RTI_REGISTRATION
//     {
//         eaw::rti::register_type<Health>().property("points", &Health::points).property("armor", &Health::armor);
//     }
#include "rti/type_info.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace eaw::rti {

namespace detail {

template <class T> struct IsVector : std::false_type
{};
template <class T, class Allocator> struct IsVector<std::vector<T, Allocator>> : std::true_type
{
    using Element = T;
};

template <class Member> struct MemberValue;
template <class Value, class Class> struct MemberValue<Value Class::*>
{
    using Type = Value;
};

} // namespace detail

template <class T> class Registrator;

template <class T> Registrator<T> register_type();

// Enters each type that is not registered yet.
template <class... Types> void ensure_registered()
{
    ((TypeInfo::get<Types>() == nullptr ? static_cast<void>(register_type<Types>()) : static_cast<void>(0)), ...);
}

template <class T> class Registrator
{
public:
    using Registered = std::remove_cvref_t<T>;

    Registrator() : info_(enter())
    {}

    // A constructor taking these arguments.
    template <class... Arguments> Registrator& constructor()
    {
        ensure_registered<std::remove_cvref_t<Arguments>...>();
        info_.constructors_.push_back({{Type::of<Arguments>()...},
                                       [](const std::span<Variant> values)
                                       {
                                           return construct<Arguments...>(values, std::index_sequence_for<Arguments...>{});
                                       }});
        return *this;
    }

    // A data member as a property, with its metadata (values of types that name their kind in a
    // static `meta_name`). A member pointer of any class, so the signature stays valid for
    // registrators of non-class types.
    template <class Member, class... Metas>
        requires std::is_member_object_pointer_v<Member>
    Registrator& property(const std::string_view name, const Member member, Metas&&... metas)
    {
        using Value = typename detail::MemberValue<Member>::Type;
        ensure_registered<Value>();
        auto& added = add_property(name, Type::of<Value>(), std::forward<Metas>(metas)...);
        added.get = [member](const void* instance)
        {
            return Variant(static_cast<const Registered*>(instance)->*member);
        };
        added.set = [member](void* instance, Variant&& value)
        {
            auto& field = static_cast<Registered*>(instance)->*member;
            if constexpr (std::is_same_v<Value, Variant>)
            {
                field = std::move(value);
            }
            else
            {
                field = std::move(value.get<Value>());
            }
        };
        added.address = [member](void* instance) -> void*
        {
            return &(static_cast<Registered*>(instance)->*member);
        };
        return *this;
    }

    // A property read through `getter(const T&)` and written through `setter(T&, value)`.
    template <class Getter, class Setter, class... Metas>
        requires(!std::is_member_object_pointer_v<Getter> && std::is_invocable_v<Getter, const Registered&>)
    Registrator& property(const std::string_view name, Getter getter, Setter setter, Metas&&... metas)
    {
        using Value = std::remove_cvref_t<std::invoke_result_t<Getter, const Registered&>>;
        ensure_registered<Value>();
        auto& added = add_property(name, Type::of<Value>(), std::forward<Metas>(metas)...);
        added.get = [getter](const void* instance)
        {
            return Variant(getter(*static_cast<const Registered*>(instance)));
        };
        added.set = [setter](void* instance, Variant&& value)
        {
            setter(*static_cast<Registered*>(instance), std::move(value.get<Value>()));
        };
        return *this;
    }

    // Metadata of the type itself.
    template <class Value> Registrator& meta(const std::string_view name, Value&& value)
    {
        ensure_registered<std::remove_cvref_t<Value>>();
        info_.meta_.insert_or_assign(std::string(name), Variant(std::forward<Value>(value)));
        return *this;
    }

    // The types this type derives from (each records it as a child).
    template <class... Parents> Registrator& parents()
    {
        ensure_registered<Parents...>();
        (add_parent(Type::of<Parents>()), ...);
        return *this;
    }

protected:
    TypeInfo& info_;

private:
    static TypeInfo& enter()
    {
        bool made = false;
        auto& info = TypeInfo::entry(Type::of<Registered>(), made);
        if (!made)
        {
            return info;
        }
        info.type_ = Type::of<Registered>();
        info.name_ = info.type_.name();
        if constexpr (!std::is_void_v<Registered> && !std::is_function_v<Registered>)
        {
            info.size_ = sizeof(Registered);
            info.alignment_ = alignof(Registered);
        }
        if constexpr (std::is_arithmetic_v<Registered>)
        {
            info.kind_ = TypeKind::arithmetic;
        }
        if constexpr (detail::IsVector<Registered>::value)
        {
            using Element = typename detail::IsVector<Registered>::Element;
            ensure_registered<Element>();
            info.kind_ = TypeKind::array;
            info.array_ = {Type::of<Element>(),
                           [](const void* array)
                           {
                               return static_cast<const Registered*>(array)->size();
                           },
                           [](void* array, const std::size_t index) -> void*
                           {
                               return &(*static_cast<Registered*>(array))[index];
                           },
                           [](void* array, const std::size_t size)
                           {
                               static_cast<Registered*>(array)->resize(size);
                           }};
        }
        // Copy and move constructors take the same argument type here, so only one is entered.
        if constexpr (std::is_default_constructible_v<Registered>)
        {
            add_constructor<>(info);
        }
        if constexpr (std::is_copy_constructible_v<Registered>)
        {
            add_constructor<const Registered&>(info);
        }
        else if constexpr (std::is_move_constructible_v<Registered>)
        {
            add_constructor<Registered&&>(info);
        }
        return info;
    }

    template <class... Arguments> static void add_constructor(TypeInfo& info)
    {
        info.constructors_.push_back({{Type::of<Arguments>()...},
                                      [](const std::span<Variant> values)
                                      {
                                          return construct<Arguments...>(values, std::index_sequence_for<Arguments...>{});
                                      }});
    }

    template <class... Arguments, std::size_t... Index> static Variant construct([[maybe_unused]] const std::span<Variant> values, std::index_sequence<Index...>)
    {
        return Variant::make<Registered>(std::forward<Arguments>(values[Index].template get<std::remove_cvref_t<Arguments>>())...);
    }

    template <class... Metas> Property& add_property(const std::string_view name, const Type type, Metas&&... metas)
    {
        auto& added = info_.properties_.emplace_back();
        added.name = std::string(name);
        added.type = type;
        (ensure_registered<std::remove_cvref_t<Metas>>(), ...);
        (added.meta.insert_or_assign(std::string(std::remove_cvref_t<Metas>::meta_name), Variant(std::forward<Metas>(metas))), ...);
        return added;
    }

    void add_parent(const Type parent)
    {
        info_.parents_.push_back(parent);
        TypeInfo::entry(parent).children_.push_back(info_.type_);
    }
};

template <class T> Registrator<T> register_type()
{
    return Registrator<T>();
}

} // namespace eaw::rti
