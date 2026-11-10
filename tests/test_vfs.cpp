#include "test_support.hpp"

namespace {

using namespace eaw_test;

void test_meg_archive_and_vfs_basics()
{
    require(expect_ok(eaw::normalize_asset_path("data\\xml/./GameObjectFiles.xml"), "asset normalization") == "DATA/XML/GAMEOBJECTFILES.XML", "asset normalization failed");
    require(!eaw::normalize_asset_path("Data/../secret").ok(), "parent traversal was not rejected");
    // Copying out of the VFS: archive names are untrusted, so nothing may land outside the folder,
    // in an NTFS stream, or on a device.
    require(expect_ok(eaw::asset_output_path("Data\\Scripts/PGBase.lua"), "an output path") == std::filesystem::path("DATA") / "SCRIPTS" / "PGBASE.LUA", "an output path must follow the virtual path");
    for (const auto* hostile : {"../../Windows/evil.lua", "/root.lua", "C:/evil.lua", "Data/name:stream.lua", "Data/nul.lua", "Data/Com3.txt", "Data/LPT1"})
    {
        require(!eaw::asset_output_path(hostile).ok(), std::string("a hostile entry name must be refused: ") + hostile);
    }
    require(eaw::asset_output_path("Data/CONSOLE.LUA").ok() && eaw::asset_output_path("Data/COM10.LUA").ok(), "names that only start like devices must be allowed");

    // Relative mod paths are looked up as the game's MODPATH: under the runtime root, else as given.
    {
        const auto root = std::filesystem::temp_directory_path() / "eawengine-mod-path-test";
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root / "corruption" / "Mods" / "Example" / "Data");
        eaw::GameProfile profile;
        profile.root = root;
        profile.runtime_root = root / "corruption";
        require(eaw::resolve_mod_path(profile, std::filesystem::path("Mods") / "Example") == root / "corruption" / "Mods" / "Example", "a relative mod path must resolve under the runtime root");
        require(eaw::resolve_mod_path(profile, "Elsewhere") == std::filesystem::path("Elsewhere"), "a relative mod path not under the runtime root must stay as given");
        require(eaw::resolve_mod_path(profile, root / "x") == root / "x", "an absolute mod path must stay as it is");
        // An explicit game root must be an installation; a mod folder given as one says so.
        std::filesystem::create_directories(root / "GameData" / "Data");
        std::filesystem::create_directories(root / "corruption" / "Data");
        require(eaw::check_installation_root(root).ok(), "an installation must pass the root check");
        const auto mod_as_root = eaw::check_installation_root(root / "corruption" / "Mods" / "Example");
        require(!mod_as_root && mod_as_root.error().message.find("mod") != std::string::npos, "a mod folder given as the game root must be named as a mod");
        require(!eaw::check_installation_root(root / "corruption" / "Mods"), "a folder without the game's data must fail the root check");
        std::filesystem::remove_all(root, ignored);
    }
    const auto archive_path = write_test_meg();
    const auto archive = expect_ok(eaw::MegArchive::open(archive_path), "opening the test MEG archive");
    const auto* entry = archive.find("data/xml/test.xml");
    require(entry != nullptr, "MEG lookup failed");
    const auto bytes = expect_ok(archive.read(*entry), "reading the MEG entry");
    const std::string payload(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(payload == "<Test />", "MEG payload read failed");
    eaw::VirtualFileSystem vfs;
    vfs.mount(expect_ok(eaw::MegMount::open("test", archive_path), "mounting the test MEG archive"));
    const auto resolved = vfs.resolve("DATA/XML/TEST.XML");
    require(resolved && resolved->size == 8, "VFS MEG resolution failed");
    std::error_code ignored;
    std::filesystem::remove(archive_path, ignored);
}

void test_patch_order()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-patch-order-test";
    const auto data = root / "Data";
    std::filesystem::create_directories(data);
    for (const auto* name : {"Config.meg", "Patch2.meg", "64Patch.meg", "Patch.meg"})
    {
        std::ofstream(data / name, std::ios::binary).put('\0');
    }
    {
        std::ofstream manifest(data / "megafiles.xml", std::ios::binary);
        manifest << "<Mega_Files><File>Data\\Config.meg</File></Mega_Files>";
    }

