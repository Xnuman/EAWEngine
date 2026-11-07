#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/lua_script.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/mtd_directory.hpp"
#include "eaw/status.hpp"
#include "eaw/text_encoding.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"
#include "eaw/xml_document.hpp"
#include "eaw/xml_schema.hpp"

#include <algorithm>
#include <cctype>
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

namespace {

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
};

void usage()
{
    std::cout << "EAWAssetInspector - layered Empire at War asset browser\n\n"
              << "Usage:\n"
              << "  EAWAssetInspector summary [options]\n"
              << "  EAWAssetInspector list [prefix] [options]\n"
              << "  EAWAssetInspector inspect <virtual-path> [options]\n"
              << "  EAWAssetInspector map-audit [prefix] [options]\n"
              << "  EAWAssetInspector map-objects [prefix] [options]\n"
              << "  EAWAssetInspector texture-audit [prefix] [options]\n"
              << "  EAWAssetInspector lua-audit [prefix] [options]\n"
              << "  EAWAssetInspector xml-audit [prefix] [options]\n"
              << "  EAWAssetInspector localization-refs <xml-path> [options]\n\n"
              << "Options:\n"
              << "  --game-root <path>      Installation root (auto-detected by default)\n"
              << "  --game <eaw|foc>        Game profile (default: foc)\n"
              << "  --schema-root <path>    Optional eaw-schema checkout for XML validation\n"
              << "  --mod <path>            Mod root; repeatable, first has priority\n"
              << "  --prefix <path>         Summary/list prefix (default: Data)\n"
              << "  --extension <.xml>      Filter list by extension\n"
              << "  --limit <count>         Maximum list/sample rows\n";
}

eaw::Result<std::size_t> parse_size(const std::string& value)
{
    std::size_t parsed = 0;
    const auto* const end = value.data() + value.size();
    const auto [position, error] = std::from_chars(value.data(), end, parsed);
    if (value.empty() || error != std::errc{} || position != end)
    {
        return eaw::make_error("invalid number: " + value);
    }
    return parsed;
}

eaw::Result<Options> parse_options(const int argc, char** argv)
{
    Options options;
    int index = 1;
    if (index < argc && argv[index][0] != '-')
    {
        options.command = argv[index++];
    }
    if ((options.command == "inspect" || options.command == "localization-refs") && index < argc && argv[index][0] != '-')
    {
        options.operand = argv[index++];
    }
    else if ((options.command == "list" || options.command == "map-audit" || options.command == "map-objects" || options.command == "texture-audit" || options.command == "lua-audit" ||
              options.command == "xml-audit") &&
             index < argc && argv[index][0] != '-')
    {
        options.prefix = argv[index++];
    }

    for (; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            usage();
            std::exit(0);
        }
        if (index + 1 >= argc)
        {
            return eaw::make_error("missing value after " + argument);
        }
        const std::string value = argv[++index];
        if (argument == "--game-root")
        {
            options.game_root = value;
        }
        else if (argument == "--game")
        {
            const auto variant = eaw::parse_game_variant(value);
            if (!variant)
            {
                return eaw::make_error("unknown game variant: " + value);
            }
            options.variant = *variant;
        }
        else if (argument == "--schema-root")
        {
            options.schema_root = value;
        }
        else if (argument == "--mod")
        {
            options.mods.emplace_back(value);
        }
        else if (argument == "--prefix")
        {
            options.prefix = value;
        }
        else if (argument == "--extension")
        {
            options.extension = value;
            if (!options.extension.empty() && options.extension.front() != '.')
            {
                options.extension.insert(options.extension.begin(), '.');
            }
            std::ranges::transform(options.extension, options.extension.begin(),
                                   [](const unsigned char character)
                                   {
                                       return static_cast<char>(std::tolower(character));
                                   });
        }
        else if (argument == "--limit")
        {
            EAW_TRY_ASSIGN(options.limit, parse_size(value));
        }
        else
        {
            return eaw::make_error("unknown option: " + argument);
        }
    }
    if ((options.command == "inspect" || options.command == "localization-refs") && options.operand.empty())
    {
        return eaw::make_error(options.command + " requires a virtual asset path");
    }
    return options;
}

std::string extension_of(const std::string_view path)
{
    auto extension = std::filesystem::path(path).extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return extension.empty() ? "<none>" : extension;
}

std::string lower_ascii(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

std::uint32_t little_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    EAW_ASSERT_MSG(offset + 4 <= bytes.size(), "asset is too short for the requested header");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

// Reports a failure: "asset inspector error: ..." and exit code 1.
int report_error(const eaw::Error& error)
{
    std::cerr << "asset inspector error: " << error.message << '\n';
    return 1;
}

// Reads and decodes one asset through the VFS.
template <class Asset> eaw::Result<Asset> decode_asset(const eaw::AssetReader& vfs, const std::string_view path)
{
    EAW_TRY_ASSIGN(const auto bytes, vfs.read(path));
    if constexpr (std::is_same_v<Asset, eaw::LuaScriptInfo>)
    {
        return eaw::LuaScriptInfo::inspect(bytes);
    }
    else
    {
        return Asset::parse(bytes);
    }
}

eaw::Status inspect_dat(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto dat, eaw::DatFile::parse(bytes));
    const auto issues = dat.validate();
    std::cout << "Format: Petroglyph string DAT\n"
              << "Entries: " << dat.entries().size() << '\n'
              << "CRC sorted: " << (dat.crc_sorted() ? "yes" : "no") << '\n'
              << "Validation issues: " << issues.size() << '\n';
    std::size_t shown = 0;
    for (const auto& entry : dat.entries())
    {
        if (shown++ >= limit)
        {
            break;
        }
        EAW_TRY_ASSIGN(auto value, eaw::utf16_to_utf8(entry.value));
        std::ranges::replace(value, '\n', ' ');
        std::ranges::replace(value, '\r', ' ');
        if (value.size() > 100)
        {
            value.resize(97);
            value += "...";
        }
        std::cout << "  " << entry.key << " = " << value << '\n';
    }
    return eaw::Status::success();
}

eaw::Status inspect_dds(const std::span<const std::byte> bytes)
{
    EAW_TRY_ASSIGN(const auto texture, eaw::DdsTexture::parse(bytes));
    std::cout << "Format: DirectDraw Surface\n"
              << "Pixel format: " << eaw::to_string(texture.format) << '\n'
              << "Dimensions: " << texture.width << 'x' << texture.height;
    if (texture.volume)
    {
        std::cout << 'x' << texture.depth;
    }
    std::cout << "\nMip levels: " << texture.mip_levels << '\n'
              << "Array layers: " << texture.array_size << '\n'
              << "Faces: " << texture.faces << '\n'
              << "Cube map: " << (texture.cube_map ? "yes" : "no") << '\n'
              << "Alpha channel: " << (texture.has_alpha ? "yes" : "no") << '\n'
              << "Image bytes: " << texture.data_size << '\n';
    return eaw::Status::success();
}

eaw::Status inspect_mtd(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto directory, eaw::MtdDirectory::parse(bytes));
    std::cout << "Format: Petroglyph mega-texture directory\n"
              << "Entries: " << directory.entries.size() << '\n'
              << "Atlas extent: " << directory.atlas_width << 'x' << directory.atlas_height << '\n'
              << "Entries with alpha: " << directory.alpha_entries << "\nEntries:\n";
    for (std::size_t index = 0; index < std::min(limit, directory.entries.size()); ++index)
    {
        const auto& entry = directory.entries[index];
        std::cout << "  [" << index << "] " << entry.name << " crc=0x" << std::hex << std::setw(8) << std::setfill('0') << entry.name_crc << std::dec << std::setfill(' ') << " rect=" << entry.x << ','
                  << entry.y << ' ' << entry.width << 'x' << entry.height << (entry.has_alpha ? " alpha" : "") << '\n';
    }
    return eaw::Status::success();
}

