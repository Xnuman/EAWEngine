#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct DatEntry
{
    std::uint32_t crc{};
    std::string key;
    std::u16string value;
};

enum class DatIssueSeverity
{
    warning,
    error,
};

struct DatIssue
{
    DatIssueSeverity severity{};
    std::size_t entry{};
    std::string message;
};

[[nodiscard]] std::uint32_t dat_key_crc(std::string_view key) noexcept;

class DatFile final
{
public:
    [[nodiscard]] static DatFile load(const std::filesystem::path& path);
    [[nodiscard]] static DatFile parse(std::span<const std::byte> bytes);

    [[nodiscard]] std::span<const DatEntry> entries() const noexcept;
    [[nodiscard]] bool crc_sorted() const noexcept;
    [[nodiscard]] const DatEntry* find(std::string_view key) const noexcept;
    [[nodiscard]] DatEntry* find(std::string_view key) noexcept;

    void upsert(std::string key, std::u16string value);
    [[nodiscard]] bool erase(std::string_view key);
    [[nodiscard]] std::vector<DatIssue> validate() const;
    [[nodiscard]] std::vector<std::byte> serialize() const;
    void save(const std::filesystem::path& path, bool overwrite = false) const;

private:
    std::vector<DatEntry> entries_;
    bool crc_sorted_{true};
};

} // namespace eaw