    const auto archives = eaw::discover_meg_archives(root);
    require(archives.size() == 4, "unexpected archive discovery count");
    require(archives[0].filename() == "Config.meg", "manifest order failed");
    require(archives[1].filename() == "Patch.meg", "base patch order failed");
    require(archives[2].filename() == "Patch2.meg", "numbered patch order failed");
    require(archives[3].filename() == "64Patch.meg", "64-bit patch order failed");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

void test_foc_inherits_eaw_assets()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-foc-inheritance-test";
    const auto eaw_data = root / "GameData" / "Data" / "Art" / "Textures";
    const auto foc_data = root / "corruption" / "Data" / "Art" / "Textures";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(eaw_data);
    std::filesystem::create_directories(foc_data);
    {
        std::ofstream(eaw_data / "Shared.dds", std::ios::binary) << "eaw-shared";
        std::ofstream(eaw_data / "BaseOnly.dds", std::ios::binary) << "eaw-only";
        std::ofstream(foc_data / "Shared.dds", std::ios::binary) << "foc-shared";
    }

    const eaw::GameProfile profile{root, root / "corruption", root / "corruption" / "Data", eaw::GameVariant::forces_of_corruption};
    eaw::VirtualFileSystem vfs;
    const auto report = eaw::mount_game_assets(vfs, profile);
    require(report.warnings.empty(), "unexpected inherited-asset mount warning");
    const auto base_only = expect_ok(vfs.read("Data/Art/Textures/BaseOnly.dds"), "reading BaseOnly.dds");
    const auto shared = expect_ok(vfs.read("Data/Art/Textures/Shared.dds"), "reading Shared.dds");
    require(std::string(reinterpret_cast<const char*>(base_only.data()), base_only.size()) == "eaw-only", "FoC did not inherit a base-game-only asset");
    require(std::string(reinterpret_cast<const char*>(shared.data()), shared.size()) == "foc-shared", "FoC did not override a shared base-game asset");
    std::filesystem::remove_all(root, ignored);
}

