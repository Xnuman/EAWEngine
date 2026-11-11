// Helpers shared by the workbench panels: text, DDS previews, and asset previews.

#include "assets/pose.hpp"
#include "gui_internal.hpp"

#include "assets/alo_interpretation.hpp"
#include "core/string_utils.hpp"
#include "formats/alo_model_helpers.hpp"
#include "formats/ala_animation_helpers.hpp"
#include "formats/dat_string_table_helpers.hpp"

namespace eaw::tools {
namespace {

std::uint32_t little_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    EAW_ASSERT_MSG(offset + 4 <= bytes.size(), "asset is shorter than its expected header");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

// Surfaces drawn in the color pass. Collision, shadow-volume, heat-distortion, and occluded-unit
// geometry is not visible surface and is hidden from previews (collision is opt-in).
bool preview_hidden_family(const LegacyMaterialFamily family, const bool show_collision) noexcept
{
    return (family == LegacyMaterialFamily::collision && !show_collision) || family == LegacyMaterialFamily::shadow || family == LegacyMaterialFamily::heat || family == LegacyMaterialFamily::occluded;
}

// The texture reference a preview should sample: the material's base-color binding, otherwise its
// first texture.
std::optional<std::string> preview_texture_reference(const LegacyMaterial& material)
{
    for (const auto& binding : material.textures)
    {
        if (binding.semantic == LegacyTextureSemantic::base_color)
        {
            return binding.reference;
        }
    }
    return material.textures.empty() ? std::nullopt : std::optional<std::string>{material.textures.front().reference};
}

PreviewBlend preview_blend(const LegacyMaterial& material) noexcept
{
    switch (material.blend)
    {
    case LegacyBlendMode::opaque:
        return PreviewBlend::opaque;
    case LegacyBlendMode::alpha_test:
        return PreviewBlend::alpha_test;
    case LegacyBlendMode::alpha_blend:
        return PreviewBlend::blended;
    case LegacyBlendMode::additive:
        return PreviewBlend::additive;
    }
    return PreviewBlend::opaque;
}

Vector3 animated_vertex(const AloVertex& vertex, const AloSubMesh& submesh, const std::vector<PoseBone>& pose, const std::vector<Matrix4>& inverse_bind)
{
    if (submesh.skin_bones.empty())
    {
        return vertex.position;
    }
    Vector3 result{};
    float total_weight = 0.0F;
    for (std::size_t influence = 0; influence < vertex.bone_indices.size(); ++influence)
    {
        const auto weight = vertex.bone_weights[influence];
        const auto palette_index = vertex.bone_indices[influence];
        if (weight <= 0.0F || palette_index >= submesh.skin_bones.size())
        {
            continue;
        }
        const auto bone_index = submesh.skin_bones[palette_index];
        if (bone_index >= pose.size() || bone_index >= inverse_bind.size())
        {
            continue;
        }
        const auto skin = inverse_bind[bone_index] * pose[bone_index].transform;
        const auto point = transform_point(vertex.position, skin);
        result.x += point.x * weight;
        result.y += point.y * weight;
        result.z += point.z * weight;
        total_weight += weight;
    }
    if (total_weight <= 1.0e-6F)
    {
        return vertex.position;
    }
    if (std::fabs(total_weight - 1.0F) > 1.0e-4F)
    {
        result.x /= total_weight;
        result.y /= total_weight;
        result.z /= total_weight;
    }
    return result;
}

} // namespace

std::string human_size(const std::uint64_t bytes)
{
    static constexpr std::array units{"B", "KiB", "MiB", "GiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size())
    {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << units[unit];
    return text.str();
}

Result<PreviewTexture> create_dds_texture(ID3D11Device* device, const std::span<const std::byte> bytes)
{
    if (device == nullptr)
    {
        return make_error("DirectX 11 device is unavailable");
    }
    EAW_TRY_ASSIGN(const auto texture_info, read_dds(bytes));
    if (texture_info.subresources.empty())
    {
        return make_error("DDS contains no image data");
    }
    const auto& image = texture_info.subresources.front();
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    switch (texture_info.format)
    {
    case DdsFormat::bc1_unorm:
        format = DXGI_FORMAT_BC1_UNORM;
        break;
    case DdsFormat::bc1_srgb:
        format = DXGI_FORMAT_BC1_UNORM_SRGB;
        break;
    case DdsFormat::bc2_unorm:
        format = DXGI_FORMAT_BC2_UNORM;
        break;
    case DdsFormat::bc2_srgb:
        format = DXGI_FORMAT_BC2_UNORM_SRGB;
        break;
    case DdsFormat::bc3_unorm:
        format = DXGI_FORMAT_BC3_UNORM;
        break;
    case DdsFormat::bc3_srgb:
        format = DXGI_FORMAT_BC3_UNORM_SRGB;
        break;
    case DdsFormat::bc4_unorm:
        format = DXGI_FORMAT_BC4_UNORM;
        break;
    case DdsFormat::bc4_snorm:
        format = DXGI_FORMAT_BC4_SNORM;
        break;
    case DdsFormat::bc5_unorm:
        format = DXGI_FORMAT_BC5_UNORM;
        break;
    case DdsFormat::bc5_snorm:
        format = DXGI_FORMAT_BC5_SNORM;
        break;
    case DdsFormat::rgba8_unorm:
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DdsFormat::rgba8_srgb:
        format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        break;
    case DdsFormat::bgra8_unorm:
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case DdsFormat::bgra8_srgb:
        format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        break;
    case DdsFormat::bgrx8_unorm:
        format = DXGI_FORMAT_B8G8R8X8_UNORM;
        break;
    case DdsFormat::bgrx8_srgb:
        format = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        break;
    case DdsFormat::r8_unorm:
        format = DXGI_FORMAT_R8_UNORM;
        break;
    case DdsFormat::a8_unorm:
        format = DXGI_FORMAT_A8_UNORM;
        break;
    case DdsFormat::rg8_unorm:
        format = DXGI_FORMAT_R8G8_UNORM;
        break;
    case DdsFormat::rgbx8_unorm:
    case DdsFormat::rgb8_unorm:
    case DdsFormat::bgr8_unorm:
    case DdsFormat::unknown:
        break;
    }

    std::vector<std::byte> converted_pixels;
    const void* texture_data = bytes.data() + image.offset;
    if (image.row_pitch > std::numeric_limits<std::uint32_t>::max())
    {
        return make_error("DDS row pitch exceeds DirectX 11 limits");
    }
    auto row_pitch = static_cast<std::uint32_t>(image.row_pitch);
    if (texture_info.format == DdsFormat::rgb8_unorm || texture_info.format == DdsFormat::bgr8_unorm || texture_info.format == DdsFormat::rgbx8_unorm)
    {
        if (texture_info.width > std::numeric_limits<std::uint32_t>::max() / 4U)
        {
            return make_error("DDS row pitch exceeds DirectX 11 limits");
        }
        const auto source_stride = texture_info.format == DdsFormat::rgbx8_unorm ? 4U : 3U;
        converted_pixels.resize(static_cast<std::size_t>(texture_info.width) * texture_info.height * 4U);
        for (std::uint32_t y = 0; y < texture_info.height; ++y)
        {
            for (std::uint32_t x = 0; x < texture_info.width; ++x)
            {
                const auto source = image.offset + static_cast<std::size_t>(y) * image.row_pitch + static_cast<std::size_t>(x) * source_stride;
                const auto destination = (static_cast<std::size_t>(y) * texture_info.width + x) * 4U;
                const bool bgr = texture_info.format == DdsFormat::bgr8_unorm;
                converted_pixels[destination] = bytes[source + (bgr ? 2U : 0U)];
                converted_pixels[destination + 1U] = bytes[source + 1U];
                converted_pixels[destination + 2U] = bytes[source + (bgr ? 0U : 2U)];
                converted_pixels[destination + 3U] = std::byte{0xff};
            }
        }
        texture_data = converted_pixels.data();
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        row_pitch = texture_info.width * 4U;
    }
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return make_error("DDS pixel format cannot be uploaded to DirectX 11");
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = texture_info.width;
    description.Height = texture_info.height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = texture_data;
    initial.SysMemPitch = row_pitch;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&description, &initial, &texture)))
    {
        return make_error("DirectX 11 could not create the DDS texture");
    }
    PreviewTexture result;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &result.view)))
    {
        return make_error("DirectX 11 could not create a texture view");
    }
    result.width = texture_info.width;
    result.height = texture_info.height;
    return result;
}

