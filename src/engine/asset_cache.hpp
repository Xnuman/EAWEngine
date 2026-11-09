#pragma once

// Engine-side cache of decoded assets with dependency tracking for hot reload.
//
// Every cached asset records the files it consulted while loading, as fingerprints of their
// resolved source: winning mount, container, entry offset and size, and the container's last write
// time. Lookups that found nothing are recorded too, so a file added in a higher-priority layer
// invalidates assets that previously fell back to a lower one. Listings record a hash of their
// entries. An entry is stale when any recorded fingerprint no longer matches.
//
// The owned installation and mods are only read. A rebuilt MEG archive changes its write time, so
// every asset read from it is invalidated even if its bytes did not change; that is conservative
// but never serves stale data.

#include "core/status.hpp"
#include "eaw/virtual_file_system.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eaw {

struct AssetFingerprint
{
    bool present{};
    AssetSourceKind kind{};
    std::string mount_name;
    std::filesystem::path container;
    std::uint64_t offset{};
    std::uint64_t size{};
    std::int64_t write_time{};

    friend bool operator==(const AssetFingerprint&, const AssetFingerprint&) = default;
};

// Fingerprint of a resolve result; `write_time` is the container's last write time (0 when the
// file system cannot report it).
[[nodiscard]] AssetFingerprint fingerprint_asset(const std::optional<ResolvedAsset>& resolved);

// Stable hash of a listing result (paths and fingerprints, in listing order).
[[nodiscard]] std::uint64_t fingerprint_listing(const std::vector<ResolvedAsset>& assets);

class DependencySet final
{
public:
    // Keys are normalized virtual paths (or the raw request when it does not normalize).
    void record_file(std::string_view virtual_path, AssetFingerprint fingerprint);
    void record_listing(std::string_view prefix, std::uint64_t fingerprint);
    // Adds another set's entries; an existing key keeps its first fingerprint.
    void merge(const DependencySet& other);

    // Re-resolves every dependency through `reader` and returns the keys whose fingerprint changed
    // ("list:" precedes listing prefixes). Empty means the recorded state is still current.
    [[nodiscard]] std::vector<std::string> changes(const AssetReader& reader) const;

    [[nodiscard]] std::size_t file_count() const noexcept;
    [[nodiscard]] std::size_t listing_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::map<std::string, AssetFingerprint> files_;
    std::map<std::string, std::uint64_t> listings_;
};

// Forwards to another reader and records every resolve, read, and listing as a dependency.
// Thread-safe for concurrent const use.
class RecordingAssetReader final : public AssetReader
{
public:
    explicit RecordingAssetReader(const AssetReader& base);

    [[nodiscard]] std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const override;
    [[nodiscard]] Result<std::vector<std::byte>> read(std::string_view virtual_path) const override;
    [[nodiscard]] std::vector<ResolvedAsset> list(std::string_view prefix = {}) const override;

    // Adds dependencies gathered elsewhere (for example by a cached asset this load reused).
    void merge(const DependencySet& dependencies) const;
    [[nodiscard]] DependencySet dependencies() const;
    [[nodiscard]] const AssetReader& base() const noexcept;

private:
    const AssetReader& base_;
    mutable std::mutex mutex_;
    mutable DependencySet dependencies_;
};

// Decoded assets keyed by kind and key (usually a virtual path), shared as immutable values.
// Loads may run on worker threads; invalidation must not run concurrently with a load that reads
// through a file system being remounted, which callers guarantee by remounting between builds.
class AssetCache final
{
public:
    struct Stats
    {
        std::size_t entries{};
        std::uint64_t hits{};
        std::uint64_t misses{};
    };

