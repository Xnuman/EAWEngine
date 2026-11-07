#include "eaw/dat_file.hpp"
#include "eaw/localization_workspace.hpp"
#include "eaw/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void usage()
{
    std::cout << "EAWDatEditor - safe Petroglyph DAT localization workflow\n\n"
              << "Usage:\n"
              << "  EAWDatEditor info <file.dat>\n"
              << "  EAWDatEditor list <file.dat> [--filter text] [--limit count]\n"
              << "  EAWDatEditor get <file.dat> <key>\n"
              << "  EAWDatEditor set <input.dat> <output.dat> <key> <text> [--force]\n"
              << "  EAWDatEditor remove <input.dat> <output.dat> <key> [--force]\n"
              << "  EAWDatEditor validate <file.dat> [--reference original.dat]\n"
              << "  EAWDatEditor export <file.dat> <workspace.tsv> [--force]\n"
              << "  EAWDatEditor diff <base.dat> <edited.dat> <workspace.tsv> [--force]\n"
              << "  EAWDatEditor build <base.dat> <workspace.tsv> <output.dat>\n"
              << "                     [--allow-todo] [--force]\n"
              << "  EAWDatEditor scan-mod <mod-root> <base.dat> <workspace.tsv> [--force]\n"
              << "  EAWDatEditor add-unit <workspace.tsv> <unit-id> <name> <description>\n"
              << "                        [--force]\n\n"
              << "Workspace columns are Key, Text, Group, Status, Context, and Notes.\n"
              << "Status 'todo' blocks builds unless --allow-todo is supplied; 'deleted'\n"
              << "removes a key. Output files are never replaced without --force.\n";
}

bool take_flag(std::vector<std::string>& arguments, const std::string_view flag)
{
    const auto found = std::ranges::find(arguments, flag);
    if (found == arguments.end())
    {
        return false;
    }
    arguments.erase(found);
    return true;
}

std::optional<std::string> take_option(std::vector<std::string>& arguments, const std::string_view option)
{
    const auto found = std::ranges::find(arguments, option);
    if (found == arguments.end())
    {
        return std::nullopt;
    }
    if (found + 1 == arguments.end())
    {
        throw std::invalid_argument("missing value after " + std::string(option));
    }
    const auto value = *(found + 1);
    arguments.erase(found, found + 2);
    return value;
}

std::size_t parse_size(const std::string& value)
{
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed);
    if (consumed != value.size())
    {
        throw std::invalid_argument("invalid number: " + value);
    }
    return static_cast<std::size_t>(parsed);
}

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
        throw std::runtime_error("could not open text file: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

std::string one_line(std::string value, const std::size_t maximum = 160)
{
    std::ranges::replace(value, '\n', ' ');
    std::ranges::replace(value, '\r', ' ');
    std::ranges::replace(value, '\t', ' ');
    if (value.size() > maximum)
    {
        value.resize(maximum - 3);
        value += "...";
    }
    return value;
}

eaw::Result<std::multiset<std::string>> formatting_tokens(const std::u16string& text)
{
    EAW_TRY_ASSIGN(const auto utf8, eaw::utf16_to_utf8(text));
    static const std::regex token(R"((?:%(?:\d+\$)?[-+0# ]*(?:\d+|\*)?(?:\.\d+)?[A-Za-z%]|\\[nr]|\[[^\]\r\n]+\]|<[^>\r\n]+>|\{[^}\r\n]+\}))");
    std::multiset<std::string> result;
    for (auto match = std::sregex_iterator(utf8.begin(), utf8.end(), token); match != std::sregex_iterator(); ++match)
    {
        result.insert(match->str());
    }
    return result;
}

bool edge_whitespace_matches(const std::u16string& reference, const std::u16string& translation)
{
    const auto whitespace = [](const char16_t character)
    {
        return character == u' ' || character == u'\t' || character == u'\r' || character == u'\n';
    };
    const auto leading = [&](const std::u16string& value)
    {
        return static_cast<std::size_t>(std::ranges::find_if_not(value, whitespace) - value.begin());
    };
    const auto trailing = [&](const std::u16string& value)
    {
        return static_cast<std::size_t>(std::ranges::find_if_not(value.rbegin(), value.rend(), whitespace) - value.rbegin());
    };
    return leading(reference) == leading(translation) && trailing(reference) == trailing(translation);
}

// Reports the same way main's handler does: "DAT editor error: ..." and exit code 1.
int report_error(const eaw::Error& error)
{
    std::cerr << "DAT editor error: " << error.message << '\n';
    return 1;
}

eaw::Result<int> validate_dat(const eaw::DatFile& dat, const std::optional<std::filesystem::path>& reference_path)
{
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const auto& issue : dat.validate())
    {
        const bool error = issue.severity == eaw::DatIssueSeverity::error;
        (error ? errors : warnings)++;
        std::cout << (error ? "error" : "warning") << " entry " << issue.entry << ": " << issue.message << '\n';
    }

    if (reference_path)
    {
        EAW_TRY_ASSIGN(const auto reference, eaw::DatFile::load(*reference_path));
        for (const auto& original : reference.entries())
        {
            const auto* translated = dat.find(original.key);
            if (translated == nullptr)
            {
                ++errors;
                std::cout << "error " << original.key << ": key is missing from translation\n";
                continue;
            }
            EAW_TRY_ASSIGN(const auto original_tokens, formatting_tokens(original.value));
            EAW_TRY_ASSIGN(const auto translated_tokens, formatting_tokens(translated->value));
            if (original_tokens != translated_tokens)
            {
                ++errors;
                std::cout << "error " << original.key << ": formatting tokens differ from reference\n";
            }
            if (!edge_whitespace_matches(original.value, translated->value))
            {
                ++warnings;
                std::cout << "warning " << original.key << ": leading/trailing whitespace differs from reference\n";
            }
        }
        for (const auto& translated : dat.entries())
        {
            if (reference.find(translated.key) == nullptr)
            {
                ++warnings;
                std::cout << "warning " << translated.key << ": key is not present in reference\n";
            }
        }
    }
    std::cout << "Validation complete: " << errors << " errors, " << warnings << " warnings\n";
    return errors == 0 ? 0 : 2;
}

