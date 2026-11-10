// Commands on localization workspaces (TSV) and mods: export, diff, build, scan-mod, add-unit.

#include "dat_editor.hpp"

#include "formats/dat_string_table.hpp"
#include "eaw/localization_scan.hpp"
#include "eaw/localization_workspace.hpp"
#include "formats/dat_string_table_helpers.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unordered_set>

namespace eaw::dat_editor {
namespace {

Status require_count(const Arguments& arguments, const std::size_t count, const std::string_view usage)
{
    if (arguments.positional.size() != count)
    {
        return make_error(std::string(usage));
    }
    return {};
}

// The rows of a workspace file, or none when it does not exist yet.
Result<std::vector<LocalizationRow>> existing_workspace(const std::filesystem::path& path)
{
    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error))
    {
        return std::vector<LocalizationRow>{};
    }
    return read_localization_workspace(path);
}

} // namespace

Result<int> export_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 2, "export requires a DAT path and workspace path"));
    EAW_TRY_ASSIGN(const auto dat, load_dat_string_table(arguments.positional[0]));
    EAW_TRY_ASSIGN(const auto rows, workspace_rows_from_dat(dat));
    EAW_TRY(write_localization_workspace(arguments.positional[1], rows, arguments.force));
    std::cout << "Exported " << rows.size() << " entries to " << arguments.positional[1] << '\n';
    return 0;
}

Result<int> diff_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 3, "diff requires base DAT, edited DAT, and workspace path"));
    EAW_TRY_ASSIGN(const auto base, load_dat_string_table(arguments.positional[0]));
    EAW_TRY_ASSIGN(const auto edited, load_dat_string_table(arguments.positional[1]));
    EAW_TRY_ASSIGN(const auto rows, diff_localization(base, edited));
    EAW_TRY(write_localization_workspace(arguments.positional[2], rows, arguments.force));
    std::cout << "Wrote " << rows.size() << " changed entries to " << arguments.positional[2] << '\n';
    return 0;
}

Result<int> build_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 3, "build requires base DAT, workspace, and output DAT"));
    EAW_TRY_ASSIGN(auto dat, load_dat_string_table(arguments.positional[0]));
    EAW_TRY_ASSIGN(const auto rows, read_localization_workspace(arguments.positional[1]));
    EAW_TRY(apply_localization_workspace(dat, rows, arguments.allow_todo));
    const auto issues = dat_validate(dat);
    const auto errors = std::ranges::count_if(issues,
                                              [](const DatIssue& issue)
                                              {
                                                  return issue.severity == DatIssueSeverity::error;
                                              });
    if (errors != 0)
    {
        return make_error("built DAT failed validation with " + std::to_string(errors) + " errors");
    }
    EAW_TRY(save_dat_string_table(arguments.positional[2], dat, arguments.force ? WriteMode::replace : WriteMode::create_only));
    std::cout << "Built " << arguments.positional[2] << " with " << dat.entries.size() << " entries\n";
    return 0;
}

Result<int> scan_mod_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 3, "scan-mod requires mod root, base DAT, and workspace path"));
    EAW_TRY_ASSIGN(const auto base, load_dat_string_table(arguments.positional[1]));
    EAW_TRY_ASSIGN(const auto scan, scan_mod_localization(arguments.positional[0], base));
    // Missing keys join the workspace unless it already has them.
    EAW_TRY_ASSIGN(auto rows, existing_workspace(arguments.positional[2]));
    std::unordered_set<std::string> existing;
    for (const auto& row : rows)
    {
        existing.insert(row.key);
    }
    std::size_t added = 0;
    for (const auto& row : scan.missing)
    {
        if (!existing.contains(row.key))
        {
            rows.push_back(row);
            ++added;
        }
    }
    sort_localization_rows(rows);
    EAW_TRY(write_localization_workspace(arguments.positional[2], rows, arguments.force));
    std::cout << "Scanned " << scan.reference_count << " unique XML references; added " << added << " missing keys to " << arguments.positional[2] << '\n';
    return 0;
}

Result<int> add_unit_command(const Arguments& arguments)
{
    EAW_TRY(require_count(arguments, 4, "add-unit requires workspace, unit id, name, and description"));
    const auto& workspace = arguments.positional[0];
    EAW_TRY_ASSIGN(auto rows, existing_workspace(workspace));
    const auto id = normalize_unit_id(arguments.positional[1]);
    if (id.empty())
    {
        return make_error("unit id cannot be empty");
    }
    for (const auto& addition : unit_localization_rows(id, arguments.positional[2], arguments.positional[3]))
    {
        const auto found = std::ranges::find(rows, addition.key, &LocalizationRow::key);
        if (found != rows.end() && !arguments.force)
        {
            return make_error("workspace already contains " + addition.key + "; pass --force to replace it");
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
    sort_localization_rows(rows);
    EAW_TRY(write_localization_workspace(workspace, rows, arguments.force));
    std::cout << "Added TEXT_UNIT_" << id << " and TEXT_TOOLTIP_" << id << " to " << workspace << '\n';
    return 0;
}

} // namespace eaw::dat_editor
