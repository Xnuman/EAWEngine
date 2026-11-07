#include "eaw/asset_path.hpp"

#include <cctype>
#include <stdexcept>
#include <vector>

namespace eaw {

std::string normalize_asset_path(const std::string_view path)
{
    if (path.empty())
    {
        throw std::invalid_argument("asset path cannot be empty");
    }

    std::string input(path);
    for (char& character : input)
    {
        if (character == '\\')
        {
            character = '/';
        }
    }

    if (input.front() == '/' || (input.size() >= 2 && input[1] == ':'))
    {
        throw std::invalid_argument("asset path must be relative");
    }

    std::vector<std::string> parts;
    std::size_t cursor = 0;
    while (cursor <= input.size())
    {
        const auto separator = input.find('/', cursor);
        const auto end = separator == std::string::npos ? input.size() : separator;
        auto part = input.substr(cursor, end - cursor);

        if (!part.empty() && part != ".")
        {
            if (part == "..")
            {
                throw std::invalid_argument("asset path cannot traverse to a parent");
            }
            for (char& character : part)
            {
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            }
            parts.push_back(std::move(part));
        }

        if (separator == std::string::npos)
        {
            break;
        }
        cursor = separator + 1;
    }

    if (parts.empty())
    {
        throw std::invalid_argument("asset path has no filename components");
    }

    std::string result;
    for (const auto& part : parts)
    {
        if (!result.empty())
        {
            result.push_back('/');
        }
        result += part;
    }
    return result;
}

} // namespace eaw