eaw::Result<std::vector<eaw::LocalizationRow>> export_rows(const eaw::DatFile& dat)
{
    std::vector<eaw::LocalizationRow> rows;
    rows.reserve(dat.entries().size());
    for (const auto& entry : dat.entries())
    {
        auto text = eaw::utf16_to_utf8(entry.value);
        if (!text)
        {
            return eaw::with_context("DAT entry " + entry.key, std::move(text).error());
        }
        rows.push_back({entry.key, std::move(text).value(), {}, "existing", {}, {}});
    }
    std::ranges::sort(rows, {}, &eaw::LocalizationRow::key);
    return rows;
}

struct ReferenceContext
{
    std::string group;
    std::set<std::string> locations;
};

std::map<std::string, ReferenceContext> scan_xml_references(const std::filesystem::path& mod_root)
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

    std::map<std::string, ReferenceContext> result;
    for (const auto& item : std::filesystem::recursive_directory_iterator(xml_root))
    {
        if (!item.is_regular_file())
        {
            continue;
        }
        auto extension = lower(item.path().extension().string());
        if (extension != ".xml")
        {
            continue;
        }
        auto contents = read_text_file(item.path());
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
            auto& context = result[key];
            context.group = item.path().stem().string();
            context.locations.insert(relative + ":" + std::to_string(line));
            position = end;
        }
    }
    return result;
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

