#include "sandbox/text_format.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <sstream>

namespace eaw {

std::string format_fixed(const double value, const int decimals)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals) << value;
    return stream.str();
}

std::string format_hex(const std::uint64_t value)
{
    std::array<char, 17> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
    (void)error;
    return "0x" + std::string(buffer.data(), end);
}

} // namespace eaw
