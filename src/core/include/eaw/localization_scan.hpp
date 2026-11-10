#pragma once

#include "formats/dat_string_table.hpp"
#include "eaw/localization_workspace.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace eaw {

struct LocalizationScanResult
{
    std::size_t reference_count{};
    std::vector<LocalizationRow> missing;
};

// Scans active XML (comments excluded) for TEXT_* references that are absent
// from the supplied language DAT. Source locations are retained for editing.
// Errors name the missing XML directory or the file that could not be read.
[[nodiscard]] Result<LocalizationScanResult> scan_mod_localization(const std::filesystem::path& mod_root, const DatFile& baseline);

} // namespace eaw
