#pragma once
// A type's name at compile time, from the compiler's function signature (MSVC's __FUNCSIG__, the
// only compiler this project builds with): `int`, `eaw::ecs::Entity`, `std::vector<float,...>`.
// The class-key MSVC writes before a class name (`struct `, `class `, `enum `, `union `) is left
// out, so a type has one name however it is declared.
#include <string_view>

namespace eaw::rti {

namespace detail {

template <class T> consteval std::string_view signature() noexcept
{
    return __FUNCSIG__;
}

consteval std::string_view without_class_key(std::string_view name) noexcept
{
    for (const std::string_view key : {std::string_view{"struct "}, std::string_view{"class "}, std::string_view{"enum "}, std::string_view{"union "}})
    {
        if (name.starts_with(key))
        {
            return name.substr(key.size());
        }
    }
    return name;
}

} // namespace detail

template <class T> consteval std::string_view type_name() noexcept
{
    // "... __cdecl eaw::rti::detail::signature<NAME>(void) noexcept"
    constexpr std::string_view full = detail::signature<T>();
    constexpr std::string_view open = "signature<";
    constexpr auto begin = full.find(open) + open.size();
    constexpr auto end = full.rfind(">(void)");
    return detail::without_class_key(full.substr(begin, end - begin));
}

} // namespace eaw::rti