void test_layered_precedence_corpus()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-layered-precedence-corpus";
    const auto eaw_runtime = root / "GameData";
    const auto foc_runtime = root / "corruption";
    const auto high_mod = root / "Mods" / "HighPriority";
    const auto low_mod = root / "Mods" / "LowPriority";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    const auto write_manifest = [](const std::filesystem::path& runtime, const std::string_view archive)
    {
        std::filesystem::create_directories(runtime / "Data");
        std::ofstream manifest(runtime / "Data" / "megafiles.xml", std::ios::binary | std::ios::trunc);
        manifest << "<Mega_Files><File>Data\\" << archive << "</File></Mega_Files>";
    };
    const auto write_loose = [](const std::filesystem::path& runtime, const std::filesystem::path& relative, const std::string_view contents)
    {
        const auto path = runtime / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << contents;
    };

    write_manifest(eaw_runtime, "Base.meg");
    write_test_meg(eaw_runtime / "Data" / "Base.meg", {{"DATA\\XML\\TARGET.XML", "eaw-manifest"}, {"DATA\\XML\\EAW_ONLY.XML", "eaw-only"}, {"DATA\\XML\\PATCH_CHAIN.XML", "manifest"}});
    write_test_meg(eaw_runtime / "Data" / "Extra.meg", {{"DATA/XML/PATCH_CHAIN.XML", "unlisted"}});
    write_test_meg(eaw_runtime / "Data" / "Patch.meg", {{"DATA/XML/PATCH_CHAIN.XML", "patch"}, {"DATA/XML/NUMBERED_PATCH.XML", "patch-base"}});
    write_test_meg(eaw_runtime / "Data" / "Patch2.meg", {{"DATA/XML/PATCH_CHAIN.XML", "patch2"}, {"DATA/XML/NUMBERED_PATCH.XML", "patch2"}});
    write_test_meg(eaw_runtime / "Data" / "Patch10.meg", {{"DATA/XML/NUMBERED_PATCH.XML", "patch10"}});
    write_test_meg(eaw_runtime / "Data" / "64Patch.meg", {{"DATA/XML/PATCH_CHAIN.XML", "64patch"}});

    write_manifest(foc_runtime, "Base.meg");
    write_test_meg(foc_runtime / "Data" / "Base.meg", {{"DATA/XML/TARGET.XML", "foc-manifest"}, {"DATA/XML/FOC_ONLY.XML", "foc-only"}});
    write_test_meg(foc_runtime / "Data" / "Patch.meg", {{"DATA/XML/TARGET.XML", "foc-patch"}});
    write_loose(foc_runtime, "Data/XML/Target.xml", "foc-loose");

    write_manifest(low_mod, "Assets.meg");
    write_test_meg(low_mod / "Data" / "Assets.meg", {{"DATA/XML/TARGET.XML", "low-archive"}, {"DATA/XML/LOW_ONLY.XML", "low-only"}});
    write_loose(low_mod, "Data/XML/Target.xml", "low-loose");

    write_manifest(high_mod, "Assets.meg");
    write_test_meg(high_mod / "Data" / "Assets.meg", {{"DATA/XML/TARGET.XML", "high-archive"}, {"DATA/XML/HIGH_ARCHIVE_ONLY.XML", "high-archive-only"}});
    write_loose(high_mod, "Data/XML/Target.xml", "high-loose");

    const eaw::GameProfile profile{root, foc_runtime, foc_runtime / "Data", eaw::GameVariant::forces_of_corruption};
    eaw::VirtualFileSystem vfs;
    const std::array mods{high_mod, low_mod};
    const auto report = eaw::mount_game_assets(vfs, profile, mods);
    require(report.warnings.empty(), "precedence corpus produced mount warnings");

    require(read_vfs_text(vfs, "data\\xml/TARGET.xml") == "high-loose", "first mod loose file did not win the full override stack");
    require(read_vfs_text(vfs, "DATA/XML/HIGH_ARCHIVE_ONLY.XML") == "high-archive-only", "high-priority mod archive was not mounted");
    require(read_vfs_text(vfs, "Data/XML/Low_Only.xml") == "low-only", "lower-priority mod fallback was not preserved");
    require(read_vfs_text(vfs, "data/xml/foc_only.xml") == "foc-only", "FoC fallback asset was not preserved");
    require(read_vfs_text(vfs, "data/xml/eaw_only.xml") == "eaw-only", "base-game fallback asset was not preserved");
    require(read_vfs_text(vfs, "DATA/XML/PATCH_CHAIN.XML") == "64patch", "64Patch did not win the base archive patch chain");
    require(read_vfs_text(vfs, "DATA/XML/NUMBERED_PATCH.XML") == "patch10", "numbered patch archives were not ordered numerically");

    const auto target = vfs.resolve("Data/XML/Target.xml");
    require(target && target->kind == eaw::AssetSourceKind::directory && target->mount_name == "mod:loose:HighPriority", "resolved winner did not report the highest-priority mod layer");
    const auto listed = vfs.list("data\\xml");
    require(std::ranges::count(listed, std::string("DATA/XML/TARGET.XML"), &eaw::ResolvedAsset::virtual_path) == 1, "VFS list exposed duplicate overridden paths");
    const auto listed_target = std::ranges::find(listed, std::string("DATA/XML/TARGET.XML"), &eaw::ResolvedAsset::virtual_path);
    require(listed_target != listed.end() && listed_target->mount_name == "mod:loose:HighPriority", "VFS list did not expose the same winner as resolve");

    std::filesystem::remove_all(root, ignored);
}

const ModuleRegistrar registrar("vfs", {
                                           {"test_meg_archive_and_vfs_basics", &test_meg_archive_and_vfs_basics},
                                           {"test_patch_order", &test_patch_order},
                                           {"test_foc_inherits_eaw_assets", &test_foc_inherits_eaw_assets},
                                           {"test_layered_precedence_corpus", &test_layered_precedence_corpus},
                                       });

} // namespace
