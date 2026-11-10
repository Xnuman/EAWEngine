#pragma once

#include "core/status.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace eaw {

// Produces the case-insensitive, slash-normalized namespace used by Alamo data. Empty paths,
// absolute paths, and parent traversal are errors.
[[nodiscard]] Result<std::string> normalize_asset_path(std::string_view path);

// The relative file-system path an asset is written to when copied out of the VFS (its normalized
// virtual path). Archive entry names are untrusted, so besides normalize_asset_path's errors a
// name holding a colon (a drive or an NTFS stream) or naming a Windows device (CON, NUL, COM1,
// ...) is an error: such a name could never be written where it claims to go.
[[nodiscard]] Result<std::filesystem::path> asset_output_path(std::string_view path);

} // namespace eaw