std::string clean_one_line(std::string value, const std::size_t maximum)
{
    std::ranges::replace(value, '\r', ' ');
    std::ranges::replace(value, '\n', ' ');
    std::ranges::replace(value, '\t', ' ');
    if (value.size() > maximum)
    {
        value.resize(maximum - 3);
        value += "...";
    }
    return value;
}

Result<AloModelData> read_model(const AssetReader& vfs, const std::string& path)
{
    EAW_TRY_ASSIGN(const auto bytes, vfs.read(path));
    return read_alo<AloModelData>(bytes);
}

Result<std::string> bytes_preview(const ResolvedAsset& asset, const std::span<const std::byte> bytes)
{
    const auto extension = ascii_lower(std::filesystem::path(asset.virtual_path).extension().string());
    std::ostringstream output;
    output << "Virtual path: " << asset.virtual_path << '\n'
           << "Source: " << asset.mount_name << '\n'
           << "Container: " << path_to_utf8(asset.container) << '\n'
           << "Size: " << human_size(asset.size) << " (" << asset.size << " bytes)\n\n";

    if (extension == ".dat")
    {
        EAW_TRY_ASSIGN(const auto dat, read_dat(bytes));
        const auto issues = dat_validate(dat);
        output << "Petroglyph localization DAT\nEntries: " << dat.entries.size() << "\nCRC sorted: " << (dat.crc_sorted ? "yes" : "no") << "\nValidation issues: " << issues.size() << "\n\nSample:\n";
        std::size_t shown = 0;
        for (const auto& entry : dat.entries)
        {
            if (shown++ == 24)
            {
                output << "...\n";
                break;
            }
            auto value = utf16_to_utf8(entry.value);
            if (!value)
            {
                return make_error("DAT entry " + entry.key + " is not valid UTF-16");
            }
            output << entry.key << " = " << clean_one_line(std::move(*value), 110) << '\n';
        }
        return output.str();
    }

    if (extension == ".dds")
    {
        EAW_TRY_ASSIGN(const auto texture, read_dds(bytes));
        output << "DirectDraw Surface\nPixel format: " << to_string(texture.format) << "\nDimensions: " << texture.width << 'x' << texture.height;
        if (texture.volume)
        {
            output << 'x' << texture.depth;
        }
        output << "\nMip levels: " << texture.mip_levels << "\nArray layers: " << texture.array_size << "\nFaces: " << texture.faces << "\nAlpha channel: " << (texture.has_alpha ? "yes" : "no")
               << "\nImage bytes: " << human_size(texture.data_size) << '\n';
        return output.str();
    }

    if (extension == ".mtd")
    {
        EAW_TRY_ASSIGN(const auto directory, read_mtd(bytes));
        output << "Petroglyph mega-texture directory\nEntries: " << directory.entries.size() << "\nAtlas extent: " << directory.atlas_width << 'x' << directory.atlas_height
               << "\nEntries with alpha: " << directory.alpha_entries << "\n\nEntries:\n";
        for (std::size_t index = 0; index < std::min<std::size_t>(directory.entries.size(), 120); ++index)
        {
            const auto& entry = directory.entries[index];
            output << "  [" << index << "] " << entry.name << "  rect=" << entry.x << ',' << entry.y << ' ' << entry.width << 'x' << entry.height << (entry.has_alpha ? "  alpha" : "") << '\n';
        }
        return output.str();
    }

    if (extension == ".alo")
    {
        const auto decoded = read_alo<AloModelData>(bytes);
        if (decoded)
        {
            const auto& model = *decoded;
            output << "Alamo semantic model\n"
                   << "Bones: " << model.bones.size() << "\nMeshes: " << model.meshes.size() << "\nSubmeshes: " << alo_submesh_count(model) << "\nVertices: " << alo_vertex_count(model)
                   << "\nTriangles: " << alo_triangle_count(model) << "\nLights: " << model.lights.size() << "\nProxies: " << model.proxies.size() << "\nDazzles: " << model.dazzles.size() << "\n";
            for (const auto& warning_text : model.warnings)
            {
                output << "Warning: " << warning_text << '\n';
            }

            output << "\nSkeleton:\n";
            for (std::size_t index = 0; index < std::min<std::size_t>(model.bones.size(), 48); ++index)
            {
                const auto& bone = model.bones[index];
                output << "  [" << index << "] " << bone.name << "  parent=" << bone.parent << "  " << (bone.visible ? "visible" : "hidden");
                if (bone.billboard != 0)
                {
                    output << "  billboard=" << bone.billboard;
                }
                output << '\n';
            }

            output << "\nMeshes and materials:\n";
            for (std::size_t mesh_index = 0; mesh_index < std::min<std::size_t>(model.meshes.size(), 64); ++mesh_index)
            {
                const auto& mesh = model.meshes[mesh_index];
                output << "  [" << mesh_index << "] " << mesh.name << "  bounds=(" << mesh.bounds.minimum.x << ", " << mesh.bounds.minimum.y << ", " << mesh.bounds.minimum.z << ")..("
                       << mesh.bounds.maximum.x << ", " << mesh.bounds.maximum.y << ", " << mesh.bounds.maximum.z << ')';
                if (mesh.attached_bone)
                {
                    output << "  bone=" << *mesh.attached_bone;
                }
                if (mesh.hidden)
                {
                    output << "  hidden";
                }
                if (mesh.collision_enabled)
                {
                    output << "  collision";
                }
                output << '\n';
                for (std::size_t submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index)
                {
                    const auto& submesh = mesh.submeshes[submesh_index];
                    const auto material = LegacyMaterial::translate(submesh);
                    output << "      material " << submesh_index << ": " << submesh.shader << "  vertices=" << submesh.vertex_count << "  triangles=" << submesh.triangle_count
                           << "  stride=" << submesh.vertex_stride << "  family=" << to_string(material.family) << "  blend=" << to_string(material.blend)
                           << "  phase=" << to_string(material.render.phase) << "  lighting=" << to_string(material.features.lighting) << "  source=" << to_string(material.source) << '\n';
                    for (const auto& parameter : submesh.parameters)
                    {
                        if (parameter.type == AloShaderParameterType::texture)
                        {
                            output << "        " << parameter.name << " = " << parameter.value << '\n';
                        }
                    }
                }
            }
            if (!model.proxies.empty())
            {
                output << "\nProxies:\n";
                for (const auto& proxy : model.proxies)
                {
                    output << "  " << proxy.name << "  bone=" << proxy.bone << (proxy.hidden ? "  hidden" : "") << '\n';
                }
            }
            return output.str();
        }
        else
        {
            output << "Semantic model decoding unavailable: " << decoded.error().message << "\nFalling back to the chunk inventory.\n\n";
        }
    }

    if (extension == ".ala")
    {
        const auto decoded = read_ala(bytes);
        if (decoded)
        {
            const auto& animation = *decoded;
            output << "Alamo semantic animation\nVersion: " << animation.version << "\nStored frames: " << animation.stored_frame_count << "\nPlayable frames: " << ala_playable_frame_count(animation)
                   << "\nFrames per second: " << animation.frames_per_second << "\nDuration: " << ala_duration_seconds(animation) << " seconds\nBone tracks: " << animation.tracks.size()
                   << "\nRotation data: " << human_size(animation.rotation_data_bytes) << "\nTranslation data: " << human_size(animation.translation_data_bytes)
                   << "\nScale data: " << human_size(animation.scale_data_bytes) << '\n';
            for (const auto& warning_text : animation.warnings)
            {
                output << "Warning: " << warning_text << '\n';
            }
            output << "\nTracks:\n";
            for (std::size_t index = 0; index < std::min<std::size_t>(animation.tracks.size(), 96); ++index)
            {
                const auto& track = animation.tracks[index];
                output << "  [" << track.bone_index << "] " << track.name << "  ";
                if (track.translation)
                {
                    output << 'T';
                }
                if (track.rotation)
                {
                    output << 'R';
                }
                if (track.scale)
                {
                    output << 'S';
                }
                if (track.visibility)
                {
                    output << " visibility";
                }
                if (track.stepped)
                {
                    output << " stepped";
                }
                output << '\n';
            }
            return output.str();
        }
        else
        {
            output << "Semantic animation decoding unavailable: " << decoded.error().message << "\nFalling back to the chunk inventory.\n\n";
        }
    }

    if (extension == ".ted")
    {
        const auto decoded = read_ted(bytes);
        if (decoded)
        {
            const auto& map = *decoded;
            output << "Alamo semantic map\nType: " << to_string(map.type) << "\nName: " << map.name << "\nPlanet: " << map.planet << "\nPlayers: " << map.players
                   << "\nWorld dimensions: " << map.world_width << " x " << map.world_height << "\nTerrain grid: " << map.terrain_width << " x " << map.terrain_height
                   << "\nElevation: " << map.elevation_sample_count << " samples (" << map.minimum_elevation << ".." << map.maximum_elevation << ")\nPassability: " << map.passability_sample_count
                   << " samples\nTerrain layers: " << map.terrain_layers.size() << "\nDecals: " << map.decal_count << "\nTracks: " << map.track_textures.size()
                   << "\nEnvironments: " << map.environments.size() << "\nObjects: " << map.object_count << " (" << map.object_types.size() << " types, "
                   << std::ranges::count_if(map.placed_objects,
                                            [](const auto& object)
                                            {
                                                return object.has_transform;
                                            })
                   << " positioned)\nZones: " << map.zone_count << "\nEmbedded preview: " << (map.has_preview ? "yes" : "no") << '\n';
            for (const auto& warning_text : map.warnings)
            {
                output << "Warning: " << warning_text << '\n';
            }
            output << "\nTerrain textures:\n";
            for (std::size_t index = 0; index < std::min<std::size_t>(map.terrain_layers.size(), 48); ++index)
            {
                const auto& layer = map.terrain_layers[index];
                output << "  [" << index << "] " << layer.color_texture;
                if (!layer.normal_texture.empty())
                {
                    output << " | " << layer.normal_texture;
                }
                output << '\n';
            }
            output << "\nEnvironments:\n";
            for (std::size_t index = 0; index < std::min<std::size_t>(map.environments.size(), 32); ++index)
            {
                const auto& environment = map.environments[index];
                output << "  [" << index << "] " << environment.name;
                if (!environment.scenario.empty())
                {
                    output << " scenario=" << environment.scenario;
                }
                output << '\n';
            }
            output << "\nObject CRC counts:\n";
            for (std::size_t index = 0; index < std::min<std::size_t>(map.object_types.size(), 64); ++index)
            {
                const auto& object = map.object_types[index];
                output << "  0x" << std::hex << std::setw(8) << std::setfill('0') << object.crc << std::dec << std::setfill(' ') << " x " << object.count << '\n';
            }
            return output.str();
        }
        else
        {
            output << "Semantic map decoding unavailable: " << decoded.error().message << "\nFalling back to the chunk inventory.\n\n";
        }
    }

    if (extension == ".alo" || extension == ".ala" || extension == ".ted")
    {
        output << "Alamo chunk container (top-level inventory)\n";
        std::size_t cursor = 0;
        std::size_t count = 0;
        while (cursor + 8 <= bytes.size() && count < 80)
        {
            const auto type = little_u32(bytes, cursor);
            const auto encoded_size = little_u32(bytes, cursor + 4);
            const auto size = encoded_size & 0x7fffffffU;
            output << '[' << count << "] type=0x" << std::hex << std::setw(8) << std::setfill('0') << type << std::dec << std::setfill(' ') << "  size=" << size << "  "
                   << ((encoded_size & 0x80000000U) != 0 ? "container" : "data") << '\n';
            if (static_cast<std::uint64_t>(cursor) + 8U + size > bytes.size())
            {
                output << "Truncated chunk body\n";
                break;
            }
            cursor += 8U + size;
            ++count;
        }
        return output.str();
    }

    if (extension == ".lua")
    {
        EAW_TRY_ASSIGN(const auto script, read_lua(bytes));
        output << to_string(script.kind) << '\n';
        if (script.kind == LuaScriptKind::source)
        {
            output << "Lines: " << script.lines << "\nDependencies: " << script.dependencies.size() << "\nDefined functions: " << script.defined_functions.size()
                   << "\nCalled functions: " << script.called_functions.size() << '\n';
        }
        else if (script.kind == LuaScriptKind::lua50_bytecode || script.kind == LuaScriptKind::petroglyph_bytecode)
        {
            output << "Version: 0x" << std::hex << static_cast<unsigned>(script.bytecode_header.version) << std::dec
                   << "\nByte order: " << (script.bytecode_header.little_endian ? "little-endian" : "big-endian") << "\nPrototypes: " << script.prototypes << "\nInstructions: " << script.instructions
                   << "\nPersistent function IDs: " << script.persistent_function_ids.size() << '\n';
        }
        for (const auto& warning_text : script.warnings)
        {
            output << "Warning: " << warning_text << '\n';
        }
        output << "\nDependencies:\n";
        for (const auto& dependency : script.dependencies)
        {
            output << "  " << dependency << '\n';
        }
        output << "\nDefined functions:\n";
        for (std::size_t index = 0; index < std::min<std::size_t>(script.defined_functions.size(), 120); ++index)
        {
            output << "  " << script.defined_functions[index] << '\n';
        }
        output << "\nCalled functions:\n";
        for (std::size_t index = 0; index < std::min<std::size_t>(script.called_functions.size(), 160); ++index)
        {
            output << "  " << script.called_functions[index] << '\n';
        }
        if (script.kind == LuaScriptKind::source)
        {
            const auto maximum = std::min<std::size_t>(bytes.size(), 64U * 1024U);
            output << "\nSource preview\n\n";
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(maximum));
            if (maximum != bytes.size())
            {
                output << "\n\n[preview truncated]";
            }
        }
        return output.str();
    }

    if (extension == ".xml" || extension == ".txt")
    {
        const auto maximum = std::min<std::size_t>(bytes.size(), 64U * 1024U);
        output << "Text preview\n\n";
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(maximum));
        if (maximum != bytes.size())
        {
            output << "\n\n[preview truncated]";
        }
        return output.str();
    }

    output << "Binary signature (first 256 bytes)\n\n";
    const auto maximum = std::min<std::size_t>(bytes.size(), 256);
    for (std::size_t index = 0; index < maximum; index += 16)
    {
        output << std::hex << std::setw(8) << std::setfill('0') << index << "  ";
        for (std::size_t column = 0; column < 16; ++column)
        {
            if (index + column < maximum)
            {
                output << std::setw(2) << std::to_integer<unsigned>(bytes[index + column]) << ' ';
            }
            else
            {
                output << "   ";
            }
        }
        output << "  ";
        for (std::size_t column = 0; column < 16 && index + column < maximum; ++column)
        {
            const auto character = std::to_integer<unsigned char>(bytes[index + column]);
            output << (std::isprint(character) ? static_cast<char>(character) : '.');
        }
        output << '\n';
    }
    return output.str();
}

