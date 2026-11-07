#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/asset_path.hpp"
#include "camera_controller.hpp"
#include "visual_scene_builder.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_installation.hpp"
#include "asset_cache.hpp"
#include "camera_math.hpp"
#include "debug_draw.hpp"
#include "rts_control.hpp"
#include "terrain_height.hpp"
#include "scene_presentation.hpp"
#include "eaw/fatal_error.hpp"
#include "eaw/log.hpp"
#include "eaw/status.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/simulation.hpp"
#include "eaw/gpu_resource_lifetime.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/localization_scan.hpp"
#include "eaw/localization_workspace.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/lua_script.hpp"
#include "eaw/meg_archive.hpp"
#include "eaw/mtd_directory.hpp"
#include "eaw/text_encoding.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"
#include "eaw/xml_document.hpp"
#include "eaw/xml_schema.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Integer> void write_little_endian(std::ostream& stream, Integer value)
{
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
    {
        stream.put(static_cast<char>((value >> (index * 8U)) & 0xffU));
    }
}

void write_test_meg(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "could not create synthetic MEG archive");
    write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(entries.size()));
    write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(entries.size()));
    std::uint32_t payload_offset = 8U + static_cast<std::uint32_t>(entries.size() * 20U);
    for (const auto& [name, payload] : entries)
    {
        (void)payload;
        require(name.size() <= std::numeric_limits<std::uint16_t>::max(), "synthetic MEG filename is too long");
        write_little_endian<std::uint16_t>(stream, static_cast<std::uint16_t>(name.size()));
        stream.write(name.data(), static_cast<std::streamsize>(name.size()));
        payload_offset += 2U + static_cast<std::uint32_t>(name.size());
    }
    auto next_offset = payload_offset;
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& payload = entries[index].second;
        write_little_endian<std::uint32_t>(stream, 0);
        write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(index));
        write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(payload.size()));
        write_little_endian<std::uint32_t>(stream, next_offset);
        write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(index));
        next_offset += static_cast<std::uint32_t>(payload.size());
    }
    for (const auto& [name, payload] : entries)
    {
        (void)name;
        stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    stream.close();
}

std::filesystem::path write_test_meg()
{
    const auto path = std::filesystem::temp_directory_path() / "eawengine-meg-reader-test.meg";
    write_test_meg(path, {{"DATA\\XML\\TEST.XML", "<Test />"}});
    return path;
}

