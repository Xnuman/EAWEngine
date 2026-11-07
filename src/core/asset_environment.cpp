#include "eaw/asset_environment.hpp"

#include <memory>

namespace eaw {
namespace {

void mount_archives(VirtualFileSystem& vfs, const std::filesystem::path& runtime_root, const std::string& prefix, AssetMountReport& report)
{
    for (const auto& archive : discover_meg_archives(runtime_root))
    {
        auto mount = MegMount::open(prefix + ":" + archive.filename().string(), archive);
        if (!mount)
        {
            report.warnings.push_back("skipped " + archive.string() + ": " + mount.error().message);
            continue;
        }
        vfs.mount(std::move(mount).value());
    }
}

} // namespace

AssetMountReport mount_game_assets(VirtualFileSystem& vfs, const GameProfile& profile, const std::span<const std::filesystem::path> mods)
{
    AssetMountReport report;
    if (profile.variant == GameVariant::forces_of_corruption)
    {
        // The expansion is not a self-contained asset set. Its models, XML, and
        // maps routinely reference resources stored in the original game's
        // GameData archives, so mount those first as the lowest-priority layer.
        const auto eaw_runtime = profile.root / "GameData";
        mount_archives(vfs, eaw_runtime, "eaw", report);
        vfs.mount(std::make_unique<DirectoryMount>("eaw:loose", eaw_runtime));
        mount_archives(vfs, profile.runtime_root, "foc", report);
        vfs.mount(std::make_unique<DirectoryMount>("foc:loose", profile.runtime_root));
    }
    else
    {
        mount_archives(vfs, profile.runtime_root, "eaw", report);
        vfs.mount(std::make_unique<DirectoryMount>("eaw:loose", profile.runtime_root));
    }

    for (auto mod = mods.rbegin(); mod != mods.rend(); ++mod)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(*mod / "Data", error))
        {
            report.warnings.push_back("skipped mod without a Data directory: " + mod->string());
            continue;
        }
        mount_archives(vfs, *mod, "mod:" + mod->filename().string(), report);
        vfs.mount(std::make_unique<DirectoryMount>("mod:loose:" + mod->filename().string(), *mod));
    }
    return report;
}

} // namespace eaw
