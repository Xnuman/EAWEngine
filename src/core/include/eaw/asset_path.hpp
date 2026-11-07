#pragma once

#include <string>
#include <string_view>

namespace eaw {

// Produces the case-insensitive, slash-normalized namespace used by Alamo data.
// Absolute paths and parent traversal are rejected.
[[nodiscard]] std::string normalize_asset_path(std::string_view path);

} // namespace eaw

