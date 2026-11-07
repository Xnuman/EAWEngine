#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/text_encoding.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"
#include "eaw/xml_document.hpp"
#include "eaw/xml_schema.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
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
              << "  EAWAssetInspector texture-audit [prefix] [options]\n"
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

std::size_t parse_size(const std::string& value)
{
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed);
    if (consumed != value.size())
    {
        throw std::invalid_argument("invalid number: " + value);
    }
    return static_cast<std::size_t>(result);
}

Options parse_options(const int argc, char** argv)
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
    else if ((options.command == "list" || options.command == "map-audit" || options.command == "texture-audit" || options.command == "xml-audit") && index < argc && argv[index][0] != '-')
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
            throw std::invalid_argument("missing value after " + argument);
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
                throw std::invalid_argument("unknown game variant: " + value);
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
            options.limit = parse_size(value);
        }
        else
        {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    if ((options.command == "inspect" || options.command == "localization-refs") && options.operand.empty())
    {
        throw std::invalid_argument(options.command + " requires a virtual asset path");
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

std::uint32_t little_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size())
    {
        throw std::runtime_error("asset is too short for the requested header");
    }
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

void inspect_dat(const std::span<const std::byte> bytes, const std::size_t limit)
{
    const auto dat = eaw::DatFile::parse(bytes);
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
        auto value = eaw::utf16_to_utf8(entry.value);
        std::ranges::replace(value, '\n', ' ');
        std::ranges::replace(value, '\r', ' ');
        if (value.size() > 100)
        {
            value.resize(97);
            value += "...";
        }
        std::cout << "  " << entry.key << " = " << value << '\n';
    }
}

