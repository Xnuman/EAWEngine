#pragma once

#include "eaw/dat_file.hpp"

#include <filesystem>
#include <string>
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
[[nodiscard]] Result<std::vector<LocalizationRow>> read_localization_workspace(const std::filesystem::path& path);
[[nodiscard]] Status write_localization_workspace(const std::filesystem::path& path, const std::vector<LocalizationRow>& rows, bool overwrite = false);
[[nodiscard]] Result<std::vector<LocalizationRow>> diff_localization(const DatFile& base, const DatFile& edited);
// Stops at the first row that cannot be applied (an unresolved TODO unless allowed, invalid UTF-8).
[[nodiscard]] Status apply_localization_workspace(DatFile& target, const std::vector<LocalizationRow>& rows, bool allow_todo = false);

} // namespace eaw
