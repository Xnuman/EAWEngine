#include "formats/dat_string_table_helpers.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace eaw {

const DatEntry* dat_find(const DatStringTableData& table, const std::string_view key) noexcept
{
    const auto crc = dat_key_crc(key);
    if (table.crc_sorted)
    {
        auto candidate = std::ranges::lower_bound(table.entries, crc, {}, &DatEntry::crc);
        for (; candidate != table.entries.end() && candidate->crc == crc; ++candidate)
        {
            if (candidate->key == key)
            {
                return &*candidate;
            }
        }
        return nullptr;
    }
    const auto found = std::ranges::find(table.entries, key, &DatEntry::key);
    return found == table.entries.end() ? nullptr : &*found;
}

DatEntry* dat_find(DatStringTableData& table, const std::string_view key) noexcept
{
    return const_cast<DatEntry*>(dat_find(std::as_const(table), key));
}

Status dat_upsert(DatStringTableData& table, std::string key, std::u16string value)
{
    if (key.empty())
    {
        return make_error("DAT key cannot be empty");
    }
    if (auto* existing = dat_find(table, key))
    {
        existing->value = std::move(value);
        return Status::success();
    }

    DatEntry entry{dat_key_crc(key), std::move(key), std::move(value)};
    if (table.crc_sorted)
    {
        const auto position = std::ranges::upper_bound(table.entries, entry.crc, {}, &DatEntry::crc);
        table.entries.insert(position, std::move(entry));
    }
    else
    {
        table.entries.push_back(std::move(entry));
    }
    return Status::success();
}

bool dat_erase(DatStringTableData& table, const std::string_view key)
{
    const auto found = std::ranges::find(table.entries, key, &DatEntry::key);
    if (found == table.entries.end())
    {
        return false;
    }
    table.entries.erase(found);
    return true;
}

std::vector<DatIssue> dat_validate(const DatStringTableData& table)
{
    std::vector<DatIssue> issues;
    std::unordered_map<std::string_view, std::size_t> keys;
    keys.reserve(table.entries.size());
    for (std::size_t index = 0; index < table.entries.size(); ++index)
    {
        const auto& entry = table.entries[index];
        if (entry.key.empty())
        {
            issues.push_back({DatIssueSeverity::error, index, "key is empty"});
        }
        if (std::ranges::any_of(entry.key,
                                [](const unsigned char character)
                                {
                                    return character < 0x20U || character > 0x7eU;
                                }))
        {
            issues.push_back({DatIssueSeverity::error, index, "key contains a non-printable or non-ASCII byte"});
        }
        if (entry.crc != dat_key_crc(entry.key))
        {
            issues.push_back({DatIssueSeverity::error, index, "stored CRC does not match the key"});
        }
        if (const auto [existing, inserted] = keys.emplace(entry.key, index); !inserted)
        {
            issues.push_back({DatIssueSeverity::warning, index, "duplicate key; first occurrence is entry " + std::to_string(existing->second)});
        }
        for (std::size_t character = 0; character < entry.value.size(); ++character)
        {
            const auto code_unit = static_cast<std::uint32_t>(entry.value[character]);
            if (code_unit >= 0xd800U && code_unit <= 0xdbffU)
            {
                if (character + 1 >= entry.value.size() || entry.value[character + 1] < 0xdc00U || entry.value[character + 1] > 0xdfffU)
                {
                    issues.push_back({DatIssueSeverity::error, index, "value contains an unpaired high surrogate"});
                    break;
                }
                ++character;
            }
            else if (code_unit >= 0xdc00U && code_unit <= 0xdfffU)
            {
                issues.push_back({DatIssueSeverity::error, index, "value contains an unpaired low surrogate"});
                break;
            }
        }
    }
    // Lookups and the game search a sorted table by CRC; a table claiming order without having it
    // (entries changed by hand) would miss keys.
    if (table.crc_sorted && !std::ranges::is_sorted(table.entries, {}, &DatEntry::crc))
    {
        issues.push_back({DatIssueSeverity::error, 0, "entries are marked CRC-sorted but are not in CRC order"});
    }
    return issues;
}

} // namespace eaw
