#include "vfs/meg_archive.hpp"

#include "vfs/asset_path.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <type_traits>

namespace eaw {
namespace {

template <typename Integer> std::optional<Integer> read_little_endian(std::istream& stream)
{
    static_assert(std::is_unsigned_v<Integer>);
    std::array<unsigned char, sizeof(Integer)> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        return std::nullopt;
    }
    Integer value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        value |= static_cast<Integer>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::optional<std::uint64_t> stream_length(std::ifstream& stream)
{
    const auto original = stream.tellg();
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    stream.seekg(original);
    if (end < 0)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(end);
}

} // namespace

Result<MegArchive> MegArchive::open(std::filesystem::path archive_path)
{
    MegArchive archive;
    archive.path_ = std::move(archive_path);
    const auto where = [&](const std::string& problem)
    {
        return make_error(problem + ": " + archive.path_.string());
    };

    std::ifstream stream(archive.path_, std::ios::binary);
    if (!stream)
    {
        return where("could not open MEG archive");
    }
    const auto length = stream_length(stream);
    const auto filename_count = read_little_endian<std::uint32_t>(stream);
    const auto file_count = read_little_endian<std::uint32_t>(stream);
    if (!length || !filename_count || !file_count)
    {
        return where("truncated MEG header");
    }

    constexpr std::uint32_t maximum_reasonable_entries = 2'000'000;
    if (*filename_count > maximum_reasonable_entries || *file_count > maximum_reasonable_entries)
    {
        return where("unsupported or corrupt MEG header");
    }

    std::vector<std::string> names;
    names.reserve(*filename_count);
    for (std::uint32_t index = 0; index < *filename_count; ++index)
    {
        const auto name_length = read_little_endian<std::uint16_t>(stream);
        if (!name_length)
        {
            return where("truncated MEG filename table");
        }
        std::string name(*name_length, '\0');
        stream.read(name.data(), static_cast<std::streamsize>(name.size()));
        if (!stream)
        {
            return where("truncated MEG filename table");
        }
        auto normalized = normalize_asset_path(name);
        if (!normalized)
        {
            return where("invalid MEG filename " + std::to_string(index) + " (" + normalized.error().message + ")");
        }
        names.push_back(std::move(normalized).value());
    }

    archive.entries_.reserve(*file_count);
    archive.lookup_.reserve(*file_count);
    for (std::uint32_t record = 0; record < *file_count; ++record)
    {
        MegEntry entry;
        const auto crc = read_little_endian<std::uint32_t>(stream);
        const auto index = read_little_endian<std::uint32_t>(stream);
        const auto size = read_little_endian<std::uint32_t>(stream);
        const auto offset = read_little_endian<std::uint32_t>(stream);
        const auto name_index = read_little_endian<std::uint32_t>(stream);
        if (!crc || !index || !size || !offset || !name_index)
        {
            return where("unexpected end of MEG file table");
        }
        if (*name_index >= names.size())
        {
            return where("invalid MEG filename index " + std::to_string(*name_index));
        }
        if (static_cast<std::uint64_t>(*offset) + *size > *length)
        {
            return where("MEG entry " + names[*name_index] + " extends beyond the archive");
        }
        entry.crc = *crc;
        entry.index = *index;
        entry.size = *size;
        entry.offset = *offset;
        entry.path = names[*name_index];
        const auto position = archive.entries_.size();
        archive.entries_.push_back(std::move(entry));
        archive.lookup_.insert_or_assign(archive.entries_.back().path, position);
    }
    return archive;
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
    if (!normalized)
    {
        return nullptr;
    }
    const auto found = lookup_.find(*normalized);
    return found == lookup_.end() ? nullptr : &entries_[found->second];
}

Result<std::vector<std::byte>> MegArchive::read(const MegEntry& entry) const
{
    std::ifstream stream(path_, std::ios::binary);
    if (!stream)
    {
        return make_error("could not reopen MEG archive: " + path_.string());
    }
    stream.seekg(entry.offset, std::ios::beg);
    std::vector<std::byte> bytes(entry.size);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        return make_error("could not read MEG entry " + entry.path + " from " + path_.string());
    }
    return bytes;
}

} // namespace eaw
