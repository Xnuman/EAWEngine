#pragma once

#include "core/file_io.hpp"
#include "formats/dat_string_table.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct LocalizationRow
{
    std::string key;
    std::string text;
    std::string group;
    std::string status;
    std::string context;
    std::string notes;
};

// Errors name the file, the malformed TSV structure, or the entry that could not be converted.
// A DAT string table from disk, and back (file_io plus the format's reader and writer).
[[nodiscard]] Result<DatFile> load_dat_file(const std::filesystem::path& path);
[[nodiscard]] Status save_dat_file(const std::filesystem::path& path, const DatFile& dat, WriteMode mode);

[[nodiscard]] Result<std::vector<LocalizationRow>> read_localization_workspace(const std::filesystem::path& path);
[[nodiscard]] Status write_localization_workspace(const std::filesystem::path& path, const std::vector<LocalizationRow>& rows, bool overwrite = false);
[[nodiscard]] Result<std::vector<LocalizationRow>> diff_localization(const DatFile& base, const DatFile& edited);
// Stops at the first row that cannot be applied (an unresolved TODO unless allowed, invalid UTF-8).
[[nodiscard]] Status apply_localization_workspace(DatFile& target, const std::vector<LocalizationRow>& rows, bool allow_todo = false);

// Every entry of a DAT as an "existing" workspace row, sorted by key; errors name an entry whose text
// is not valid UTF-16.
[[nodiscard]] Result<std::vector<LocalizationRow>> workspace_rows_from_dat(const DatFile& dat);

// Workspace order: by group, then key.
void sort_localization_rows(std::vector<LocalizationRow>& rows);

// A unit id as localization keys use it: ASCII letters and digits upper-cased, every other character
// an underscore, runs of underscores collapsed, and none at either end ("republic venator" ->
// "REPUBLIC_VENATOR"). Empty when nothing usable remains.
[[nodiscard]] std::string normalize_unit_id(std::string_view id);

// The display-name (TEXT_UNIT_<id>) and description (TEXT_TOOLTIP_<id>) rows of a new unit, marked
// ready, for a normalized id.
[[nodiscard]] std::array<LocalizationRow, 2> unit_localization_rows(const std::string& id, std::string name, std::string description);

} // namespace eaw
