#pragma once

#include <string>

namespace eaw {

// `value` with exactly `decimals` digits after the point (console and overlay text).
[[nodiscard]] std::string format_fixed(double value, int decimals);

} // namespace eaw
