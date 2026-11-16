#pragma once

// The keys and mouse buttons the window reports (platform_window.hpp), and their names in text such as
// the input bindings file (input_bindings.hpp).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace eaw {

// Letters, digits, and function keys are contiguous, in order (the window maps them by offset).
enum class Key : std::uint8_t
{
    escape,
    enter,
    space,
    tab,
    backspace,
    del,
    insert,
    home,
    end,
    page_up,
    page_down,
    left,
    right,
    up,
    down,
    shift,
    control,
    alt,
    a,
    b,
    c,
    d,
    e,
    f,
    g,
    h,
    i,
    j,
    k,
    l,
    m,
    n,
    o,
    p,
    q,
    r,
    s,
    t,
    u,
    v,
    w,
    x,
    y,
    z,
    digit0,
    digit1,
    digit2,
    digit3,
    digit4,
    digit5,
    digit6,
    digit7,
    digit8,
    digit9,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
    minus,
    equals,
    left_bracket,
    right_bracket,
    semicolon,
    apostrophe,
    comma,
    period,
    slash,
    backslash,
    grave,
    count,
};

enum class MouseButton : std::uint8_t
{
    left,
    right,
    middle,
    count,
};

inline constexpr std::size_t key_count = static_cast<std::size_t>(Key::count);

// A key's name in text ("w", "left", "f1", "0", "page_up"); empty for Key::count.
[[nodiscard]] std::string_view key_name(Key key) noexcept;
// The key a name names, ignoring ASCII case; nothing for an unknown name.
[[nodiscard]] std::optional<Key> key_from_name(std::string_view name) noexcept;

} // namespace eaw
