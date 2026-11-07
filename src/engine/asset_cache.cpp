#include "asset_cache.hpp"

#include "eaw/asset_path.hpp"

#include <stdexcept>
#include <system_error>

namespace eaw {
namespace {

std::string dependency_key(const std::string_view virtual_path)
{
    return normalize_asset_path(virtual_path).value_or(std::string(virtual_path));
}

std::uint64_t fnv1a(std::uint64_t hash, const void* data, const std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
    {
        hash = (hash ^ bytes[index]) * 1099511628211ULL;
    }
    return hash;
}

std::uint64_t hash_string(const std::uint64_t hash, const std::string& value) noexcept
{
    const auto length = static_cast<std::uint64_t>(value.size());
    return fnv1a(fnv1a(hash, &length, sizeof(length)), value.data(), value.size());
}

std::optional<std::int64_t> source_write_time(const std::filesystem::path& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

std::optional<std::uint64_t> source_file_size(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(size);
}

} // namespace

AssetFingerprint fingerprint_asset(const std::optional<ResolvedAsset>& resolved)
{
    AssetFingerprint result;
    if (!resolved)
    {
        return result;
    }
    result.present = true;
    result.kind = resolved->kind;
    result.mount_name = resolved->mount_name;
    result.container = resolved->container;
    result.offset = resolved->offset;
    result.size = resolved->size;
    result.write_time = source_write_time(resolved->container).value_or(0);
    return result;
}

std::uint64_t fingerprint_listing(const std::vector<ResolvedAsset>& assets)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& asset : assets)
    {
        const auto print = fingerprint_asset(asset);
        hash = hash_string(hash, asset.virtual_path);
        hash = hash_string(hash, print.mount_name);
        hash = hash_string(hash, print.container.generic_string());
        hash = fnv1a(hash, &print.offset, sizeof(print.offset));
        hash = fnv1a(hash, &print.size, sizeof(print.size));
        hash = fnv1a(hash, &print.write_time, sizeof(print.write_time));
    }
    return hash;
}

void DependencySet::record_file(const std::string_view virtual_path, AssetFingerprint fingerprint)
{
    files_.try_emplace(dependency_key(virtual_path), std::move(fingerprint));
}

void DependencySet::record_listing(const std::string_view prefix, const std::uint64_t fingerprint)
{
    listings_.try_emplace(std::string(prefix), fingerprint);
}

void DependencySet::merge(const DependencySet& other)
{
    for (const auto& [key, print] : other.files_)
    {
        files_.try_emplace(key, print);
    }
    for (const auto& [key, print] : other.listings_)
    {
        listings_.try_emplace(key, print);
    }
}

std::vector<std::string> DependencySet::changes(const AssetReader& reader) const
{
    std::vector<std::string> changed;
    for (const auto& [key, print] : files_)
    {
        std::optional<ResolvedAsset> resolved;
        try
        {
            resolved = reader.resolve(key);
        }
        catch (const std::exception&)
        {
            // An unresolvable request stays absent.
        }
        if (fingerprint_asset(resolved) != print)
        {
            changed.push_back(key);
        }
    }
    for (const auto& [prefix, print] : listings_)
    {
        if (fingerprint_listing(reader.list(prefix)) != print)
        {
            changed.push_back("list:" + prefix);
        }
    }
    return changed;
}

std::size_t DependencySet::file_count() const noexcept
{
    return files_.size();
}

std::size_t DependencySet::listing_count() const noexcept
{
    return listings_.size();
}

bool DependencySet::empty() const noexcept
{
    return files_.empty() && listings_.empty();
}

RecordingAssetReader::RecordingAssetReader(const AssetReader& base) : base_(base)
{}

std::optional<ResolvedAsset> RecordingAssetReader::resolve(const std::string_view virtual_path) const
{
    auto resolved = base_.resolve(virtual_path);
    auto print = fingerprint_asset(resolved);
    std::scoped_lock lock(mutex_);
    dependencies_.record_file(virtual_path, std::move(print));
    return resolved;
}

Result<std::vector<std::byte>> RecordingAssetReader::read(const std::string_view virtual_path) const
{
    (void)resolve(virtual_path);
    return base_.read(virtual_path);
}

std::vector<ResolvedAsset> RecordingAssetReader::list(const std::string_view prefix) const
{
    auto assets = base_.list(prefix);
    const auto print = fingerprint_listing(assets);
    std::scoped_lock lock(mutex_);
    dependencies_.record_listing(prefix, print);
    return assets;
}

void RecordingAssetReader::merge(const DependencySet& dependencies) const
{
    std::scoped_lock lock(mutex_);
    dependencies_.merge(dependencies);
}

DependencySet RecordingAssetReader::dependencies() const
{
    std::scoped_lock lock(mutex_);
    return dependencies_;
}

const AssetReader& RecordingAssetReader::base() const noexcept
{
    return base_;
}

std::string AssetCache::make_key(const std::string_view kind, const std::string_view key)
{
    std::string result(kind);
    result.push_back('|');
    result += dependency_key(key);
    return result;
}

std::vector<std::string> AssetCache::invalidate(const AssetReader& reader)
{
    std::scoped_lock lock(mutex_);
    std::vector<std::string> dropped;
    for (auto entry = entries_.begin(); entry != entries_.end();)
    {
        if (!entry->second.dependencies.changes(reader).empty())
        {
            dropped.push_back(entry->first);
            entry = entries_.erase(entry);
        }
        else
        {
            ++entry;
        }
    }
    return dropped;
}

std::uint64_t AssetCache::begin_epoch()
{
    std::scoped_lock lock(mutex_);
    return ++epoch_;
}

std::size_t AssetCache::evict_unused_since(const std::uint64_t epoch)
{
    std::scoped_lock lock(mutex_);
    return std::erase_if(entries_,
                         [epoch](const auto& entry)
                         {
                             return entry.second.last_epoch < epoch;
                         });
}

void AssetCache::clear()
{
    std::scoped_lock lock(mutex_);
    entries_.clear();
}

AssetCache::Stats AssetCache::stats() const
{
    std::scoped_lock lock(mutex_);
    return {entries_.size(), hits_, misses_};
}

ArchiveWatch::ArchiveWatch(const VirtualFileSystem& vfs)
{
    for (const auto& mount : vfs.mounts())
    {
        states_.push_back(current(*mount));
    }
}

std::vector<std::size_t> ArchiveWatch::changed(const VirtualFileSystem& vfs) const
{
    std::vector<std::size_t> result;
    const auto mounts = vfs.mounts();
    for (std::size_t index = 0; index < mounts.size() && index < states_.size(); ++index)
    {
        if (mounts[index]->kind() == AssetSourceKind::meg_archive && current(*mounts[index]) != states_[index])
        {
            result.push_back(index);
        }
    }
    return result;
}

void ArchiveWatch::accept(const VirtualFileSystem& vfs, const std::size_t index)
{
    const auto mounts = vfs.mounts();
    if (index < mounts.size() && index < states_.size())
    {
        states_[index] = current(*mounts[index]);
    }
}

std::optional<ArchiveWatch::State> ArchiveWatch::current(const AssetMount& mount)
{
    if (mount.kind() != AssetSourceKind::meg_archive)
    {
        return std::nullopt;
    }
    const auto size = source_file_size(mount.source_path());
    const auto time = source_write_time(mount.source_path());
    if (!size || !time)
    {
        return std::nullopt;
    }
    return State{*size, *time};
}

} // namespace eaw
