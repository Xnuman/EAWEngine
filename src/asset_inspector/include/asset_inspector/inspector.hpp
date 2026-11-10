#pragma once

// Shared declarations of EAWAssetInspector's commands (one source file per command group).

#include "assets/game_object_catalog.hpp"
#include "assets/legacy_material.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/xml_asset_database.hpp"
#include "assets/xml_schema.hpp"
#include "core/status.hpp"
#include "core/string_utils.hpp"
#include "formats/ala_animation.hpp"
#include "formats/alo_model.hpp"
#include "formats/alo_particle_system.hpp"
#include "formats/dat_string_table.hpp"
#include "formats/dds_texture.hpp"
#include "formats/lua_bytecode.hpp"
#include "formats/mtd_atlas.hpp"
#include "formats/ted_map.hpp"
#include "formats/xml_document.hpp"
#include "vfs/asset_environment.hpp"
#include "vfs/game_installation.hpp"
#include "vfs/virtual_file_system.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace eaw::inspector {

struct Options
{
    std::string command{"summary"};
    std::string operand;
    std::optional<std::filesystem::path> game_root;
    std::optional<std::filesystem::path> schema_root;
    eaw::GameVariant variant{eaw::GameVariant::forces_of_corruption};
    std::vector<std::filesystem::path> mods;
    std::string prefix{"Data"};
    std::string extension;
    std::size_t limit{200};
    // extract: the folder files are written to, and whether existing files there are replaced.
    std::optional<std::filesystem::path> output;
    bool overwrite{};
};

void usage();

eaw::Result<Options> parse_options(int argc, char** argv);

std::string extension_of(std::string_view path);

std::uint32_t little_u32(std::span<const std::byte> bytes, std::size_t offset);

// Reports a failure: "asset inspector error: ..." and exit code 1.
int report_error(const eaw::Error& error);

// Reads and decodes one asset through the VFS.
template <class Asset> eaw::Result<Asset> decode_asset(const eaw::AssetReader& vfs, const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto bytes, vfs.read(path));
    if constexpr (std::is_same_v<Asset, eaw::LuaChunkData>)
    {
        return eaw::read_lua(bytes);
    }
    else if constexpr (std::is_same_v<Asset, eaw::DdsTextureData>)
    {
        return eaw::read_dds(bytes);
    }
    else if constexpr (std::is_same_v<Asset, eaw::MtdAtlasData>)
    {
        return eaw::read_mtd(bytes);
    }
    else if constexpr (std::is_same_v<Asset, eaw::TedMapData>)
    {
        return eaw::read_ted(bytes);
    }
    else
    {
        return eaw::read_alo<Asset>(bytes);
    }
}

std::vector<std::string> localization_references(const std::string& xml);

eaw::Status inspect_asset(const eaw::VirtualFileSystem& vfs, const std::string& path, std::size_t limit);

void audit_texture_references(const eaw::VirtualFileSystem& vfs, std::string_view prefix, std::size_t limit);

void audit_lua(const eaw::VirtualFileSystem& vfs, std::string_view prefix, std::size_t limit);

// Decodes every particle ALO and reports formats, failures, properties, blend modes, renderers, and links.
void audit_particles(const eaw::VirtualFileSystem& vfs, std::string_view prefix, std::size_t limit);

struct AuditExample
{
    std::size_t count{};
    std::string map;
    std::string detail;
};

template <typename Key> std::vector<std::pair<Key, AuditExample>> by_count(const std::map<Key, AuditExample>& entries)
{
    std::vector<std::pair<Key, AuditExample>> result(entries.begin(), entries.end());
    std::ranges::stable_sort(result,
                             [](const auto& left, const auto& right)
                             {
                                 return left.second.count > right.second.count;
                             });
    return result;
}

void record_example(std::map<std::string, AuditExample>& entries, const std::string& key, const std::string& map, const std::string& detail = {});

// Resolves every placed TED object through its type CRC to an XML definition and model, then
// reports coverage. This is the evidence for the object-name CRC rule and the model fields.
void audit_map_objects(const eaw::VirtualFileSystem& vfs, std::string_view prefix, std::size_t limit);

void audit_maps(const eaw::VirtualFileSystem& vfs, std::string_view prefix, std::size_t limit);

// Writes every resolved file under options.prefix (filtered by options.extension) to
// options.output at its virtual path. Refuses an output folder inside any of `read_only_roots`
// (the installation and the mods) and skips entry names that could escape the folder.
eaw::Status extract_assets(const eaw::VirtualFileSystem& vfs, const Options& options, const std::vector<std::filesystem::path>& read_only_roots);

eaw::Status audit_xml(const eaw::VirtualFileSystem& vfs, std::string_view prefix, std::size_t limit, const std::optional<std::filesystem::path>& schema_root, bool include_foc);

} // namespace eaw::inspector