std::string normalize_unit_id(std::string id)
{
    for (char& character : id)
    {
        const auto byte = static_cast<unsigned char>(character);
        character = std::isalnum(byte) ? static_cast<char>(std::toupper(byte)) : '_';
    }
    while (id.find("__") != std::string::npos)
    {
        id.replace(id.find("__"), 2, "_");
    }
    if (id.empty())
    {
        throw std::invalid_argument("unit id cannot be empty");
    }
    return id;
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try
    {
        if (argc < 2)
        {
            usage();
            return 1;
        }
        std::vector<std::string> arguments(argv + 2, argv + argc);
        const std::string command = argv[1];
        if (command == "--help" || command == "-h" || command == "help")
        {
            usage();
            return 0;
        }
        const bool force = take_flag(arguments, "--force");
        const bool allow_todo = take_flag(arguments, "--allow-todo");
        const auto filter = take_option(arguments, "--filter");
        const auto limit_option = take_option(arguments, "--limit");
        const auto reference_option = take_option(arguments, "--reference");
        const auto limit = limit_option ? parse_size(*limit_option) : 200U;

        if (command == "info")
        {
            if (arguments.size() != 1)
            {
                throw std::invalid_argument("info requires one DAT path");
            }
            const auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            const auto& dat = *loaded;
            const auto issues = dat.validate();
            const auto errors = std::ranges::count_if(issues,
                                                      [](const auto& issue)
                                                      {
                                                          return issue.severity == eaw::DatIssueSeverity::error;
                                                      });
            std::cout << "File: " << arguments[0] << '\n'
                      << "Entries: " << dat.entries().size() << '\n'
                      << "CRC sorted: " << (dat.crc_sorted() ? "yes" : "no") << '\n'
                      << "Validation issues: " << issues.size() << '\n';
            return errors == 0 ? 0 : 2;
        }
        if (command == "list")
        {
            if (arguments.size() != 1)
            {
                throw std::invalid_argument("list requires one DAT path");
            }
            const auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            const auto& dat = *loaded;
            const auto query = filter ? lower(*filter) : std::string{};
            std::size_t matched = 0;
            std::size_t shown = 0;
            for (const auto& entry : dat.entries())
            {
                const auto converted = eaw::utf16_to_utf8(entry.value);
                if (!converted)
                {
                    return report_error(eaw::with_context("DAT entry " + entry.key, converted.error()));
                }
                const auto& value = *converted;
                if (!query.empty() && lower(entry.key).find(query) == std::string::npos && lower(value).find(query) == std::string::npos)
                {
                    continue;
                }
                ++matched;
                if (shown++ < limit)
                {
                    std::cout << entry.key << '\t' << one_line(value) << '\n';
                }
            }
            std::cerr << "showing " << std::min(limit, matched) << " of " << matched << " matching entries\n";
            return 0;
        }
        if (command == "get")
        {
            if (arguments.size() != 2)
            {
                throw std::invalid_argument("get requires a DAT path and key");
            }
            const auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            const auto* entry = loaded->find(arguments[1]);
            if (entry == nullptr)
            {
                std::cerr << "key not found: " << arguments[1] << '\n';
                return 3;
            }
            const auto value = eaw::utf16_to_utf8(entry->value);
            if (!value)
            {
                return report_error(eaw::with_context("DAT entry " + entry->key, value.error()));
            }
            std::cout << *value << '\n';
            return 0;
        }
        if (command == "set")
        {
            if (arguments.size() != 4)
            {
                throw std::invalid_argument("set requires input DAT, output DAT, key, and text");
            }
            auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            auto& dat = *loaded;
            auto text = eaw::utf8_to_utf16(arguments[3]);
            if (!text)
            {
                return report_error(text.error());
            }
            if (const auto status = dat.upsert(arguments[2], std::move(text).value()); !status)
            {
                return report_error(status.error());
            }
            if (const auto status = dat.save(arguments[1], force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Wrote " << arguments[1] << " with " << dat.entries().size() << " entries\n";
            return 0;
        }
        if (command == "remove")
        {
            if (arguments.size() != 3)
            {
                throw std::invalid_argument("remove requires input DAT, output DAT, and key");
            }
            auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            auto& dat = *loaded;
            if (!dat.erase(arguments[2]))
            {
                throw std::runtime_error("key not found: " + arguments[2]);
            }
            if (const auto status = dat.save(arguments[1], force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Wrote " << arguments[1] << " with " << dat.entries().size() << " entries\n";
            return 0;
        }
        if (command == "validate")
        {
            if (arguments.size() != 1)
            {
                throw std::invalid_argument("validate requires one DAT path");
            }
            const auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            const auto result = validate_dat(*loaded, reference_option ? std::optional<std::filesystem::path>(*reference_option) : std::nullopt);
            return result ? *result : report_error(result.error());
        }
        if (command == "export")
        {
            if (arguments.size() != 2)
            {
                throw std::invalid_argument("export requires a DAT path and workspace path");
            }
            const auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            const auto exported = export_rows(*loaded);
            if (!exported)
            {
                return report_error(exported.error());
            }
            const auto& rows = *exported;
            if (const auto status = eaw::write_localization_workspace(arguments[1], rows, force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Exported " << rows.size() << " entries to " << arguments[1] << '\n';
            return 0;
        }
        if (command == "diff")
        {
            if (arguments.size() != 3)
            {
                throw std::invalid_argument("diff requires base DAT, edited DAT, and workspace path");
            }
            const auto base = eaw::DatFile::load(arguments[0]);
            if (!base)
            {
                return report_error(base.error());
            }
            const auto edited = eaw::DatFile::load(arguments[1]);
            if (!edited)
            {
                return report_error(edited.error());
            }
            const auto difference = eaw::diff_localization(*base, *edited);
            if (!difference)
            {
                return report_error(difference.error());
            }
            const auto& rows = *difference;
            if (const auto status = eaw::write_localization_workspace(arguments[2], rows, force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Wrote " << rows.size() << " changed entries to " << arguments[2] << '\n';
            return 0;
        }
        if (command == "build")
        {
            if (arguments.size() != 3)
            {
                throw std::invalid_argument("build requires base DAT, workspace, and output DAT");
            }
            auto loaded = eaw::DatFile::load(arguments[0]);
            if (!loaded)
            {
                return report_error(loaded.error());
            }
            auto& dat = *loaded;
            const auto rows = eaw::read_localization_workspace(arguments[1]);
            if (!rows)
            {
                return report_error(rows.error());
            }
            if (const auto status = eaw::apply_localization_workspace(dat, *rows, allow_todo); !status)
            {
                return report_error(status.error());
            }
            const auto issues = dat.validate();
            const auto errors = std::ranges::count_if(issues,
                                                      [](const auto& issue)
                                                      {
                                                          return issue.severity == eaw::DatIssueSeverity::error;
                                                      });
            if (errors != 0)
            {
                throw std::runtime_error("built DAT failed validation with " + std::to_string(errors) + " errors");
            }
            if (const auto status = dat.save(arguments[2], force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Built " << arguments[2] << " with " << dat.entries().size() << " entries\n";
            return 0;
        }
        if (command == "scan-mod")
        {
            if (arguments.size() != 3)
            {
                throw std::invalid_argument("scan-mod requires mod root, base DAT, and workspace path");
            }
            const auto base = eaw::DatFile::load(arguments[1]);
            if (!base)
            {
                return report_error(base.error());
            }
            auto references = scan_xml_references(arguments[0]);
            std::vector<eaw::LocalizationRow> rows;
            if (std::filesystem::exists(arguments[2]))
            {
                auto existing_rows = eaw::read_localization_workspace(arguments[2]);
                if (!existing_rows)
                {
                    return report_error(existing_rows.error());
                }
                rows = std::move(existing_rows).value();
            }
            std::unordered_map<std::string, std::size_t> existing;
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                existing.emplace(rows[index].key, index);
            }
            std::size_t missing = 0;
            for (const auto& [key, context] : references)
            {
                if (base->find(key) != nullptr || existing.contains(key))
                {
                    continue;
                }
                rows.push_back({key, {}, context.group, "todo", join_locations(context.locations), "Referenced by mod XML but missing from the baseline DAT"});
                ++missing;
            }
            std::ranges::sort(rows,
                              [](const auto& left, const auto& right)
                              {
                                  return std::tie(left.group, left.key) < std::tie(right.group, right.key);
                              });
            if (const auto status = eaw::write_localization_workspace(arguments[2], rows, force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Scanned " << references.size() << " unique XML references; added " << missing << " missing keys to " << arguments[2] << '\n';
            return 0;
        }
        if (command == "add-unit")
        {
            if (arguments.size() != 4)
            {
                throw std::invalid_argument("add-unit requires workspace, unit id, name, and description");
            }
            std::vector<eaw::LocalizationRow> rows;
            if (std::filesystem::exists(arguments[0]))
            {
                auto existing_rows = eaw::read_localization_workspace(arguments[0]);
                if (!existing_rows)
                {
                    return report_error(existing_rows.error());
                }
                rows = std::move(existing_rows).value();
            }
            const auto id = normalize_unit_id(arguments[1]);
            const std::array additions{eaw::LocalizationRow{"TEXT_UNIT_" + id, arguments[2], "unit:" + id, "ready", "Use in <Text_ID>", "Generated unit display-name key"},
                                       eaw::LocalizationRow{"TEXT_TOOLTIP_" + id, arguments[3], "unit:" + id, "ready", "Use in <Encyclopedia_Text>", "Generated unit description key"}};
            for (const auto& addition : additions)
            {
                const auto found = std::ranges::find(rows, addition.key, &eaw::LocalizationRow::key);
                if (found != rows.end() && !force)
                {
                    throw std::runtime_error("workspace already contains " + addition.key + "; pass --force to replace it");
                }
                if (found == rows.end())
                {
                    rows.push_back(addition);
                }
                else
                {
                    *found = addition;
                }
            }
            std::ranges::sort(rows,
                              [](const auto& left, const auto& right)
                              {
                                  return std::tie(left.group, left.key) < std::tie(right.group, right.key);
                              });
            if (const auto status = eaw::write_localization_workspace(arguments[0], rows, force); !status)
            {
                return report_error(status.error());
            }
            std::cout << "Added TEXT_UNIT_" << id << " and TEXT_TOOLTIP_" << id << " to " << arguments[0] << '\n';
            return 0;
        }

        throw std::invalid_argument("unknown command: " + command);
    }
    catch (const std::exception& error)
    {
        std::cerr << "DAT editor error: " << error.what() << '\n';
        return 1;
    }
}
