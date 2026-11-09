#include "eaw/virtual_file_system.hpp"

#include "eaw/asset_path.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <unordered_set>

namespace eaw {

DirectoryMount::DirectoryMount(std::string name, std::filesystem::path root) : name_(std::move(name)), root_(std::move(root))
{}

std::string_view DirectoryMount::name() const noexcept
{
    return name_;
}

AssetSourceKind DirectoryMount::kind() const noexcept
{
    return AssetSourceKind::directory;
}

const std::filesystem::path& DirectoryMount::source_path() const noexcept
{
    return root_;
}

std::optional<ResolvedAsset> DirectoryMount::resolve(const std::string_view virtual_path) const
{
    auto normalized = normalize_asset_path(virtual_path);
    if (!normalized)
    {
        return std::nullopt;
    }
    const auto candidate = root_ / std::filesystem::path(*normalized);
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error)
    {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(candidate, error);
    if (error)
    {
        return std::nullopt;
    }
    return ResolvedAsset{AssetSourceKind::directory, name_, candidate, std::move(normalized).value(), 0, size};
}

Result<std::vector<std::byte>> DirectoryMount::read(const ResolvedAsset& asset) const
{
    std::ifstream stream(asset.container, std::ios::binary);
    if (!stream)
    {
        return make_error("could not open loose asset: " + asset.container.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(asset.size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        return make_error("could not read loose asset: " + asset.container.string());
    }
    return bytes;
}

std::size_t DirectoryMount::entry_count() const noexcept
{
    return 0;
}

std::vector<std::string> DirectoryMount::paths() const
{
    std::vector<std::string> result;
    std::error_code error;
    const auto walk_root = std::filesystem::is_directory(root_ / "Data", error) ? root_ / "Data" : root_;
    error.clear();
    const std::filesystem::recursive_directory_iterator end;
    for (std::filesystem::recursive_directory_iterator item(walk_root, std::filesystem::directory_options::skip_permission_denied, error); item != end; item.increment(error))
    {
        if (error)
        {
            error.clear();
            continue;
        }
        if (!item->is_regular_file(error) || error)
        {
            error.clear();
            continue;
        }
        // Host-only files outside the relative Alamo namespace are ignored.
        auto normalized = normalize_asset_path(item->path().lexically_relative(root_).generic_string());
        if (!normalized)
        {
            continue;
        }
        auto extension = std::filesystem::path(*normalized).extension().string();
        extension = ascii_lower(extension);
        if (extension == ".meg" || *normalized == "DATA/MEGAFILES.XML")
        {
            continue;
        }
        result.push_back(std::move(normalized).value());
    }
    return result;
}

Result<std::unique_ptr<MegMount>> MegMount::open(std::string name, std::filesystem::path archive)
{
    EAW_TRY_ASSIGN(auto opened, MegArchive::open(std::move(archive)));
    return std::make_unique<MegMount>(std::move(name), std::move(opened));
}

MegMount::MegMount(std::string name, MegArchive archive) : name_(std::move(name)), archive_(std::move(archive))
{}

std::string_view MegMount::name() const noexcept
{
    return name_;
}

AssetSourceKind MegMount::kind() const noexcept
{
    return AssetSourceKind::meg_archive;
}

const std::filesystem::path& MegMount::source_path() const noexcept
{
    return archive_.path();
}

std::optional<ResolvedAsset> MegMount::resolve(const std::string_view virtual_path) const
{
    const auto* entry = archive_.find(virtual_path);
    if (entry == nullptr)
    {
        return std::nullopt;
    }
    return ResolvedAsset{AssetSourceKind::meg_archive, name_, archive_.path(), entry->path, entry->offset, entry->size};
}

Result<std::vector<std::byte>> MegMount::read(const ResolvedAsset& asset) const
{
    const auto* entry = archive_.find(asset.virtual_path);
    if (entry == nullptr || entry->offset != asset.offset || entry->size != asset.size)
    {
        return make_error("MEG asset " + asset.virtual_path + " no longer matches its resolved entry in " + archive_.path().string());
    }
    return archive_.read(*entry);
}

std::size_t MegMount::entry_count() const noexcept
{
    return archive_.entries().size();
}

std::vector<std::string> MegMount::paths() const
{
    std::vector<std::string> result;
    result.reserve(archive_.entries().size());
    for (const auto& entry : archive_.entries())
    {
        result.push_back(entry.path);
    }
    return result;
}

void VirtualFileSystem::mount(std::unique_ptr<AssetMount> mount_point)
{
    EAW_ASSERT_MSG(mount_point != nullptr, "cannot add a null asset mount");
    mounts_.push_back(std::move(mount_point));
}

void VirtualFileSystem::replace_mount(const std::size_t index, std::unique_ptr<AssetMount> mount_point)
{
    EAW_ASSERT_MSG(mount_point != nullptr, "cannot replace an asset mount with null");
    EAW_ASSERT_MSG(index < mounts_.size(), "asset mount index " + std::to_string(index) + " is out of range");
    mounts_[index] = std::move(mount_point);
}

std::optional<ResolvedAsset> VirtualFileSystem::resolve(const std::string_view virtual_path) const
{
    for (auto mount = mounts_.rbegin(); mount != mounts_.rend(); ++mount)
    {
        if (auto asset = (*mount)->resolve(virtual_path))
        {
            return asset;
        }
    }
    return std::nullopt;
}

Result<std::vector<std::byte>> VirtualFileSystem::read(const std::string_view virtual_path) const
{
    for (auto mount = mounts_.rbegin(); mount != mounts_.rend(); ++mount)
    {
        if (auto asset = (*mount)->resolve(virtual_path))
        {
            return (*mount)->read(*asset);
        }
    }
    return make_error("asset was not found: " + std::string(virtual_path));
}

std::vector<ResolvedAsset> VirtualFileSystem::list(const std::string_view prefix) const
{
    std::string normalized_prefix;
    if (!prefix.empty())
    {
        auto normalized = normalize_asset_path(prefix);
        if (!normalized)
        {
            return {};
        }
        normalized_prefix = std::move(normalized).value();
        if (!normalized_prefix.ends_with('/'))
        {
            normalized_prefix.push_back('/');
        }
    }

    std::unordered_set<std::string> seen;
    std::vector<ResolvedAsset> result;
    for (auto mount = mounts_.rbegin(); mount != mounts_.rend(); ++mount)
    {
        for (const auto& path : (*mount)->paths())
        {
            if (!normalized_prefix.empty() && !path.starts_with(normalized_prefix) && path != normalized_prefix.substr(0, normalized_prefix.size() - 1))
            {
                continue;
            }
            if (!seen.insert(path).second)
            {
                continue;
            }
            if (auto asset = (*mount)->resolve(path))
            {
                result.push_back(std::move(*asset));
            }
        }
    }
    std::ranges::sort(result, {}, &ResolvedAsset::virtual_path);
    return result;
}

std::span<const std::unique_ptr<AssetMount>> VirtualFileSystem::mounts() const noexcept
{
    return mounts_;
}

} // namespace eaw
