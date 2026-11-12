#pragma once

// Lookups, edits, and validation of DAT string tables. They keep a CRC-sorted table sorted, as the
// game expects; an unsorted table (some mod tools write them) is searched linearly.

#include "core/status.hpp"
#include "formats/dat_string_table.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace eaw {

[[nodiscard]] const DatEntry* dat_find(const DatStringTableData& table, std::string_view key) noexcept;
[[nodiscard]] DatEntry* dat_find(DatStringTableData& table, std::string_view key) noexcept;
// Replaces the value of `key`, or inserts it in CRC order. An empty key is an error.
[[nodiscard]] Status dat_upsert(DatStringTableData& table, std::string key, std::u16string value);
// False when the key is absent.
[[nodiscard]] bool dat_erase(DatStringTableData& table, std::string_view key);
// Problems the game or editors may trip over: empty or non-printable keys, CRC mismatches,
// duplicate keys, unpaired surrogates.
[[nodiscard]] std::vector<DatIssue> dat_validate(const DatStringTableData& table);

} // namespace eaw