std::string read_vfs_text(const eaw::VirtualFileSystem& vfs, const std::string_view path)
{
    const auto bytes = vfs.read(path);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
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
    const auto base_only = vfs.read("Data/Art/Textures/BaseOnly.dds");
    const auto shared = vfs.read("Data/Art/Textures/Shared.dds");
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

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void append_float(std::vector<std::byte>& bytes, const float value)
{
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_string(std::vector<std::byte>& bytes, const std::string_view value)
{
    for (const char character : value)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
    bytes.push_back(std::byte{});
}

void append_wide_string(std::vector<std::byte>& bytes, const std::u16string_view value)
{
    for (const auto character : value)
    {
        append_u16(bytes, static_cast<std::uint16_t>(character));
    }
    append_u16(bytes, 0);
}

void append_chunk(std::vector<std::byte>& target, const std::uint32_t type, const std::vector<std::byte>& payload, const bool container = false)
{
    append_u32(target, type);
    append_u32(target, static_cast<std::uint32_t>(payload.size()) | (container ? 0x80000000U : 0U));
    target.insert(target.end(), payload.begin(), payload.end());
}

void append_mini(std::vector<std::byte>& target, const std::uint8_t type, const std::vector<std::byte>& payload)
{
    require(payload.size() <= 255, "test mini-chunk payload is too large");
    target.push_back(static_cast<std::byte>(type));
    target.push_back(static_cast<std::byte>(payload.size()));
    target.insert(target.end(), payload.begin(), payload.end());
}

std::vector<std::byte> make_test_alo(const bool root_visible = true)
{
    std::vector<std::byte> bone_name;
    append_string(bone_name, "ROOT");
    std::vector<std::byte> bone_data;
    append_u32(bone_data, 0xffffffffU);
    append_u32(bone_data, root_visible ? 1U : 0U);
    append_u32(bone_data, 0);
    for (int index = 0; index < 12; ++index)
    {
        append_float(bone_data, index == 0 || index == 5 || index == 10 ? 1.0F : 0.0F);
    }
    std::vector<std::byte> bone;
    append_chunk(bone, 0x203, bone_name);
    append_chunk(bone, 0x206, bone_data);
    std::vector<std::byte> skeleton;
    std::vector<std::byte> bone_count;
    append_u32(bone_count, 1);
    append_chunk(skeleton, 0x201, bone_count);
    append_chunk(skeleton, 0x202, bone, true);

    std::vector<std::byte> shader_name;
    append_string(shader_name, "MeshGloss.fx");
    std::vector<std::byte> material;
    append_chunk(material, 0x10101, shader_name);
    // A well-formed vector4 parameter and a vector3 whose data chunk is too short.
    const auto append_parameter = [&](const std::uint32_t type, const std::string& name, const std::vector<float>& data)
    {
        std::vector<std::byte> name_bytes;
        append_string(name_bytes, name);
        std::vector<std::byte> data_bytes;
        for (const auto value : data)
        {
            append_float(data_bytes, value);
        }
        std::vector<std::byte> minis;
        append_mini(minis, 1, name_bytes);
        append_mini(minis, 2, data_bytes);
        append_chunk(material, type, minis);
    };
    append_parameter(0x10106, "Diffuse", {0.25F, 0.5F, 0.75F, 0.123456789F});
    append_parameter(0x10104, "Specular", {1.0F, 2.0F});

    std::vector<std::byte> submesh_info(128, std::byte{});
    submesh_info.clear();
    append_u32(submesh_info, 3);
    append_u32(submesh_info, 1);
    submesh_info.resize(128, std::byte{});
    std::vector<std::byte> vertex_format;
    append_string(vertex_format, "PNT1");
    std::vector<std::byte> vertices(3U * 144U, std::byte{});
    std::vector<std::byte> indices(6, std::byte{});
    std::vector<std::byte> submesh;
    append_chunk(submesh, 0x10001, submesh_info);
    append_chunk(submesh, 0x10002, vertex_format);
    append_chunk(submesh, 0x10007, vertices);
    append_chunk(submesh, 0x10004, indices);

    std::vector<std::byte> mesh_name;
    append_string(mesh_name, "TEST_MESH");
    std::vector<std::byte> mesh_info;
    append_u32(mesh_info, 1);
    for (const float value : {-1.0F, -2.0F, -3.0F, 1.0F, 2.0F, 3.0F})
    {
        append_float(mesh_info, value);
    }
    append_u32(mesh_info, 0);
    append_u32(mesh_info, 0);
    append_u32(mesh_info, 0);
    mesh_info.resize(128, std::byte{});
    std::vector<std::byte> mesh;
    append_chunk(mesh, 0x401, mesh_name);
    append_chunk(mesh, 0x402, mesh_info);
    append_chunk(mesh, 0x10100, material, true);
    append_chunk(mesh, 0x10000, submesh, true);

    std::vector<std::byte> count_minis;
    std::vector<std::byte> one;
    append_u32(one, 1);
    std::vector<std::byte> zero;
    append_u32(zero, 0);
    append_mini(count_minis, 1, one);
    append_mini(count_minis, 4, zero);
    std::vector<std::byte> connection_minis;
    append_mini(connection_minis, 2, zero);
    append_mini(connection_minis, 3, zero);
    std::vector<std::byte> connections;
    append_chunk(connections, 0x601, count_minis);
    append_chunk(connections, 0x602, connection_minis);

    std::vector<std::byte> alo;
    append_chunk(alo, 0x200, skeleton, true);
    append_chunk(alo, 0x400, mesh, true);
    append_chunk(alo, 0x600, connections, true);
    return alo;
}

void test_alo_model()
{
    const auto model = eaw::AloModel::parse(make_test_alo());
    require(model.bones.size() == 1 && model.bones[0].name == "ROOT" && model.bones[0].parent == -1, "ALO skeleton decoding failed");
    require(model.meshes.size() == 1 && model.meshes[0].name == "TEST_MESH" && model.meshes[0].attached_bone == 0U, "ALO mesh decoding failed");
    require(model.submesh_count() == 1 && model.vertex_count() == 3 && model.triangle_count() == 1 && model.meshes[0].submeshes[0].vertex_stride == 144 &&
                model.meshes[0].submeshes[0].vertices.size() == 3 && model.meshes[0].submeshes[0].indices.size() == 3 && model.meshes[0].submeshes[0].shader == "MeshGloss.fx",
            "ALO submesh decoding failed");
    const auto& parameters = model.meshes[0].submeshes[0].parameters;
    require(parameters.size() == 2 && parameters[0].name == "Diffuse" && parameters[0].values == std::vector<float>{0.25F, 0.5F, 0.75F, 0.123456789F} && parameters[1].name == "Specular" &&
                parameters[1].values.empty() && parameters[1].value.empty(),
            "ALO numeric shader parameters were not decoded exactly or a short chunk was over-read");
    const auto bind_pose = model.bind_pose();
    require(bind_pose.size() == 1 && bind_pose[0].values[0] == 1.0F && bind_pose[0].values[5] == 1.0F && bind_pose[0].values[10] == 1.0F, "ALO bind-pose decoding failed");
}

void test_alo_default_mesh_visibility()
{
    require(eaw::alo_name_variant("NB_dish_ALT2").kind == eaw::AloVariantKind::alternate && eaw::alo_name_variant("NB_dish_ALT2").index == 2, "ALT suffix was not parsed");
    require(eaw::alo_name_variant("hull_lod1").kind == eaw::AloVariantKind::level_of_detail && eaw::alo_name_variant("hull_lod1").index == 1, "LOD suffix is case-insensitive");
    require(eaw::alo_name_variant("ALTAR").kind == eaw::AloVariantKind::none && eaw::alo_name_variant("BASE_ALT").kind == eaw::AloVariantKind::none &&
                eaw::alo_name_variant("BASE_ALT1X").kind == eaw::AloVariantKind::none,
            "names without a trailing numbered variant were misread");

    eaw::AloModel model;
    model.bones = {{.name = "ROOT", .visible = true}, {.name = "FIRE_ALT1", .visible = true}, {.name = "DEBRIS", .visible = false}};
    const auto mesh = [](std::string name, std::optional<std::uint32_t> bone = std::nullopt, const bool hidden = false)
    {
        eaw::AloMesh result;
        result.name = std::move(name);
        result.attached_bone = bone;
        result.hidden = hidden;
        return result;
    };
    model.meshes = {
        mesh("BASE_ALT0"), mesh("BASE_ALT1"), mesh("TOWER"), mesh("HULL_LOD0"), mesh("HULL_LOD2"), mesh("HULL_LOD1"), mesh("FLAME", 1U), mesh("RUBBLE", 2U), mesh("PLATE", std::nullopt, true)};
    const auto visible = eaw::alo_default_mesh_visibility(model);
    require(visible == std::vector<bool>{true, false, true, false, true, false, false, false, false}, "default mesh visibility must keep ALT0 and the highest LOD only");
    const auto animated = eaw::alo_default_mesh_visibility(model, false);
    require(animated[7] && !animated[6] && !animated[8], "an animated caller must decide bind-pose bone visibility from its pose");
}

void test_gpu_resource_lifetime()
{
    struct TestTag
    {};
    eaw::HandlePool<TestTag, std::string> pool;
    require(!eaw::ResourceHandle<TestTag>{}.valid() && pool.get({}) == nullptr, "a default handle must not resolve");
    const auto first = pool.insert("first");
    const auto second = pool.insert("second");
    require(first.valid() && pool.size() == 2 && *pool.get(first) == "first" && *pool.get(second) == "second", "handle pool insert or lookup failed");
    require(pool.erase(first) == std::optional<std::string>("first") && pool.get(first) == nullptr && !pool.erase(first), "an erased handle must stop resolving");
    const auto reused = pool.insert("third");
    require(reused.index == first.index && reused.generation != first.generation && pool.get(first) == nullptr && *pool.get(reused) == "third", "a reused slot must not resolve the stale handle");
    require(pool.get({second.index, second.generation + 1U}) == nullptr && pool.get({99, 1}) == nullptr, "foreign or future handles must not resolve");

    eaw::DescriptorSlotAllocator slots(3, 3);
    const auto a = slots.allocate();
    const auto b = slots.allocate();
    const auto c = slots.allocate();
    require(a == 3U && b == 4U && c == 5U && !slots.allocate(), "descriptor slots must come from the configured range, lowest first");
    slots.release(*a, 10);
    slots.release(*c, 12);
    require(!slots.allocate() && slots.pending_count() == 2, "a released slot must not be reused before its fence completes");
    require(slots.reclaim(9).empty() && slots.reclaim(10) == std::vector<std::uint32_t>{3U} && slots.allocate() == 3U && slots.pending_count() == 1, "a slot returns once its release fence completes");
    bool rejected = false;
    try
    {
        slots.release(*b, 11);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    require(rejected, "out-of-order release fences must be rejected");
    rejected = false;
    try
    {
        slots.release(*c, 13);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    require(rejected, "releasing a slot that is already pending must be rejected");

    eaw::FenceRetirementQueue<std::shared_ptr<int>> retired;
    auto tracked = std::make_shared<int>(7);
    const std::weak_ptr<int> observer = tracked;
    retired.push(std::move(tracked), 5);
    retired.push(std::make_shared<int>(8), 6);
    require(retired.retire(4) == 0 && !observer.expired(), "a retired object must outlive its fence");
    require(retired.retire(5) == 1 && observer.expired() && retired.size() == 1, "a retired object must be released once its fence completes");
    rejected = false;
    try
    {
        retired.push(std::make_shared<int>(9), 2);
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    require(rejected && retired.retire(6) == 1 && retired.size() == 0, "retirement fences must be non-decreasing");
}

void test_asset_cache_dependencies()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-asset-cache-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "base" / "Data" / "Art" / "Models");
    std::filesystem::create_directories(root / "mod" / "Data");
    const auto write_file = [](const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    const auto model_bytes = make_test_alo();
    write_file(root / "base" / "Data" / "Art" / "Models" / "TEST.ALO", model_bytes);
    const auto archive = root / "data.meg";
    write_test_meg(archive, {{"DATA\\XML\\A.XML", "<A />"}});

    // Priority: base directory < archive < mod directory.
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("base", root / "base"));
    vfs.mount(std::make_unique<eaw::MegMount>("archive", archive));
    vfs.mount(std::make_unique<eaw::DirectoryMount>("mod", root / "mod"));

    eaw::AssetCache cache;
    const eaw::RecordingAssetReader first_build(vfs);
    (void)eaw::build_visual_model(first_build, "Data/Art/Models/TEST.ALO", &cache);
    const eaw::RecordingAssetReader second_build(vfs);
    (void)eaw::build_visual_model(second_build, "Data/Art/Models/TEST.ALO", &cache);
    const auto stats = cache.stats();
    require(stats.entries == 1 && stats.misses == 1 && stats.hits == 1, "a second build must reuse the cached model");
    const auto scene_dependencies = second_build.dependencies();
    require(scene_dependencies.file_count() >= 1 && scene_dependencies.changes(vfs).empty(), "a reused asset must contribute its dependencies, unchanged");

    // A missing file is a dependency too: adding it changes the recorded state.
    const eaw::RecordingAssetReader probe(vfs);
    require(!probe.resolve("Data/Art/Textures/NEW.DDS"), "the probe file must start absent");
    const std::array<std::byte, 1> one_byte{std::byte{1}};
    write_file(root / "mod" / "Data" / "Art" / "Textures" / "NEW.DDS", one_byte);
    require(probe.dependencies().changes(vfs) == std::vector<std::string>{"DATA/ART/TEXTURES/NEW.DDS"}, "adding a previously missing file must be detected");

    // A loose override in a higher layer invalidates the model cached from the lower layer.
    write_file(root / "mod" / "Data" / "Art" / "Models" / "TEST.ALO", model_bytes);
    require(scene_dependencies.changes(vfs) == std::vector<std::string>{"DATA/ART/MODELS/TEST.ALO"}, "a mod override must change the dependency");
    require(cache.invalidate(vfs).size() == 1 && cache.stats().entries == 0, "the overridden model must be dropped from the cache");

    // Archive entries: a rebuilt archive is detected, remounted, and its assets reload.
    const auto read_text = [](const eaw::AssetReader& reader)
    {
        const auto bytes = reader.read("Data/XML/A.XML");
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto text = cache.load<std::string>("text", "Data/XML/A.XML", vfs, read_text);
    require(*text == "<A />", "archive text was not loaded");
    eaw::ArchiveWatch watch(vfs);
    require(watch.changed(vfs).empty(), "an untouched archive must not report a change");
    write_test_meg(archive, {{"DATA\\XML\\A.XML", "<A version='2' />"}});
    require(watch.changed(vfs) == std::vector<std::size_t>{1}, "a rebuilt archive must be reported by mount index");
    vfs.replace_mount(1, std::make_unique<eaw::MegMount>("archive", archive));
    watch.accept(vfs, 1);
    require(watch.changed(vfs).empty(), "an accepted remount must clear the change");
    require(cache.invalidate(vfs) == std::vector<std::string>{"text|DATA/XML/A.XML"}, "assets from a rebuilt archive must be invalidated");
    require(*cache.load<std::string>("text", "Data/XML/A.XML", vfs, read_text) == "<A version='2' />", "the remounted archive must serve the new bytes");

    // A failing loader caches nothing, so the next load retries.
    bool threw = false;
    try
    {
        (void)cache.load<std::string>("text", "Data/XML/MISSING.XML", vfs,
                                      [](const eaw::AssetReader& reader)
                                      {
                                          const auto bytes = reader.read("Data/XML/MISSING.XML");
                                          return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                                      });
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    require(threw && cache.stats().entries == 1, "a failed load must not be cached");

    // Eviction: entries a build did not touch are dropped once it succeeds.
    const auto epoch = cache.begin_epoch();
    (void)cache.load<std::string>("text", "Data/XML/OTHER.XML", vfs,
                                  [](const eaw::AssetReader&)
                                  {
                                      return std::string("other");
                                  });
    require(cache.evict_unused_since(epoch) == 1 && cache.stats().entries == 1 && *cache.load<std::string>("text", "Data/XML/OTHER.XML", vfs, read_text) == "other",
            "only entries unused since the epoch must be evicted");
    std::filesystem::remove_all(root, ignored);
}

void test_debug_draw()
{
    // A unit box translated by (10, 20, 30): 12 edges, each of length 1 along one axis, all corners
    // inside the translated box.
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
    std::vector<eaw::VisualDebugLine> lines;
    eaw::append_debug_box(lines, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, transform, {1.0F, 0.0F, 0.0F, 1.0F});
    require(lines.size() == 12, "a debug box must have 12 edges");
    std::array<int, 3> axis_edges{};
    for (const auto& line : lines)
    {
        int changed = -1;
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto base = axis == 0 ? 10.0F : axis == 1 ? 20.0F : 30.0F;
            require(line.from[axis] >= base && line.from[axis] <= base + 1.0F && line.to[axis] >= base && line.to[axis] <= base + 1.0F, "debug box corner outside the transformed box");
            if (line.from[axis] != line.to[axis])
            {
                require(changed < 0 && std::fabs(line.to[axis] - line.from[axis]) == 1.0F, "a debug box edge must run along exactly one axis");
                changed = axis;
            }
        }
        require(changed >= 0, "a debug box edge must not be degenerate");
        ++axis_edges[static_cast<std::size_t>(changed)];
    }
    require(axis_edges == std::array<int, 3>{4, 4, 4}, "a debug box has four edges per axis");
    lines.clear();
    eaw::append_debug_axes(lines, {1.0F, 2.0F, 3.0F}, 5.0F);
    require(lines.size() == 3 && lines[0].to == std::array<float, 3>{6.0F, 2.0F, 3.0F} && lines[2].to == std::array<float, 3>{1.0F, 2.0F, 8.0F}, "debug axes have the wrong extent");
}

void test_simulation_core()
{
    // Movement: 3 units/s at 10 ticks/s reaches a point 1 unit away in four ticks (0.3 per tick).
    eaw::SimulationWorld world(0.1);
    const auto mover = world.spawn({.type_name = "Mover", .owner = 1, .position = {0.0F, 0.0F, 0.0F}, .max_speed = 3.0F, .visual = 7});
    const auto statue = world.spawn({.type_name = "Statue", .position = {5.0F, 5.0F, 0.0F}});
    world.submit({eaw::SimCommandKind::move, {mover, statue}, {1.0F, 0.0F, 0.0F}});
    world.step();
    const auto& first = world.snapshot();
    require(first.tick == 1 && first.entities.size() == 2 && first.entities[0].id == mover && std::fabs(first.entities[0].position.x - 0.3F) < 1.0e-5F && first.entities[0].moving &&
                first.entities[0].visual == 7 && first.entities[0].owner == 1,
            "a moving entity must advance by speed times tick length");
    require(!first.entities[1].moving && first.entities[1].position == eaw::SimVector3{5.0F, 5.0F, 0.0F}, "an entity without speed must ignore move commands");
    for (int tick = 0; tick < 3; ++tick)
    {
        world.step();
    }
    require(world.snapshot().entities[0].position == eaw::SimVector3{1.0F, 0.0F, 0.0F} && !world.snapshot().entities[0].moving, "an entity must stop exactly at its target");

    // Destruction: ids are generation-checked, stale references are counted, slots are reused.
    world.submit({eaw::SimCommandKind::destroy, {mover}, {}});
    world.step();
    require(!world.alive(mover) && world.alive(statue) && world.snapshot().entities.size() == 1, "a destroyed entity must leave the world");
    world.submit({eaw::SimCommandKind::move, {mover}, {2.0F, 0.0F, 0.0F}});
    world.step();
    require(world.stats().stale_references == 1, "a command naming a destroyed entity must count as stale");
    const auto reused = world.spawn({.type_name = "Mover", .max_speed = 1.0F});
    require(reused.index == mover.index && reused.generation != mover.generation && !world.alive(mover) && world.alive(reused), "a reused slot must not revive the old id");

    // Determinism: identical histories give identical state; a different command does not.
    const auto run = [](const float target_x)
    {
        eaw::SimulationWorld sim(1.0 / 30.0);
        std::vector<eaw::EntityId> ids;
        for (int index = 0; index < 20; ++index)
        {
            ids.push_back(sim.spawn({.type_name = "Unit", .position = {static_cast<float>(index), 0.0F, 0.0F}, .heading = 0.1F * static_cast<float>(index), .max_speed = 12.5F}));
        }
        sim.submit({eaw::SimCommandKind::move, {ids.begin(), ids.begin() + 10}, {target_x, 40.0F, 0.0F}});
        for (int tick = 0; tick < 45; ++tick)
        {
            if (tick == 20)
            {
                sim.submit({eaw::SimCommandKind::destroy, {ids[3], ids[15]}, {}});
                sim.submit({eaw::SimCommandKind::stop, {ids[4]}, {}});
            }
            sim.step();
        }
        return sim.state_hash();
    };
    require(run(25.0F) == run(25.0F) && run(25.0F) != run(26.0F), "identical command histories must produce identical state");

    // Clock: whole ticks, a bounded catch-up, and an interpolation fraction.
    eaw::FixedStepClock clock(10.0, 4);
    require(clock.advance(0.25) == 2 && std::fabs(clock.interpolation() - 0.5F) < 1.0e-4F, "the clock must return whole ticks and the remaining fraction");
    require(clock.advance(10.0) == 4 && clock.dropped_ticks() > 90, "a long stall must be capped and the excess dropped");

    // Interpolation blends matching entities and keeps new ones at their current state.
    eaw::SimSnapshot before{1, {{{1, 1}, {0.0F, 0.0F, 0.0F}, 0.0F}}};
    eaw::SimSnapshot after{2, {{{1, 1}, {10.0F, 0.0F, 0.0F}, 1.0F}, {{2, 1}, {4.0F, 4.0F, 0.0F}, 0.0F}}};
    const auto blended = eaw::interpolate_snapshots(before, after, 0.25F);
    require(blended.size() == 2 && std::fabs(blended[0].position.x - 2.5F) < 1.0e-5F && std::fabs(blended[0].heading - 0.25F) < 1.0e-5F && blended[1].position.x == 4.0F,
            "snapshot interpolation must blend matching entities only");

    bool rejected = false;
    try
    {
        (void)world.spawn({.type_name = "Bad", .position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "a non-finite spawn must be rejected");
}

void test_scene_presentation()
{
    // pose_transform keeps scale and tilt, turns about +Z by the heading change, and moves.
    const std::array<float, 16> base{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 5, 6, 7, 1};
    const auto same = eaw::pose_transform(base, 0.0F, {1.0F, 2.0F, 3.0F}, 0.0F);
    require(same[0] == 2.0F && same[5] == 2.0F && same[12] == 1.0F && same[13] == 2.0F && same[14] == 3.0F, "an unchanged heading must only move the transform");
    const auto turned = eaw::pose_transform(base, 0.0F, {0.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F);
    require(std::fabs(turned[0]) < 1.0e-5F && std::fabs(turned[1] - 2.0F) < 1.0e-5F && std::fabs(turned[4] + 2.0F) < 1.0e-5F && turned[10] == 2.0F,
            "a quarter turn must rotate the model X axis onto +Y and keep its scale");

    // The presentation spawns entities from scene instances, keeps backgrounds static, and maps
    // instances back to entities.
    eaw::VisualScene scene;
    scene.meshes.resize(2);
    scene.meshes[0].name = "UNIT.ALO";
    scene.meshes[1].name = "SKY.ALO";
    eaw::VisualInstance sky{1};
    sky.background = true;
    eaw::VisualInstance unit{0};
    unit.transform[12] = 10.0F;
    scene.instances = {sky, unit};
    eaw::SimulationWorld world(0.1);
    eaw::EntitySpawn spawn;
    spawn.type_name = "Test_Unit";
    spawn.position = {10.0F, 0.0F, 0.0F};
    spawn.max_speed = 10.0F;
    const std::array<eaw::MapEntitySpawn, 1> entities{{{spawn, 1}}};
    const eaw::ScenePresentation presentation(scene, entities, world);
    world.step();
    const auto id = world.snapshot().entities.at(0).id;
    world.submit({eaw::SimCommandKind::move, {id}, {10.0F, 5.0F, 0.0F}});
    world.step();
    std::vector<eaw::VisualInstance> instances;
    std::vector<eaw::EntityId> ids;
    presentation.build(world.snapshot().entities, {eaw::entity_key(id)}, instances, ids);
    require(instances.size() == 2 && instances[0].background && !ids[0].valid() && ids[1] == id && instances[1].highlighted && instances[1].mesh == 0,
            "presentation must list backgrounds first, then entities with their selection state");
    require(std::fabs(instances[1].transform[13] - 1.0F) < 1.0e-5F && std::fabs(instances[1].transform[1] - 1.0F) < 1.0e-5F, "an entity's instance must follow its simulated position and heading");
    require(presentation.type_name(id) == "Test_Unit" && presentation.mesh_name(0) == "UNIT.ALO", "presentation must name entities by type and mesh");
}

void test_rts_control()
{
    // A camera above the origin looking down +X and slightly down.
    eaw::VisualCamera camera;
    camera.eye = {-100.0F, 0.0F, 80.0F};
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.vertical_fov_degrees = 50.0F;
    camera.near_plane = 1.0F;
    camera.far_plane = 5000.0F;
    const float width = 800.0F;
    const float height = 600.0F;

    // The target projects to the screen center; unprojecting it at its depth returns it.
    const auto center = eaw::project_to_screen(camera, width, height, {0.0F, 0.0F, 0.0F});
    require(center && std::fabs((*center)[0] - 399.5F) < 0.01F && std::fabs((*center)[1] - 299.5F) < 0.01F, "the look-at target must project to the screen center");
    require(!eaw::project_to_screen(camera, width, height, {-200.0F, 0.0F, 80.0F}), "a point behind the camera must not project");
    const std::array<float, 3> point{30.0F, 20.0F, 5.0F};
    const auto screen = eaw::project_to_screen(camera, width, height, point);
    require(screen.has_value(), "a visible point must project");
    // Depth of the point as the renderer stores it: z_ndc = f/(f-n) - n f/((f-n) z_view).
    const std::array<float, 3> forward{100.0F / std::sqrt(100.0F * 100.0F + 80.0F * 80.0F), 0.0F, -80.0F / std::sqrt(100.0F * 100.0F + 80.0F * 80.0F)};
    const auto view_depth = (point[0] - camera.eye[0]) * forward[0] + (point[2] - camera.eye[2]) * forward[2];
    const auto depth = camera.far_plane / (camera.far_plane - camera.near_plane) - camera.near_plane * camera.far_plane / ((camera.far_plane - camera.near_plane) * view_depth);
    const auto back = eaw::unproject_screen(camera, width, height, (*screen)[0], (*screen)[1], depth);
    require(std::fabs(back[0] - point[0]) < 0.05F && std::fabs(back[1] - point[1]) < 0.05F && std::fabs(back[2] - point[2]) < 0.05F, "unprojecting at the stored depth must return the point");
    const auto ground = eaw::intersect_ground_plane(camera, width, height, (*center)[0], (*center)[1], 0.0F);
    require(ground && std::fabs((*ground)[0]) < 0.05F && std::fabs((*ground)[1]) < 0.05F, "the center ray must meet the ground at the target");

    // Box selection keeps mobile, selectable entities inside the rectangle.
    std::vector<eaw::EntitySnapshot> entities(3);
    entities[0] = {{1, 1}, {0.0F, 0.0F, 0.0F}, 0.0F, 0, 0, false, true, 10.0F};
    entities[1] = {{2, 1}, {0.0F, 0.0F, 0.0F}, 0.0F, 0, 0, false, true, 0.0F};
    entities[2] = {{3, 1}, {0.0F, 300.0F, 0.0F}, 0.0F, 0, 0, false, true, 10.0F};
    const auto boxed = eaw::entities_in_rectangle(entities, camera, width, height, {350.0F, 250.0F, 450.0F, 350.0F}, true);
    require(boxed.size() == 1 && boxed[0] == eaw::EntityId{1, 1}, "drag-selection must keep only mobile entities inside the box");
    require(eaw::entities_in_rectangle(entities, camera, width, height, {450.0F, 350.0F, 350.0F, 250.0F}, false).size() == 2, "any corner order works and props count without mobile_only");

    // Grid targets are centered on the order point.
    const auto targets = eaw::grid_targets({100.0F, 50.0F, 2.0F}, 4, 10.0F);
    require(targets.size() == 4 && targets[0].x == 95.0F && targets[0].y == 45.0F && targets[3].x == 105.0F && targets[3].y == 55.0F && targets[2].z == 2.0F,
            "grid targets must form a centered square");
    require(eaw::grid_targets({}, 0, 10.0F).empty(), "no units need no targets");
}

void test_terrain_height_field()
{
    // A 4x4 field rising along X (height = 5 * column) with a liquid at 7.
    eaw::TerrainHeightField field;
    field.width = 4;
    field.height = 4;
    field.spacing = 20.0F;
    for (std::uint32_t y = 0; y < 4; ++y)
    {
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            field.heights.push_back(5.0F * static_cast<float>(x));
        }
    }
    require(field.valid() && std::fabs(field.terrain_height(30.0F, 10.0F) - 7.5F) < 1.0e-4F && field.terrain_height(-50.0F, 0.0F) == 0.0F && field.terrain_height(500.0F, 0.0F) == 15.0F,
            "terrain heights must interpolate within cells and clamp at the edges");
    field.liquid_level = 7.0F;
    require(field.surface_height(10.0F, 10.0F) == 7.0F && field.surface_height(50.0F, 10.0F) == 12.5F, "the liquid surface must cover lower terrain");

    // A camera looking down onto the field: the hit lies on the surface and on the pixel's ray.
    eaw::VisualCamera camera;
    camera.eye = {-40.0F, 30.0F, 120.0F};
    camera.target = {40.0F, 30.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.vertical_fov_degrees = 60.0F;
    camera.near_plane = 1.0F;
    camera.far_plane = 2000.0F;
    const float width = 640.0F;
    const float height = 480.0F;
    for (const auto& pixel : {std::array<float, 2>{320.0F, 240.0F}, std::array<float, 2>{420.0F, 300.0F}, std::array<float, 2>{250.0F, 180.0F}})
    {
        const auto hit = eaw::intersect_terrain(camera, width, height, pixel[0], pixel[1], field);
        require(hit.has_value(), "a ray into the map must hit its surface");
        require(std::fabs((*hit)[2] - field.surface_height((*hit)[0], (*hit)[1])) < 1.0e-3F, "a terrain hit must lie on the surface");
        const auto screen = eaw::project_to_screen(camera, width, height, *hit);
        require(screen && std::fabs((*screen)[0] - pixel[0]) < 0.5F && std::fabs((*screen)[1] - pixel[1]) < 0.5F, "a terrain hit must lie on the clicked pixel's ray");
    }
    camera.target = {-40.0F, 30.0F, 500.0F};
    camera.up = {1.0F, 0.0F, 0.0F};
    require(!eaw::intersect_terrain(camera, width, height, 320.0F, 240.0F, field), "a ray pointing away from the map must miss");
}

void test_fatal_error_report()
{
    // fatal_error itself ends the process, so only the report format is testable in-process.
    const auto location = std::source_location::current();
    const auto report = eaw::format_fatal_error("Bad state", location);
    require(report.starts_with("Bad state\n\nFile: ") && report.find(location.file_name()) != std::string::npos && report.find("Line: " + std::to_string(location.line())) != std::string::npos &&
                report.find("Function: ") != std::string::npos,
            "a fatal error report must name the message, file, line, and function");
    EAW_ASSERT(report.size() > 10);
    EAW_ASSERT_MSG(!report.empty(), "passing assertions must not end the process");
}

eaw::Result<int> parse_positive(const int value)
{
    if (value <= 0)
    {
        return eaw::make_error("value " + std::to_string(value) + " is not positive");
    }
    return value;
}

eaw::Result<int> doubled_positive(const int value)
{
    EAW_TRY_ASSIGN(const auto checked, parse_positive(value));
    return checked * 2;
}

eaw::Status require_positive(const int value)
{
    EAW_TRY(parse_positive(value));
    return eaw::Status::success();
}

void test_status_and_log()
{
    const auto good = doubled_positive(4);
    require(good.ok() && *good == 8 && good.value_or(0) == 8, "a successful Result must carry its value");
    const auto bad = doubled_positive(-1);
    require(!bad && bad.error().message == "value -1 is not positive" && bad.value_or(7) == 7, "a failure must propagate unchanged through EAW_TRY_ASSIGN");
    require(require_positive(3).ok() && !require_positive(0).ok() && require_positive(0).error().message.find("not positive") != std::string::npos, "EAW_TRY must return the failed status");
    require(eaw::with_context("reading X.ALO", eaw::make_error("truncated")).message == "reading X.ALO: truncated", "context must prefix the message");

    // The log hands records to every sink and counts them by level.
    std::vector<std::string> captured;
    eaw::clear_log_sinks();
    eaw::add_log_sink(
        [&captured](const eaw::LogRecord& record)
        {
            captured.push_back(std::string(eaw::to_string(record.level)) + "|" + std::string(record.category) + "|" + std::string(record.message));
        });
    const auto warnings_before = eaw::log_count(eaw::LogLevel::warning);
    eaw::log_warning("test", "first");
    eaw::log_warning("test", eaw::make_error("second"));
    eaw::log_info("test", "third");
    eaw::clear_log_sinks();
    require(captured == std::vector<std::string>{"warning|test|first", "warning|test|second", "info|test|third"} && eaw::log_count(eaw::LogLevel::warning) == warnings_before + 2,
            "log records must reach sinks in order and be counted");
    const auto line = eaw::format_log_record({eaw::LogLevel::warning, "map", "bad placement"});
    require(line.starts_with("[") && line.ends_with("] [warning] [map] bad placement"), "log lines must carry time, level, category, and message");
}

// A minimal 4x4 BC1 DDS with one mip level.
std::vector<std::byte> make_test_dds()
{
    std::vector<std::byte> dds(128U + 8U, std::byte{});
    const auto write_u32 = [](std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
    };
    write_u32(dds, 0, 0x20534444U);
    write_u32(dds, 4, 124);
    write_u32(dds, 12, 4);
    write_u32(dds, 16, 4);
    write_u32(dds, 28, 1);
    write_u32(dds, 76, 32);
    write_u32(dds, 80, 0x4U);
    write_u32(dds, 84, 0x31545844U);
    return dds;
}

void test_texture_formats_and_materials()
{
    auto dds = make_test_dds();
    const auto write_u32 = [](std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
    };
    const auto texture = eaw::DdsTexture::parse(dds);
    require(texture.width == 4 && texture.height == 4 && texture.mip_levels == 1 && texture.format == eaw::DdsFormat::bc1_unorm && texture.data_offset == 128 && texture.data_size == 8 &&
                texture.subresources.size() == 1 && texture.subresources[0].row_pitch == 8,
            "DDS semantic decoding failed");
    bool truncated_dds_rejected = false;
    try
    {
        static_cast<void>(eaw::DdsTexture::parse(std::span<const std::byte>(dds).first(dds.size() - 1U)));
    }
    catch (const std::runtime_error&)
    {
        truncated_dds_rejected = true;
    }
    require(truncated_dds_rejected, "truncated DDS data was accepted");

    std::vector<std::byte> mtd;
    append_u32(mtd, 2);
    const auto append_mtd_entry = [&](const std::string_view name, const std::uint32_t x, const std::uint32_t y, const std::uint32_t width, const std::uint32_t height, const bool alpha)
    {
        const auto begin = mtd.size();
        for (const auto character : name)
        {
            mtd.push_back(static_cast<std::byte>(character));
        }
        mtd.resize(begin + 64U, std::byte{});
        append_u32(mtd, x);
        append_u32(mtd, y);
        append_u32(mtd, width);
        append_u32(mtd, height);
        mtd.push_back(alpha ? std::byte{1} : std::byte{});
    };
    append_mtd_entry("ICON_A.TGA", 0, 0, 32, 16, true);
    append_mtd_entry("ICON_B.TGA", 32, 8, 8, 24, false);
    const auto directory = eaw::MtdDirectory::parse(mtd);
    require(directory.entries.size() == 2 && directory.entries[0].name == "ICON_A.TGA" && directory.entries[0].has_alpha && directory.entries[0].name_crc == eaw::dat_key_crc("ICON_A.TGA") &&
                directory.atlas_width == 40 && directory.atlas_height == 32 && directory.alpha_entries == 1,
            "MTD semantic decoding failed");
    bool truncated_mtd_rejected = false;
    try
    {
        static_cast<void>(eaw::MtdDirectory::parse(std::span<const std::byte>(mtd).first(mtd.size() - 1U)));
    }
    catch (const std::runtime_error&)
    {
        truncated_mtd_rejected = true;
    }
    require(truncated_mtd_rejected, "truncated MTD data was accepted");

    eaw::AloSubMesh submesh;
    submesh.shader = "MeshGloss.fx";
    submesh.parameters.push_back({"DiffuseTexture", eaw::AloShaderParameterType::texture, "UNIT.TGA"});
    submesh.parameters.push_back({"BumpTexture", eaw::AloShaderParameterType::texture, "UNIT_BUMP.TGA"});
    submesh.parameters.push_back({"Glossiness", eaw::AloShaderParameterType::scalar, "32"});
    const auto material = eaw::LegacyMaterial::translate(submesh);
    require(material.family == eaw::LegacyMaterialFamily::mesh && material.blend == eaw::LegacyBlendMode::opaque && material.textures.size() == 2 &&
                material.textures[0].semantic == eaw::LegacyTextureSemantic::base_color && material.textures[1].semantic == eaw::LegacyTextureSemantic::normal && material.parameters.size() == 3,
            "legacy material translation failed");

    require(material.source == eaw::LegacyTranslationSource::reference_profile && material.features.gloss == eaw::LegacyGlossSource::base_alpha && material.render.depth_write &&
                material.render.fade_blend,
            "gloss reference profile was not applied");

    // The reference corpus renders shields additively (ONE/ONE) with culling disabled.
    submesh.shader = "MeshShield.fx";
    const auto shield = eaw::LegacyMaterial::translate(submesh);
    require(shield.family == eaw::LegacyMaterialFamily::shield && shield.blend == eaw::LegacyBlendMode::additive && shield.two_sided() && !shield.render.depth_write &&
                shield.features.uv_animation == eaw::LegacyUvAnimation::shield_layers,
            "shield material did not select the additive two-sided profile");

    // Lookup ignores case, directories, and the .fx/.fxo extension; skinning is a vertex feature.
    submesh.shader = "Shaders\\RSKINBUMPCOLORIZE.FX";
    const auto skinned = eaw::LegacyMaterial::translate(submesh);
    require(skinned.source == eaw::LegacyTranslationSource::reference_profile && skinned.family == eaw::LegacyMaterialFamily::mesh && skinned.vertex.rigid_skinning && skinned.vertex.tangent_frame &&
                skinned.features.colorize && skinned.features.gloss == eaw::LegacyGlossSource::normal_alpha && skinned.features.lighting == eaw::LegacyLightingModel::normal_mapped,
            "skinned bump-colorize profile was not applied");

    submesh.shader = "MeshHeat.fx";
    const auto heat = eaw::LegacyMaterial::translate(submesh);
    require(heat.family == eaw::LegacyMaterialFamily::heat && heat.render.phase == eaw::LegacyRenderPhase::heat && !heat.render.depth_write, "heat profile was not applied");

    submesh.shader = "Nebula.fx";
    const auto nebula = eaw::LegacyMaterial::translate(submesh);
    require(nebula.blend == eaw::LegacyBlendMode::additive && nebula.two_sided() && nebula.features.vertex_animation == eaw::LegacyVertexAnimation::nebula_wobble, "nebula profile was not applied");

    submesh.shader = "Tree.fx";
    const auto tree = eaw::LegacyMaterial::translate(submesh);
    require(tree.blend == eaw::LegacyBlendMode::alpha_test && tree.render.alpha_test_threshold && *tree.render.alpha_test_threshold > 0.5F && *tree.render.alpha_test_threshold < 0.51F &&
                tree.render.depth_write,
            "tree alpha-test profile was not applied");

    submesh.shader = "MeshOccludedUnit.fx";
    const auto occluded = eaw::LegacyMaterial::translate(submesh);
    require(occluded.render.depth_test == eaw::LegacyDepthTest::greater && occluded.render.stencil, "occluded-unit profile did not select a depth-greater test");

    submesh.shader = "MeshShadowVolume.fx";
    const auto shadow = eaw::LegacyMaterial::translate(submesh);
    require(shadow.family == eaw::LegacyMaterialFamily::shadow && !shadow.render.color_write && shadow.render.phase == eaw::LegacyRenderPhase::shadow,
            "shadow-volume profile did not disable color writes");

    // Planet cloud layers must not be mistaken for a second normal or base-color map.
    submesh.shader = "Planet.fx";
    submesh.parameters = {{"BaseTexture", eaw::AloShaderParameterType::texture, "P.TGA"},
                          {"NormalTexture", eaw::AloShaderParameterType::texture, "P_B.TGA"},
                          {"CloudTexture", eaw::AloShaderParameterType::texture, "C.TGA"},
                          {"CloudNormalTexture", eaw::AloShaderParameterType::texture, "C_B.TGA"}};
    const auto planet = eaw::LegacyMaterial::translate(submesh);
    require(planet.textures.size() == 4 && planet.textures[0].semantic == eaw::LegacyTextureSemantic::base_color && planet.textures[1].semantic == eaw::LegacyTextureSemantic::normal &&
                planet.textures[2].semantic == eaw::LegacyTextureSemantic::cloud && planet.textures[3].semantic == eaw::LegacyTextureSemantic::cloud_normal &&
                planet.features.lighting == eaw::LegacyLightingModel::planet_atmosphere,
            "planet texture semantics were not separated");

    // Shaders outside the reference corpus keep a conservative, explicitly tagged fallback.
    submesh.shader = "alDefault.fx";
    require(eaw::LegacyMaterial::translate(submesh).source == eaw::LegacyTranslationSource::name_heuristic, "unreferenced shader was not tagged as heuristic");
    submesh.shader = "ModRSkinAlphaGlow.fx";
    const auto modded = eaw::LegacyMaterial::translate(submesh);
    require(modded.source == eaw::LegacyTranslationSource::name_heuristic && modded.blend == eaw::LegacyBlendMode::alpha_blend && modded.vertex.rigid_skinning && !modded.render.depth_write,
            "mod shader heuristics were not applied");
    submesh.shader.clear();
    require(eaw::LegacyMaterial::translate(submesh).family == eaw::LegacyMaterialFamily::unknown, "empty shader name was not reported as unknown");

    const auto names = eaw::legacy_reference_shader_names();
    std::set<std::string_view> unique_names(names.begin(), names.end());
    require(names.size() == 39 && unique_names.size() == names.size(), "legacy reference profile names are missing or duplicated");
    for (const auto name : names)
    {
        submesh.shader = std::string(name) + ".fx";
        require(eaw::LegacyMaterial::translate(submesh).source == eaw::LegacyTranslationSource::reference_profile, "reference profile name did not resolve: " + std::string(name));
    }
}

void test_lua_inspection()
{
    const std::string source = "-- require('IGNORED')\n"
                               "require('PGStateMachine')\n"
                               "Script.Load(\"Story/Helper\")\n"
                               "function On_Enter_State()\n"
                               "  Find_First_Object('Executor')\n"
                               "  Controller:Advance()\n"
                               "end\n";
    const auto source_info = eaw::LuaScriptInfo::inspect(std::as_bytes(std::span(source)));
    require(source_info.kind == eaw::LuaScriptKind::source && source_info.lines == 7 && source_info.dependencies.size() == 2 && source_info.dependencies[0] == "PGStateMachine" &&
                source_info.dependencies[1] == "Story/Helper" && source_info.defined_functions.size() == 1 && source_info.defined_functions[0] == "On_Enter_State" &&
                std::ranges::find(source_info.called_functions, "Find_First_Object") != source_info.called_functions.end() &&
                std::ranges::find(source_info.called_functions, "Controller:Advance") != source_info.called_functions.end(),
            "Lua source inspection failed");

    std::vector<std::byte> bytecode{std::byte{0x1b}, std::byte{'L'}, std::byte{'u'}, std::byte{'p'}, std::byte{0x51}, std::byte{1}, std::byte{4},
                                    std::byte{4},    std::byte{4},   std::byte{6},   std::byte{8},   std::byte{9},    std::byte{9}, std::byte{8}};
    bytecode.resize(bytecode.size() + 8U, std::byte{});
    const auto append_lua_string = [&](const std::string_view value)
    {
        append_u32(bytecode, static_cast<std::uint32_t>(value.size() + 1U));
        for (const auto character : value)
        {
            bytecode.push_back(static_cast<std::byte>(character));
        }
        bytecode.push_back(std::byte{});
    };
    append_lua_string("@TEST.LUA");
    append_u32(bytecode, 1);
    append_u32(bytecode, 42);
    bytecode.insert(bytecode.end(), 4U, std::byte{});
    append_u32(bytecode, 0);
    append_u32(bytecode, 0);
    append_u32(bytecode, 0);
    append_u32(bytecode, 2);
    bytecode.push_back(std::byte{3});
    bytecode.resize(bytecode.size() + 8U, std::byte{});
    bytecode.push_back(std::byte{4});
    append_lua_string("constant");
    append_u32(bytecode, 0);
    append_u32(bytecode, 1);
    append_u32(bytecode, 0);

    const auto bytecode_info = eaw::LuaScriptInfo::inspect(bytecode);
    require(bytecode_info.kind == eaw::LuaScriptKind::petroglyph_bytecode && bytecode_info.bytecode_header.version == 0x51 && bytecode_info.prototypes == 1 && bytecode_info.instructions == 1 &&
                bytecode_info.number_constants == 1 && bytecode_info.string_constants == 1 && bytecode_info.persistent_function_ids == std::vector<std::uint64_t>{42},
            "Petroglyph Lua bytecode inspection failed");
}

std::vector<std::byte> make_test_ala()
{
    std::vector<std::byte> info_minis;
    std::vector<std::byte> frames;
    append_u32(frames, 31);
    append_mini(info_minis, 1, frames);
    std::vector<std::byte> fps;
    append_float(fps, 30.0F);
    append_mini(info_minis, 2, fps);
    std::vector<std::byte> one;
    append_u32(one, 1);
    append_mini(info_minis, 3, one);
    std::vector<std::byte> rotation_words;
    append_u32(rotation_words, 4);
    append_mini(info_minis, 11, rotation_words);
    std::vector<std::byte> translation_words;
    append_u32(translation_words, 3);
    append_mini(info_minis, 12, translation_words);
    std::vector<std::byte> scale_words;
    append_u32(scale_words, 0);
    append_mini(info_minis, 13, scale_words);

    std::vector<std::byte> track_minis;
    std::vector<std::byte> track_name;
    append_string(track_name, "ROOT");
    append_mini(track_minis, 4, track_name);
    std::vector<std::byte> bone_index;
    append_u32(bone_index, 0);
    append_mini(track_minis, 5, bone_index);
    std::vector<std::byte> active;
    append_u16(active, 0);
    std::vector<std::byte> inactive;
    append_u16(inactive, 0xffffU);
    append_mini(track_minis, 14, active);
    append_mini(track_minis, 15, inactive);
    append_mini(track_minis, 16, active);

    std::vector<std::byte> track;
    append_chunk(track, 0x1003, track_minis);
    append_chunk(track, 0x1007, {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0x7f}});

    std::vector<std::byte> animation;
    append_chunk(animation, 0x1001, info_minis);
    append_chunk(animation, 0x1002, track, true);
    append_chunk(animation, 0x100a, std::vector<std::byte>(31U * 3U * 2U, std::byte{}));
    append_chunk(animation, 0x1009, std::vector<std::byte>(31U * 4U * 2U, std::byte{}));
    std::vector<std::byte> ala;
    append_chunk(ala, 0x1000, animation, true);
    return ala;
}

void test_ala_animation()
{
    const auto animation = eaw::AlaAnimation::parse(make_test_ala());
    require(animation.version == 2 && animation.stored_frame_count == 31 && animation.playable_frame_count() == 30 && animation.frames_per_second == 30.0F && animation.duration_seconds() == 1.0F,
            "ALA animation header decoding failed");
    require(animation.tracks.size() == 1 && animation.tracks[0].name == "ROOT" && animation.tracks[0].translation && animation.tracks[0].rotation && !animation.tracks[0].scale &&
                animation.tracks[0].visibility && animation.tracks[0].frames.size() == 31,
            "ALA bone track decoding failed");
    const auto pose = animation.sample_pose(eaw::AloModel::parse(make_test_alo()), 0.5F);
    require(pose.size() == 1 && pose[0].visible, "ALA sampled pose decoding failed");
}

std::vector<std::byte> make_test_ted()
{
    std::vector<std::byte> properties;
    std::vector<std::byte> value;
    append_u32(value, 1);
    append_mini(properties, 1, value);
    value.clear();
    append_u32(value, 2);
    append_mini(properties, 2, value);
    value.clear();
    append_wide_string(value, u"TEXT_TEST_MAP");
    append_mini(properties, 8, value);
    value.clear();
    append_wide_string(value, u"TEST_PLANET");
    append_mini(properties, 9, value);

    std::vector<std::byte> environment_data;
    value.clear();
    append_string(value, "Noon_Clear");
    append_mini(environment_data, 0x14, value);
    value.clear();
    append_string(value, "DAY_BLUE_SKY");
    append_mini(environment_data, 0x19, value);
    // Complete lighting block: key/fill/fill colors, specular, ambient, intensities, azimuths,
    // elevations (mini types 0x00-0x0d), plus an uninterpreted fog-distance field.
    const auto append_floats = [&](const std::uint8_t type, const std::vector<float>& floats)
    {
        std::vector<std::byte> payload;
        for (const auto component : floats)
        {
            append_float(payload, component);
        }
        append_mini(environment_data, type, payload);
    };
    append_floats(0x00, {1.0F, 0.9F, 0.8F});
    append_floats(0x01, {0.1F, 0.2F, 0.3F});
    append_floats(0x02, {0.2F, 0.2F, 0.4F});
    append_floats(0x03, {0.7F, 0.7F, 0.7F});
    append_floats(0x04, {0.05F, 0.05F, 0.05F});
    append_floats(0x05, {0.5F});
    append_floats(0x06, {0.4F});
    append_floats(0x07, {0.25F});
    append_floats(0x08, {1.5F});
    append_floats(0x09, {3.0F});
    append_floats(0x0a, {4.5F});
    append_floats(0x0b, {0.4F});
    append_floats(0x0c, {-0.3F});
    append_floats(0x0d, {-0.2F});
    append_floats(0x12, {10000.0F});
    std::vector<std::byte> environment_list;
    append_chunk(environment_list, 6, environment_data);
    std::vector<std::byte> night_environment;
    value.clear();
    append_string(value, "Night");
    append_mini(night_environment, 0x14, value);
    append_chunk(environment_list, 6, night_environment);
    std::vector<std::byte> environments;
    append_chunk(environments, 4, environment_list, true);
    // The active-environment index is stored as a mini-chunk, as in stock maps.
    value.clear();
    append_u32(value, 1);
    std::vector<std::byte> active_environment;
    append_mini(active_environment, 0x25, value);
    append_chunk(environments, 8, active_environment);

    std::vector<std::byte> terrain_info;
    value.clear();
    append_u32(value, 3);
    append_mini(terrain_info, 0, value);
    value.clear();
    append_u32(value, 2);
    append_mini(terrain_info, 1, value);
    value.clear();
    append_u32(value, 1);
    append_mini(terrain_info, 5, value);
    // Water: level 0x15 (world units), type 0x16 (1 = water), color 0x1a, textures 0x1d/0x1e.
    value.clear();
    append_float(value, 1.5F);
    append_mini(terrain_info, 0x15, value);
    value.clear();
    append_u32(value, 1);
    append_mini(terrain_info, 0x16, value);
    value.clear();
    append_float(value, 0.4F);
    append_float(value, 0.6F);
    append_float(value, 0.8F);
    append_mini(terrain_info, 0x1a, value);
    value.clear();
    append_string(value, "WATER_BUMP.TGA");
    append_mini(terrain_info, 0x1d, value);
    value.clear();
    append_string(value, "WATER.TGA");
    append_mini(terrain_info, 0x1e, value);
    // Stock samples pack the signed elevation in bits 0-15, the layer index in bits 16-23, and a
    // small signed value (usually -1) in bits 24-31.
    std::vector<std::byte> elevation;
    const std::array<int, 6> heights{-10, 0, 10, 20, 30, 40};
    const std::array<std::uint32_t, 6> sample_layers{0, 0, 1, 1, 0, 1};
    for (std::size_t index = 0; index < heights.size(); ++index)
    {
        append_u32(elevation, (static_cast<std::uint32_t>(heights[index]) & 0xffffU) | (sample_layers[index] << 16U) | (0xffU << 24U));
    }
    std::vector<std::byte> layer_data;
    value.clear();
    append_string(value, "GROUND.TGA");
    append_mini(layer_data, 12, value);
    value.clear();
    append_string(value, "GROUND_B.TGA");
    append_mini(layer_data, 19, value);
    value.clear();
    append_float(value, 64.0F);
    append_mini(layer_data, 6, value);
    std::vector<std::byte> layers;
    append_chunk(layers, 3, layer_data);
    std::vector<std::byte> second_layer;
    value.clear();
    append_string(value, "ROCK.TGA");
    append_mini(second_layer, 12, value);
    append_chunk(layers, 3, second_layer);
    // A third, untextured layer (stock maps use these for terrain that is not drawn).
    std::vector<std::byte> empty_layer;
    value.clear();
    append_string(value, "");
    append_mini(empty_layer, 12, value);
    append_chunk(layers, 3, empty_layer);
    // One track: header (scroll 0.1/0.2), two points, texture, two widths, opacity keys 0/1/0.
    std::vector<std::byte> track_header;
    value.clear();
    append_float(value, 0.1F);
    append_mini(track_header, 0x04, value);
    value.clear();
    append_float(value, 0.2F);
    append_mini(track_header, 0x05, value);
    std::vector<std::byte> track_points;
    for (const auto& point : {std::array<float, 3>{5.0F, 5.0F, 0.0F}, std::array<float, 3>{35.0F, 15.0F, 0.0F}})
    {
        value.clear();
        for (const auto component : point)
        {
            append_float(value, component);
        }
        append_mini(track_points, 0x07, value);
    }
    std::vector<std::byte> track_widths;
    for (const auto width : {10.0F, 6.0F})
    {
        value.clear();
        append_float(value, width);
        append_mini(track_widths, 0x08, value);
    }
    std::vector<std::byte> track_opacity;
    for (const auto key : {0.0F, 1.0F, 0.0F})
    {
        value.clear();
        append_float(value, key);
        append_mini(track_opacity, 0x09, value);
    }
    value.clear();
    append_string(value, "RIVER.TGA");
    std::vector<std::byte> ted_track;
    append_chunk(ted_track, 0x101, track_header);
    append_chunk(ted_track, 0x103, value);
    append_chunk(ted_track, 0x102, track_points);
    append_chunk(ted_track, 0x104, track_widths);
    append_chunk(ted_track, 0x105, track_opacity);
    std::vector<std::byte> ted_tracks;
    append_chunk(ted_tracks, 0x100, ted_track, true);

    std::vector<std::byte> terrain;
    append_chunk(terrain, 0, terrain_info);
    append_chunk(terrain, 5, elevation);
    append_chunk(terrain, 2, layers, true);
    append_chunk(terrain, 9, ted_tracks, true);

    std::vector<std::byte> identity_data;
    value.clear();
    append_u32(value, 42U);
    append_mini(identity_data, 0, value);
    value.clear();
    append_u32(value, 0x12345678U);
    append_mini(identity_data, 1, value);
    value.clear();
    append_u32(value, 3U);
    append_mini(identity_data, 2, value);
    value.clear();
    append_float(value, 1.5F);
    append_mini(identity_data, 3, value);
    value.clear();
    append_float(value, 100.0F);
    append_float(value, 25.0F);
    append_float(value, -50.0F);
    append_mini(identity_data, 4, value);
    value.clear();
    append_float(value, 0.0F);
    append_float(value, 90.0F);
    append_float(value, 0.0F);
    append_mini(identity_data, 5, value);
    std::vector<std::byte> identity;
    append_chunk(identity, 0x4b0, identity_data);
    std::vector<std::byte> identity_group;
    append_chunk(identity_group, 0x459, identity, true);
    std::vector<std::byte> object_body;
    append_chunk(object_body, 0x454, identity_group, true);
    std::vector<std::byte> object_list;
    append_chunk(object_list, 0x44c, object_body, true);
    std::vector<std::byte> objects;
    append_chunk(objects, 1, object_list, true);

    std::vector<std::byte> zones;
    append_chunk(zones, 0, {}, true);
    std::vector<std::byte> passability;
    for (int index = 0; index < 6; ++index)
    {
        append_u32(passability, static_cast<std::uint32_t>(index));
    }

    std::vector<std::byte> root;
    append_chunk(root, 0x100, environments, true);
    append_chunk(root, 0x101, terrain, true);
    append_chunk(root, 0x10a, passability);
    append_chunk(root, 0x102, objects, true);
    append_chunk(root, 0x10b, zones, true);
    std::vector<std::byte> ted;
    append_chunk(ted, 0, properties);
    append_chunk(ted, 1, root, true);
    return ted;
}

void test_ted_map()
{
    const auto map = eaw::TedMap::parse(make_test_ted());
    require(map.type == eaw::TedMapType::land && map.players == 2 && map.name == "TEXT_TEST_MAP" && map.planet == "TEST_PLANET", "TED property decoding failed");
    require(map.terrain_width == 3 && map.terrain_height == 2 && map.elevation_sample_count == 6 && map.minimum_elevation == -10 && map.maximum_elevation == 40 && map.passability_sample_count == 6,
            "TED terrain decoding failed");
    require(map.elevation_map == std::vector<std::int16_t>({-10, 0, 10, 20, 30, 40}) && map.passability_map == std::vector<std::uint32_t>({0, 2, 4, 1, 3, 5}),
            "TED terrain sample retention or passability alignment failed");
    require(map.water.type == eaw::TedWaterType::water && map.water.raw_type == 1 && map.water.level == 1.5F && map.water.color == std::array<float, 3>{0.4F, 0.6F, 0.8F} &&
                map.water_maps[0] == "WATER_BUMP.TGA" && map.water_maps[1] == "WATER.TGA" && map.terrain_info_fields.size() == 8,
            "TED water settings were not decoded");
    require(map.tracks.size() == 1 && map.tracks[0].texture == "RIVER.TGA" && map.tracks[0].points.size() == 2 && map.tracks[0].points[1] == std::array<float, 3>{35.0F, 15.0F, 0.0F} &&
                map.tracks[0].widths == std::vector<float>{10.0F, 6.0F} && map.tracks[0].scroll == std::array<float, 2>{0.1F, 0.2F} &&
                map.tracks[0].opacity == std::array<float, 3>{0.0F, 1.0F, 0.0F} && map.tracks[0].header_fields.size() == 2 && map.track_textures.size() == 1,
            "TED track decoding failed");
    require(map.terrain_layer_map == std::vector<std::uint8_t>({0, 0, 1, 1, 0, 1}) && map.terrain_samples.size() == 6 && (map.terrain_samples[0] >> 24U) == 0xffU,
            "TED terrain layer indices or raw samples were not retained");
    require(map.terrain_layers.size() == 3 && map.terrain_layers[2].color_texture.empty() && map.terrain_layers[0].tile_size == 64.0F && map.terrain_layers[0].fields.size() == 3 &&
                map.terrain_layers[1].tile_size == 0.0F,
            "TED terrain layer tiling or raw fields were not decoded");
    require(map.terrain_layers[0].color_texture == "GROUND.TGA" && map.environments.size() == 2 && map.environments[0].name == "Noon_Clear" && map.environments[0].skybox_primary == "DAY_BLUE_SKY" &&
                map.active_environment == 1 && map.environments[0].has_lighting && map.environments[0].lights[0].color == std::array<float, 3>{1.0F, 0.9F, 0.8F} &&
                map.environments[0].lights[2].intensity == 0.25F && map.environments[0].lights[1].azimuth == 3.0F && map.environments[0].lights[0].elevation == 0.4F &&
                map.environments[0].ambient[0] == 0.05F && map.environments[0].fields.size() == 17 && map.environments[0].fields.back().type == 0x12 &&
                map.environments[0].fields.back().payload.size() == 4 && !map.environments[1].has_lighting && map.warnings.size() == 1,
            "TED dependency decoding failed");
    require(map.object_count == 1 && map.object_types.size() == 1 && map.object_types[0].crc == 0x12345678U && map.zone_count == 1, "TED object or zone decoding failed");
    require(map.placed_objects.size() == 1 && map.placed_objects[0].instance_id == 42U && map.placed_objects[0].crc == 0x12345678U && map.placed_objects[0].player == 3U &&
                map.placed_objects[0].scale == 1.5F && map.placed_objects[0].position.x == 100.0F && map.placed_objects[0].position.y == 25.0F && map.placed_objects[0].position.z == -50.0F &&
                map.placed_objects[0].rotation.y == 90.0F && map.placed_objects[0].has_transform,
            "TED placed-object transform decoding failed");
}

std::vector<std::byte> make_test_dat()
{
    const std::string first_key = "TEXT_UNIT_TEST";
    const std::string second_key = "TEXT_TOOLTIP_TEST";
    const std::u16string first_value = u"Test Unit";
    const std::u16string second_value = u"Line one\nLine two";
    std::vector<std::byte> bytes;
    append_u32(bytes, 2);
    append_u32(bytes, eaw::dat_key_crc(first_key));
    append_u32(bytes, static_cast<std::uint32_t>(first_value.size()));
    append_u32(bytes, static_cast<std::uint32_t>(first_key.size()));
    append_u32(bytes, eaw::dat_key_crc(second_key));
    append_u32(bytes, static_cast<std::uint32_t>(second_value.size()));
    append_u32(bytes, static_cast<std::uint32_t>(second_key.size()));
    for (const auto& value : {first_value, second_value})
    {
        for (const char16_t character : value)
        {
            bytes.push_back(static_cast<std::byte>(character & 0xffU));
            bytes.push_back(static_cast<std::byte>((character >> 8U) & 0xffU));
        }
    }
    for (const auto& key : {first_key, second_key})
    {
        for (const unsigned char character : key)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
    }
    return bytes;
}

void test_dat_and_workspace()
{
    const auto original = make_test_dat();
    auto dat = eaw::DatFile::parse(original);
    require(dat.entries().size() == 2, "DAT entry count failed");
    require(dat.find("TEXT_UNIT_TEST") != nullptr, "DAT key lookup failed");
    require(dat.serialize() == original, "DAT byte-identical round trip failed");
    require(dat.validate().empty(), "valid DAT reported issues");
    require(eaw::dat_key_crc("TEXT_SPEECH_EMPIRE_ACT01_MISSION_THREE_62") == 0x0001db80U, "Petroglyph DAT CRC32 failed");

    dat.upsert("TEXT_UNIT_NEW", eaw::utf8_to_utf16("Žluťoučký 🚀"));
    require(eaw::utf16_to_utf8(dat.find("TEXT_UNIT_NEW")->value) == "Žluťoučký 🚀", "UTF-8/UTF-16 conversion failed");

    const auto workspace = std::filesystem::temp_directory_path() / "eawengine-localization-test.tsv";
    std::error_code ignored;
    std::filesystem::remove(workspace, ignored);
    const std::vector<eaw::LocalizationRow> rows{{"TEXT_UNIT_NEW", "Name\twith tab", "unit:new", "ready", "Data/XML/Test.xml:2", "A \"quoted\" note\nwith a new line"}};
    eaw::write_localization_workspace(workspace, rows);
    const auto loaded = eaw::read_localization_workspace(workspace);
    require(loaded.size() == 1 && loaded[0].text == rows[0].text && loaded[0].notes == rows[0].notes, "localization TSV round trip failed");

    auto resolved_todo = eaw::DatFile::parse(original);
    eaw::apply_localization_workspace(resolved_todo, {{"TEXT_UNIT_TEST", "Completed TODO value", "unit:test", "todo", {}, {}}});
    require(eaw::utf16_to_utf8(resolved_todo.find("TEXT_UNIT_TEST")->value) == "Completed TODO value", "completed TODO localization value was skipped");

    auto unresolved_todo = eaw::DatFile::parse(original);
    bool unresolved_rejected = false;
    try
    {
        eaw::apply_localization_workspace(unresolved_todo, {{"TEXT_UNIT_EMPTY", {}, "unit:empty", "todo", {}, {}}});
    }
    catch (const std::runtime_error&)
    {
        unresolved_rejected = true;
    }
    require(unresolved_rejected, "empty TODO localization row was not rejected");
    eaw::apply_localization_workspace(unresolved_todo, {{"TEXT_UNIT_EMPTY", {}, "unit:empty", "todo", {}, {}}}, true);
    require(unresolved_todo.find("TEXT_UNIT_EMPTY") == nullptr, "empty TODO localization row was not skipped");
    std::filesystem::remove(workspace, ignored);
}

void test_localization_scan()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-localization-scan-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream xml(xml_root / "Units.xml", std::ios::binary);
        xml << "<Units>\n"
               "  <Text_ID>TEXT_UNIT_TEST</Text_ID>\n"
               "  <Text_ID>TEXT_UNIT_MISSING</Text_ID>\n"
               "  <!-- TEXT_COMMENTED_OUT -->\n"
               "</Units>\n";
    }

    const auto baseline = eaw::DatFile::parse(make_test_dat());
    const auto result = eaw::scan_mod_localization(root, baseline);
    require(result.reference_count == 2, "localization scan reference count failed");
    require(result.missing.size() == 1 && result.missing.front().key == "TEXT_UNIT_MISSING" && result.missing.front().context.find("Units.xml:3") != std::string::npos,
            "localization scan missing-key context failed");
    std::filesystem::remove_all(root, ignored);
}

void test_xml_document()
{
    const auto document = eaw::XmlDocument::parse("\xef\xbb\xbf<?xml version=\"1.0\"?>\n"
                                                  "<!-- a Petroglyph file may contain several roots -->\n"
                                                  "<SpaceUnit Name='Unit_A' Enabled=\"Yes\">\n"
                                                  "  <Text_ID>TEXT_A &amp; B</Text_ID>\n"
                                                  "  <Payload><![CDATA[x < y]]></Payload>\n"
                                                  "</SpaceUnit>\n"
                                                  "<SpaceUnit Name=\"Unit_B\"><Value>&#x41;&#66;</Value></SpaceUnit>");
    require(document.roots.size() == 2, "XML fragment root parsing failed");
    const auto& first = document.roots.front();
    require(first.name == "SpaceUnit" && first.attribute("name") != nullptr && first.attribute("NAME")->value == "Unit_A", "XML case-insensitive attribute lookup failed");
    require(first.child("text_id") != nullptr && first.child("Text_ID")->text == "TEXT_A & B", "XML entity decoding failed");
    require(first.child("Payload") != nullptr && first.child("Payload")->text == "x < y", "XML CDATA parsing failed");
    require(document.roots[1].child("Value")->text == "AB", "XML numeric character decoding failed");
    require(eaw::XmlDocument::parse("<?xml version=\"1.0\"><A />").roots.size() == 1, "Petroglyph XML declaration compatibility failed");

    bool mismatch_rejected = false;
    try
    {
        static_cast<void>(eaw::XmlDocument::parse("<A><B /></C>"));
    }
    catch (const std::runtime_error&)
    {
        mismatch_rejected = true;
    }
    require(mismatch_rejected, "mismatched XML closing element was accepted");
}

void test_xml_asset_database()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-xml-database-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream file(xml_root / "GameObjectFiles.xml", std::ios::binary);
        file << "<GameObjectFiles><File>Units.xml</File>"
                "<File>Missing.xml</File></GameObjectFiles>";
    }
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<SpaceUnit Name=\"Unit_A\"><Text_ID>TEXT_A</Text_ID>"
                "<Build_Cost_Credits>100</Build_Cost_Credits></SpaceUnit>"
                "<SpaceUnit Name=\"Unit_B\" />";
    }
    {
        std::ofstream file(xml_root / "Overrides.xml", std::ios::binary);
        file << "<SpaceUnit Name=\"Unit_A\"><Build_Cost_Credits>200"
                "</Build_Cost_Credits></SpaceUnit>";
    }

    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    require(database.document_count() == 3 && database.parsed_document_count() == 3 && database.failures().empty(), "XML database document accounting failed");
    require(database.definitions().size() == 3, "XML database named definition discovery failed");
    const auto matches = database.find("spaceunit", "unit_a");
    const auto ordered_fields = std::ranges::find_if(matches,
                                                     [](const auto* match)
                                                     {
                                                         return match->fields.size() == 2 && match->fields[0].name == "Text_ID" && match->fields[1].name == "Build_Cost_Credits";
                                                     });
    require(matches.size() == 2 && ordered_fields != matches.end(), "XML database typed lookup or field order failed");
    require(database.duplicates().size() == 1, "XML duplicate definition diagnostics failed");
    require(database.includes().size() == 2 && database.includes()[0].resolved && !database.includes()[1].resolved, "XML include resolution diagnostics failed");
    std::filesystem::remove_all(root, ignored);
}

void test_game_object_catalog()
{
    // CRC-32 of the uppercased name; reference values from an independent CRC-32 implementation.
    require(eaw::game_object_name_crc("Unit_A") == 0xbb137f89U && eaw::game_object_name_crc("UNIT_A") == 0xbb137f89U, "object-name CRC is not case-insensitive CRC-32");

    const auto root = std::filesystem::temp_directory_path() / "eawengine-object-catalog-test";
    const auto xml_root = root / "Data" / "XML";
    const auto model_root = root / "Data" / "Art" / "Models";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    std::filesystem::create_directories(model_root);
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<SpaceUnits>"
                "<SpaceUnit Name=\"Unit_A\"><Space_Model_Name> EV_Unit.alo </Space_Model_Name><Land_Model_Name>EI_Unit</Land_Model_Name></SpaceUnit>"
                "<SpaceUnit Name=\"Unit_A_Variant\"><Variant_Of_Existing_Type>unit_a</Variant_Of_Existing_Type>"
                "<Land_Model_Name>EI_Variant.alo</Land_Model_Name></SpaceUnit>"
                "<SpaceUnit Name=\"Orphan\"><Variant_Of_Existing_Type>Not_Defined</Variant_Of_Existing_Type></SpaceUnit>"
                "<SpaceUnit Name=\"Loop_A\"><Variant_Of_Existing_Type>Loop_B</Variant_Of_Existing_Type></SpaceUnit>"
                "<SpaceUnit Name=\"Loop_B\"><Variant_Of_Existing_Type>Loop_A</Variant_Of_Existing_Type></SpaceUnit>"
                "<SFXEvent Name=\"Shared_Name\" />"
                "<SpaceProp Name=\"Shared_Name\"><Space_Model_Name>Missing_Model</Space_Model_Name></SpaceProp>"
                "<Squadron Name=\"Test_Squadron\"><Squadron_Units>Unit_A</Squadron_Units><Squadron_Units>Unit_A, Unit_A_Variant, Missing_Member</Squadron_Units>"
                "<Squadron_Offsets>10.0, 0.0, 0.0</Squadron_Offsets><Squadron_Offsets>bad</Squadron_Offsets></Squadron>"
                "<Container Name=\"Test_Team\" />"
                "<Faction Name=\"Test_Empire\"><Color> 54, 134, 242, 255 </Color></Faction>"
                "<Faction Name=\"Broken_Faction\"><Color>red</Color></Faction>"
                "<SpaceUnit Name=\"Imperial_Unit\"><Affiliation>Test_Empire, Rebel</Affiliation></SpaceUnit>"
                "<SpaceUnit Name=\"Broken_Unit\"><Affiliation>Broken_Faction</Affiliation></SpaceUnit>"
                "</SpaceUnits>";
    }
    std::ofstream(model_root / "EV_UNIT.ALO", std::ios::binary) << "x";
    std::ofstream(model_root / "EI_Unit.alo", std::ios::binary) << "x";

    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);

    const auto unit = catalog.resolve(0xbb137f89U, eaw::GameModelContext::space);
    require(unit.definition != nullptr && unit.definition->name == "Unit_A" && unit.candidates == 1, "object CRC did not resolve to its definition");
    const auto space_model = catalog.model(*unit.definition, eaw::GameModelContext::space);
    require(space_model && space_model->value == "EV_Unit.alo" && space_model->field == "Space_Model_Name", "space model reference was not read and trimmed");

    // A variant inherits fields it does not override and keeps the ones it does.
    const auto variant = catalog.find("UNIT_A_VARIANT");
    require(variant.size() == 1, "case-insensitive name lookup failed");
    const auto inherited = catalog.model(*variant.front(), eaw::GameModelContext::space);
    const auto overridden = catalog.model(*variant.front(), eaw::GameModelContext::land);
    require(inherited && inherited->value == "EV_Unit.alo" && inherited->declared_by == unit.definition && overridden && overridden->value == "EI_Variant.alo", "variant field inheritance failed");
    require(!catalog.model(*variant.front(), eaw::GameModelContext::galactic), "absent model field was reported");

    std::vector<std::string> diagnostics;
    require(!catalog.model(*catalog.find("Orphan").front(), eaw::GameModelContext::space, &diagnostics) && diagnostics.size() == 1 && diagnostics.front().find("Not_Defined") != std::string::npos,
            "missing variant parent was not diagnosed");
    diagnostics.clear();
    require(!catalog.model(*catalog.find("Loop_A").front(), eaw::GameModelContext::space, &diagnostics) && diagnostics.size() == 1 && diagnostics.front().find("cycle") != std::string::npos,
            "variant cycle was not diagnosed");

    // When names collide across element types, the definition with a model wins the CRC.
    const auto shared = catalog.resolve(eaw::game_object_name_crc("Shared_Name"), eaw::GameModelContext::space);
    require(shared.definition != nullptr && shared.definition->type == "SpaceProp" && shared.candidates == 2, "ambiguous CRC did not prefer the modeled definition");
    require(catalog.resolve(0x12345678U, eaw::GameModelContext::space).definition == nullptr, "unknown CRC resolved");

    require(eaw::resolve_model_path(vfs, "ev_unit.alo").has_value() && eaw::resolve_model_path(vfs, "Art\\Models\\EI_Unit").has_value(), "model reference did not resolve through the VFS");
    require(!eaw::resolve_model_path(vfs, "Missing_Model") && !eaw::resolve_model_path(vfs, "  ") && !eaw::resolve_model_path(vfs, "..\\"), "missing model reference resolved");

    // Placed-object transforms: degrees, uniform scale, then translation (row-vector convention).
    eaw::TedPlacedObject placed;
    placed.crc = 0xbb137f89U;
    placed.scale = 2.0F;
    placed.rotation = {0.0F, 0.0F, 90.0F};
    placed.position = {10.0F, 20.0F, 30.0F};
    placed.has_transform = true;
    const auto moved = eaw::alo_transform_point({1.0F, 0.0F, 0.0F}, eaw::placed_object_transform(placed));
    require(std::fabs(moved.x - 10.0F) < 1.0e-4F && std::fabs(moved.y - 22.0F) < 1.0e-4F && std::fabs(moved.z - 30.0F) < 1.0e-4F, "placed-object heading/scale/translation is wrong");
    auto degenerate = placed;
    degenerate.scale = 0.0F;
    degenerate.rotation = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const auto safe = eaw::alo_transform_point({1.0F, 0.0F, 0.0F}, eaw::placed_object_transform(degenerate));
    require(std::isfinite(safe.x) && std::fabs(safe.x - 11.0F) < 1.0e-4F, "degenerate placed-object transform was not sanitized");

    eaw::TedMap map;
    map.type = eaw::TedMapType::space;
    map.placed_objects.push_back(placed);
    auto unknown = placed;
    unknown.crc = 0x12345678U;
    map.placed_objects.push_back(unknown);
    auto unpositioned = placed;
    unpositioned.has_transform = false;
    map.placed_objects.push_back(unpositioned);
    auto prop = placed;
    prop.crc = eaw::game_object_name_crc("Shared_Name");
    map.placed_objects.push_back(prop);
    const auto layout = eaw::build_map_object_layout(map, catalog, vfs);
    require(layout.placements.size() == 3 && layout.untyped == 1 && layout.missing_model_files == 1 && layout.placements[0].model_path == "DATA/ART/MODELS/EV_UNIT.ALO" &&
                layout.placements[0].type == unit.definition && layout.placements[2].object_index == 3 && layout.placements[2].model_path.empty(),
            "map object layout resolution failed");

    // Faction colors come from the first affiliated faction's 0..255 Color.
    const auto imperial_color = catalog.affiliation_color(*catalog.find("Imperial_Unit").front());
    require(imperial_color && std::fabs((*imperial_color)[0] - 54.0F / 255.0F) < 1.0e-6F && std::fabs((*imperial_color)[2] - 242.0F / 255.0F) < 1.0e-6F && (*imperial_color)[3] == 1.0F,
            "affiliation faction color was not resolved");
    require(!catalog.affiliation_color(*catalog.find("Broken_Unit").front()) && !catalog.affiliation_color(*unit.definition), "missing or malformed faction colors were accepted");

    // Repeated fields keep source order; a single-valued lookup takes the last occurrence.
    const auto squadron_type = catalog.find("Test_Squadron");
    require(squadron_type.size() == 1 && catalog.fields(*squadron_type.front(), "Squadron_Offsets").fields.size() == 2 &&
                catalog.field(*squadron_type.front(), "Squadron_Units").field->text.find("Missing_Member") != std::string::npos,
            "repeated XML field lookup failed");

    // A placed space squadron expands into its members at their rotated Squadron_Offsets; a land
    // team container is counted instead of guessed.
    eaw::TedMap squadron_map;
    squadron_map.type = eaw::TedMapType::space;
    auto squadron_object = placed;
    squadron_object.scale = 1.0F;
    squadron_object.crc = eaw::game_object_name_crc("Test_Squadron");
    squadron_map.placed_objects.push_back(squadron_object);
    auto team = placed;
    team.crc = eaw::game_object_name_crc("Test_Team");
    squadron_map.placed_objects.push_back(team);
    const auto squadron_layout = eaw::build_map_object_layout(squadron_map, catalog, vfs);
    require(squadron_layout.expanded_members == 2 && squadron_layout.untyped == 1 && squadron_layout.unexpanded_containers == 1 && squadron_layout.placements.size() == 3,
            "squadron expansion counts are wrong");
    const auto& lead = squadron_layout.placements[0];
    const auto lead_origin = eaw::alo_transform_point({}, lead.transform);
    require(lead.container == squadron_type.front() && lead.member == 0 && lead.type == unit.definition && std::fabs(lead_origin.x - 10.0F) < 1.0e-4F && std::fabs(lead_origin.y - 30.0F) < 1.0e-4F &&
                std::fabs(lead_origin.z - 30.0F) < 1.0e-4F,
            "squadron member offset was not rotated into the squadron frame");
    const auto wing_origin = eaw::alo_transform_point({}, squadron_layout.placements[1].transform);
    require(squadron_layout.placements[1].member == 1 && std::fabs(wing_origin.x - 10.0F) < 1.0e-4F && std::fabs(wing_origin.y - 20.0F) < 1.0e-4F,
            "malformed squadron offset did not fall back to the squadron origin");
    const auto mentions = [&](const std::string_view text)
    {
        return std::ranges::any_of(squadron_layout.diagnostics,
                                   [&](const std::string& message)
                                   {
                                       return message.find(text) != std::string::npos;
                                   });
    };
    require(mentions("malformed Squadron_Offsets 'bad'") && mentions("'Missing_Member' is not defined"), "squadron expansion diagnostics are missing");
    std::filesystem::remove_all(root, ignored);
}

