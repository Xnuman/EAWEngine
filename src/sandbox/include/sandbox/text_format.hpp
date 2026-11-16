#pragma once

#include <cstdint>
#include <string>

namespace eaw {

// `value` with exactly `decimals` digits after the point (console and overlay text).
[[nodiscard]] std::string format_fixed(double value, int decimals);
// `value` in hexadecimal with a 0x prefix, as state hashes are shown.
[[nodiscard]] std::string format_hex(std::uint64_t value);

} // namespace eaw