void inspect_chunks(const std::span<const std::byte> bytes, const std::size_t limit)
{
    std::cout << "Format: Alamo chunk container (top-level inventory)\n";
    std::size_t cursor = 0;
    std::size_t count = 0;
    while (cursor + 8 <= bytes.size() && count < limit)
    {
        const auto type = little_u32(bytes, cursor);
        const auto encoded_size = little_u32(bytes, cursor + 4);
        const bool container = (encoded_size & 0x80000000U) != 0;
        const auto size = encoded_size & 0x7fffffffU;
        std::cout << "  [" << count << "] type=0x" << std::hex << std::setw(8) << std::setfill('0') << type << std::dec << std::setfill(' ') << " size=" << size
                  << " kind=" << (container ? "container" : "data") << '\n';
        if (static_cast<std::uint64_t>(cursor) + 8U + size > bytes.size())
        {
            std::cout << "  truncated chunk body\n";
            break;
        }
        cursor += 8U + size;
        ++count;
    }
    std::cout << "Top-level bytes consumed: " << cursor << '/' << bytes.size() << '\n';
}

eaw::Status inspect_alo(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto model, eaw::AloModel::parse(bytes));
    std::cout << "Format: Alamo semantic model\n"
              << "Bones: " << model.bones.size() << '\n'
              << "Meshes: " << model.meshes.size() << '\n'
              << "Submeshes: " << model.submesh_count() << '\n'
              << "Vertices: " << model.vertex_count() << '\n'
              << "Triangles: " << model.triangle_count() << '\n'
              << "Lights: " << model.lights.size() << '\n'
              << "Proxies: " << model.proxies.size() << '\n'
              << "Dazzles: " << model.dazzles.size() << '\n';
    for (const auto& warning : model.warnings)
    {
        std::cout << "Warning: " << warning << '\n';
    }

    std::cout << "Skeleton:\n";
    for (std::size_t index = 0; index < std::min(limit, model.bones.size()); ++index)
    {
        const auto& bone = model.bones[index];
        std::cout << "  [" << index << "] " << bone.name << " parent=" << bone.parent << ' ' << (bone.visible ? "visible" : "hidden") << '\n';
    }
    std::cout << "Meshes:\n";
    for (std::size_t index = 0; index < std::min(limit, model.meshes.size()); ++index)
    {
        const auto& mesh = model.meshes[index];
        std::cout << "  [" << index << "] " << mesh.name << " submeshes=" << mesh.submeshes.size();
        if (mesh.attached_bone)
        {
            std::cout << " bone=" << *mesh.attached_bone;
        }
        std::cout << '\n';
        for (const auto& submesh : mesh.submeshes)
        {
            std::cout << "      " << submesh.shader << " vertices=" << submesh.vertex_count << " triangles=" << submesh.triangle_count << " stride=" << submesh.vertex_stride << '\n';
            for (const auto& parameter : submesh.parameters)
            {
                if (parameter.type == eaw::AloShaderParameterType::texture)
                {
                    std::cout << "        " << parameter.name << '=' << parameter.value << '\n';
                }
            }
        }
    }
    return eaw::Status::success();
}

eaw::Status inspect_ala(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto animation, eaw::AlaAnimation::parse(bytes));
    std::cout << "Format: Alamo semantic animation\n"
              << "Version: " << animation.version << '\n'
              << "Stored frames: " << animation.stored_frame_count << '\n'
              << "Playable frames: " << animation.playable_frame_count() << '\n'
              << "Frames per second: " << animation.frames_per_second << '\n'
              << "Duration: " << animation.duration_seconds() << " seconds\n"
              << "Bone tracks: " << animation.tracks.size() << '\n'
              << "Rotation data: " << animation.rotation_data_bytes << " bytes\n"
              << "Translation data: " << animation.translation_data_bytes << " bytes\n"
              << "Scale data: " << animation.scale_data_bytes << " bytes\n";
    for (const auto& warning : animation.warnings)
    {
        std::cout << "Warning: " << warning << '\n';
    }
    std::cout << "Tracks:\n";
    for (std::size_t index = 0; index < std::min(limit, animation.tracks.size()); ++index)
    {
        const auto& track = animation.tracks[index];
        std::cout << "  [" << track.bone_index << "] " << track.name << ' ';
        if (track.translation)
        {
            std::cout << 'T';
        }
        if (track.rotation)
        {
            std::cout << 'R';
        }
        if (track.scale)
        {
            std::cout << 'S';
        }
        if (track.visibility)
        {
            std::cout << " visibility";
        }
        if (track.stepped)
        {
            std::cout << " stepped";
        }
        std::cout << '\n';
    }
    return eaw::Status::success();
}

eaw::Status inspect_ted(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto map, eaw::TedMap::parse(bytes));
    std::cout << "Format: Alamo semantic map\n"
              << "Type: " << eaw::to_string(map.type) << '\n'
              << "Name: " << map.name << '\n'
              << "Planet: " << map.planet << '\n'
              << "Players: " << map.players << '\n'
              << "World dimensions: " << map.world_width << " x " << map.world_height << '\n'
              << "Terrain grid: " << map.terrain_width << " x " << map.terrain_height << '\n'
              << "Elevation samples: " << map.elevation_sample_count << " (" << map.minimum_elevation << ".." << map.maximum_elevation << ")\n"
              << "Passability samples: " << map.passability_sample_count << '\n'
              << "Terrain layers: " << map.terrain_layers.size() << '\n'
              << "Decals: " << map.decal_count << '\n'
              << "Tracks: " << map.track_textures.size() << '\n'
              << "Environments: " << map.environments.size() << '\n'
              << "Objects: " << map.object_count << " (" << map.object_types.size() << " types, "
              << std::ranges::count_if(map.placed_objects,
                                       [](const auto& object)
                                       {
                                           return object.has_transform;
                                       })
              << " positioned)\n"
              << "Zones: " << map.zone_count << '\n'
              << "Embedded preview: " << (map.has_preview ? "yes" : "no");
    if (map.has_preview)
    {
        std::cout << " (" << map.preview_width << 'x' << map.preview_height << ", " << map.preview_bytes << " bytes)";
    }
    std::cout << '\n';
    for (const auto& warning : map.warnings)
    {
        std::cout << "Warning: " << warning << '\n';
    }
    std::cout << "Terrain textures:\n";
    for (std::size_t index = 0; index < std::min(limit, map.terrain_layers.size()); ++index)
    {
        const auto& layer = map.terrain_layers[index];
        std::cout << "  [" << index << "] " << layer.color_texture;
        if (!layer.normal_texture.empty())
        {
            std::cout << " | " << layer.normal_texture;
        }
        std::cout << '\n';
    }
    std::cout << "Environments:\n";
    for (std::size_t index = 0; index < std::min(limit, map.environments.size()); ++index)
    {
        const auto& environment = map.environments[index];
        std::cout << "  [" << index << "] " << environment.name;
        if (!environment.scenario.empty())
        {
            std::cout << " scenario=" << environment.scenario;
        }
        if (!environment.skybox_primary.empty())
        {
            std::cout << " skybox=" << environment.skybox_primary;
        }
        if (!environment.skybox_secondary.empty())
        {
            std::cout << " secondary=" << environment.skybox_secondary;
        }
        if (index == map.active_environment)
        {
            std::cout << " (active)";
        }
        std::cout << '\n';
        if (environment.has_lighting)
        {
            static constexpr std::array<const char*, 3> roles{"key", "fill", "fill"};
            for (std::size_t light = 0; light < environment.lights.size(); ++light)
            {
                const auto& source = environment.lights[light];
                std::cout << "      " << roles[light] << " color=" << source.color[0] << ',' << source.color[1] << ',' << source.color[2] << " intensity=" << source.intensity
                          << " azimuth=" << source.azimuth << " elevation=" << source.elevation << '\n';
            }
            std::cout << "      specular=" << environment.specular[0] << ',' << environment.specular[1] << ',' << environment.specular[2] << " ambient=" << environment.ambient[0] << ','
                      << environment.ambient[1] << ',' << environment.ambient[2] << " fields=" << environment.fields.size() << '\n';
        }
    }
    std::cout << "Object CRCs:\n";
    for (std::size_t index = 0; index < std::min(limit, map.object_types.size()); ++index)
    {
        const auto& object = map.object_types[index];
        std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << object.crc << std::dec << std::setfill(' ') << " x " << object.count << '\n';
    }
    return eaw::Status::success();
}

