#include "eaw/localization_scan.hpp"
#include "core/string_utils.hpp"
#include "core/file_io.hpp"
#include "formats/dat_string_table_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>

namespace eaw {
namespace {

struct ReferenceContext
{
    std::string group;
    std::set<std::string> locations;
};

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

Result<LocalizationScanResult> scan_mod_localization(const std::filesystem::path& mod_root, const DatStringTableData& baseline)
{
    std::error_code filesystem_error;
    auto xml_root = mod_root / "Data" / "XML";
    if (!std::filesystem::is_directory(xml_root, filesystem_error))
    {
        xml_root = mod_root / "Data" / "Xml";
    }
    if (!std::filesystem::is_directory(xml_root, filesystem_error))
    {
        return make_error("mod root does not contain Data/XML: " + mod_root.string());
    }

    std::map<std::string, ReferenceContext> references;
    std::filesystem::recursive_directory_iterator iterator(xml_root, filesystem_error);
    for (const std::filesystem::recursive_directory_iterator finished; !filesystem_error && iterator != finished; iterator.increment(filesystem_error))
    {
        const auto& item = *iterator;
        std::error_code type_error;
        if (!item.is_regular_file(type_error) || ascii_lower(item.path().extension().string()) != ".xml")
        {
            continue;
        }
        EAW_TRY_ASSIGN(auto contents, read_text_file(item.path()));
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
    if (filesystem_error)
    {
        return make_error("could not enumerate " + xml_root.string() + " (" + filesystem_error.message() + ")");
    }

    LocalizationScanResult result;
    result.reference_count = references.size();
    for (const auto& [key, context] : references)
    {
        if (dat_find(baseline, key) != nullptr)
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
