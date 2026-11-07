#pragma once

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
    explicit MegArchive(std::filesystem::path archive_path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::span<const MegEntry> entries() const noexcept;
    [[nodiscard]] const MegEntry* find(std::string_view virtual_path) const;
    [[nodiscard]] std::vector<std::byte> read(const MegEntry& entry) const;

private:
    std::filesystem::path path_;
    std::vector<MegEntry> entries_;
    std::unordered_map<std::string, std::size_t> lookup_;
};

} // namespace eaw
