#pragma once

#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
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
    // Errors name the malformed part of the table.
    [[nodiscard]] static Result<DatFile> parse(std::span<const std::byte> bytes);

    [[nodiscard]] std::span<const DatEntry> entries() const noexcept;
    [[nodiscard]] bool crc_sorted() const noexcept;
    [[nodiscard]] const DatEntry* find(std::string_view key) const noexcept;
    [[nodiscard]] DatEntry* find(std::string_view key) noexcept;

    // An empty key is an error.
    [[nodiscard]] Status upsert(std::string key, std::u16string value);
    [[nodiscard]] bool erase(std::string_view key);
    [[nodiscard]] std::vector<DatIssue> validate() const;
    [[nodiscard]] Result<std::vector<std::byte>> serialize() const;

private:
    std::vector<DatEntry> entries_;
    bool crc_sorted_{true};
};

} // namespace eaw
