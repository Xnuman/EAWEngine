#pragma once

// Helpers shared by the schema loader (xml_schema_load.cpp) and the validator
// (xml_schema_validate.cpp); private to eaw_core.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace eaw::xml_schema_detail {

inline std::string fold_ascii(const std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return result;
}

inline std::string trim(const std::string_view value)
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
    {
        ++first;
    }
    auto last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
    {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

} // namespace eaw::xml_schema_detail