std::vector<std::string> localization_references(const std::string& xml)
{
    auto active_xml = xml;
    std::size_t comment = 0;
    while ((comment = active_xml.find("<!--", comment)) != std::string::npos)
    {
        const auto close = active_xml.find("-->", comment + 4);
        const auto end = close == std::string::npos ? active_xml.size() : close + 3;
        std::fill(active_xml.begin() + static_cast<std::ptrdiff_t>(comment), active_xml.begin() + static_cast<std::ptrdiff_t>(end), ' ');
        comment = end;
    }
    // Keys are "TEXT_" plus one or more of [A-Za-z0-9_.-], starting at a word boundary.
    const auto word_character = [](const char character)
    {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
    };
    std::set<std::string> unique;
    std::size_t position = 0;
    while ((position = active_xml.find("TEXT_", position)) != std::string::npos)
    {
        if (position > 0 && word_character(active_xml[position - 1]))
        {
            ++position;
            continue;
        }
        auto end = position + 5;
        while (end < active_xml.size() && (word_character(active_xml[end]) || active_xml[end] == '.' || active_xml[end] == '-'))
        {
            ++end;
        }
        if (end == position + 5)
        {
            ++position;
            continue;
        }
        unique.insert(active_xml.substr(position, end - position));
        position = end;
    }
    return {unique.begin(), unique.end()};
}

void collect_named_xml_nodes(const eaw::XmlNode& node, std::vector<const eaw::XmlNode*>& output)
{
    if (node.attribute("Name") != nullptr)
    {
        output.push_back(&node);
    }
    for (const auto& child : node.children)
    {
        collect_named_xml_nodes(child, output);
    }
}

