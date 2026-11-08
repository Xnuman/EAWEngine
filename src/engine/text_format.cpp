#include "text_format.hpp"

#include <iomanip>
#include <sstream>

namespace eaw {

std::string format_fixed(const double value, const int decimals)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals) << value;
    return stream.str();
}

} // namespace eaw
