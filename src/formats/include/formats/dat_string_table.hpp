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

struct DatStringTableData
{
    // In file order. The stock tables are sorted by key CRC (crc_sorted), which lookups and inserts
    // keep; dat_validate reports a table whose entries were changed out of order.
    std::vector<DatEntry> entries;
    bool crc_sorted{true};
};

// Errors name the malformed part of the table.
[[nodiscard]] Result<DatStringTableData> read_dat(std::span<const std::byte> bytes);
[[nodiscard]] Result<std::vector<std::byte>> write_dat(const DatStringTableData& table);

} // namespace eaw