eaw::Status inspect_asset(const eaw::VirtualFileSystem& vfs, const std::string& path, const std::size_t limit)
{
    const auto resolved = vfs.resolve(path);
    if (!resolved)
    {
        return eaw::make_error("asset was not found: " + path);
    }
    EAW_TRY_ASSIGN(const auto bytes, vfs.read(path));
    const auto extension = extension_of(resolved->virtual_path);
    std::cout << "Path: " << resolved->virtual_path << '\n'
              << "Source: " << resolved->mount_name << '\n'
              << "Container: " << resolved->container << '\n'
              << "Offset: " << resolved->offset << '\n'
              << "Size: " << resolved->size << " bytes\n";

    if (extension == ".dat")
    {
        EAW_TRY(inspect_dat(bytes, limit));
    }
    else if (extension == ".dds")
    {
        EAW_TRY(inspect_dds(bytes));
    }
    else if (extension == ".alo")
    {
        if (const auto status = inspect_alo(bytes, limit); !status)
        {
            std::cout << "Semantic model decoding unavailable: " << status.error().message << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
    }
    else if (extension == ".ala")
    {
        if (const auto status = inspect_ala(bytes, limit); !status)
        {
            std::cout << "Semantic animation decoding unavailable: " << status.error().message << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
    }
    else if (extension == ".ted")
    {
        if (const auto status = inspect_ted(bytes, limit); !status)
        {
            std::cout << "Semantic map decoding unavailable: " << status.error().message << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
    }
    else if (extension == ".mtd")
    {
        EAW_TRY(inspect_mtd(bytes, limit));
    }
    else if (extension == ".xml")
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto references = localization_references(text);
        EAW_TRY_ASSIGN(const auto document, eaw::XmlDocument::parse(std::span<const std::byte>(bytes)));
        std::vector<const eaw::XmlNode*> definitions;
        for (const auto& root : document.roots)
        {
            collect_named_xml_nodes(root, definitions);
        }
        std::cout << "Format: Petroglyph XML\n"
                  << "Top-level elements: " << document.roots.size() << '\n'
                  << "Named definitions: " << definitions.size() << '\n'
                  << "Localization references: " << references.size() << '\n';
        for (std::size_t index = 0; index < std::min(limit, definitions.size()); ++index)
        {
            const auto* name = definitions[index]->attribute("Name");
            std::cout << "  " << definitions[index]->name << " Name=\"" << name->value << "\" (line " << definitions[index]->line << ")\n";
        }
    }
    else if (extension == ".lua")
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto references = localization_references(text);
        EAW_TRY_ASSIGN(const auto script, eaw::LuaScriptInfo::inspect(bytes));
        std::cout << "Format: " << eaw::to_string(script.kind) << '\n';
        if (script.kind == eaw::LuaScriptKind::source)
        {
            std::cout << "Lines: " << script.lines << '\n'
                      << "Dependencies: " << script.dependencies.size() << '\n'
                      << "Defined functions: " << script.defined_functions.size() << '\n'
                      << "Called functions: " << script.called_functions.size() << '\n';
        }
        else if (script.kind == eaw::LuaScriptKind::lua50_bytecode || script.kind == eaw::LuaScriptKind::petroglyph_bytecode)
        {
            std::cout << "Version: 0x" << std::hex << static_cast<unsigned>(script.bytecode_header.version) << std::dec << '\n'
                      << "Byte order: " << (script.bytecode_header.little_endian ? "little-endian" : "big-endian") << '\n'
                      << "Integer/size_t/instruction/number: " << static_cast<unsigned>(script.bytecode_header.integer_size) << '/' << static_cast<unsigned>(script.bytecode_header.size_type_size)
                      << '/' << static_cast<unsigned>(script.bytecode_header.instruction_size) << '/' << static_cast<unsigned>(script.bytecode_header.number_size) << " bytes\n"
                      << "Prototypes: " << script.prototypes << '\n'
                      << "Instructions: " << script.instructions << '\n'
                      << "Number constants: " << script.number_constants << '\n'
                      << "String constants: " << script.string_constants << '\n';
        }
        std::cout << "Localization references: " << references.size() << '\n';
        for (const auto& warning : script.warnings)
        {
            std::cout << "Warning: " << warning << '\n';
        }
        std::cout << "Dependencies:\n";
        for (std::size_t index = 0; index < std::min(limit, script.dependencies.size()); ++index)
        {
            std::cout << "  " << script.dependencies[index] << '\n';
        }
        std::cout << "Defined functions:\n";
        for (std::size_t index = 0; index < std::min(limit, script.defined_functions.size()); ++index)
        {
            std::cout << "  " << script.defined_functions[index] << '\n';
        }
        std::cout << "Called functions:\n";
        for (std::size_t index = 0; index < std::min(limit, script.called_functions.size()); ++index)
        {
            std::cout << "  " << script.called_functions[index] << '\n';
        }
        std::cout << "Localization references:\n";
        for (std::size_t index = 0; index < std::min(limit, references.size()); ++index)
        {
            std::cout << "  " << references[index] << '\n';
        }
    }
    else
    {
        std::cout << "Format: unknown\nSignature:";
        for (std::size_t index = 0; index < std::min<std::size_t>(16, bytes.size()); ++index)
        {
            std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<unsigned>(bytes[index]);
        }
        std::cout << std::dec << std::setfill(' ') << '\n';
    }
    return eaw::Status::success();
}

std::optional<std::string> resolve_texture_reference(const eaw::VirtualFileSystem& vfs, const std::string_view reference)
{
    auto filename = std::filesystem::path(reference).filename();
    filename.replace_extension(".DDS");
    const std::array candidates{std::string("DATA/ART/TEXTURES/") + filename.generic_string(), std::string("DATA/ART/") + filename.generic_string(), filename.generic_string()};
    for (const auto& candidate : candidates)
    {
        if (vfs.resolve(candidate))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

void audit_texture_references(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    const auto model_assets = vfs.list(prefix);
    std::size_t model_files = 0;
    std::size_t semantic_models = 0;
    std::size_t particle_or_invalid = 0;
    std::size_t materials = 0;
    std::set<std::string> references;
    std::set<std::string> resolved;
    std::map<std::string, std::set<std::string>> unresolved;
    std::map<std::string, std::size_t> shaders;
    std::map<std::string, std::size_t> material_families;
    std::map<std::string, std::size_t> blend_modes;
    std::map<std::string, std::size_t> translation_sources;
    std::map<std::string, std::size_t> render_phases;
    std::map<std::string, std::size_t> lighting_models;
    std::map<std::string, std::size_t> heuristic_shaders;
    std::map<std::string, std::size_t> texture_semantics;
    std::map<std::string, std::size_t> texture_parameters;
    struct NumericUsage
    {
        std::size_t count{};
        std::array<float, 4> minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        std::array<float, 4> maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        std::size_t components{};
    };
    std::map<std::string, NumericUsage> numeric_parameters;
    // State-variant naming census: models and meshes using _ALT<n>/_LOD<n> suffixes.
    std::size_t models_with_alternates = 0;
    std::size_t models_with_lods = 0;
    std::map<std::uint32_t, std::size_t> alternate_mesh_indices;
    std::map<std::uint32_t, std::size_t> lod_mesh_indices;
    std::map<std::uint32_t, std::uint64_t> lod_triangles;
    // Per model: which LOD index carries the most and the fewest triangles.
    std::map<std::uint32_t, std::size_t> densest_lod;
    std::map<std::uint32_t, std::size_t> sparsest_lod;
    std::size_t alternate_bones_without_meshes = 0;
    for (const auto& asset : model_assets)
    {
        if (extension_of(asset.virtual_path) != ".alo")
        {
            continue;
        }
        ++model_files;
        const auto decoded = decode_asset<eaw::AloModel>(vfs, asset.virtual_path);
        if (!decoded)
        {
            ++particle_or_invalid;
            continue;
        }
        const auto& model = *decoded;
        ++semantic_models;
        bool has_alternate = false;
        bool has_lod = false;
        std::map<std::uint32_t, std::uint64_t> model_lod_triangles;
        for (const auto& mesh : model.meshes)
        {
            const auto variant = eaw::alo_name_variant(mesh.name);
            if (variant.kind == eaw::AloVariantKind::alternate)
            {
                has_alternate = true;
                ++alternate_mesh_indices[variant.index];
            }
            else if (variant.kind == eaw::AloVariantKind::level_of_detail)
            {
                has_lod = true;
                ++lod_mesh_indices[variant.index];
                for (const auto& submesh : mesh.submeshes)
                {
                    lod_triangles[variant.index] += submesh.triangle_count;
                    model_lod_triangles[variant.index] += submesh.triangle_count;
                }
            }
        }
        for (const auto& bone : model.bones)
        {
            alternate_bones_without_meshes += eaw::alo_name_variant(bone.name).kind == eaw::AloVariantKind::alternate ? 1U : 0U;
        }
        models_with_alternates += has_alternate ? 1U : 0U;
        models_with_lods += has_lod ? 1U : 0U;
        if (model_lod_triangles.size() > 1)
        {
            const auto by_triangles = [](const auto& left, const auto& right)
            {
                return left.second < right.second;
            };
            ++densest_lod[std::ranges::max_element(model_lod_triangles, by_triangles)->first];
            ++sparsest_lod[std::ranges::min_element(model_lod_triangles, by_triangles)->first];
        }
        for (const auto& mesh : model.meshes)
        {
            for (const auto& submesh : mesh.submeshes)
            {
                const auto material = eaw::LegacyMaterial::translate(submesh);
                ++materials;
                ++shaders[material.source_shader.empty() ? "<unnamed>" : material.source_shader];
                ++material_families[std::string(eaw::to_string(material.family))];
                ++blend_modes[std::string(eaw::to_string(material.blend))];
                ++translation_sources[std::string(eaw::to_string(material.source))];
                ++render_phases[std::string(eaw::to_string(material.render.phase))];
                ++lighting_models[std::string(eaw::to_string(material.features.lighting))];
                if (material.source == eaw::LegacyTranslationSource::name_heuristic)
                {
                    ++heuristic_shaders[material.source_shader.empty() ? "<unnamed>" : material.source_shader];
                }
                for (const auto& parameter : submesh.parameters)
                {
                    if (parameter.values.empty())
                    {
                        continue;
                    }
                    auto& usage = numeric_parameters[parameter.name];
                    ++usage.count;
                    usage.components = std::max(usage.components, parameter.values.size());
                    for (std::size_t component = 0; component < parameter.values.size() && component < usage.minimum.size(); ++component)
                    {
                        usage.minimum[component] = std::min(usage.minimum[component], parameter.values[component]);
                        usage.maximum[component] = std::max(usage.maximum[component], parameter.values[component]);
                    }
                }
                for (const auto& texture : material.textures)
                {
                    ++texture_semantics[std::string(eaw::to_string(texture.semantic))];
                    ++texture_parameters[texture.parameter];
                    references.insert(texture.reference);
                    if (resolve_texture_reference(vfs, texture.reference))
                    {
                        resolved.insert(texture.reference);
                    }
                    else
                    {
                        unresolved[texture.reference].insert(asset.virtual_path);
                    }
                }
            }
        }
    }

    std::size_t dds_files = 0;
    std::size_t decoded_dds = 0;
    std::size_t dds_mip_levels = 0;
    std::size_t dds_cube_maps = 0;
    std::size_t dds_volumes = 0;
    std::uint32_t maximum_width = 0;
    std::uint32_t maximum_height = 0;
    std::map<std::string, std::size_t> dds_formats;
    std::map<std::string, std::string> dds_format_examples;
    std::vector<std::pair<std::string, std::string>> dds_failures;
    std::size_t mtd_files = 0;
    std::size_t decoded_mtd = 0;
    std::size_t mtd_entries = 0;
    std::size_t mtd_alpha_entries = 0;
    std::size_t mtd_atlases_resolved = 0;
    std::vector<std::pair<std::string, std::string>> mtd_failures;
    for (const auto& asset : vfs.list("DATA"))
    {
        const auto extension = extension_of(asset.virtual_path);
        if (extension == ".dds")
        {
            ++dds_files;
            const auto decoded = decode_asset<eaw::DdsTexture>(vfs, asset.virtual_path);
            if (!decoded)
            {
                dds_failures.emplace_back(asset.virtual_path, decoded.error().message);
                continue;
            }
            const auto& texture = *decoded;
            ++decoded_dds;
            dds_mip_levels += texture.mip_levels;
            dds_cube_maps += texture.cube_map ? 1U : 0U;
            dds_volumes += texture.volume ? 1U : 0U;
            maximum_width = std::max(maximum_width, texture.width);
            maximum_height = std::max(maximum_height, texture.height);
            const auto format = std::string(eaw::to_string(texture.format));
            ++dds_formats[format];
            dds_format_examples.try_emplace(format, asset.virtual_path);
        }
        else if (extension == ".mtd")
        {
            ++mtd_files;
            const auto decoded = decode_asset<eaw::MtdDirectory>(vfs, asset.virtual_path);
            if (!decoded)
            {
                mtd_failures.emplace_back(asset.virtual_path, decoded.error().message);
                continue;
            }
            const auto& directory = *decoded;
            ++decoded_mtd;
            mtd_entries += directory.entries.size();
            mtd_alpha_entries += directory.alpha_entries;
            auto atlas = std::filesystem::path(asset.virtual_path);
            atlas.replace_filename(atlas.stem().string() + "COMPRESSED.DDS");
            if (vfs.resolve(atlas.generic_string()))
            {
                ++mtd_atlases_resolved;
            }
            else
            {
                mtd_failures.emplace_back(asset.virtual_path, "matching compressed DDS atlas not found");
            }
        }
    }

    std::cout << "Texture and material compatibility audit\n"
              << "Model files: " << model_files << '\n'
              << "Semantic models: " << semantic_models << '\n'
              << "Particle/unsupported files: " << particle_or_invalid << '\n'
              << "Material instances: " << materials << '\n'
              << "Unique texture references: " << references.size() << '\n'
              << "Resolved references: " << resolved.size() << '\n'
              << "Unresolved references: " << unresolved.size() << "\n\n"
              << "DDS corpus\n"
              << "Files: " << dds_files << '\n'
              << "Decoded: " << decoded_dds << '\n'
              << "Unsupported/corrupt: " << dds_failures.size() << '\n'
              << "Total mip levels: " << dds_mip_levels << '\n'
              << "Largest dimensions: " << maximum_width << 'x' << maximum_height << '\n'
              << "Cube maps: " << dds_cube_maps << '\n'
              << "Volume textures: " << dds_volumes << "\nFormats:\n";
    for (const auto& [format, count] : dds_formats)
    {
        const auto example = dds_format_examples.find(format);
        std::cout << "  " << format << ": " << count << "  (e.g. " << (example != dds_format_examples.end() ? example->second : std::string{}) << ")\n";
    }
    std::cout << "\nMTD corpus\n"
              << "Files: " << mtd_files << '\n'
              << "Decoded: " << decoded_mtd << '\n'
              << "Entries: " << mtd_entries << '\n'
              << "Entries with alpha: " << mtd_alpha_entries << '\n'
              << "Resolved compressed atlases: " << mtd_atlases_resolved << '/' << decoded_mtd << "\n\n"
              << "Material translation\n";
    for (const auto& [family, count] : material_families)
    {
        std::cout << "  family " << family << ": " << count << '\n';
    }
    for (const auto& [blend, count] : blend_modes)
    {
        std::cout << "  blend " << blend << ": " << count << '\n';
    }
    for (const auto& [source, count] : translation_sources)
    {
        std::cout << "  source " << source << ": " << count << '\n';
    }
    for (const auto& [phase, count] : render_phases)
    {
        std::cout << "  phase " << phase << ": " << count << '\n';
    }
    for (const auto& [lighting, count] : lighting_models)
    {
        std::cout << "  lighting " << lighting << ": " << count << '\n';
    }
    for (const auto& [shader, count] : heuristic_shaders)
    {
        std::cout << "  heuristic-only shader " << shader << ": " << count << '\n';
    }
    for (const auto& [semantic, count] : texture_semantics)
    {
        std::cout << "  texture " << semantic << ": " << count << '\n';
    }
    std::cout << "Models with _ALT<n> meshes: " << models_with_alternates << ", _LOD<n> meshes: " << models_with_lods << "\nALT mesh indices:";
    for (const auto& [index, count] : alternate_mesh_indices)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nLOD mesh indices:";
    for (const auto& [index, count] : lod_mesh_indices)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nLOD triangles by index:";
    for (const auto& [index, count] : lod_triangles)
    {
        std::cout << ' ' << index << ':' << count;
    }
    std::cout << "\nModels whose densest LOD is index:";
    for (const auto& [index, count] : densest_lod)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nModels whose sparsest LOD is index:";
    for (const auto& [index, count] : sparsest_lod)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nBones with _ALT<n> names (meshes and particle emitters): " << alternate_bones_without_meshes << '\n';
    std::cout << "Numeric parameter usage (count, per-component min..max):\n";
    for (const auto& [name, usage] : numeric_parameters)
    {
        std::cout << "  " << name << ": " << usage.count;
        for (std::size_t component = 0; component < usage.components; ++component)
        {
            std::cout << (component == 0 ? "  " : ", ") << usage.minimum[component] << ".." << usage.maximum[component];
        }
        std::cout << '\n';
    }
    std::cout << "Texture parameter usage:\n";
    for (const auto& [parameter, count] : texture_parameters)
    {
        std::cout << "  " << parameter << ": " << count << '\n';
    }

    std::vector<std::pair<std::string, std::size_t>> sorted_shaders(shaders.begin(), shaders.end());
    std::ranges::sort(sorted_shaders,
                      [](const auto& left, const auto& right)
                      {
                          return left.second != right.second ? left.second > right.second : left.first < right.first;
                      });
    std::cout << "Shader usage:\n";
    for (std::size_t index = 0; index < std::min(limit, sorted_shaders.size()); ++index)
    {
        std::cout << "  " << sorted_shaders[index].first << ": " << sorted_shaders[index].second << '\n';
    }

    std::cout << "\nUnresolved model texture references:\n";
    std::size_t shown = 0;
    for (const auto& [reference, models] : unresolved)
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << reference << "  (" << models.size() << " model(s))\n";
        if (!models.empty())
        {
            std::cout << "    e.g. " << *models.begin() << '\n';
        }
    }
    std::cout << "DDS failures:\n";
    for (std::size_t index = 0; index < std::min(limit, dds_failures.size()); ++index)
    {
        std::cout << "  " << dds_failures[index].first << ": " << dds_failures[index].second << '\n';
    }
    std::cout << "MTD failures/dependencies:\n";
    for (std::size_t index = 0; index < std::min(limit, mtd_failures.size()); ++index)
    {
        std::cout << "  " << mtd_failures[index].first << ": " << mtd_failures[index].second << '\n';
    }
}

void audit_lua(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    struct ScriptRecord
    {
        std::string path;
        eaw::LuaScriptInfo info;
    };

    std::vector<ScriptRecord> scripts;
    std::vector<std::pair<std::string, std::string>> failures;
    std::map<std::string, std::size_t> kinds;
    std::map<std::string, std::size_t> groups;
    std::map<std::string, std::size_t> dependencies;
    std::map<std::string, std::set<std::string>> dependency_users;
    std::map<std::string, std::size_t> definitions;
    std::map<std::string, std::size_t> calls;
    std::set<std::string> definition_keys;
    std::set<std::string> module_keys;
    std::size_t total_lines = 0;
    std::size_t total_prototypes = 0;
    std::size_t total_instructions = 0;
    std::size_t warning_count = 0;

    constexpr std::string_view scripts_prefix = "DATA/SCRIPTS/";
    for (const auto& asset : vfs.list("DATA/SCRIPTS"))
    {
        if (extension_of(asset.virtual_path) != ".lua")
        {
            continue;
        }
        auto relative = asset.virtual_path;
        if (relative.starts_with(scripts_prefix))
        {
            relative.erase(0, scripts_prefix.size());
        }
        auto module = lower_ascii(std::filesystem::path(relative).replace_extension().generic_string());
        module_keys.insert(module);
        auto dotted = module;
        std::ranges::replace(dotted, '/', '.');
        module_keys.insert(std::move(dotted));
        module_keys.insert(lower_ascii(std::filesystem::path(relative).stem().string()));
    }

    const auto assets = vfs.list(prefix);
    for (const auto& asset : assets)
    {
        if (extension_of(asset.virtual_path) != ".lua")
        {
            continue;
        }
        auto relative = asset.virtual_path;
        if (relative.starts_with(scripts_prefix))
        {
            relative.erase(0, scripts_prefix.size());
        }
        const auto separator = relative.find('/');
        ++groups[separator == std::string::npos ? "<root>" : relative.substr(0, separator)];

        auto decoded = decode_asset<eaw::LuaScriptInfo>(vfs, asset.virtual_path);
        if (!decoded)
        {
            failures.emplace_back(asset.virtual_path, decoded.error().message);
            continue;
        }
        auto info = std::move(decoded).value();
        ++kinds[std::string(eaw::to_string(info.kind))];
        total_lines += info.lines;
        total_prototypes += info.prototypes;
        total_instructions += info.instructions;
        warning_count += info.warnings.size();
        for (const auto& dependency : info.dependencies)
        {
            ++dependencies[dependency];
            dependency_users[dependency].insert(asset.virtual_path);
        }
        for (const auto& definition : info.defined_functions)
        {
            ++definitions[definition];
            const auto folded = lower_ascii(definition);
            definition_keys.insert(folded);
            const auto leaf = folded.find_last_of(".:");
            definition_keys.insert(leaf == std::string::npos ? folded : folded.substr(leaf + 1U));
        }
        for (const auto& call : info.called_functions)
        {
            ++calls[call];
        }
        scripts.push_back({asset.virtual_path, std::move(info)});
    }

    const auto dependency_resolved = [&](std::string dependency)
    {
        dependency = lower_ascii(std::move(dependency));
        std::ranges::replace(dependency, '\\', '/');
        if (dependency.starts_with("data/scripts/"))
        {
            dependency.erase(0, 13);
        }
        if (dependency.ends_with(".lua"))
        {
            dependency.resize(dependency.size() - 4U);
        }
        if (module_keys.contains(dependency))
        {
            return true;
        }
        auto path_form = dependency;
        std::ranges::replace(path_form, '.', '/');
        if (module_keys.contains(path_form))
        {
            return true;
        }
        return module_keys.contains(lower_ascii(std::filesystem::path(dependency).stem().string()));
    };

    std::map<std::string, std::set<std::string>> unresolved_dependencies;
    for (const auto& [dependency, users] : dependency_users)
    {
        if (!dependency_resolved(dependency))
        {
            unresolved_dependencies[dependency] = users;
        }
    }

    const auto standard_library = [](const std::string_view call)
    {
        const auto separator = call.find_first_of(".:");
        const auto root = lower_ascii(std::string(call.substr(0, separator)));
        static const std::set<std::string> standard{"assert",     "collectgarbage", "coroutine",    "dofile", "error", "gcinfo",   "getfenv",  "getmetatable", "ipairs", "loadfile",
                                                    "loadstring", "math",           "next",         "pairs",  "pcall", "print",    "rawequal", "rawget",       "rawset", "require",
                                                    "select",     "setfenv",        "setmetatable", "string", "table", "tonumber", "tostring", "type",         "unpack", "xpcall"};
        return standard.contains(root);
    };
    std::map<std::string, std::size_t> host_api_calls;
    for (const auto& [call, count] : calls)
    {
        const auto folded = lower_ascii(call);
        const auto leaf = folded.find_last_of(".:");
        const auto leaf_name = leaf == std::string::npos ? folded : folded.substr(leaf + 1U);
        if (!standard_library(call) && !definition_keys.contains(folded) && !definition_keys.contains(leaf_name))
        {
            host_api_calls[call] = count;
        }
    }

    const auto sorted = [](const std::map<std::string, std::size_t>& values)
    {
        std::vector<std::pair<std::string, std::size_t>> result(values.begin(), values.end());
        std::ranges::sort(result,
                          [](const auto& left, const auto& right)
                          {
                              return left.second != right.second ? left.second > right.second : left.first < right.first;
                          });
        return result;
    };

    std::cout << "Lua compatibility audit\n"
              << "Scripts: " << scripts.size() + failures.size() << '\n'
              << "Inspected: " << scripts.size() << '\n'
              << "Failures: " << failures.size() << '\n'
              << "Warnings: " << warning_count << '\n'
              << "Source lines: " << total_lines << '\n'
              << "Bytecode prototypes: " << total_prototypes << '\n'
              << "Bytecode instructions: " << total_instructions << '\n'
              << "Unique dependencies: " << dependencies.size() << '\n'
              << "Unresolved dependencies: " << unresolved_dependencies.size() << '\n'
              << "Defined function names: " << definitions.size() << '\n'
              << "Called function names: " << calls.size() << '\n'
              << "Candidate host API calls: " << host_api_calls.size() << "\n\nFormats:\n";
    for (const auto& [kind, count] : kinds)
    {
        std::cout << "  " << kind << ": " << count << '\n';
    }
    std::cout << "Script groups:\n";
    for (const auto& [group, count] : groups)
    {
        std::cout << "  " << group << ": " << count << '\n';
    }
    std::cout << "Most-used dependencies:\n";
    const auto sorted_dependencies = sorted(dependencies);
    for (std::size_t index = 0; index < std::min(limit, sorted_dependencies.size()); ++index)
    {
        std::cout << "  " << sorted_dependencies[index].second << "  " << sorted_dependencies[index].first << '\n';
    }
    std::cout << "Candidate host API calls (scripts using each):\n";
    const auto sorted_calls = sorted(host_api_calls);
    for (std::size_t index = 0; index < std::min(limit, sorted_calls.size()); ++index)
    {
        std::cout << "  " << sorted_calls[index].second << "  " << sorted_calls[index].first << '\n';
    }
    std::cout << "Most-common defined functions:\n";
    const auto sorted_definitions = sorted(definitions);
    for (std::size_t index = 0; index < std::min(limit, sorted_definitions.size()); ++index)
    {
        std::cout << "  " << sorted_definitions[index].second << "  " << sorted_definitions[index].first << '\n';
    }
    std::cout << "Unresolved dependencies:\n";
    std::size_t shown = 0;
    for (const auto& [dependency, users] : unresolved_dependencies)
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << dependency << "  (" << users.size() << " script(s))";
        if (!users.empty())
        {
            std::cout << "  e.g. " << *users.begin();
        }
        std::cout << '\n';
    }
    std::cout << "Inspection failures:\n";
    for (std::size_t index = 0; index < std::min(limit, failures.size()); ++index)
    {
        std::cout << "  " << failures[index].first << ": " << failures[index].second << '\n';
    }
}

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

void record_example(std::map<std::string, AuditExample>& entries, const std::string& key, const std::string& map, const std::string& detail = {})
{
    auto& entry = entries[key];
    if (entry.count++ == 0)
    {
        entry.map = map;
        entry.detail = detail;
    }
}

// Resolves every placed TED object through its type CRC to an XML definition and model, then
// reports coverage. This is the evidence for the object-name CRC rule and the model fields.
void audit_map_objects(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);

    std::size_t maps = 0;
    std::size_t placed = 0;
    std::array<float, 3> rotation_minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    std::array<float, 3> rotation_maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    std::array<std::size_t, 3> rotation_nonzero{};
    std::size_t typed = 0;
    std::size_t ambiguous = 0;
    std::size_t modeled = 0;
    std::size_t model_files = 0;
    std::size_t squadron_members = 0;
    std::size_t unexpanded_containers = 0;
    std::map<std::uint32_t, AuditExample> unknown_crcs;
    std::map<std::string, std::size_t> element_types;
    // Placements per TED owner slot, by XML element type (untyped objects as "?").
    std::map<std::uint32_t, std::map<std::string, std::size_t>> owner_types;
    std::map<std::string, std::size_t> model_fields;
    std::map<std::string, AuditExample> types_without_model;
    std::map<std::string, AuditExample> missing_models;
    // Placements per resolved model path.
    std::map<std::string, std::size_t> unique_models;
    std::set<std::string> diagnostics;
    std::vector<std::pair<std::string, std::string>> failures;

    for (const auto& asset : vfs.list(prefix))
    {
        if (extension_of(asset.virtual_path) != ".ted")
        {
            continue;
        }
        auto decoded = decode_asset<eaw::TedMap>(vfs, asset.virtual_path);
        if (!decoded)
        {
            failures.emplace_back(asset.virtual_path, decoded.error().message);
            continue;
        }
        const eaw::TedMap map = std::move(decoded).value();
        ++maps;
        const auto context = map.type == eaw::TedMapType::land ? eaw::GameModelContext::land : eaw::GameModelContext::space;
        const auto layout = eaw::build_map_object_layout(map, catalog, vfs);
        squadron_members += layout.expanded_members;
        unexpanded_containers += layout.unexpanded_containers;
        for (const auto& object : map.placed_objects)
        {
            ++placed;
            const std::array<float, 3> rotation{object.rotation.x, object.rotation.y, object.rotation.z};
            for (std::size_t axis = 0; axis < rotation.size(); ++axis)
            {
                rotation_minimum[axis] = std::min(rotation_minimum[axis], rotation[axis]);
                rotation_maximum[axis] = std::max(rotation_maximum[axis], rotation[axis]);
                rotation_nonzero[axis] += rotation[axis] != 0.0F ? 1U : 0U;
            }
            const auto match = catalog.resolve(object.crc, context);
            ++owner_types[object.player][match.definition != nullptr ? match.definition->type : std::string("?")];
            if (match.definition == nullptr)
            {
                auto& entry = unknown_crcs[object.crc];
                if (entry.count++ == 0)
                {
                    entry.map = asset.virtual_path;
                }
                continue;
            }
            ++typed;
            ambiguous += match.candidates > 1 ? 1U : 0U;
            ++element_types[match.definition->type];
            std::vector<std::string> messages;
            const auto model = catalog.model(*match.definition, context, &messages);
            diagnostics.insert(messages.begin(), messages.end());
            if (!model)
            {
                record_example(types_without_model, match.definition->type + " " + match.definition->name, asset.virtual_path);
                continue;
            }
            ++modeled;
            ++model_fields[model->field];
            if (const auto path = eaw::resolve_model_path(vfs, model->value))
            {
                ++model_files;
                ++unique_models[*path];
            }
            else
            {
                record_example(missing_models, model->value, asset.virtual_path, match.definition->name);
            }
        }
    }

    const auto percent = [](const std::size_t part, const std::size_t whole)
    {
        std::ostringstream text;
        text << std::fixed << std::setprecision(1) << (whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole)) << '%';
        return text.str();
    };

    std::cout << "Map object resolution audit\n"
              << "XML definitions: " << catalog.size() << '\n'
              << "Maps: " << maps << " (failures: " << failures.size() << ")\n"
              << "Placed objects: " << placed << '\n'
              << "Typed by name CRC: " << typed << " (" << percent(typed, placed) << ")\n"
              << "Ambiguous CRC matches: " << ambiguous << '\n'
              << "With a model reference: " << modeled << " (" << percent(modeled, placed) << ")\n"
              << "Model files resolved: " << model_files << " (" << percent(model_files, placed) << "), unique models: " << unique_models.size() << '\n'
              << "Unknown CRCs: " << unknown_crcs.size() << '\n'
              << "Space squadron members expanded: " << squadron_members << '\n'
              << "Land teams/companies not expanded: " << unexpanded_containers << '\n';
    for (std::size_t axis = 0; axis < rotation_minimum.size() && placed > 0; ++axis)
    {
        std::cout << "Rotation component " << axis << ": " << rotation_minimum[axis] << " .. " << rotation_maximum[axis] << ", non-zero " << rotation_nonzero[axis] << '\n';
    }
    for (const auto& [owner, types] : owner_types)
    {
        std::size_t total = 0;
        for (const auto& [type, count] : types)
        {
            total += count;
        }
        std::cout << "Owner slot " << owner << ": " << total << " placements (";
        bool first = true;
        for (const auto& [type, count] : types)
        {
            std::cout << (first ? "" : ", ") << type << ' ' << count;
            first = false;
        }
        std::cout << ")\n";
    }
    // A single-map audit lists its models and the layer each loads from, which shows which files
    // a mod overrides for that map.
    if (maps == 1)
    {
        std::cout << "\nModels (placements, winning layer):\n";
        for (const auto& [path, count] : unique_models)
        {
            const auto resolved = vfs.resolve(path);
            std::cout << "  " << path << ": " << count << ", " << (resolved ? resolved->mount_name : std::string("unresolved")) << '\n';
        }
    }
    std::cout << "\nResolved element types:\n";
    for (const auto& [type, count] : element_types)
    {
        std::cout << "  " << type << ": " << count << '\n';
    }
    std::cout << "Model fields:\n";
    for (const auto& [field, count] : model_fields)
    {
        std::cout << "  " << field << ": " << count << '\n';
    }
    std::cout << "\nUnknown CRCs:\n";
    std::size_t shown = 0;
    for (const auto& [crc, entry] : by_count(unknown_crcs))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << crc << std::dec << std::setfill(' ') << "  x" << entry.count << "  e.g. " << entry.map << '\n';
    }
    std::cout << "\nTyped objects without a model for their map context:\n";
    shown = 0;
    for (const auto& [type, entry] : by_count(types_without_model))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << type << "  x" << entry.count << "  e.g. " << entry.map << '\n';
    }
    std::cout << "\nMissing model files:\n";
    shown = 0;
    for (const auto& [model, entry] : by_count(missing_models))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << model << "  x" << entry.count << "  (" << entry.detail << ", e.g. " << entry.map << ")\n";
    }
    std::cout << "\nVariant diagnostics: " << diagnostics.size() << '\n';
    shown = 0;
    for (const auto& message : diagnostics)
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << message << '\n';
    }
    for (const auto& [path, message] : failures)
    {
        std::cout << "map failure: " << path << ": " << message << '\n';
    }
}

