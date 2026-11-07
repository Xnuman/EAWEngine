#include "eaw/meg_archive.hpp"

#include "eaw/asset_path.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace eaw {
namespace {

template <typename Integer> Integer read_little_endian(std::istream& stream)
{
    static_assert(std::is_unsigned_v<Integer>);
    std::array<unsigned char, sizeof(Integer)> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        throw std::runtime_error("unexpected end of MEG archive");
    }

    Integer value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        value |= static_cast<Integer>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t stream_length(std::ifstream& stream)
{
    const auto original = stream.tellg();
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    stream.seekg(original);
    if (end < 0)
    {
        throw std::runtime_error("could not determine MEG archive size");
    }
    return static_cast<std::uint64_t>(end);
}

} // namespace

MegArchive::MegArchive(std::filesystem::path archive_path) : path_(std::move(archive_path))
{
    std::ifstream stream(path_, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("could not open MEG archive: " + path_.string());
    }

    const auto length = stream_length(stream);
    const auto filename_count = read_little_endian<std::uint32_t>(stream);
    const auto file_count = read_little_endian<std::uint32_t>(stream);

    constexpr std::uint32_t maximum_reasonable_entries = 2'000'000;
    if (filename_count > maximum_reasonable_entries || file_count > maximum_reasonable_entries)
    {
        throw std::runtime_error("unsupported or corrupt MEG header: " + path_.string());
    }

    std::vector<std::string> names;
    names.reserve(filename_count);
    for (std::uint32_t index = 0; index < filename_count; ++index)
    {
        const auto name_length = read_little_endian<std::uint16_t>(stream);
        std::string name(name_length, '\0');
        stream.read(name.data(), static_cast<std::streamsize>(name.size()));
        if (!stream)
        {
            throw std::runtime_error("truncated MEG filename table: " + path_.string());
        }
        names.push_back(normalize_asset_path(name));
    }

    entries_.reserve(file_count);
    lookup_.reserve(file_count);
    for (std::uint32_t record = 0; record < file_count; ++record)
    {
        MegEntry entry;
        entry.crc = read_little_endian<std::uint32_t>(stream);
        entry.index = read_little_endian<std::uint32_t>(stream);
        entry.size = read_little_endian<std::uint32_t>(stream);
        entry.offset = read_little_endian<std::uint32_t>(stream);
        const auto name_index = read_little_endian<std::uint32_t>(stream);

        if (name_index >= names.size())
        {
            throw std::runtime_error("invalid MEG filename index: " + path_.string());
        }
        if (static_cast<std::uint64_t>(entry.offset) + entry.size > length)
        {
            throw std::runtime_error("MEG entry extends beyond archive: " + path_.string());
        }

        entry.path = names[name_index];
        const auto position = entries_.size();
        entries_.push_back(std::move(entry));
        lookup_.insert_or_assign(entries_.back().path, position);
    }
}

const std::filesystem::path& MegArchive::path() const noexcept
{
    return path_;
}

std::span<const MegEntry> MegArchive::entries() const noexcept
{
    return entries_;
}

const MegEntry* MegArchive::find(const std::string_view virtual_path) const
{
    const auto normalized = normalize_asset_path(virtual_path);
    const auto found = lookup_.find(normalized);
    return found == lookup_.end() ? nullptr : &entries_[found->second];
}

std::vector<std::byte> MegArchive::read(const MegEntry& entry) const
{
    std::ifstream stream(path_, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("could not reopen MEG archive: " + path_.string());
    }
    stream.seekg(entry.offset, std::ios::beg);
    std::vector<std::byte> bytes(entry.size);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        throw std::runtime_error("could not read MEG entry: " + entry.path);
    }
    return bytes;
}

} // namespace eaw
