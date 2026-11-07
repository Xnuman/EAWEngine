#pragma once

#include "eaw/meg_archive.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

enum class AssetSourceKind
{
    directory,
    meg_archive,
};

struct ResolvedAsset
{
    AssetSourceKind kind{};
    std::string mount_name;
    std::filesystem::path container;
    std::string virtual_path;
    std::uint64_t offset{};
    std::uint64_t size{};
};

// Read-only access to layered assets. The VirtualFileSystem implements it; wrappers (for example a
// dependency recorder) forward to another reader. Implementations must allow concurrent const calls.
class AssetReader
{
public:
    virtual ~AssetReader() = default;
    [[nodiscard]] virtual std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const = 0;
    [[nodiscard]] virtual std::vector<std::byte> read(std::string_view virtual_path) const = 0;
    [[nodiscard]] virtual std::vector<ResolvedAsset> list(std::string_view prefix = {}) const = 0;

protected:
    AssetReader() = default;
    AssetReader(const AssetReader&) = default;
    AssetReader& operator=(const AssetReader&) = default;
    AssetReader(AssetReader&&) = default;
    AssetReader& operator=(AssetReader&&) = default;
};

class AssetMount
{
public:
    virtual ~AssetMount() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual AssetSourceKind kind() const noexcept = 0;
    // The loose-file root directory or the archive file this mount reads from.
    [[nodiscard]] virtual const std::filesystem::path& source_path() const noexcept = 0;
    [[nodiscard]] virtual std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const = 0;
    [[nodiscard]] virtual std::vector<std::byte> read(const ResolvedAsset& asset) const = 0;
    [[nodiscard]] virtual std::size_t entry_count() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> paths() const = 0;
};

class DirectoryMount final : public AssetMount
{
public:
    DirectoryMount(std::string name, std::filesystem::path root);
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] AssetSourceKind kind() const noexcept override;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept override;
    [[nodiscard]] std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const override;
    [[nodiscard]] std::vector<std::byte> read(const ResolvedAsset& asset) const override;
    [[nodiscard]] std::size_t entry_count() const noexcept override;
    [[nodiscard]] std::vector<std::string> paths() const override;

private:
    std::string name_;
    std::filesystem::path root_;
};

class MegMount final : public AssetMount
{
public:
    MegMount(std::string name, std::filesystem::path archive);
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] AssetSourceKind kind() const noexcept override;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept override;
    [[nodiscard]] std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const override;
    [[nodiscard]] std::vector<std::byte> read(const ResolvedAsset& asset) const override;
    [[nodiscard]] std::size_t entry_count() const noexcept override;
    [[nodiscard]] std::vector<std::string> paths() const override;

private:
    std::string name_;
    MegArchive archive_;
};

class VirtualFileSystem final : public AssetReader
{
public:
    VirtualFileSystem() = default;
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;
    VirtualFileSystem(VirtualFileSystem&&) = default;
    VirtualFileSystem& operator=(VirtualFileSystem&&) = default;
    ~VirtualFileSystem() override = default;

    // Mounts are added from lowest to highest priority.
    void mount(std::unique_ptr<AssetMount> mount);
    // Swaps the mount at `index` (same priority), for example after its archive was rebuilt. The
    // caller must ensure no other thread is reading through this file system meanwhile.
    void replace_mount(std::size_t index, std::unique_ptr<AssetMount> mount);
    [[nodiscard]] std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const override;
    [[nodiscard]] std::vector<std::byte> read(std::string_view virtual_path) const override;
    [[nodiscard]] std::vector<ResolvedAsset> list(std::string_view prefix = {}) const override;
    [[nodiscard]] std::span<const std::unique_ptr<AssetMount>> mounts() const noexcept;

private:
    std::vector<std::unique_ptr<AssetMount>> mounts_;
};

} // namespace eaw