void audit_maps(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    const auto assets = vfs.list(prefix);
    std::size_t files = 0;
    std::size_t land = 0;
    std::size_t space = 0;
    std::size_t unknown = 0;
    std::size_t warnings = 0;
    std::size_t terrain_layers = 0;
    std::size_t objects = 0;
    std::size_t positioned_objects = 0;
    std::size_t active_environment_out_of_range = 0;
    std::map<std::uint32_t, std::size_t> active_environments;
    std::vector<std::pair<std::string, std::string>> failures;
    for (const auto& asset : assets)
    {
        if (extension_of(asset.virtual_path) != ".ted")
        {
            continue;
        }
        ++files;
        const auto decoded = decode_asset<eaw::TedMap>(vfs, asset.virtual_path);
        if (!decoded)
        {
            failures.emplace_back(asset.virtual_path, decoded.error().message);
            continue;
        }
        const auto& map = *decoded;
        if (map.type == eaw::TedMapType::land)
        {
            ++land;
        }
        else if (map.type == eaw::TedMapType::space)
        {
            ++space;
        }
        else
        {
            ++unknown;
        }
        warnings += map.warnings.size();
        if (!map.environments.empty())
        {
            ++active_environments[map.active_environment];
            active_environment_out_of_range += map.active_environment >= map.environments.size() ? 1U : 0U;
        }
        terrain_layers += map.terrain_layers.size();
        objects += map.object_count;
        positioned_objects += std::ranges::count_if(map.placed_objects,
                                                    [](const auto& object)
                                                    {
                                                        return object.has_transform;
                                                    });
    }
    std::cout << "TED semantic audit\n"
              << "Map files: " << files << '\n'
              << "Decoded: " << files - failures.size() << '\n'
              << "Land maps: " << land << '\n'
              << "Space maps: " << space << '\n'
              << "Unknown type: " << unknown << '\n'
              << "Terrain layers: " << terrain_layers << '\n'
              << "Objects: " << objects << '\n'
              << "Positioned objects: " << positioned_objects << '\n'
              << "Active environment out of range: " << active_environment_out_of_range << '\n'
              << "Active environment indices:";
    for (const auto& [index, count] : active_environments)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << '\n' << "Warnings: " << warnings << '\n' << "Failures: " << failures.size() << '\n';
    for (std::size_t index = 0; index < std::min(limit, failures.size()); ++index)
    {
        std::cout << "  " << failures[index].first << ": " << failures[index].second << '\n';
    }
}

