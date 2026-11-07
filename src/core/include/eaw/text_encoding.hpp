#pragma once

#include "eaw/status.hpp"

#include <string>
#include <string_view>

namespace eaw {

// Errors describe the first invalid sequence.
[[nodiscard]] Result<std::u16string> utf8_to_utf16(std::string_view text);
[[nodiscard]] Result<std::string> utf16_to_utf8(std::u16string_view text);

} // namespace eaw