void label_value(const char* label, const std::string_view value)
{
    ImGui::TextColored(muted, "%s", label);
    ImGui::SameLine(132.0F);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}

PreviewGeometry build_preview_geometry(const AloModelData& model, const std::vector<PoseBone>* pose, const std::vector<Matrix4>& inverse_bind, const bool show_collision,
                                       const std::function<ID3D11ShaderResourceView*(const std::string&)>& texture, const std::vector<bool>& mesh_visible, const std::vector<bool>& hidden_bones,
                                       const Matrix4& transform)
{
    static constexpr std::array<ImU32, 6> palette{IM_COL32(54, 177, 186, 255), IM_COL32(71, 137, 201, 255), IM_COL32(121, 101, 198, 255),
                                                  IM_COL32(55, 183, 133, 255), IM_COL32(199, 145, 62, 255), IM_COL32(185, 91, 129, 255)};
    PreviewGeometry geometry;
    const auto bind_pose = alo_bind_pose(model);
    const auto default_visible = mesh_visible.size() == model.meshes.size() ? mesh_visible : alo_mesh_visibility(model, {.apply_bind_pose_bones = pose == nullptr});
    float radius_squared = 0.0F;
    for (std::size_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
    {
        const auto& mesh = model.meshes[mesh_index];
        // With a pose, bone visibility comes from the animation; alternate states and lower detail
        // levels stay hidden either way.
        if (!default_visible[mesh_index])
        {
            continue;
        }
        if (pose != nullptr && mesh.attached_bone && *mesh.attached_bone < pose->size() && !(*pose)[*mesh.attached_bone].visible)
        {
            continue;
        }
        if (mesh.attached_bone && *mesh.attached_bone < hidden_bones.size() && hidden_bones[*mesh.attached_bone])
        {
            continue;
        }
        for (const auto& submesh : mesh.submeshes)
        {
            const auto material = LegacyMaterial::translate(submesh);
            if (preview_hidden_family(material.family, show_collision) || submesh.vertices.empty() || submesh.indices.size() < 3)
            {
                continue;
            }
            const bool collision = material.family == LegacyMaterialFamily::collision;
            ID3D11ShaderResourceView* view = nullptr;
            if (!collision)
            {
                if (const auto reference = preview_texture_reference(material))
                {
                    view = texture(*reference);
                }
            }
            const auto color = view != nullptr ? IM_COL32_WHITE : collision ? IM_COL32(224, 79, 72, 150) : palette[mesh_index % palette.size()];
            const auto base = static_cast<std::uint32_t>(geometry.vertices.size());
            const auto rigid = submesh.skin_bones.empty() && mesh.attached_bone.has_value();
            for (const auto& vertex : submesh.vertices)
            {
                Vector3 point = vertex.position;
                if (pose != nullptr)
                {
                    point = animated_vertex(vertex, submesh, *pose, inverse_bind);
                    if (rigid && *mesh.attached_bone < pose->size())
                    {
                        point = transform_point(point, (*pose)[*mesh.attached_bone].transform);
                    }
                }
                else if (rigid && *mesh.attached_bone < bind_pose.size())
                {
                    point = transform_point(point, bind_pose[*mesh.attached_bone]);
                }
                point = transform_point(point, transform);
                geometry.vertices.push_back({{point.x, point.y, point.z}, {vertex.texture_coordinates[0].x, vertex.texture_coordinates[0].y}, color});
                if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z))
                {
                    geometry.minimum = {std::min(geometry.minimum.x, point.x), std::min(geometry.minimum.y, point.y), std::min(geometry.minimum.z, point.z)};
                    geometry.maximum = {std::max(geometry.maximum.x, point.x), std::max(geometry.maximum.y, point.y), std::max(geometry.maximum.z, point.z)};
                    radius_squared = std::max(radius_squared, point.x * point.x + point.y * point.y + point.z * point.z);
                }
            }
            PreviewPart part;
            part.first_index = static_cast<std::uint32_t>(geometry.indices.size());
            part.texture = view;
            part.blend = collision ? PreviewBlend::blended : preview_blend(material);
            part.alpha_cutoff = material.render.alpha_test_threshold.value_or(0.5F);
            for (std::size_t index = 0; index + 2 < submesh.indices.size(); index += 3)
            {
                const auto a = submesh.indices[index];
                const auto b = submesh.indices[index + 1];
                const auto c = submesh.indices[index + 2];
                if (a < submesh.vertices.size() && b < submesh.vertices.size() && c < submesh.vertices.size())
                {
                    geometry.indices.insert(geometry.indices.end(), {base + a, base + b, base + c});
                }
            }
            part.index_count = static_cast<std::uint32_t>(geometry.indices.size()) - part.first_index;
            if (part.index_count > 0)
            {
                geometry.parts.push_back(part);
            }
        }
    }
    geometry.radius = std::sqrt(radius_squared);
    return geometry;
}

