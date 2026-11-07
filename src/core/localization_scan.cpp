#include "eaw/localization_scan.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace eaw {
namespace {

struct ReferenceContext
{
    std::string group;
    std::set<std::string> locations;
};

std::string lower(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("could not open XML file: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

void remove_xml_comments(std::string& contents)
{
    std::size_t comment = 0;
    while ((comment = contents.find("<!--", comment)) != std::string::npos)
    {
        const auto close = contents.find("-->", comment + 4);
        const auto end = close == std::string::npos ? contents.size() : close + 3;
        for (std::size_t index = comment; index < end; ++index)
        {
            if (contents[index] != '\r' && contents[index] != '\n')
            {
                contents[index] = ' ';
            }
        }
        comment = end;
    }
}

std::string join_locations(const std::set<std::string>& locations)
{
    std::string result;
    std::size_t included = 0;
    for (const auto& location : locations)
    {
        if (included++ == 5)
        {
            result += "; ...";
            break;
        }
        if (!result.empty())
        {
            result += "; ";
        }
        result += location;
    }
    return result;
}

} // namespace

LocalizationScanResult scan_mod_localization(const std::filesystem::path& mod_root, const DatFile& baseline)
{
    auto xml_root = mod_root / "Data" / "XML";
    if (!std::filesystem::is_directory(xml_root))
    {
        xml_root = mod_root / "Data" / "Xml";
    }
    if (!std::filesystem::is_directory(xml_root))
    {
        throw std::runtime_error("mod root does not contain Data/XML: " + mod_root.string());
    }

    std::map<std::string, ReferenceContext> references;
    for (const auto& item : std::filesystem::recursive_directory_iterator(xml_root))
    {
        if (!item.is_regular_file() || lower(item.path().extension().string()) != ".xml")
        {
            continue;
        }
        auto contents = read_text_file(item.path());
        remove_xml_comments(contents);
        std::vector<std::size_t> newlines;
        for (std::size_t index = 0; index < contents.size(); ++index)
        {
            if (contents[index] == '\n')
            {
                newlines.push_back(index);
            }
        }

        const auto relative = item.path().lexically_relative(mod_root).generic_string();
        std::size_t position = 0;
        while ((position = contents.find("TEXT_", position)) != std::string::npos)
        {
            std::size_t end = position + 5;
            while (end < contents.size())
            {
                const auto character = static_cast<unsigned char>(contents[end]);
                if (!std::isalnum(character) && character != '_' && character != '.' && character != '-')
                {
                    break;
                }
                ++end;
            }
            const auto key = contents.substr(position, end - position);
            const auto line = static_cast<std::size_t>(std::upper_bound(newlines.begin(), newlines.end(), position) - newlines.begin()) + 1;
            auto& context = references[key];
            context.group = item.path().stem().string();
            context.locations.insert(relative + ":" + std::to_string(line));
            position = end;
        }
    }

    LocalizationScanResult result;
    result.reference_count = references.size();
    for (const auto& [key, context] : references)
    {
        if (baseline.find(key) != nullptr)
        {
            continue;
        }
        result.missing.push_back({key, {}, context.group, "todo", join_locations(context.locations), "Referenced by mod XML but missing from the baseline DAT"});
    }
    std::ranges::sort(result.missing,
                      [](const auto& left, const auto& right)
                      {
                          return std::tie(left.group, left.key) < std::tie(right.group, right.key);
                      });
    return result;
}

} // namespace eaw