void test_camera_controllers()
{
    const auto near_equal = [](const float left, const float right, const float tolerance = 1.0e-3F)
    {
        return std::fabs(left - right) <= tolerance;
    };
    const auto distance = [](const eaw::VisualCamera& camera)
    {
        const auto x = camera.eye[0] - camera.target[0];
        const auto y = camera.eye[1] - camera.target[1];
        const auto z = camera.eye[2] - camera.target[2];
        return std::sqrt(x * x + y * y + z * z);
    };

    // Orbit: frames the bounds center at 2.6 bounding radii and clamps pitch.
    eaw::OrbitCamera orbit({{-1.0F, -2.0F, -2.0F}, {3.0F, 2.0F, 2.0F}});
    auto view = orbit.camera();
    require(near_equal(view.target[0], 1.0F) && near_equal(view.target[1], 0.0F) && near_equal(distance(view), 2.6F * std::sqrt(12.0F)), "orbit camera does not frame the bounds");
    orbit.apply({.pitch = 10.0F});
    view = orbit.camera();
    require(view.eye[2] - view.target[2] < distance(view) && view.eye[2] > view.target[2], "orbit pitch was not clamped below vertical");
    orbit.apply({.reset = true});
    require(near_equal(distance(orbit.camera()), 2.6F * std::sqrt(12.0F)), "orbit reset failed");

    // Tactical: centered on the map, looking north (+Y), zoom limited to the map size.
    eaw::TacticalCamera tactical({{-1000.0F, -500.0F, 0.0F}, {1000.0F, 500.0F, 0.0F}});
    view = tactical.camera();
    require(near_equal(view.target[0], 0.0F) && near_equal(view.target[1], 0.0F) && view.eye[1] < view.target[1] && view.eye[2] > 0.0F, "tactical camera did not start south of the map center");
    require(near_equal(tactical.distance(), 1500.0F) && view.far_plane > view.near_plane && view.near_plane > 0.0F, "tactical camera default distance or planes are wrong");
    tactical.apply({.zoom_steps = 1000.0F});
    require(near_equal(tactical.distance(), tactical.minimum_distance()), "tactical zoom-in was not clamped");
    tactical.apply({.zoom_steps = -1000.0F});
    require(near_equal(tactical.distance(), tactical.maximum_distance()), "tactical zoom-out was not clamped");
    tactical.apply({.reset = true});

    // Panning is relative to the heading: facing north, "right" moves the focus along the
    // renderer's screen-right axis and "forward" moves it north.
    tactical.apply({.pan_forward = 0.1F});
    require(near_equal(tactical.focus()[1], 150.0F) && near_equal(tactical.focus()[0], 0.0F), "tactical forward pan did not follow the heading");
    tactical.apply({.reset = true});
    tactical.apply({.pan_right = 0.1F});
    const auto right_focus = tactical.focus();
    view = tactical.camera();
    // Screen right for a left-handed look-at is cross(up, forward).
    const std::array<float, 2> forward{view.target[0] - view.eye[0], view.target[1] - view.eye[1]};
    require(near_equal(right_focus[0] * forward[0] + right_focus[1] * forward[1], 0.0F, 1.0F) && near_equal(std::fabs(right_focus[0]), 150.0F) && right_focus[0] * -forward[1] > 0.0F,
            "tactical right pan is not perpendicular to the heading");

    // The focus stays inside the map, and malformed input or bounds cannot poison the view.
    tactical.apply({.pan_right = 1.0e6F, .pan_forward = 1.0e6F});
    require(tactical.focus()[0] >= -1000.0F && tactical.focus()[0] <= 1000.0F && tactical.focus()[1] >= -500.0F && tactical.focus()[1] <= 500.0F, "tactical focus left the map bounds");
    tactical.apply({.yaw = std::numeric_limits<float>::quiet_NaN(), .pitch = std::numeric_limits<float>::infinity(), .zoom_steps = std::numeric_limits<float>::quiet_NaN()});
    view = tactical.camera();
    require(std::isfinite(view.eye[0]) && std::isfinite(view.eye[1]) && std::isfinite(view.eye[2]), "non-finite camera input produced a non-finite view");
    eaw::TacticalCamera degenerate({{std::numeric_limits<float>::quiet_NaN(), 5.0F, 0.0F}, {1.0F, -5.0F, 0.0F}});
    view = degenerate.camera();
    require(std::isfinite(view.eye[0]) && distance(view) >= degenerate.minimum_distance() * 0.999F, "degenerate bounds produced an invalid tactical view");
}

