#include "vfs/asset_path.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace eaw {
namespace {

// Windows device names, which address a device instead of a file even with an extension.
bool is_device_name(std::string_view component)
{
    component = component.substr(0, component.find('.'));
    static constexpr std::array<std::string_view, 4> plain{"CON", "PRN", "AUX", "NUL"};
    if (std::ranges::find(plain, component) != plain.end())
    {
        return true;
    }
    return component.size() == 4 && (component.starts_with("COM") || component.starts_with("LPT")) && component[3] >= '1' && component[3] <= '9';
}

} // namespace

Result<std::string> normalize_asset_path(const std::string_view path)
{
    if (path.empty())
    {
        return make_error("asset path cannot be empty");
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
        return make_error("asset path must be relative: " + std::string(path));
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
                return make_error("asset path cannot traverse to a parent: " + std::string(path));
            }
            for (char& character : part)
            {
                character = ascii_upper(character);
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
        return make_error("asset path has no filename components: " + std::string(path));
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

Result<std::filesystem::path> asset_output_path(const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto normalized, normalize_asset_path(path));
    if (normalized.find(':') != std::string::npos)
    {
        return make_error("asset path holds a colon: " + std::string(path));
    }
    std::filesystem::path relative;
    std::size_t start = 0;
    while (start <= normalized.size())
    {
        const auto end = std::min(normalized.find('/', start), normalized.size());
        const auto component = std::string_view(normalized).substr(start, end - start);
        if (is_device_name(component))
        {
            return make_error("asset path names a Windows device: " + std::string(path));
        }
        relative /= std::string(component);
        start = end + 1;
    }
    return relative;
}

} // namespace eaw
