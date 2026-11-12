#pragma once

#include "core/status.hpp"
#include "vfs/meg_archive.hpp"

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
    // The winning layer's bytes; an error when the asset is missing or cannot be read.
    [[nodiscard]] virtual Result<std::vector<std::byte>> read(std::string_view virtual_path) const = 0;
    // Winning assets under a prefix (an invalid prefix lists nothing).
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
    [[nodiscard]] virtual Result<std::vector<std::byte>> read(const ResolvedAsset& asset) const = 0;
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
    [[nodiscard]] Result<std::vector<std::byte>> read(const ResolvedAsset& asset) const override;
    [[nodiscard]] std::size_t entry_count() const noexcept override;
    [[nodiscard]] std::vector<std::string> paths() const override;

private:
    std::string name_;
    std::filesystem::path root_;
};

class MegMount final : public AssetMount
{
public:
    // Opens the archive's index; the error names the archive and what is malformed. Entries appear
    // under `virtual_folder` (empty: at their own paths); the sound-effect archives store bare file
    // names that the game finds under DATA/AUDIO/SFX/.
    [[nodiscard]] static Result<std::unique_ptr<MegMount>> open(std::string name, std::filesystem::path archive, std::string_view virtual_folder = {});
    MegMount(std::string name, MegArchive archive, std::string virtual_folder = {});
    // The folder entries appear under (normalized, ending in '/'), or empty.
    [[nodiscard]] std::string_view virtual_folder() const noexcept;
    // Opens the archive again with the same name and folder (after it was rebuilt).
    [[nodiscard]] Result<std::unique_ptr<MegMount>> reopen() const;
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] AssetSourceKind kind() const noexcept override;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept override;
    [[nodiscard]] std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const override;
    [[nodiscard]] Result<std::vector<std::byte>> read(const ResolvedAsset& asset) const override;
    [[nodiscard]] std::size_t entry_count() const noexcept override;
    [[nodiscard]] std::vector<std::string> paths() const override;

private:
    // The archive path of a virtual path under the folder, or nothing.
    [[nodiscard]] std::optional<std::string> archive_path(std::string_view virtual_path) const;

    std::string name_;
    MegArchive archive_;
    std::string virtual_folder_;
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

    // Mounts are added from lowest to highest priority (a null mount is a programming error).
    void mount(std::unique_ptr<AssetMount> mount);
    // Swaps the mount at `index` (same priority), for example after its archive was rebuilt. The
    // caller must ensure no other thread is reading through this file system meanwhile. The index
    // must name an existing mount.
    void replace_mount(std::size_t index, std::unique_ptr<AssetMount> mount);
    [[nodiscard]] std::optional<ResolvedAsset> resolve(std::string_view virtual_path) const override;
    [[nodiscard]] Result<std::vector<std::byte>> read(std::string_view virtual_path) const override;
    [[nodiscard]] std::vector<ResolvedAsset> list(std::string_view prefix = {}) const override;
    [[nodiscard]] std::span<const std::unique_ptr<AssetMount>> mounts() const noexcept;

private:
    std::vector<std::unique_ptr<AssetMount>> mounts_;
};

} // namespace eaw