eaw::Status audit_xml(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit, const std::optional<std::filesystem::path>& schema_root, const bool include_foc)
{
    const auto database = eaw::XmlAssetDatabase::build(vfs, prefix);
    std::map<std::string, std::size_t> definitions_by_type;
    for (const auto& definition : database.definitions())
    {
        ++definitions_by_type[definition.type];
    }
    const auto missing_includes = std::ranges::count_if(database.includes(),
                                                        [](const auto& reference)
                                                        {
                                                            return !reference.resolved;
                                                        });

    std::cout << "XML semantic audit\n"
              << "Documents: " << database.document_count() << '\n'
              << "Parsed: " << database.parsed_document_count() << '\n'
              << "Parse failures: " << database.failures().size() << '\n'
              << "Named definitions: " << database.definitions().size() << '\n'
              << "Definition types: " << definitions_by_type.size() << '\n'
              << "Duplicate type/name keys: " << database.duplicates().size() << '\n'
              << "XML file references: " << database.includes().size() << '\n'
              << "Missing XML file references: " << missing_includes << '\n';

    std::cout << "\nMost common definition types:\n";
    std::vector<std::pair<std::string, std::size_t>> sorted_types(definitions_by_type.begin(), definitions_by_type.end());
    std::ranges::sort(sorted_types,
                      [](const auto& left, const auto& right)
                      {
                          if (left.second != right.second)
                          {
                              return left.second > right.second;
                          }
                          return left.first < right.first;
                      });
    for (std::size_t index = 0; index < std::min(limit, sorted_types.size()); ++index)
    {
        std::cout << "  " << std::setw(7) << sorted_types[index].second << "  " << sorted_types[index].first << '\n';
    }

    if (!database.failures().empty())
    {
        std::cout << "\nParse failures:\n";
        for (std::size_t index = 0; index < std::min(limit, database.failures().size()); ++index)
        {
            std::cout << "  " << database.failures()[index].source_path << ": " << database.failures()[index].message << '\n';
        }
    }
    if (!database.duplicates().empty())
    {
        std::cout << "\nDuplicate definitions:\n";
        for (std::size_t index = 0; index < std::min(limit, database.duplicates().size()); ++index)
        {
            const auto& duplicate = database.duplicates()[index];
            std::cout << "  " << duplicate.type << " Name=\"" << duplicate.name << "\"  (" << duplicate.occurrences.size() << " occurrence(s))\n";
            for (const auto& occurrence : duplicate.occurrences)
            {
                std::cout << "    " << occurrence.source_path << ':' << occurrence.line << '\n';
            }
        }
    }
    if (missing_includes != 0)
    {
        std::cout << "\nMissing XML file references:\n";
        std::size_t shown = 0;
        for (const auto& reference : database.includes())
        {
            if (reference.resolved || shown++ >= limit)
            {
                continue;
            }
            std::cout << "  " << reference.resolved_path << "  from " << reference.source_path << ':' << reference.line << '\n';
        }
    }

    if (schema_root)
    {
        EAW_TRY_ASSIGN(const auto schema, eaw::XmlSchema::load(*schema_root, include_foc));
        const auto validation = schema.validate(database);
        const auto errors = std::ranges::count_if(validation.diagnostics,
                                                  [](const auto& diagnostic)
                                                  {
                                                      return diagnostic.severity == eaw::XmlSchemaDiagnosticSeverity::error;
                                                  });
        std::cout << "\nExternal schema validation\n"
                  << "Schema types: " << schema.types().size() << '\n'
                  << "Schema enums/token sets: " << schema.enums().size() << '\n'
                  << "Schema object kinds: " << schema.kinds().size() << '\n'
                  << "Metafile rules: " << schema.metafiles().size() << '\n'
                  << "Definitions validated: " << validation.definitions_validated << '/' << validation.definitions_considered << '\n'
                  << "  Canonical element types: " << validation.canonical_type_definitions << '\n'
                  << "  Metafile-inferred types: " << validation.metafile_type_definitions << '\n'
                  << "  Structurally inferred types: " << validation.structural_type_definitions << '\n'
                  << "Untyped definitions: " << validation.untyped_definitions << '\n'
                  << "Fields validated: " << validation.fields_validated << '\n'
                  << "Unknown fields: " << validation.unknown_fields << '\n'
                  << "Duplicate non-repeatable fields: " << validation.duplicate_fields << '\n'
                  << "Invalid scalar/enum values: " << validation.invalid_values << '\n'
                  << "Unresolved XML references: " << validation.unresolved_references << '\n'
                  << "Wrong-type XML references: " << validation.mismatched_references << '\n'
                  << "Diagnostics: " << validation.diagnostics.size() << " (" << errors << " error(s))\n";
        for (std::size_t index = 0; index < std::min(limit, validation.diagnostics.size()); ++index)
        {
            const auto& diagnostic = validation.diagnostics[index];
            std::cout << "  " << eaw::to_string(diagnostic.severity) << '[' << eaw::to_string(diagnostic.kind) << "] " << diagnostic.source_path << ':' << diagnostic.line << "  "
                      << diagnostic.object_type << " Name=\"" << diagnostic.object_name << "\" / " << diagnostic.field << ": " << diagnostic.message << '\n';
        }
    }
    return eaw::Status::success();
}

