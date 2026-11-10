#pragma once

// Typed access to loaded resources. `get<T>(path, ...)` returns the resource of type T at a virtual
// path, loading it on first request through T's ResourceLoader and keeping it in the AssetCache
// (with the files it depended on, so hot reload invalidates exactly what changed). Without a cache
// every request loads.
//
// Each resource type says once how it loads by specializing ResourceLoader<T> next to the type:
//
//     template <> struct ResourceLoader<Model>
//     {
//         using Value = Result<Model>;           // what is cached; failures are cached too
//         static constexpr std::string_view kind = "model";
//         static std::string key(std::string_view path);
//         static Value load(const AssetReader& reader, std::string_view path);
//     };
//
// Extra arguments to get() reach key() and load() (a particle effect's fidelity profile).

#include "assets/asset_cache.hpp"
#include "vfs/virtual_file_system.hpp"

#include <memory>
#include <string_view>

namespace eaw {

template <class T> struct ResourceLoader;

class ResourceManager final
{
public:
    explicit ResourceManager(const AssetReader& reader, AssetCache* cache = nullptr) noexcept : reader_(reader), cache_(cache)
    {}

    template <class T, class... Arguments> [[nodiscard]] std::shared_ptr<const typename ResourceLoader<T>::Value> get(const std::string_view path, const Arguments&... arguments) const
    {
        using Loader = ResourceLoader<T>;
        using Value = typename Loader::Value;
        const auto load = [&](const AssetReader& reader)
        {
            return Loader::load(reader, path, arguments...);
        };
        if (cache_ == nullptr)
        {
            return std::make_shared<const Value>(load(reader_));
        }
        return cache_->load<Value>(Loader::kind, Loader::key(path, arguments...), reader_, load);
    }

    [[nodiscard]] const AssetReader& reader() const noexcept
    {
        return reader_;
    }
    [[nodiscard]] AssetCache* cache() const noexcept
    {
        return cache_;
    }

private:
    const AssetReader& reader_;
    AssetCache* cache_;
};

} // namespace eaw
