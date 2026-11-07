#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/asset_path.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/localization_scan.hpp"
#include "eaw/localization_workspace.hpp"
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
#include <ranges>
#include <stdexcept>
#include <string>
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

std::vector<std::byte> make_test_alo()
{
    std::vector<std::byte> bone_name;
    append_string(bone_name, "ROOT");
    std::vector<std::byte> bone_data;
    append_u32(bone_data, 0xffffffffU);
    append_u32(bone_data, 1);
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
    const auto bind_pose = model.bind_pose();
    require(bind_pose.size() == 1 && bind_pose[0].values[0] == 1.0F && bind_pose[0].values[5] == 1.0F && bind_pose[0].values[10] == 1.0F, "ALO bind-pose decoding failed");
}

void test_texture_formats_and_materials()
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
    std::vector<std::byte> environment_list;
    append_chunk(environment_list, 6, environment_data);
    std::vector<std::byte> environments;
    append_chunk(environments, 4, environment_list, true);
    value.clear();
    append_u32(value, 0);
    append_chunk(environments, 8, value);

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
    std::vector<std::byte> elevation;
    for (const auto height : {-10, 0, 10, 20, 30, 40})
    {
        append_u32(elevation, static_cast<std::uint32_t>(height));
    }
    std::vector<std::byte> layer_data;
    value.clear();
    append_string(value, "GROUND.TGA");
    append_mini(layer_data, 12, value);
    value.clear();
    append_string(value, "GROUND_B.TGA");
    append_mini(layer_data, 19, value);
    std::vector<std::byte> layers;
    append_chunk(layers, 3, layer_data);
    std::vector<std::byte> terrain;
    append_chunk(terrain, 0, terrain_info);
    append_chunk(terrain, 5, elevation);
    append_chunk(terrain, 2, layers, true);

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
    require(map.terrain_layers.size() == 1 && map.terrain_layers[0].color_texture == "GROUND.TGA" && map.environments.size() == 1 && map.environments[0].name == "Noon_Clear",
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
        test_texture_formats_and_materials();
        test_lua_inspection();
        test_ala_animation();
        test_ted_map();
        test_dat_and_workspace();
        test_localization_scan();
        test_xml_document();
        test_xml_asset_database();
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
