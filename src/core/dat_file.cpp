#include "eaw/dat_file.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eaw {
namespace {

struct IndexRecord
{
    std::uint32_t crc{};
    std::uint32_t value_characters{};
    std::uint32_t key_characters{};
};

std::uint32_t read_u32(const std::span<const std::byte> bytes, std::size_t& cursor)
{
    if (cursor + 4 > bytes.size())
    {
        throw std::runtime_error("truncated DAT file");
    }
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[cursor++])) << shift;
    }
    return value;
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

std::vector<std::byte> read_binary_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        throw std::runtime_error("could not open DAT file: " + path.string());
    }
    const auto end = stream.tellg();
    if (end < 0)
    {
        throw std::runtime_error("could not determine DAT file size: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        throw std::runtime_error("could not read DAT file: " + path.string());
    }
    return bytes;
}

} // namespace

std::uint32_t dat_key_crc(const std::string_view key) noexcept
{
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char character : key)
    {
        crc ^= character;
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            const auto mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

DatFile DatFile::load(const std::filesystem::path& path)
{
    const auto bytes = read_binary_file(path);
    return parse(bytes);
}

DatFile DatFile::parse(const std::span<const std::byte> bytes)
{
    std::size_t cursor = 0;
    const auto count = read_u32(bytes, cursor);
    constexpr std::uint32_t maximum_reasonable_entries = 5'000'000;
    if (count > maximum_reasonable_entries || static_cast<std::uint64_t>(count) * 12U + 4U > bytes.size())
    {
        throw std::runtime_error("invalid DAT entry count");
    }

    std::vector<IndexRecord> index;
    index.reserve(count);
    std::uint64_t total_value_characters = 0;
    std::uint64_t total_key_characters = 0;
    for (std::uint32_t entry = 0; entry < count; ++entry)
    {
        IndexRecord record;
        record.crc = read_u32(bytes, cursor);
        record.value_characters = read_u32(bytes, cursor);
        record.key_characters = read_u32(bytes, cursor);
        total_value_characters += record.value_characters;
        total_key_characters += record.key_characters;
        index.push_back(record);
    }

    const std::uint64_t expected_size = cursor + total_value_characters * 2U + total_key_characters;
    if (expected_size != bytes.size())
    {
        throw std::runtime_error(expected_size > bytes.size() ? "truncated DAT string data" : "unexpected trailing data in DAT file");
    }

    DatFile result;
    result.entries_.resize(count);
    for (std::size_t entry = 0; entry < count; ++entry)
    {
        result.entries_[entry].crc = index[entry].crc;
        auto& value = result.entries_[entry].value;
        value.reserve(index[entry].value_characters);
        for (std::uint32_t character = 0; character < index[entry].value_characters; ++character)
        {
            if (cursor + 2 > bytes.size())
            {
                throw std::runtime_error("truncated UTF-16 DAT value");
            }
            const auto low = std::to_integer<unsigned char>(bytes[cursor++]);
            const auto high = std::to_integer<unsigned char>(bytes[cursor++]);
            value.push_back(static_cast<char16_t>(low | (high << 8U)));
        }
    }
    for (std::size_t entry = 0; entry < count; ++entry)
    {
        const auto length = index[entry].key_characters;
        auto& key = result.entries_[entry].key;
        key.reserve(length);
        for (std::uint32_t character = 0; character < length; ++character)
        {
            key.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes[cursor++])));
        }
    }

    result.crc_sorted_ = std::ranges::is_sorted(result.entries_, {}, &DatEntry::crc);
    return result;
}

std::span<const DatEntry> DatFile::entries() const noexcept
{
    return entries_;
}

bool DatFile::crc_sorted() const noexcept
{
    return crc_sorted_;
}

const DatEntry* DatFile::find(const std::string_view key) const noexcept
{
    const auto crc = dat_key_crc(key);
    if (crc_sorted_)
    {
        auto candidate = std::ranges::lower_bound(entries_, crc, {}, &DatEntry::crc);
        for (; candidate != entries_.end() && candidate->crc == crc; ++candidate)
        {
            if (candidate->key == key)
            {
                return &*candidate;
            }
        }
        return nullptr;
    }
    const auto found = std::ranges::find(entries_, key, &DatEntry::key);
    return found == entries_.end() ? nullptr : &*found;
}

DatEntry* DatFile::find(const std::string_view key) noexcept
{
    return const_cast<DatEntry*>(std::as_const(*this).find(key));
}

void DatFile::upsert(std::string key, std::u16string value)
{
    if (key.empty())
    {
        throw std::invalid_argument("DAT key cannot be empty");
    }
    if (auto* existing = find(key))
    {
        existing->value = std::move(value);
        return;
    }

    DatEntry entry{dat_key_crc(key), std::move(key), std::move(value)};
    if (crc_sorted_)
    {
        const auto position = std::ranges::upper_bound(entries_, entry.crc, {}, &DatEntry::crc);
        entries_.insert(position, std::move(entry));
    }
    else
    {
        entries_.push_back(std::move(entry));
    }
}

bool DatFile::erase(const std::string_view key)
{
    const auto found = std::ranges::find(entries_, key, &DatEntry::key);
    if (found == entries_.end())
    {
        return false;
    }
    entries_.erase(found);
    return true;
}

std::vector<DatIssue> DatFile::validate() const
{
    std::vector<DatIssue> issues;
    std::unordered_map<std::string_view, std::size_t> keys;
    keys.reserve(entries_.size());
    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        const auto& entry = entries_[index];
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
    return issues;
}

std::vector<std::byte> DatFile::serialize() const
{
    if (entries_.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("too many DAT entries");
    }
    std::uint64_t byte_count = 4U + entries_.size() * 12U;
    for (const auto& entry : entries_)
    {
        byte_count += entry.value.size() * 2U + entry.key.size();
        if (entry.value.size() > std::numeric_limits<std::uint32_t>::max() || entry.key.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("DAT string is too long");
        }
    }
    if (byte_count > std::numeric_limits<std::size_t>::max())
    {
        throw std::runtime_error("DAT output is too large");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(byte_count));
    append_u32(bytes, static_cast<std::uint32_t>(entries_.size()));
    for (const auto& entry : entries_)
    {
        append_u32(bytes, dat_key_crc(entry.key));
        append_u32(bytes, static_cast<std::uint32_t>(entry.value.size()));
        append_u32(bytes, static_cast<std::uint32_t>(entry.key.size()));
    }
    for (const auto& entry : entries_)
    {
        for (const char16_t character : entry.value)
        {
            bytes.push_back(static_cast<std::byte>(character & 0xffU));
            bytes.push_back(static_cast<std::byte>((character >> 8U) & 0xffU));
        }
    }
    for (const auto& entry : entries_)
    {
        for (const unsigned char character : entry.key)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
    }
    return bytes;
}

void DatFile::save(const std::filesystem::path& path, const bool overwrite) const
{
    if (std::filesystem::exists(path) && !overwrite)
    {
        throw std::runtime_error("output already exists; pass --force to replace it: " + path.string());
    }
    if (const auto parent = path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent);
    }

    const auto bytes = serialize();
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            throw std::runtime_error("could not create DAT output: " + temporary.string());
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream)
        {
            throw std::runtime_error("could not write DAT output: " + temporary.string());
        }
    }
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0U);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), flags))
    {
        const auto error = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("could not commit DAT output (Windows error " + std::to_string(error) + "): " + path.string());
    }
#else
    std::filesystem::rename(temporary, path);
#endif
}

} // namespace eaw