void test_visual_material_translation()
{
    // The synthetic ALO uses MeshGloss.fx with an exact Diffuse vector4 and a malformed Specular.
    const auto root = std::filesystem::temp_directory_path() / "eawengine-visual-material-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "Art" / "Models");
    {
        const auto bytes = make_test_alo();
        std::ofstream file(root / "Data" / "Art" / "Models" / "TEST.ALO", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto asset = eaw::build_visual_model(vfs, "Data/Art/Models/TEST.ALO");
    const auto& scene = asset.scene;
    require(scene.meshes.size() == 1 && scene.instances.size() == 1 && scene.materials.size() == 1 && scene.vertices.size() == 3, "visual model scene structure is wrong");
    const auto& material = scene.materials[0];
    require(material.lighting == eaw::VisualLightingModel::lit && material.gloss == eaw::VisualGlossSource::base_alpha && !material.team_color_mask && material.blend == eaw::VisualBlendMode::opaque,
            "gloss profile did not become a lit, base-alpha-gloss material");
    require(material.diffuse == std::array<float, 4>{0.25F, 0.5F, 0.75F, 0.123456789F} && material.specular == std::array<float, 3>{1.0F, 1.0F, 1.0F} &&
                material.emissive == std::array<float, 3>{0.0F, 0.0F, 0.0F},
            "material colors were not taken from exact parameters with legacy defaults");
    require(material.base_color_texture == eaw::invalid_visual_texture && material.tint != std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F}, "untextured material did not receive a debug tint");
    require(scene.instances[0].team_color[3] == 1.0F && !scene.instances[0].background, "model instance team color or layer is wrong");

    // A mesh attached to a bone hidden in the bind pose (an alternate state such as debris) is kept
    // in a model scene but bound to the visibility half of the palette, which collapses it until an
    // animation shows the bone.
    {
        const auto bytes = make_test_alo(false);
        std::ofstream file(root / "Data" / "Art" / "Models" / "HIDDEN.ALO", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto hidden = eaw::build_visual_model(vfs, "Data/Art/Models/HIDDEN.ALO");
    require(hidden.visibility_palette && hidden.scene.bone_count == 2 && hidden.scene.vertices.size() == 3 && hidden.scene.vertices[0].bone_indices[0] == 1,
            "a rigid attachment must reference the visibility half of the palette");
    const auto hidden_palette = eaw::sample_skin_palette(hidden, nullptr, 0.0F);
    std::array<float, 16> collapsed{};
    collapsed[15] = 1.0F;
    require(hidden_palette.size() == 2 && hidden_palette[1] == collapsed && hidden_palette[0][0] == 1.0F, "a bone hidden in the bind pose must collapse its attachments");
    const auto shown_palette = eaw::sample_skin_palette(asset, nullptr, 0.0F);
    require(shown_palette.size() == 2 && shown_palette[1] == shown_palette[0], "a visible bone's visibility entry must match its skin matrix");
    std::filesystem::remove_all(root, ignored);
}

void test_visual_terrain()
{
    // The synthetic land TED has a 3x2 grid, two layers, and per-vertex layers {0,0,1,1,0,1}.
    const auto root = std::filesystem::temp_directory_path() / "eawengine-visual-terrain-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "Art" / "Maps");
    std::filesystem::create_directories(root / "Data" / "Art" / "Textures");
    std::filesystem::create_directories(root / "Data" / "XML");
    {
        const auto bytes = make_test_dds();
        std::ofstream file(root / "Data" / "Art" / "Textures" / "RIVER.DDS", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    {
        const auto bytes = make_test_ted();
        std::ofstream file(root / "Data" / "Art" / "Maps" / "TEST.TED", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto asset = eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED");

    // Spawnable types: a type with a land model joins the mesh library with its speed; an unknown
    // type is reported, not fatal.
    {
        std::filesystem::create_directories(root / "Data" / "Art" / "Models");
        const auto model = make_test_alo();
        std::ofstream(root / "Data" / "Art" / "Models" / "TANK.ALO", std::ios::binary).write(reinterpret_cast<const char*>(model.data()), static_cast<std::streamsize>(model.size()));
        std::ofstream(root / "Data" / "XML" / "UNITS.XML", std::ios::binary)
            << "<Units><GroundVehicle Name=\"Test_Tank\"><Land_Model_Name>TANK.ALO</Land_Model_Name><Max_Speed>2</Max_Speed></GroundVehicle></Units>";
        const std::array<std::string, 2> types{"Test_Tank", "Missing_Type"};
        const auto with_spawns = eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED", nullptr, types);
        require(with_spawns.spawnable.size() == 1 && with_spawns.spawnable[0].type_name == "Test_Tank" &&
                    std::fabs(with_spawns.spawnable[0].max_speed - 2.0F * eaw::provisional_max_speed_scale) < 1.0e-4F && with_spawns.spawnable[0].mesh < with_spawns.scene.meshes.size(),
                "a spawn type must resolve to a mesh and its scaled speed");
        require(std::ranges::any_of(with_spawns.scene.warnings,
                                    [](const std::string& warning)
                                    {
                                        return warning.find("Missing_Type") != std::string::npos;
                                    }),
                "an unknown spawn type must be reported");
        std::filesystem::remove(root / "Data" / "XML" / "UNITS.XML", ignored);
    }

    // Texture identities: stable across builds served from a cache (so the renderer keeps them on
    // the GPU), fresh for every decode.
    {
        eaw::AssetCache cache;
        const auto first = eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED", &cache);
        const auto second = eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED", &cache);
        require(!first.scene.textures.empty() && first.scene.textures.size() == second.scene.textures.size(), "cached map builds must produce the same textures");
        for (std::size_t index = 0; index < first.scene.textures.size(); ++index)
        {
            require(!first.scene.textures[index].identity.empty() && first.scene.textures[index].identity == second.scene.textures[index].identity,
                    "a texture served from the cache must keep its identity");
        }
        const auto uncached = eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED");
        require(uncached.scene.textures.front().identity != first.scene.textures.front().identity, "a fresh decode must get a new identity");
    }
    const auto& terrain = asset.scene.terrain;
    require(asset.land && terrain.layer_count == 3 && terrain.vertices.size() == 12 && terrain.first_material + 3 <= asset.scene.materials.size(), "terrain mesh structure is wrong");
    require(asset.scene.materials[terrain.first_material].tint[3] == 1.0F && asset.scene.materials[terrain.first_material + 2].tint[3] == 0.0F, "an untextured terrain layer must carry zero coverage");
    require(std::fabs(asset.scene.materials[terrain.first_material].uv_scale - 1.0F / 64.0F) < 1.0e-7F, "terrain layer tiling was not applied");
    // First triangle: (0,0), (1,0), (1,1) with layers {0, 0, 0} -> sample (1,1) is index 4, layer 0.
    const auto& first = terrain.vertices[0];
    const auto& third = terrain.vertices[2];
    require(first.position == std::array<float, 3>{0.0F, 0.0F, -0.5F} && third.position == std::array<float, 3>{20.0F, 20.0F, 1.5F} && first.layers == std::array<std::uint32_t, 3>{0U, 0U, 0U} &&
                first.barycentric == std::array<float, 3>{1.0F, 0.0F, 0.0F} && third.barycentric == std::array<float, 3>{0.0F, 0.0F, 1.0F},
            "terrain vertex positions, layers, or barycentrics are wrong");
    // Second cell's first triangle uses samples (1,0), (2,0), (2,1): layers {0, 1, 1}.
    require(terrain.vertices[6].layers == std::array<std::uint32_t, 3>{0U, 1U, 1U}, "terrain corner layers were not taken from the samples");
    require(terrain.bounds_minimum[2] == -0.5F && terrain.bounds_maximum[0] == 40.0F && terrain.bounds_maximum[2] == 2.0F && asset.scene.bounds_maximum[1] == 20.0F,
            "terrain bounds were not applied to the scene");
    require(!asset.lighting, "an environment without lighting fields produced a light rig");
    // Water at level 1.5 crosses the terrain (-0.5..2.0) and becomes a blended quad instance.
    const auto& water_material = asset.scene.materials[asset.scene.draws[asset.scene.meshes[0].first_draw].material];
    require(asset.water == eaw::TedWaterType::water && asset.scene.meshes.size() == 2 && asset.scene.instances.size() == 2 && asset.scene.vertices.size() == 10 &&
                asset.scene.vertices[2].position == std::array<float, 3>{40.0F, 20.0F, 1.5F} && water_material.blend == eaw::VisualBlendMode::alpha_blend &&
                water_material.diffuse == std::array<float, 4>{0.4F, 0.6F, 0.8F, 0.75F},
            "water surface was not built over the terrain");
    // The track (5,5)->(35,15), widths 10->6, opacity 0/1/0 becomes a 3-sample draped ribbon.
    const auto& ribbon = asset.scene.vertices;
    require(asset.tracks == 1 && asset.scene.indices.size() == 6 + 12 && ribbon[4].color[3] == 0.0F && ribbon[6].color[3] == 1.0F && ribbon[9].color[3] == 0.0F &&
                ribbon[4].texture_coordinates[0] == 0.0F && ribbon[5].texture_coordinates[0] == 1.0F,
            "track ribbon structure or opacity fade is wrong");
    // First left edge vertex lies at (6.581, 0.257): the lower-right triangle of cell (0,0) gives
    // height -0.3163, plus the 0.6 lift.
    require(std::fabs(ribbon[4].position[0] - 6.5811F) < 1.0e-3F && std::fabs(ribbon[4].position[2] - 0.2837F) < 2.0e-3F, "track ribbon does not follow the terrain surface");
    std::filesystem::remove_all(root, ignored);
}

void test_xml_schema_validation()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-xml-schema-test";
    const auto game_root = root / "game";
    const auto xml_root = game_root / "Data" / "XML";
    const auto schema_root = root / "schema" / "eaw";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    std::filesystem::create_directories(schema_root / "tags");
    std::filesystem::create_directories(schema_root / "enums");
    std::filesystem::create_directories(schema_root / "meta");

    {
        std::ofstream file(schema_root / "types.yaml", std::ios::binary);
        file << "types:\n"
                "  - typeName: GameObjectType\n"
                "    nameTag: Name\n"
                "  - typeName: StoryParser\n"
                "    nameTag: Name\n";
    }
    {
        std::ofstream file(schema_root / "tags" / "GameObjectType.yaml", std::ios::binary);
        file << "tags:\n"
                "  - tag: Is_Buildable\n"
                "    type: Boolean\n"
                "  - tag: Build_Cost_Credits\n"
                "    type: UInt\n"
                "  - tag: Faction\n"
                "    type: DynamicEnumValue\n"
                "    enumName: FactionName\n"
                "  - tag: Target\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: GameObjectType\n"
                "  - tag: Buildable_Target\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: Buildable\n"
                "  - tag: Inherited_Buildable_Target\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: Buildable\n"
                "  - tag: Variant_Of_Existing_Type\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: GameObjectType\n"
                "  - tag: Repeated\n"
                "    type: Float\n";
    }
    {
        std::ofstream file(schema_root / "tags" / "StoryParser.yaml", std::ios::binary);
        file << "tags:\n"
                "  - tag: Event_Type\n"
                "    type: NameReference\n"
                "  - tag: Reward_Type\n"
                "    type: NameReference\n";
    }
    {
        std::ofstream file(schema_root / "enums" / "FactionName.yaml", std::ios::binary);
        file << "name: FactionName\n"
                "values:\n"
                "  - name: Empire\n"
                "  - name: Rebel\n";
    }
    {
        std::ofstream file(schema_root / "kinds.yaml", std::ios::binary);
        file << "kinds:\n"
                "  - kind: Buildable\n"
                "    flags: [Is_Buildable]\n";
    }
    {
        std::ofstream file(schema_root / "meta" / "metafiles.yaml", std::ios::binary);
        file << "metafiles:\n"
                "  - path: data/xml/gameobjectfiles.xml\n"
                "    metaFileType: fileRegistry\n"
                "    types:\n"
                "      - GameObjectType\n";
    }
    {
        std::ofstream file(xml_root / "GameObjectFiles.xml", std::ios::binary);
        file << "<Game_Object_Files><File>Units.xml</File></Game_Object_Files>";
    }
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<Units><SpaceUnit Name=\"Unit_A\">"
                "<Is_Buildable>Maybe</Is_Buildable>"
                "<Build_Cost_Credits>-1</Build_Cost_Credits>"
                "<Faction>Hutts</Faction>"
                "<Target>Missing_Unit</Target>"
                "<Buildable_Target>Unit_B</Buildable_Target>"
                "<Inherited_Buildable_Target>Unit_Variant</Inherited_Buildable_Target>"
                "<Repeated>1</Repeated><Repeated>2</Repeated>"
                "<Unknown_Field>value</Unknown_Field>"
                "</SpaceUnit><SpaceUnit Name=\"Unit_B\" />"
                "<SpaceUnit Name=\"Unit_Base\"><Is_Buildable>Yes</Is_Buildable></SpaceUnit>"
                "<SpaceUnit Name=\"Unit_Variant\"><Variant_Of_Existing_Type>Unit_Base</Variant_Of_Existing_Type></SpaceUnit>"
                "</Units>";
    }
    {
        std::ofstream file(xml_root / "Story.xml", std::ios::binary);
        file << "<Story><Event Name=\"Story_Event\"><Event_Type>Trigger</Event_Type>"
                "<Reward_Type>Reward</Reward_Type></Event></Story>";
    }

    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", game_root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto schema = eaw::XmlSchema::load(root / "schema");
    const auto validation = schema.validate(database);
    require(schema.types().size() == 2 && schema.enums().size() == 1 && schema.kinds().size() == 1 && schema.metafiles().size() == 1, "XML schema loading failed");
    require(validation.definitions_considered == 5 && validation.definitions_validated == 5 && validation.metafile_type_definitions == 4 && validation.structural_type_definitions == 1 &&
                validation.untyped_definitions == 0,
            "XML schema type inference failed");
    require(validation.fields_validated == 12 && validation.unknown_fields == 1 && validation.duplicate_fields == 1, "XML schema field validation failed");
    require(validation.invalid_values == 3 && validation.unresolved_references == 1 && validation.mismatched_references == 1, "XML schema value or reference validation failed");
    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main()
{
    try
    {
        require(eaw::normalize_asset_path("data\\xml/./GameObjectFiles.xml") == "DATA/XML/GAMEOBJECTFILES.XML", "asset normalization failed");

        bool traversal_rejected = false;
        try
        {
            static_cast<void>(eaw::normalize_asset_path("Data/../secret"));
        }
        catch (const std::invalid_argument&)
        {
            traversal_rejected = true;
        }
        require(traversal_rejected, "parent traversal was not rejected");

        const auto archive_path = write_test_meg();
        const eaw::MegArchive archive(archive_path);
        const auto* entry = archive.find("data/xml/test.xml");
        require(entry != nullptr, "MEG lookup failed");
        const auto bytes = archive.read(*entry);
        const std::string payload(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        require(payload == "<Test />", "MEG payload read failed");

        eaw::VirtualFileSystem vfs;
        vfs.mount(std::make_unique<eaw::MegMount>("test", archive_path));
        const auto resolved = vfs.resolve("DATA/XML/TEST.XML");
        require(resolved && resolved->size == 8, "VFS MEG resolution failed");

        test_patch_order();
        test_foc_inherits_eaw_assets();
        test_layered_precedence_corpus();
        test_alo_model();
        test_alo_default_mesh_visibility();
        test_gpu_resource_lifetime();
        test_asset_cache_dependencies();
        test_debug_draw();
        test_simulation_core();
        test_fatal_error_report();
        test_status_and_log();
        test_scene_presentation();
        test_rts_control();
        test_terrain_height_field();
        test_texture_formats_and_materials();
        test_lua_inspection();
        test_ala_animation();
        test_ted_map();
        test_dat_and_workspace();
        test_localization_scan();
        test_xml_document();
        test_xml_asset_database();
        test_game_object_catalog();
        test_camera_controllers();
        test_visual_material_translation();
        test_visual_terrain();
        test_xml_schema_validation();

        std::error_code ignored;
        std::filesystem::remove(archive_path, ignored);
        std::cout << "All EAWEngine tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
