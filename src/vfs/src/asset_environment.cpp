#include "vfs/asset_environment.hpp"
#include "core/string_utils.hpp"

#include <array>
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

// A layer's sound-effect archives under DATA/AUDIO/SFX/, in rising priority; names match ignoring
// case (stock files are sfx2d_english.meg and sfx2d_Russian.MEG).
void mount_sound_archives(VirtualFileSystem& vfs, const std::filesystem::path& layer_root, const std::string& prefix, const std::string_view language, AssetMountReport& report)
{
    const auto folder = layer_root / "Data" / "Audio" / "SFX";
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error))
    {
        return;
    }
    const std::array<std::string, 3> names{"sfx3d_non_localized.meg", "sfx2d_non_localized.meg", "sfx2d_" + ascii_lower(language) + ".meg"};
    for (const auto& name : names)
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        {
            if (!entry.is_regular_file(error) || ascii_lower(entry.path().filename().string()) != name)
            {
                continue;
            }
            auto mount = MegMount::open(prefix + ":sfx:" + entry.path().filename().string(), entry.path(), "DATA/AUDIO/SFX");
            if (!mount)
            {
                report.warnings.push_back("skipped " + entry.path().string() + ": " + mount.error().message);
                break;
            }
            vfs.mount(std::move(mount).value());
            break;
        }
    }
}

} // namespace

std::filesystem::path resolve_mod_path(const GameProfile& profile, const std::filesystem::path& mod)
{
    if (mod.is_absolute())
    {
        return mod;
    }
    std::error_code error;
    const auto under_game = profile.runtime_root / mod;
    return std::filesystem::is_directory(under_game / "Data", error) ? under_game : mod;
}

AssetMountReport mount_game_assets(VirtualFileSystem& vfs, const GameProfile& profile, const std::span<const std::filesystem::path> mods, const std::string_view language)
{
    AssetMountReport report;
    if (profile.variant == GameVariant::forces_of_corruption)
    {
        // The expansion is not a self-contained asset set. Its models, XML, and
        // maps routinely reference resources stored in the original game's
        // GameData archives, so mount those first as the lowest-priority layer.
        const auto eaw_runtime = profile.root / "GameData";
        mount_archives(vfs, eaw_runtime, "eaw", report);
        mount_sound_archives(vfs, eaw_runtime, "eaw", language, report);
        vfs.mount(std::make_unique<DirectoryMount>("eaw:loose", eaw_runtime));
        mount_archives(vfs, profile.runtime_root, "foc", report);
        mount_sound_archives(vfs, profile.runtime_root, "foc", language, report);
        vfs.mount(std::make_unique<DirectoryMount>("foc:loose", profile.runtime_root));
    }
    else
    {
        mount_archives(vfs, profile.runtime_root, "eaw", report);
        mount_sound_archives(vfs, profile.runtime_root, "eaw", language, report);
        vfs.mount(std::make_unique<DirectoryMount>("eaw:loose", profile.runtime_root));
    }

    for (auto given = mods.rbegin(); given != mods.rend(); ++given)
    {
        const auto mod = resolve_mod_path(profile, *given);
        std::error_code error;
        if (!std::filesystem::is_directory(mod / "Data", error))
        {
            report.warnings.push_back("skipped mod without a Data directory: " + given->string() +
                                      (given->is_absolute() ? "" : " (looked under " + profile.runtime_root.string() + " and the working directory)"));
            continue;
        }
        mount_archives(vfs, mod, "mod:" + mod.filename().string(), report);
        mount_sound_archives(vfs, mod, "mod:" + mod.filename().string(), language, report);
        vfs.mount(std::make_unique<DirectoryMount>("mod:loose:" + mod.filename().string(), mod));
    }
    return report;
}

} // namespace eaw