void inspect_dds(const std::span<const std::byte> bytes)
{
    if (bytes.size() < 128 || std::to_integer<char>(bytes[0]) != 'D' || std::to_integer<char>(bytes[1]) != 'D' || std::to_integer<char>(bytes[2]) != 'S' ||
        std::to_integer<unsigned char>(bytes[3]) != 0x20U)
    {
        throw std::runtime_error("invalid DDS header");
    }
    const auto height = little_u32(bytes, 12);
    const auto width = little_u32(bytes, 16);
    const auto mipmaps = little_u32(bytes, 28);
    std::string fourcc;
    for (std::size_t index = 84; index < 88; ++index)
    {
        const char character = std::to_integer<char>(bytes[index]);
        fourcc.push_back(std::isprint(static_cast<unsigned char>(character)) ? character : '.');
    }
    std::cout << "Format: DirectDraw Surface\n"
              << "Dimensions: " << width << 'x' << height << '\n'
              << "Mip levels: " << mipmaps << '\n'
              << "FourCC: " << fourcc << '\n';
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

void inspect_alo(const std::span<const std::byte> bytes, const std::size_t limit)
{
    const auto model = eaw::AloModel::parse(bytes);
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
}

void inspect_ala(const std::span<const std::byte> bytes, const std::size_t limit)
{
    const auto animation = eaw::AlaAnimation::parse(bytes);
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
}

void inspect_ted(const std::span<const std::byte> bytes, const std::size_t limit)
{
    const auto map = eaw::TedMap::parse(bytes);
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
        std::cout << '\n';
    }
    std::cout << "Object CRCs:\n";
    for (std::size_t index = 0; index < std::min(limit, map.object_types.size()); ++index)
    {
        const auto& object = map.object_types[index];
        std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << object.crc << std::dec << std::setfill(' ') << " x " << object.count << '\n';
    }
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
    static const std::regex reference(R"(\bTEXT_[A-Za-z0-9_.-]+)");
    std::set<std::string> unique;
    for (auto match = std::sregex_iterator(active_xml.begin(), active_xml.end(), reference); match != std::sregex_iterator(); ++match)
    {
        unique.insert(match->str());
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

void inspect_asset(const eaw::VirtualFileSystem& vfs, const std::string& path, const std::size_t limit)
{
    const auto resolved = vfs.resolve(path);
    if (!resolved)
    {
        throw std::runtime_error("asset was not found: " + path);
    }
    const auto bytes = vfs.read(path);
    const auto extension = extension_of(resolved->virtual_path);
    std::cout << "Path: " << resolved->virtual_path << '\n'
              << "Source: " << resolved->mount_name << '\n'
              << "Container: " << resolved->container << '\n'
              << "Offset: " << resolved->offset << '\n'
              << "Size: " << resolved->size << " bytes\n";

    if (extension == ".dat")
    {
        inspect_dat(bytes, limit);
    }
    else if (extension == ".dds")
    {
        inspect_dds(bytes);
    }
    else if (extension == ".alo")
    {
        try
        {
            inspect_alo(bytes, limit);
        }
        catch (const std::exception& error)
        {
            std::cout << "Semantic model decoding unavailable: " << error.what() << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
    }
    else if (extension == ".ala")
    {
        try
        {
            inspect_ala(bytes, limit);
        }
        catch (const std::exception& error)
        {
            std::cout << "Semantic animation decoding unavailable: " << error.what() << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
    }
    else if (extension == ".ted")
    {
        try
        {
            inspect_ted(bytes, limit);
        }
        catch (const std::exception& error)
        {
            std::cout << "Semantic map decoding unavailable: " << error.what() << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
    }
    else if (extension == ".mtd")
    {
        inspect_chunks(bytes, limit);
    }
    else if (extension == ".xml")
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto references = localization_references(text);
        const auto document = eaw::XmlDocument::parse(bytes);
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
        const bool compiled_lua = bytes.size() >= 4 && std::to_integer<unsigned char>(bytes[0]) == 0x1bU && std::to_integer<char>(bytes[1]) == 'L' && std::to_integer<char>(bytes[2]) == 'u' &&
                                  std::to_integer<char>(bytes[3]) == 'p';
        std::cout << "Format: " << (compiled_lua ? "Petroglyph Lua 5.0 bytecode" : "Lua source") << '\n' << "Localization references: " << references.size() << '\n';
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
    const auto assets = vfs.list(prefix);
    std::size_t model_files = 0;
    std::size_t semantic_models = 0;
    std::size_t particle_or_invalid = 0;
    std::set<std::string> references;
    std::set<std::string> resolved;
    std::map<std::string, std::set<std::string>> unresolved;
    for (const auto& asset : assets)
    {
        if (extension_of(asset.virtual_path) != ".alo")
        {
            continue;
        }
        ++model_files;
        try
        {
            const auto model = eaw::AloModel::parse(vfs.read(asset.virtual_path));
            ++semantic_models;
            for (const auto& mesh : model.meshes)
            {
                for (const auto& submesh : mesh.submeshes)
                {
                    for (const auto& parameter : submesh.parameters)
                    {
                        if (parameter.type != eaw::AloShaderParameterType::texture || parameter.value.empty())
                        {
                            continue;
                        }
                        references.insert(parameter.value);
                        if (resolve_texture_reference(vfs, parameter.value))
                        {
                            resolved.insert(parameter.value);
                        }
                        else
                        {
                            unresolved[parameter.value].insert(asset.virtual_path);
                        }
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            ++particle_or_invalid;
        }
    }
    std::cout << "ALO texture reference audit\n"
              << "Model files: " << model_files << '\n'
              << "Semantic models: " << semantic_models << '\n'
              << "Particle/unsupported files: " << particle_or_invalid << '\n'
              << "Unique texture references: " << references.size() << '\n'
              << "Resolved references: " << resolved.size() << '\n'
              << "Unresolved references: " << unresolved.size() << '\n';
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
    std::vector<std::pair<std::string, std::string>> failures;
    for (const auto& asset : assets)
    {
        if (extension_of(asset.virtual_path) != ".ted")
        {
            continue;
        }
        ++files;
        try
        {
            const auto map = eaw::TedMap::parse(vfs.read(asset.virtual_path));
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
            terrain_layers += map.terrain_layers.size();
            objects += map.object_count;
            positioned_objects += std::ranges::count_if(map.placed_objects,
                                                        [](const auto& object)
                                                        {
                                                            return object.has_transform;
                                                        });
        }
        catch (const std::exception& error)
        {
            failures.emplace_back(asset.virtual_path, error.what());
        }
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
              << "Warnings: " << warnings << '\n'
              << "Failures: " << failures.size() << '\n';
    for (std::size_t index = 0; index < std::min(limit, failures.size()); ++index)
    {
        std::cout << "  " << failures[index].first << ": " << failures[index].second << '\n';
    }
}

void audit_xml(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit, const std::optional<std::filesystem::path>& schema_root, const bool include_foc)
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
        const auto schema = eaw::XmlSchema::load(*schema_root, include_foc);
        const auto validation = schema.validate(database);
        const auto errors = std::ranges::count_if(validation.diagnostics,
                                                  [](const auto& diagnostic)
                                                  {
                                                      return diagnostic.severity == eaw::XmlSchemaDiagnosticSeverity::error;
                                                  });
        std::cout << "\nExternal schema validation\n"
                  << "Schema types: " << schema.types().size() << '\n'
                  << "Schema enums/token sets: " << schema.enums().size() << '\n'
                  << "Metafile rules: " << schema.metafiles().size() << '\n'
                  << "Definitions validated: " << validation.definitions_validated << '/' << validation.definitions_considered << '\n'
                  << "Untyped definitions: " << validation.untyped_definitions << '\n'
                  << "Fields validated: " << validation.fields_validated << '\n'
                  << "Unknown fields: " << validation.unknown_fields << '\n'
                  << "Duplicate non-repeatable fields: " << validation.duplicate_fields << '\n'
                  << "Invalid scalar/enum values: " << validation.invalid_values << '\n'
                  << "Unresolved XML references: " << validation.unresolved_references << '\n'
                  << "Diagnostics: " << validation.diagnostics.size() << " (" << errors << " error(s))\n";
        for (std::size_t index = 0; index < std::min(limit, validation.diagnostics.size()); ++index)
        {
            const auto& diagnostic = validation.diagnostics[index];
            std::cout << "  " << eaw::to_string(diagnostic.severity) << '[' << eaw::to_string(diagnostic.kind) << "] " << diagnostic.source_path << ':' << diagnostic.line << "  "
                      << diagnostic.object_type << " Name=\"" << diagnostic.object_name << "\" / " << diagnostic.field << ": " << diagnostic.message << '\n';
        }
    }
}

} // namespace

int main(const int argc, char** argv)
{
    try
    {
        const auto options = parse_options(argc, argv);
        const eaw::InstallationLocator locator;
        const auto installations = locator.discover(options.game_root);
        if (installations.empty())
        {
            throw std::runtime_error("no valid Empire at War installation was found");
        }
        const auto profile = eaw::make_game_profile(installations.front(), options.variant);
        if (!profile)
        {
            throw std::runtime_error("the selected game profile is unavailable");
        }

        eaw::VirtualFileSystem vfs;
        const auto report = eaw::mount_game_assets(vfs, *profile, options.mods);
        for (const auto& warning : report.warnings)
        {
            std::cerr << "warning: " << warning << '\n';
        }

        if (options.command == "inspect")
        {
            inspect_asset(vfs, options.operand, options.limit);
            return 0;
        }
        if (options.command == "localization-refs")
        {
            const auto bytes = vfs.read(options.operand);
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
        if (options.command == "map-audit")
        {
            audit_maps(vfs, options.prefix, options.limit);
            return 0;
        }
        if (options.command == "xml-audit")
        {
            audit_xml(vfs, options.prefix, options.limit, options.schema_root, options.variant == eaw::GameVariant::forces_of_corruption);
            return 0;
        }
        if (options.command != "summary" && options.command != "list")
        {
            throw std::invalid_argument("unknown command: " + options.command);
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
    catch (const std::exception& error)
    {
        std::cerr << "asset inspector error: " << error.what() << '\n';
        return 1;
    }
}
