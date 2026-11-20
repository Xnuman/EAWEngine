#pragma once
// A type's identity at run time (Singe's rti::Type): a small number given to each type name the
// first time it is asked for; 0 is no type. The numbers follow the order types are first asked for,
// so they are compared and looked up but never stored, hashed, or written: names are.
#include "rti/type_name.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>

namespace eaw::rti {

class Type final
{
public:
    Type() = default;

    // The type of T (without references and cv-qualifiers).
    template <class T> [[nodiscard]] static Type of()
    {
        static const Type type = of(type_name<std::remove_cvref_t<T>>());
        return type;
    }
    // The type of this name (a new number when the name is new).
    [[nodiscard]] static Type of(std::string_view name);

    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] std::uint16_t id() const noexcept
    {
        return id_;
    }
    [[nodiscard]] bool valid() const noexcept
    {
        return id_ != 0;
    }
    explicit operator bool() const noexcept
    {
        return valid();
    }
    friend bool operator==(Type, Type) noexcept = default;
    friend auto operator<=>(Type, Type) noexcept = default;

private:
    explicit Type(const std::uint16_t id) noexcept : id_(id)
    {}
    std::uint16_t id_{};
};

} // namespace eaw::rti

template <> struct std::hash<eaw::rti::Type>
{
    std::size_t operator()(const eaw::rti::Type type) const noexcept
    {
        return type.id();
    }
};
