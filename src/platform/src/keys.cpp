#include "platform/keys.hpp"

#include "core/string_utils.hpp"

#include <array>

namespace eaw {

namespace {

// In Key order.
constexpr std::array<std::string_view, key_count> key_names{"escape",
                                                            "enter",
                                                            "space",
                                                            "tab",
                                                            "backspace",
                                                            "delete",
                                                            "insert",
                                                            "home",
                                                            "end",
                                                            "page_up",
                                                            "page_down",
                                                            "left",
                                                            "right",
                                                            "up",
                                                            "down",
                                                            "shift",
                                                            "control",
                                                            "alt",
                                                            "a",
                                                            "b",
                                                            "c",
                                                            "d",
                                                            "e",
                                                            "f",
                                                            "g",
                                                            "h",
                                                            "i",
                                                            "j",
                                                            "k",
                                                            "l",
                                                            "m",
                                                            "n",
                                                            "o",
                                                            "p",
                                                            "q",
                                                            "r",
                                                            "s",
                                                            "t",
                                                            "u",
                                                            "v",
                                                            "w",
                                                            "x",
                                                            "y",
                                                            "z",
                                                            "0",
                                                            "1",
                                                            "2",
                                                            "3",
                                                            "4",
                                                            "5",
                                                            "6",
                                                            "7",
                                                            "8",
                                                            "9",
                                                            "f1",
                                                            "f2",
                                                            "f3",
                                                            "f4",
                                                            "f5",
                                                            "f6",
                                                            "f7",
                                                            "f8",
                                                            "f9",
                                                            "f10",
                                                            "f11",
                                                            "f12",
                                                            "minus",
                                                            "equals",
                                                            "left_bracket",
                                                            "right_bracket",
                                                            "semicolon",
                                                            "apostrophe",
                                                            "comma",
                                                            "period",
                                                            "slash",
                                                            "backslash",
                                                            "grave"};
static_assert(!key_names.back().empty(), "every key needs its name");

} // namespace

std::string_view key_name(const Key key) noexcept
{
    const auto index = static_cast<std::size_t>(key);
    return index < key_names.size() ? key_names[index] : std::string_view{};
}

std::optional<Key> key_from_name(const std::string_view name) noexcept
{
    for (std::size_t index = 0; index < key_names.size(); ++index)
    {
        if (ascii_equal_ignoring_case(key_names[index], name))
        {
            return static_cast<Key>(index);
        }
    }
    return std::nullopt;
}

} // namespace eaw
