#pragma once
// What a view asks of a component (Singe's modifiers): to write it (RWAccess), to read it (ROAccess),
// to exclude entities that have it (Without), or to see entities that gained it in the last command
// processing (ReactRW, ReactRO: init views). A modifier's Type is the component as the view hands it
// out (const for read-only access).
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace eaw::ecs {

template <class Component> struct Without
{
    using Type = Component;
};

template <class Component> struct RWAccess
{
    using Type = Component;
};

template <class Component> struct ROAccess
{
    using Type = const Component;
};

template <class Component> struct ReactRW
{
    using Type = Component;
};

template <class Component> struct ReactRO
{
    using Type = const Component;
};

namespace detail {

template <class Modifier, template <class> class Kind> inline constexpr bool is_kind = std::is_same_v<Modifier, Kind<std::remove_const_t<typename Modifier::Type>>>;

template <class Modifier, template <class> class... Kinds> inline constexpr bool is_any_kind = (is_kind<Modifier, Kinds> || ...);

template <class Tuple, template <class> class... Kinds> struct Filter;
template <class... Modifiers, template <class> class... Kinds> struct Filter<std::tuple<Modifiers...>, Kinds...>
{
    using Kept = decltype(std::tuple_cat(std::declval<std::conditional_t<is_any_kind<Modifiers, Kinds...>, std::tuple<Modifiers>, std::tuple<>>>()...));
    using Removed = decltype(std::tuple_cat(std::declval<std::conditional_t<is_any_kind<Modifiers, Kinds...>, std::tuple<>, std::tuple<Modifiers>>>()...));
};

template <class Tuple> struct Clear;
template <class... Modifiers> struct Clear<std::tuple<Modifiers...>>
{
    using Type = std::tuple<typename Modifiers::Type...>;
};

} // namespace detail

// The modifiers of a tuple that are (GetModifiers) or are not (RemoveModifiers) of these kinds.
template <class Tuple, template <class> class... Kinds> using GetModifiers = typename detail::Filter<Tuple, Kinds...>::Kept;
template <class Tuple, template <class> class... Kinds> using RemoveModifiers = typename detail::Filter<Tuple, Kinds...>::Removed;
// The component types a tuple of modifiers hands out.
template <class Tuple> using ClearModifiers = typename detail::Clear<Tuple>::Type;

} // namespace eaw::ecs