void append_preview_geometry(PreviewGeometry& into, const PreviewGeometry& from)
{
    const auto base = static_cast<std::uint32_t>(into.vertices.size());
    const auto first = static_cast<std::uint32_t>(into.indices.size());
    into.vertices.insert(into.vertices.end(), from.vertices.begin(), from.vertices.end());
    for (const auto index : from.indices)
    {
        into.indices.push_back(base + index);
    }
    for (auto part : from.parts)
    {
        part.first_index += first;
        into.parts.push_back(part);
    }
    if (!from.vertices.empty())
    {
        into.minimum = {std::min(into.minimum.x, from.minimum.x), std::min(into.minimum.y, from.minimum.y), std::min(into.minimum.z, from.minimum.z)};
        into.maximum = {std::max(into.maximum.x, from.maximum.x), std::max(into.maximum.y, from.maximum.y), std::max(into.maximum.z, from.maximum.z)};
        into.radius = std::max(into.radius, from.radius);
    }
}

bool draw_gpu_preview(GuiApp::State& state, PreviewTarget& target, const ImVec2 canvas_min, const ImVec2 canvas_max, const Vector3& center, const float yaw, const float pitch, const float scale,
                      const float depth_radius, const std::span<const PreviewMesh* const> meshes)
{
    auto* draw = ImGui::GetWindowDrawList();
    const auto width = static_cast<std::uint32_t>(std::max(1.0F, std::round(canvas_max.x - canvas_min.x)));
    const auto height = static_cast<std::uint32_t>(std::max(1.0F, std::round(canvas_max.y - canvas_min.y)));
    const auto view_projection = orbit_view_projection(center, yaw, pitch, scale, static_cast<float>(width), static_cast<float>(height), depth_radius);
    if (!state.renderer.render(target, width, height, view_projection, meshes, {7.0F / 255.0F, 13.0F / 255.0F, 20.0F / 255.0F, 1.0F}))
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 32.0F}, IM_COL32(230, 120, 110, 255), "GPU preview unavailable on this device");
        return false;
    }
    draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(target.view())), canvas_min, canvas_max);
    return true;
}

} // namespace eaw::tools
