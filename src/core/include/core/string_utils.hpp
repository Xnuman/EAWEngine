#pragma once

// Text helpers shared by every module: ASCII case folding and trimming (locale-independent), and
// UTF-8 <-> UTF-16 conversion. They know nothing of the caller's purpose: invalid input gives an
// empty result, and the caller decides what that means for its operation.

#include <optional>
#include <string>
#include <string_view>

namespace eaw {

// A-Z lowered (or a-z raised); every other byte is unchanged.
[[nodiscard]] constexpr char ascii_lower(const char character) noexcept
{
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}
[[nodiscard]] constexpr char ascii_upper(const char character) noexcept
{
    return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character;
}
[[nodiscard]] std::string ascii_lower(std::string_view text);
[[nodiscard]] std::string ascii_upper(std::string_view text);
[[nodiscard]] bool ascii_equal_ignoring_case(std::string_view left, std::string_view right) noexcept;
// `text` without leading and trailing ASCII whitespace (space, \t, \n, \v, \f, \r).
[[nodiscard]] std::string_view ascii_trim(std::string_view text) noexcept;

// Nothing when `text` is not well-formed UTF-8 (invalid or truncated sequences, overlong forms,
// surrogate or out-of-range code points).
[[nodiscard]] std::optional<std::u16string> utf8_to_utf16(std::string_view text);
// Nothing when `text` holds an unpaired surrogate.
[[nodiscard]] std::optional<std::string> utf16_to_utf8(std::u16string_view text);

} // namespace eaw