    // Returns the cached value or runs `loader(recording_reader)`, which returns a T, and caches its
    // result. The value's dependencies are added to `reader` when it is a RecordingAssetReader, so
    // a scene records the files behind every asset it reuses. A loader whose failures should be
    // cached too (they depend on files, so a fix invalidates them) returns a value that carries the
    // failure, such as a Result<U> used as T.
    template <class T, class Loader> std::shared_ptr<const T> load(std::string_view kind, std::string_view key, const AssetReader& reader, Loader&& loader)
    {
        auto loaded = try_load<T>(kind, key, reader,
                                  [&](const AssetReader& recorder) -> Result<T>
                                  {
                                      return std::invoke(std::forward<Loader>(loader), recorder);
                                  });
        return std::move(loaded).value();
    }

    // Like load, for a loader returning Result<T>: a failed load is returned and caches nothing, so
    // the next request runs the loader again. Its dependencies still reach `reader`.
    template <class T, class Loader> Result<std::shared_ptr<const T>> try_load(std::string_view kind, std::string_view key, const AssetReader& reader, Loader&& loader)
    {
        const auto cache_key = make_key(kind, key);
        const auto* outer = dynamic_cast<const RecordingAssetReader*>(&reader);
        {
            std::scoped_lock lock(mutex_);
            if (const auto found = entries_.find(cache_key); found != entries_.end() && found->second.type == std::type_index(typeid(T)))
            {
                ++hits_;
                found->second.last_epoch = epoch_;
                if (outer != nullptr)
                {
                    outer->merge(found->second.dependencies);
                }
                return std::static_pointer_cast<const T>(found->second.value);
            }
            ++misses_;
        }
        RecordingAssetReader recorder(outer != nullptr ? outer->base() : reader);
        Result<T> result = std::invoke(std::forward<Loader>(loader), static_cast<const AssetReader&>(recorder));
        auto dependencies = recorder.dependencies();
        if (outer != nullptr)
        {
            outer->merge(dependencies);
        }
        if (!result)
        {
            return std::move(result).error();
        }
        auto value = std::make_shared<const T>(std::move(result).value());
        std::scoped_lock lock(mutex_);
        entries_.insert_or_assign(cache_key, Entry{value, std::type_index(typeid(T)), std::move(dependencies), epoch_});
        return std::shared_ptr<const T>(std::move(value));
    }

    // Drops every entry whose dependencies changed and returns the dropped keys.
    std::vector<std::string> invalidate(const AssetReader& reader);
    void clear();

    // Starts a build: every entry loaded or reused from now on is stamped with the returned epoch.
    std::uint64_t begin_epoch();
    // Drops entries not used since `epoch` (for example the textures of a model a mod replaced),
    // so a long session does not accumulate unused decoded data. Call after a successful build;
    // returns the number of dropped entries.
    std::size_t evict_unused_since(std::uint64_t epoch);
    [[nodiscard]] Stats stats() const;

private:
    struct Entry
    {
        std::shared_ptr<const void> value;
        std::type_index type;
        DependencySet dependencies;
        std::uint64_t last_epoch{};
    };
    [[nodiscard]] static std::string make_key(std::string_view kind, std::string_view key);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::uint64_t hits_{};
    std::uint64_t misses_{};
    std::uint64_t epoch_{};
};

// Archive-level change detection: the size and write time of every MEG mount's archive file.
// A changed archive must be remounted (its entry index is stale) before assets are reloaded.
class ArchiveWatch final
{
public:
    explicit ArchiveWatch(const VirtualFileSystem& vfs);
    // Mount indices whose archive file changed since the last snapshot or acceptance.
    [[nodiscard]] std::vector<std::size_t> changed(const VirtualFileSystem& vfs) const;
    // Records the current state of one mount (after a successful remount).
    void accept(const VirtualFileSystem& vfs, std::size_t index);

private:
    struct State
    {
        std::uint64_t size{};
        std::int64_t write_time{};
        friend bool operator==(const State&, const State&) = default;
    };
    [[nodiscard]] static std::optional<State> current(const AssetMount& mount);
    std::vector<std::optional<State>> states_;
};

} // namespace eaw
