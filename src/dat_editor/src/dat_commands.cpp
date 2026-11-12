// Commands on one DAT file: info, list, get, set, remove, validate.

#include "core/string_utils.hpp"
#include "formats/dat_string_table.hpp"
#include "formats/dat_string_table_helpers.hpp"
#include "localization/localization_validation.hpp"
#include "localization/localization_workspace.hpp"
#include "dat_editor/dat_editor.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace eaw::dat_editor {
namespace {

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

std::size_t error_count(const std::vector<DatIssue>& issues)
{
    return static_cast<std::size_t>(std::ranges::count_if(issues,
                                                          [](const DatIssue& issue)
                                                          {
                                                              return issue.severity == DatIssueSeverity::error;
                                                          }));
}

} // namespace

Result<int> info_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 1, "info requires one DAT path"));
    EAW_TRY_ASSIGN(const auto dat, load_dat_string_table(arguments.positional[0]));
    const auto issues = dat_validate(dat);
    std::cout << "File: " << arguments.positional[0] << '\n'
              << "Entries: " << dat.entries.size() << '\n'
              << "CRC sorted: " << (dat.crc_sorted ? "yes" : "no") << '\n'
              << "Validation issues: " << issues.size() << '\n';
    return error_count(issues) == 0 ? 0 : 2;
}

Result<int> list_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 1, "list requires one DAT path"));
    EAW_TRY_ASSIGN(const auto dat, load_dat_string_table(arguments.positional[0]));
    const auto query = arguments.filter ? ascii_lower(*arguments.filter) : std::string{};
    std::size_t matched = 0;
    std::size_t shown = 0;
    for (const auto& entry : dat.entries)
    {
        auto converted = utf16_to_utf8(entry.value);
        if (!converted)
        {
            return make_error("DAT entry " + entry.key + " is not valid UTF-16");
        }
        const auto& value = *converted;
        if (!query.empty() && ascii_lower(entry.key).find(query) == std::string::npos && ascii_lower(value).find(query) == std::string::npos)
        {
            continue;
        }
        ++matched;
        if (shown++ < arguments.limit)
        {
            std::cout << entry.key << '\t' << one_line(value) << '\n';
        }
    }
    std::cerr << "showing " << std::min(arguments.limit, matched) << " of " << matched << " matching entries\n";
    return 0;
}

Result<int> get_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 2, "get requires a DAT path and key"));
    EAW_TRY_ASSIGN(const auto dat, load_dat_string_table(arguments.positional[0]));
    const auto* entry = dat_find(dat, arguments.positional[1]);
    if (entry == nullptr)
    {
        std::cerr << "key not found: " << arguments.positional[1] << '\n';
        return 3;
    }
    auto value = utf16_to_utf8(entry->value);
    if (!value)
    {
        return make_error("DAT entry " + entry->key + " is not valid UTF-16");
    }
    std::cout << *value << '\n';
    return 0;
}

Result<int> set_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 4, "set requires input DAT, output DAT, key, and text"));
    EAW_TRY_ASSIGN(auto dat, load_dat_string_table(arguments.positional[0]));
    auto text = utf8_to_utf16(arguments.positional[3]);
    if (!text)
    {
        return make_error("the text for " + arguments.positional[2] + " is not valid UTF-8");
    }
    EAW_TRY(dat_upsert(dat, arguments.positional[2], std::move(*text)));
    EAW_TRY(save_dat_string_table(arguments.positional[1], dat, arguments.force ? WriteMode::replace : WriteMode::create_only));
    std::cout << "Wrote " << arguments.positional[1] << " with " << dat.entries.size() << " entries\n";
    return 0;
}

Result<int> remove_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 3, "remove requires input DAT, output DAT, and key"));
    EAW_TRY_ASSIGN(auto dat, load_dat_string_table(arguments.positional[0]));
    if (!dat_erase(dat, arguments.positional[2]))
    {
        return make_error("key not found: " + arguments.positional[2]);
    }
    EAW_TRY(save_dat_string_table(arguments.positional[1], dat, arguments.force ? WriteMode::replace : WriteMode::create_only));
    std::cout << "Wrote " << arguments.positional[1] << " with " << dat.entries.size() << " entries\n";
    return 0;
}

Result<int> validate_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 1, "validate requires one DAT path"));
    EAW_TRY_ASSIGN(const auto dat, load_dat_string_table(arguments.positional[0]));
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const auto& issue : dat_validate(dat))
    {
        const bool error = issue.severity == DatIssueSeverity::error;
        (error ? errors : warnings)++;
        std::cout << (error ? "error" : "warning") << " entry " << issue.entry << ": " << issue.message << '\n';
    }
    if (arguments.reference)
    {
        EAW_TRY_ASSIGN(const auto reference, load_dat_string_table(std::filesystem::path(*arguments.reference)));
        EAW_TRY_ASSIGN(const auto issues, compare_translation(dat, reference));
        for (const auto& issue : issues)
        {
            const bool error = issue.severity == DatIssueSeverity::error;
            (error ? errors : warnings)++;
            std::cout << (error ? "error " : "warning ") << issue.key << ": " << issue.message << '\n';
        }
    }
    std::cout << "Validation complete: " << errors << " errors, " << warnings << " warnings\n";
    return errors == 0 ? 0 : 2;
}

} // namespace eaw::dat_editor
