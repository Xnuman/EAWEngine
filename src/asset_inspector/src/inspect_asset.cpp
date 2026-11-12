// `inspect`: one asset decoded and summarized by format.

#include "core/string_utils.hpp"
#include "formats/ala_animation_helpers.hpp"
#include "formats/alamo_chunks.hpp"
#include "formats/alo_model_helpers.hpp"
#include "formats/dat_string_table_helpers.hpp"
#include "formats/xml_helpers.hpp"
#include "asset_inspector/inspector.hpp"

#include <iomanip>

namespace eaw::asset_inspector {
namespace {

eaw::Status inspect_dat(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto dat, eaw::read_dat(bytes));
    const auto issues = dat_validate(dat);
    std::cout << "Format: Petroglyph string DAT\n"
              << "Entries: " << dat.entries.size() << '\n'
              << "CRC sorted: " << (dat.crc_sorted ? "yes" : "no") << '\n'
              << "Validation issues: " << issues.size() << '\n';
    std::size_t shown = 0;
    for (const auto& entry : dat.entries)
    {
        if (shown++ >= limit)
        {
            break;
        }
        auto converted = eaw::utf16_to_utf8(entry.value);
        if (!converted)
        {
            return eaw::make_error("DAT entry " + entry.key + " is not valid UTF-16");
        }
        auto value = std::move(*converted);
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
    EAW_TRY_ASSIGN(const auto texture, eaw::read_dds(bytes));
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
    EAW_TRY_ASSIGN(const auto directory, eaw::read_mtd(bytes));
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
    // Listed up to the first damaged chunk, so a truncated file still shows what precedes it.
    const auto listing = eaw::chunk::list_chunks(bytes, 0, bytes.size(), "Alamo");
    for (std::size_t index = 0; index < std::min(limit, listing.chunks.size()); ++index)
    {
        const auto& chunk = listing.chunks[index];
        std::cout << "  [" << index << "] type=0x" << std::hex << std::setw(8) << std::setfill('0') << chunk.type << std::dec << std::setfill(' ') << " size=" << chunk.size()
                  << " kind=" << (chunk.container ? "container" : "data") << '\n';
    }
    if (!listing.problem.empty())
    {
        std::cout << "  " << listing.problem << '\n';
    }
    std::cout << "Top-level bytes read: " << (listing.chunks.empty() ? 0U : listing.chunks.back().end) << '/' << bytes.size() << '\n';
}

eaw::Status inspect_particles(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto system, eaw::read_alo<eaw::AloParticleSystemData>(bytes));
    static constexpr std::array<const char*, 7> track_names{"red", "green", "blue", "alpha", "scale", "texture", "rotation"};
    std::cout << "Format: Alamo particle system\n"
              << "Name: " << system.name << '\n'
              << "Leave particles: " << (system.leave_particles ? "yes" : "no") << '\n'
              << "Emitters: " << system.emitters.size() << '\n';
    for (std::size_t index = 0; index < system.emitters.size() && index < limit; ++index)
    {
        const auto& emitter = system.emitters[index];
        std::cout << "  [" << index << "] " << emitter.name << " texture " << emitter.texture << " blend " << emitter.blend_mode << " lifetime " << emitter.lifetime
                  << (emitter.use_bursts ? " bursts " + std::to_string(emitter.particles_per_burst) + "x" + std::to_string(emitter.burst_count) + " every " + std::to_string(emitter.burst_delay)
                                         : " rate " + std::to_string(emitter.particles_per_second) + "/s")
                  << (emitter.has_tail ? " kite" : "") << (emitter.world_oriented ? " xy-aligned" : "") << (emitter.heat ? " heat" : "") << (emitter.weather ? " weather" : "")
                  << (emitter.emit_from_mesh != 0 ? " from-mesh " + std::to_string(emitter.emit_from_mesh) : std::string{}) << (emitter.link_to_system ? " linked" : "") << " cells "
                  << emitter.texture_cells << " groups " << emitter.groups[0].type << '/' << emitter.groups[2].type;
        if (emitter.death_child >= 0)
        {
            std::cout << " on-death " << emitter.death_child;
        }
        if (emitter.life_child >= 0)
        {
            std::cout << " during-life " << emitter.life_child;
        }
        std::cout << '\n';
        for (std::size_t track = 0; track < emitter.tracks.size(); ++track)
        {
            std::cout << "      " << track_names[track] << ':';
            for (const auto& key : emitter.tracks[track].keys)
            {
                std::cout << ' ' << key.time << '=' << key.value;
            }
            std::cout << '\n';
        }
    }
    for (const auto& warning : system.warnings)
    {
        std::cout << "Warning: " << warning << '\n';
    }
    return {};
}

eaw::Status inspect_alo(const std::span<const std::byte> bytes, const std::size_t limit)
{
    EAW_TRY_ASSIGN(const auto model, eaw::read_alo<eaw::AloModelData>(bytes));
    std::cout << "Format: Alamo semantic model\n"
              << "Bones: " << model.bones.size() << '\n'
              << "Meshes: " << model.meshes.size() << '\n'
              << "Submeshes: " << alo_submesh_count(model) << '\n'
              << "Vertices: " << alo_vertex_count(model) << '\n'
              << "Triangles: " << alo_triangle_count(model) << '\n'
              << "Lights: " << model.lights.size() << '\n'
              << "Proxies: " << model.proxies.size() << '\n'
              << "Dazzles: " << model.dazzles.size() << '\n';
    for (const auto& warning : model.warnings)
    {
        std::cout << "Warning: " << warning << '\n';
    }

    // Model-space bind pose: position and X axis (row-vector matrices: row 0 is the bone's X axis).
    std::cout << "Skeleton:\n";
    const auto pose = alo_bind_pose(model);
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
        std::cout << "  [" << index << "] " << mesh.name << " submeshes=" << mesh.submeshes.size() << (mesh.hidden ? " hidden" : "");
        if (mesh.attached_bone)
        {
            std::cout << " bone=" << *mesh.attached_bone;
        }
        std::cout << '\n';
        for (const auto& submesh : mesh.submeshes)
        {
            std::cout << "      " << submesh.shader << " vertices=" << submesh.vertex_count << " triangles=" << submesh.triangle_count << " stride=" << submesh.vertex_stride << '\n';
            // The vertices' extent in the mesh's own space (before its bone's bind transform).
            if (!submesh.vertices.empty())
            {
                auto minimum = submesh.vertices.front().position;
                auto maximum = minimum;
                for (const auto& vertex : submesh.vertices)
                {
                    minimum = {std::min(minimum.x, vertex.position.x), std::min(minimum.y, vertex.position.y), std::min(minimum.z, vertex.position.z)};
                    maximum = {std::max(maximum.x, vertex.position.x), std::max(maximum.y, vertex.position.y), std::max(maximum.z, vertex.position.z)};
                }
                std::cout << "        extent x " << minimum.x << ".." << maximum.x << " y " << minimum.y << ".." << maximum.y << " z " << minimum.z << ".." << maximum.z << '\n';
            }
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
    EAW_TRY_ASSIGN(const auto animation, eaw::read_ala(bytes));
    std::cout << "Format: Alamo semantic animation\n"
              << "Version: " << animation.version << '\n'
              << "Stored frames: " << animation.stored_frame_count << '\n'
              << "Playable frames: " << ala_playable_frame_count(animation) << '\n'
              << "Frames per second: " << animation.frames_per_second << '\n'
              << "Duration: " << ala_duration_seconds(animation) << " seconds\n"
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
    EAW_TRY_ASSIGN(const auto map, eaw::read_ted(bytes));
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
    if (xml_attribute(node, "Name") != nullptr)
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
        return ascii_is_alnum(character) || character == '_';
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
    else if (const auto generic = eaw::read_alo<eaw::AloGenericData>(bytes);
             extension == ".alo" && generic && (generic->type == eaw::AloType::particle_system || generic->type == eaw::AloType::plugin_particle_system))
    {
        if (const auto status = inspect_particles(bytes, limit); !status)
        {
            std::cout << "Particle system decoding unavailable: " << status.error().message << "\nFalling back to the chunk inventory.\n";
            inspect_chunks(bytes, limit);
        }
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
        EAW_TRY_ASSIGN(const auto document, eaw::read_xml(std::span<const std::byte>(bytes)));
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
            const auto* name = xml_attribute(*definitions[index], "Name");
            std::cout << "  " << definitions[index]->name << " Name=\"" << name->value << "\" (line " << definitions[index]->line << ")\n";
        }
    }
    else if (extension == ".lua")
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto references = localization_references(text);
        EAW_TRY_ASSIGN(const auto script, eaw::read_lua(bytes));
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

} // namespace eaw::asset_inspector
