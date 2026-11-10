#pragma once

#include "core/status.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

struct MegEntry
{
    std::string path;
    std::uint32_t crc{};
    std::uint32_t index{};
    std::uint32_t size{};
    std::uint32_t offset{};
};

class MegArchive final
{
public:
    // Reads the archive's index; the error names the archive and what is malformed.
    [[nodiscard]] static Result<MegArchive> open(std::filesystem::path archive_path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::span<const MegEntry> entries() const noexcept;
    // Nothing for paths that are absent or not valid asset paths.
    [[nodiscard]] const MegEntry* find(std::string_view virtual_path) const;
    [[nodiscard]] Result<std::vector<std::byte>> read(const MegEntry& entry) const;

private:
    MegArchive() = default;

    std::filesystem::path path_;
    std::vector<MegEntry> entries_;
    std::unordered_map<std::string, std::size_t> lookup_;
};

} // namespace eaw
