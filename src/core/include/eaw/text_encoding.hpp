#pragma once

#include <string>
#include <string_view>

namespace eaw {

[[nodiscard]] std::u16string utf8_to_utf16(std::string_view text);
[[nodiscard]] std::string utf16_to_utf8(std::u16string_view text);

} // namespace eaw

