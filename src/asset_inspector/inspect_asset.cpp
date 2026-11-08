// `inspect`: one asset decoded and summarized by format.

#include "inspector.hpp"

#include <iomanip>

namespace eaw::inspector {
namespace {

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

    // Model-space bind pose: position and X axis (row-vector matrices: row 0 is the bone's X axis).
    std::cout << "Skeleton:\n";
    const auto pose = model.bind_pose();
    for (std::size_t index = 0; index < std::min(limit, model.bones.size()); ++index)
    {
        const auto& bone = model.bones[index];
        std::cout << "  [" << index << "] " << bone.name << " parent=" << bone.parent << ' ' << (bone.visible ? "visible" : "hidden");
        if (index < pose.size())
        {
            const auto& m = pose[index].values;
            std::cout << std::fixed << std::setprecision(1) << " at (" << m[12] << ", " << m[13] << ", " << m[14] << ") x-axis (" << std::setprecision(2) << m[0] << ", " << m[1] << ", " << m[2] << ")"
                      << std::defaultfloat;
        }
        std::cout << '\n';
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

} // namespace

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

} // namespace eaw::inspector
