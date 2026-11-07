#pragma once

#include "eaw/status.hpp"

#include <string>
#include <string_view>

namespace eaw {

// Produces the case-insensitive, slash-normalized namespace used by Alamo data. Empty paths,
// absolute paths, and parent traversal are errors.
[[nodiscard]] Result<std::string> normalize_asset_path(std::string_view path);

} // namespace eaw
