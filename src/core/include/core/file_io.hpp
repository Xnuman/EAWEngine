#pragma once

// Whole-file reads and writes on the local file system. The VFS, the tools, and the engine use these;
// format readers and writers never do I/O and take or give bytes instead. Errors name the path.

#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

// Larger files are refused rather than allocated (an engine choice: far above any stock asset;
// MEG archives are read in ranges, never through these).
inline constexpr std::uint64_t default_maximum_file_bytes = 1ULL << 30U;

[[nodiscard]] Result<std::vector<std::byte>> read_file(const std::filesystem::path& path, std::uint64_t maximum_bytes = default_maximum_file_bytes);
[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path, std::uint64_t maximum_bytes = default_maximum_file_bytes);

enum class WriteMode : std::uint8_t
{
    create_only, // refuse to replace an existing file
    replace,
};

// Writes a sibling temporary file and renames it over `path`, so a failed write never leaves a
// partial file and keeps any previous one. Missing parent directories are created.
[[nodiscard]] Status write_file(const std::filesystem::path& path, std::span<const std::byte> bytes, WriteMode mode);
[[nodiscard]] Status write_text_file(const std::filesystem::path& path, std::string_view text, WriteMode mode);

} // namespace eaw