// Runs the selected command and returns its exit code; failures are returned for main to report.
eaw::Result<int> run(const Options& options)
{
    const eaw::InstallationLocator locator;
    const auto installations = locator.discover(options.game_root);
    if (installations.empty())
    {
        return eaw::make_error("no valid Empire at War installation was found");
    }
    const auto profile = eaw::make_game_profile(installations.front(), options.variant);
    if (!profile)
    {
        return eaw::make_error("the selected game profile is unavailable");
    }

    eaw::VirtualFileSystem vfs;
    const auto report = eaw::mount_game_assets(vfs, *profile, options.mods);
    for (const auto& warning : report.warnings)
    {
        std::cerr << "warning: " << warning << '\n';
    }

    if (options.command == "inspect")
    {
        const auto status = inspect_asset(vfs, options.operand, options.limit);
        return status ? 0 : report_error(status.error());
    }
    if (options.command == "localization-refs")
    {
        const auto read = vfs.read(options.operand);
        if (!read)
        {
            return report_error(read.error());
        }
        const auto& bytes = *read;
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto references = localization_references(text);
        for (const auto& reference : references)
        {
            std::cout << reference << '\n';
        }
        std::cerr << references.size() << " unique localization references\n";
        return 0;
    }
    if (options.command == "texture-audit")
    {
        audit_texture_references(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "lua-audit")
    {
        audit_lua(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "map-objects")
    {
        audit_map_objects(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "map-audit")
    {
        audit_maps(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "xml-audit")
    {
        const auto status = audit_xml(vfs, options.prefix, options.limit, options.schema_root, options.variant == eaw::GameVariant::forces_of_corruption);
        return status ? 0 : report_error(status.error());
    }
    if (options.command != "summary" && options.command != "list")
    {
        return eaw::make_error("unknown command: " + options.command);
    }

    auto assets = vfs.list(options.prefix);
    if (!options.extension.empty())
    {
        std::erase_if(assets,
                      [&](const auto& asset)
                      {
                          return extension_of(asset.virtual_path) != options.extension;
                      });
    }
    if (options.command == "list")
    {
        for (std::size_t index = 0; index < std::min(options.limit, assets.size()); ++index)
        {
            const auto& asset = assets[index];
            std::cout << std::setw(12) << asset.size << "  " << asset.virtual_path << "  [" << asset.mount_name << "]\n";
        }
        std::cout.flush();
        std::cerr << "showing " << std::min(options.limit, assets.size()) << " of " << assets.size() << " assets\n";
        return 0;
    }

    std::map<std::string, std::pair<std::size_t, std::uint64_t>> by_extension;
    std::map<std::string, std::size_t> by_mount;
    std::uint64_t total_bytes = 0;
    for (const auto& asset : assets)
    {
        auto& group = by_extension[extension_of(asset.virtual_path)];
        ++group.first;
        group.second += asset.size;
        ++by_mount[asset.mount_name];
        total_bytes += asset.size;
    }
    std::cout << "Game: " << eaw::to_string(profile->variant) << '\n'
              << "Installation: " << profile->root << '\n'
              << "Prefix: " << options.prefix << '\n'
              << "Resolved assets: " << assets.size() << '\n'
              << "Logical bytes: " << total_bytes << "\n\nBy extension:\n";
    for (const auto& [extension, values] : by_extension)
    {
        std::cout << "  " << std::setw(8) << extension << "  " << std::setw(7) << values.first << " files  " << values.second << " bytes\n";
    }
    std::cout << "\nWinning source mounts:\n";
    for (const auto& [mount, count] : by_mount)
    {
        std::cout << "  " << std::setw(7) << count << "  " << mount << '\n';
    }
    return 0;
}

} // namespace

int main(const int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    if (!options)
    {
        return report_error(options.error());
    }
    const auto result = run(*options);
    return result ? *result : report_error(result.error());
}
