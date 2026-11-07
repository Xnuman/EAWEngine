#include "gui_app.hpp"

#include "file_dialogs.hpp"
#include "preview_renderer.hpp"

#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/localization_scan.hpp"
#include "eaw/localization_workspace.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/lua_script.hpp"
#include "eaw/mtd_directory.hpp"
#include "eaw/text_encoding.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eaw::tools {
namespace {

constexpr ImVec4 accent{0.16F, 0.72F, 0.72F, 1.0F};
constexpr ImVec4 warning{0.96F, 0.69F, 0.24F, 1.0F};
constexpr ImVec4 danger{0.96F, 0.35F, 0.39F, 1.0F};
constexpr ImVec4 muted{0.58F, 0.64F, 0.70F, 1.0F};

struct KnownAssetExtension
{
    const char* label;
    const char* extension;
};

constexpr std::array known_asset_extensions{KnownAssetExtension{"All extensions", ""},        KnownAssetExtension{"ALO models / particles", ".alo"},
                                            KnownAssetExtension{"ALA animations", ".ala"},    KnownAssetExtension{"TED maps", ".ted"},
                                            KnownAssetExtension{"DDS textures", ".dds"},      KnownAssetExtension{"TGA textures", ".tga"},
                                            KnownAssetExtension{"MTD mega-textures", ".mtd"}, KnownAssetExtension{"DAT localization", ".dat"},
                                            KnownAssetExtension{"XML definitions", ".xml"},   KnownAssetExtension{"Lua scripts", ".lua"},
                                            KnownAssetExtension{"WAV audio", ".wav"},         KnownAssetExtension{"BIK videos", ".bik"}};

struct PreviewTexture
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    std::uint32_t width{};
    std::uint32_t height{};
};

// Terrain triangle for land-map previews, in preview space.
struct PreviewTriangle
{
    AloVector3 points[3];
    ImU32 color{};
};
std::string lower(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

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

std::uint32_t little_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    if (offset + 4 > bytes.size())
    {
        throw std::runtime_error("asset is shorter than its expected header");
    }
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

PreviewTexture create_dds_texture(ID3D11Device* device, const std::span<const std::byte> bytes)
{
    if (device == nullptr)
    {
        throw std::runtime_error("DirectX 11 device is unavailable");
    }
    const auto texture_info = DdsTexture::parse(bytes);
    if (texture_info.subresources.empty())
    {
        throw std::runtime_error("DDS contains no image data");
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
        throw std::runtime_error("DDS row pitch exceeds DirectX 11 limits");
    }
    auto row_pitch = static_cast<std::uint32_t>(image.row_pitch);
    if (texture_info.format == DdsFormat::rgb8_unorm || texture_info.format == DdsFormat::bgr8_unorm || texture_info.format == DdsFormat::rgbx8_unorm)
    {
        if (texture_info.width > std::numeric_limits<std::uint32_t>::max() / 4U)
        {
            throw std::runtime_error("DDS row pitch exceeds DirectX 11 limits");
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
        throw std::runtime_error("DDS pixel format cannot be uploaded to DirectX 11");
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
        throw std::runtime_error("DirectX 11 could not create the DDS texture");
    }
    PreviewTexture result;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &result.view)))
    {
        throw std::runtime_error("DirectX 11 could not create a texture view");
    }
    result.width = texture_info.width;
    result.height = texture_info.height;
    return result;
}

std::string clean_one_line(std::string value, const std::size_t maximum = 90)
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

std::string bytes_preview(const ResolvedAsset& asset, const std::span<const std::byte> bytes)
{
    const auto extension = lower(std::filesystem::path(asset.virtual_path).extension().string());
    std::ostringstream output;
    output << "Virtual path: " << asset.virtual_path << '\n'
           << "Source: " << asset.mount_name << '\n'
           << "Container: " << path_to_utf8(asset.container) << '\n'
           << "Size: " << human_size(asset.size) << " (" << asset.size << " bytes)\n\n";

    if (extension == ".dat")
    {
        const auto dat = DatFile::parse(bytes);
        const auto issues = dat.validate();
        output << "Petroglyph localization DAT\nEntries: " << dat.entries().size() << "\nCRC sorted: " << (dat.crc_sorted() ? "yes" : "no") << "\nValidation issues: " << issues.size()
               << "\n\nSample:\n";
        std::size_t shown = 0;
        for (const auto& entry : dat.entries())
        {
            if (shown++ == 24)
            {
                output << "...\n";
                break;
            }
            output << entry.key << " = " << clean_one_line(utf16_to_utf8(entry.value), 110) << '\n';
        }
        return output.str();
    }

    if (extension == ".dds")
    {
        const auto texture = DdsTexture::parse(bytes);
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
        const auto directory = MtdDirectory::parse(bytes);
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
        try
        {
            const auto model = AloModel::parse(bytes);
            output << "Alamo semantic model\n"
                   << "Bones: " << model.bones.size() << "\nMeshes: " << model.meshes.size() << "\nSubmeshes: " << model.submesh_count() << "\nVertices: " << model.vertex_count()
                   << "\nTriangles: " << model.triangle_count() << "\nLights: " << model.lights.size() << "\nProxies: " << model.proxies.size() << "\nDazzles: " << model.dazzles.size() << "\n";
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
        catch (const std::exception& error)
        {
            output << "Semantic model decoding unavailable: " << error.what() << "\nFalling back to the chunk inventory.\n\n";
        }
    }

    if (extension == ".ala")
    {
        try
        {
            const auto animation = AlaAnimation::parse(bytes);
            output << "Alamo semantic animation\nVersion: " << animation.version << "\nStored frames: " << animation.stored_frame_count << "\nPlayable frames: " << animation.playable_frame_count()
                   << "\nFrames per second: " << animation.frames_per_second << "\nDuration: " << animation.duration_seconds() << " seconds\nBone tracks: " << animation.tracks.size()
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
        catch (const std::exception& error)
        {
            output << "Semantic animation decoding unavailable: " << error.what() << "\nFalling back to the chunk inventory.\n\n";
        }
    }

    if (extension == ".ted")
    {
        try
        {
            const auto map = TedMap::parse(bytes);
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
        catch (const std::exception& error)
        {
            output << "Semantic map decoding unavailable: " << error.what() << "\nFalling back to the chunk inventory.\n\n";
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
        const auto script = LuaScriptInfo::inspect(bytes);
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

std::string normalize_unit_id(std::string id)
{
    for (char& character : id)
    {
        const auto byte = static_cast<unsigned char>(character);
        character = std::isalnum(byte) ? static_cast<char>(std::toupper(byte)) : '_';
    }
    auto duplicate = id.find("__");
    while (duplicate != std::string::npos)
    {
        id.replace(duplicate, 2, "_");
        duplicate = id.find("__");
    }
    while (!id.empty() && id.front() == '_')
    {
        id.erase(id.begin());
    }
    while (!id.empty() && id.back() == '_')
    {
        id.pop_back();
    }
    return id;
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

// GPU geometry for one placed-object model, shared by every instance on the current map.
struct MapModelGpu
{
    PreviewMesh mesh;
    float radius{1.0F};
    bool valid{};
};

AloVector3 animated_vertex(const AloVertex& vertex, const AloSubMesh& submesh, const std::vector<AlaPoseBone>& pose, const std::vector<AloMatrix4>& inverse_bind)
{
    if (submesh.skin_bones.empty())
    {
        return vertex.position;
    }
    AloVector3 result{};
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
        const auto skin = alo_multiply(inverse_bind[bone_index], pose[bone_index].transform);
        const auto point = alo_transform_point(vertex.position, skin);
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

// CPU geometry for a GPU preview: one part per visible submesh, positions posed by `pose` (CPU
// skinning) or, without a pose, placed in bind pose.
struct PreviewGeometry
{
    std::vector<PreviewVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<PreviewPart> parts;
    AloVector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    AloVector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    // Distance of the farthest vertex from the model origin.
    float radius{};
};

PreviewGeometry build_preview_geometry(const AloModel& model, const std::vector<AlaPoseBone>* pose, const std::vector<AloMatrix4>& inverse_bind, const bool show_collision,
                                       const std::function<ID3D11ShaderResourceView*(const std::string&)>& texture)
{
    static constexpr std::array<ImU32, 6> palette{IM_COL32(54, 177, 186, 255), IM_COL32(71, 137, 201, 255), IM_COL32(121, 101, 198, 255),
                                                  IM_COL32(55, 183, 133, 255), IM_COL32(199, 145, 62, 255), IM_COL32(185, 91, 129, 255)};
    PreviewGeometry geometry;
    const auto bind_pose = model.bind_pose();
    const auto default_visible = alo_default_mesh_visibility(model, pose == nullptr);
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
                AloVector3 point = vertex.position;
                if (pose != nullptr)
                {
                    point = animated_vertex(vertex, submesh, *pose, inverse_bind);
                    if (rigid && *mesh.attached_bone < pose->size())
                    {
                        point = alo_transform_point(point, (*pose)[*mesh.attached_bone].transform);
                    }
                }
                else if (rigid && *mesh.attached_bone < bind_pose.size())
                {
                    point = alo_transform_point(point, bind_pose[*mesh.attached_bone]);
                }
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

} // namespace

struct GuiApp::State
{
    enum class Workspace
    {
        assets,
        localization
    } workspace{Workspace::assets};

    ID3D11Device* graphics_device{};
    std::string game_root;
    GameVariant variant{GameVariant::forces_of_corruption};
    std::vector<std::filesystem::path> mods;
    VirtualFileSystem vfs;
    std::vector<ResolvedAsset> assets;
    std::string asset_query;
    std::string asset_extension;
    int selected_asset{-1};
    std::string asset_preview;
    std::vector<std::string> mount_warnings;
    std::optional<AloModel> preview_model;
    std::optional<TedMap> preview_map;
    std::vector<AloMatrix4> preview_inverse_bind;
    std::vector<ResolvedAsset> preview_animation_assets;
    std::optional<AlaAnimation> preview_animation;
    int preview_animation_index{-1};
    float preview_time{};
    float preview_yaw{-0.65F};
    float preview_pitch{0.35F};
    float preview_zoom{1.0F};
    bool preview_playing{true};
    bool preview_show_skeleton{};
    bool preview_show_collision{};
    // GPU preview: the model mesh is rebuilt when the model or collision toggle changes, and every
    // frame while an animation plays (skinning runs on the CPU).
    PreviewRenderer renderer;
    PreviewMesh model_mesh;
    PreviewTarget model_target;
    bool model_mesh_cached{};
    bool model_mesh_collision{};
    AloVector3 model_minimum{};
    AloVector3 model_maximum{};
    std::unordered_map<std::string, PreviewTexture> preview_textures;
    std::vector<std::string> preview_texture_warnings;
    float map_preview_yaw{-0.65F};
    float map_preview_pitch{0.72F};
    float map_preview_zoom{1.0F};
    float map_preview_vertical_scale{1.0F};
    int map_preview_triangle_budget{18000};
    bool map_preview_show_passability{};
    bool map_preview_show_wireframe{};
    bool map_preview_show_grid{true};
    bool map_preview_show_objects{true};
    bool map_preview_show_altitude{true};
    float map_preview_marker_scale{1.0F};
    bool map_preview_large_window{};
    bool map_preview_cached{};
    std::vector<PreviewTriangle> map_preview_triangles;
    PreviewTarget map_target;
    PreviewMesh terrain_mesh;
    bool terrain_mesh_cached{};

    // Object-type index for map previews, built lazily per mounted VFS. The catalog borrows the
    // database, so both live in one heap allocation with a stable address.
    struct ObjectIndex
    {
        XmlAssetDatabase database;
        GameObjectCatalog catalog;
    };
    std::unique_ptr<ObjectIndex> object_index;
    // Textures for map-object models persist across map selections, like the model proxies.
    std::unordered_map<std::string, PreviewTexture> object_textures;
    std::unordered_map<std::string, MapModelGpu> map_models;
    std::optional<MapObjectLayout> map_object_layout;
    std::vector<int> map_object_placement;
    std::vector<std::string> map_object_warnings;
    bool map_object_cached{};
    std::vector<char> map_object_drawn;

    void reset_map_objects()
    {
        map_object_layout.reset();
        map_object_placement.clear();
        map_object_warnings.clear();
        map_object_drawn.clear();
        map_object_cached = false;
    }

    void load_map_objects()
    {
        reset_map_objects();
        if (!preview_map)
        {
            return;
        }
        if (!object_index)
        {
            auto index = std::make_unique<ObjectIndex>();
            index->database = XmlAssetDatabase::build(vfs);
            index->catalog = GameObjectCatalog::build(index->database);
            object_index = std::move(index);
        }
        map_object_layout = build_map_object_layout(*preview_map, object_index->catalog, vfs);
        map_object_placement.assign(preview_map->placed_objects.size(), -1);
        for (std::size_t index = 0; index < map_object_layout->placements.size(); ++index)
        {
            const auto& placement = map_object_layout->placements[index];
            map_object_placement[placement.object_index] = static_cast<int>(index);
            if (placement.model_path.empty() || map_models.contains(placement.model_path))
            {
                continue;
            }
            auto& gpu = map_models[placement.model_path];
            try
            {
                const auto geometry = build_preview_geometry(AloModel::parse(vfs.read(placement.model_path)), nullptr, {}, false,
                                                             [this](const std::string& reference)
                                                             {
                                                                 return load_texture(object_textures, map_object_warnings, reference);
                                                             });
                gpu.radius = std::max(geometry.radius, 1.0F);
                gpu.valid = renderer.ready() && !geometry.indices.empty() && gpu.mesh.upload(renderer.device(), renderer.context(), geometry.vertices, geometry.indices, geometry.parts);
            }
            catch (const std::exception& error)
            {
                // Particle ALOs and malformed models fall back to a marker.
                map_object_warnings.push_back(placement.model_path + ": " + error.what());
            }
        }
    }

    std::optional<std::filesystem::path> base_dat_path;
    std::optional<std::filesystem::path> workspace_path;
    std::vector<LocalizationRow> rows;
    std::string row_query;
    std::string status_filter{"all"};
    int selected_row{-1};
    bool dirty{};
    bool skip_todo{};
    std::vector<std::string> validation_messages;

    std::string new_key;
    std::string unit_id;
    std::string unit_name;
    std::string unit_description;

    std::string message{"Ready"};
    bool message_error{};

    explicit State(ID3D11Device* device) : graphics_device(device)
    {
        // Previews fall back to a message when the GPU preview pipeline is unavailable.
        static_cast<void>(renderer.initialize(device));
        try
        {
            const auto installations = InstallationLocator{}.discover();
            if (!installations.empty())
            {
                game_root = path_to_utf8(installations.front());
                message = "Detected Empire at War installation";
            }
            else
            {
                game_root = "D:\\SteamLibrary\\steamapps\\common\\Star Wars Empire at War";
                message = "Choose or verify the game installation folder";
            }
        }
        catch (const std::exception& error)
        {
            message = error.what();
            message_error = true;
        }
    }

    void succeed(std::string text)
    {
        message = std::move(text);
        message_error = false;
    }

    void fail(const std::exception& error)
    {
        message = error.what();
        message_error = true;
    }

    void load_assets()
    {
        const auto profile = make_game_profile(utf8_to_path(game_root), variant);
        if (!profile)
        {
            throw std::runtime_error("The selected folder is not an Empire at War installation");
        }
        VirtualFileSystem next;
        const auto report = mount_game_assets(next, *profile, mods);
        auto listed = next.list("Data");
        std::ranges::sort(listed, {}, &ResolvedAsset::virtual_path);
        vfs = std::move(next);
        assets = std::move(listed);
        mount_warnings = report.warnings;
        selected_asset = -1;
        asset_preview.clear();
        preview_model.reset();
        preview_map.reset();
        preview_inverse_bind.clear();
        preview_animation_assets.clear();
        preview_animation.reset();
        preview_animation_index = -1;
        preview_time = 0.0F;
        model_mesh_cached = false;
        preview_textures.clear();
        preview_texture_warnings.clear();
        map_preview_triangles.clear();
        map_preview_cached = false;
        map_preview_large_window = false;
        object_index.reset();
        map_models.clear();
        object_textures.clear();
        reset_map_objects();
        succeed("Loaded " + std::to_string(assets.size()) + " logical assets from " + std::to_string(vfs.mounts().size()) + " layers");
    }

    void select_preview_animation(const int index)
    {
        preview_animation_index = index;
        preview_animation.reset();
        preview_time = 0.0F;
        if (index < 0)
        {
            return;
        }
        if (static_cast<std::size_t>(index) >= preview_animation_assets.size())
        {
            throw std::runtime_error("The selected animation is no longer available");
        }
        const auto& asset = preview_animation_assets[static_cast<std::size_t>(index)];
        const auto bytes = vfs.read(asset.virtual_path);
        preview_animation = AlaAnimation::parse(bytes);
        succeed("Loaded animation " + asset.virtual_path);
    }

    void inspect_asset(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= assets.size())
        {
            return;
        }
        selected_asset = index;
        const auto& asset = assets[static_cast<std::size_t>(index)];
        const auto bytes = vfs.read(asset.virtual_path);
        asset_preview = bytes_preview(asset, bytes);
        preview_model.reset();
        preview_map.reset();
        preview_inverse_bind.clear();
        preview_animation_assets.clear();
        preview_animation.reset();
        preview_animation_index = -1;
        preview_time = 0.0F;
        model_mesh_cached = false;
        preview_textures.clear();
        preview_texture_warnings.clear();
        map_preview_triangles.clear();
        map_preview_cached = false;
        reset_map_objects();
        terrain_mesh_cached = false;

        const auto selected_path = std::filesystem::path(asset.virtual_path);
        const auto extension = lower(selected_path.extension().string());
        if (extension != ".ted")
        {
            map_preview_large_window = false;
        }
        if (extension == ".ted")
        {
            try
            {
                preview_map = TedMap::parse(bytes);
            }
            catch (const std::exception&)
            {
                return;
            }
            try
            {
                load_map_objects();
            }
            catch (const std::exception& error)
            {
                reset_map_objects();
                map_object_warnings.push_back(std::string("Placed-object models unavailable: ") + error.what());
            }
            return;
        }
        if (extension != ".alo")
        {
            return;
        }
        try
        {
            preview_model = AloModel::parse(bytes);
        }
        catch (const std::exception&)
        {
            return;
        }
        const auto bind_pose = preview_model->bind_pose();
        preview_inverse_bind.reserve(bind_pose.size());
        for (const auto& transform : bind_pose)
        {
            preview_inverse_bind.push_back(alo_inverse(transform));
        }

        const auto directory = lower(selected_path.parent_path().generic_string());
        const auto stem = lower(selected_path.stem().string());
        for (const auto& candidate : assets)
        {
            const auto candidate_path = std::filesystem::path(candidate.virtual_path);
            if (lower(candidate_path.extension().string()) != ".ala" || lower(candidate_path.parent_path().generic_string()) != directory)
            {
                continue;
            }
            const auto candidate_stem = lower(candidate_path.stem().string());
            if (candidate_stem == stem || candidate_stem.starts_with(stem + "_"))
            {
                preview_animation_assets.push_back(candidate);
            }
        }

        for (const auto& mesh : preview_model->meshes)
        {
            for (const auto& submesh : mesh.submeshes)
            {
                for (const auto& parameter : submesh.parameters)
                {
                    if (parameter.type == AloShaderParameterType::texture && !parameter.value.empty())
                    {
                        static_cast<void>(load_texture(preview_textures, preview_texture_warnings, parameter.value));
                    }
                }
            }
        }
    }

    // Loads the DDS for an ALO texture reference into `cache` (keyed by folded reference) and
    // returns its view, or null when it is missing or undecodable. Failures are cached too.
    ID3D11ShaderResourceView* load_texture(std::unordered_map<std::string, PreviewTexture>& cache, std::vector<std::string>& warnings, const std::string& reference)
    {
        const auto key = lower(reference);
        if (const auto found = cache.find(key); found != cache.end())
        {
            return found->second.view.Get();
        }
        auto& entry = cache[key];
        auto filename = std::filesystem::path(reference).filename();
        filename.replace_extension(".DDS");
        const std::array candidates{std::string("DATA/ART/TEXTURES/") + filename.generic_string(), std::string("DATA/ART/") + filename.generic_string(), filename.generic_string()};
        for (const auto& virtual_path : candidates)
        {
            if (!vfs.resolve(virtual_path))
            {
                continue;
            }
            try
            {
                entry = create_dds_texture(graphics_device, vfs.read(virtual_path));
            }
            catch (const std::exception& error)
            {
                warnings.push_back(reference + ": " + error.what());
            }
            return entry.view.Get();
        }
        warnings.push_back(reference + ": matching DDS not found");
        return nullptr;
    }

    void open_dat(const std::filesystem::path& path)
    {
        const auto dat = DatFile::load(path);
        std::vector<LocalizationRow> loaded;
        loaded.reserve(dat.entries().size());
        for (const auto& entry : dat.entries())
        {
            loaded.push_back({entry.key, utf16_to_utf8(entry.value), {}, "existing", {}, {}});
        }
        std::ranges::sort(loaded, {}, &LocalizationRow::key);
        base_dat_path = path;
        workspace_path.reset();
        rows = std::move(loaded);
        selected_row = rows.empty() ? -1 : 0;
        dirty = false;
        validation_messages.clear();
        succeed("Opened " + std::to_string(rows.size()) + " localization entries");
    }

    void open_workspace(const std::filesystem::path& path)
    {
        rows = read_localization_workspace(path);
        workspace_path = path;
        selected_row = rows.empty() ? -1 : 0;
        dirty = false;
        validation_messages.clear();
        succeed("Opened workspace with " + std::to_string(rows.size()) + " rows");
    }

    void save_workspace(const std::filesystem::path& path)
    {
        write_localization_workspace(path, rows, true);
        workspace_path = path;
        dirty = false;
        succeed("Saved localization workspace");
    }

    void validate_workspace()
    {
        validation_messages.clear();
        std::unordered_set<std::string> keys;
        for (const auto& row : rows)
        {
            if (row.key.empty())
            {
                validation_messages.emplace_back("Error: a row has an empty key");
            }
            else if (!keys.insert(lower(row.key)).second)
            {
                validation_messages.push_back(std::string(lower(row.key) == "text_end_of_data" ? "Warning: " : "Error: ") + "duplicate key " + row.key);
            }
            if (lower(row.status) == "todo")
            {
                if (row.text.empty())
                {
                    validation_messages.push_back("TODO: " + row.key);
                }
                else
                {
                    validation_messages.push_back("Warning: " + row.key + " has text but is still marked TODO; its value will be included");
                }
            }
        }
        if (base_dat_path)
        {
            try
            {
                auto candidate = DatFile::load(*base_dat_path);
                apply_localization_workspace(candidate, rows, skip_todo);
                for (const auto& issue : candidate.validate())
                {
                    validation_messages.push_back(std::string(issue.severity == DatIssueSeverity::error ? "Error: " : "Warning: ") + issue.message);
                }
            }
            catch (const std::exception& error)
            {
                validation_messages.push_back(std::string("Error: ") + error.what());
            }
        }
        const auto errors = std::ranges::count_if(validation_messages,
                                                  [](const auto& item)
                                                  {
                                                      return item.starts_with("Error:");
                                                  });
        succeed(errors == 0 ? "Validation completed without errors" : "Validation found " + std::to_string(errors) + " errors");
        message_error = errors != 0;
    }

    void build_dat(const std::filesystem::path& output)
    {
        if (!base_dat_path)
        {
            throw std::runtime_error("Open a baseline DAT before building an output DAT");
        }
        std::error_code path_error;
        const auto baseline = std::filesystem::weakly_canonical(*base_dat_path, path_error);
        path_error.clear();
        const auto destination = std::filesystem::weakly_canonical(output, path_error);
        if (lower(path_to_utf8(baseline)) == lower(path_to_utf8(destination)))
        {
            throw std::runtime_error("Choose a different output path; EAWTools will not overwrite the baseline DAT");
        }
        auto dat = DatFile::load(*base_dat_path);
        apply_localization_workspace(dat, rows, skip_todo);
        const auto issues = dat.validate();
        const auto errors = std::ranges::count_if(issues,
                                                  [](const auto& issue)
                                                  {
                                                      return issue.severity == DatIssueSeverity::error;
                                                  });
        if (errors != 0)
        {
            throw std::runtime_error("The built DAT has " + std::to_string(errors) + " validation errors");
        }
        dat.save(output, true);
        succeed("Built " + path_to_utf8(output) + " with " + std::to_string(dat.entries().size()) + " entries");
    }

    void scan_mod(const std::filesystem::path& mod_root)
    {
        if (!base_dat_path)
        {
            throw std::runtime_error("Open a baseline DAT before scanning a mod");
        }
        const auto baseline = DatFile::load(*base_dat_path);
        const auto result = scan_mod_localization(mod_root, baseline);
        std::unordered_set<std::string> existing;
        for (const auto& row : rows)
        {
            existing.insert(lower(row.key));
        }
        std::size_t added = 0;
        for (const auto& row : result.missing)
        {
            if (existing.insert(lower(row.key)).second)
            {
                rows.push_back(row);
                ++added;
            }
        }
        std::ranges::sort(rows,
                          [](const auto& left, const auto& right)
                          {
                              return std::tie(left.group, left.key) < std::tie(right.group, right.key);
                          });
        dirty = dirty || added != 0;
        selected_row = rows.empty() ? -1 : 0;
        succeed("Scanned " + std::to_string(result.reference_count) + " references and added " + std::to_string(added) + " missing keys");
    }

    void add_localization_row(LocalizationRow row)
    {
        if (row.key.empty())
        {
            throw std::runtime_error("Localization key cannot be empty");
        }
        if (std::ranges::any_of(rows,
                                [&](const auto& existing)
                                {
                                    return lower(existing.key) == lower(row.key);
                                }))
        {
            throw std::runtime_error("The workspace already contains " + row.key);
        }
        rows.push_back(std::move(row));
        selected_row = static_cast<int>(rows.size() - 1);
        dirty = true;
    }
};

GuiApp::GuiApp(ID3D11Device* device) : state_(std::make_unique<State>(device))
{}
GuiApp::~GuiApp() = default;

namespace {

void render_status_bar(GuiApp::State& state)
{
    ImGui::Separator();
    ImGui::TextColored(state.message_error ? danger : accent, "%s", state.message_error ? "!" : "●");
    ImGui::SameLine();
    ImGui::TextUnformatted(state.message.c_str());
}

std::vector<AlaPoseBone> preview_pose(const GuiApp::State& state)
{
    if (!state.preview_model)
    {
        return {};
    }
    if (state.preview_animation)
    {
        return state.preview_animation->sample_pose(*state.preview_model, state.preview_time);
    }
    const auto transforms = state.preview_model->bind_pose();
    std::vector<AlaPoseBone> pose;
    pose.reserve(transforms.size());
    for (std::size_t index = 0; index < transforms.size(); ++index)
    {
        pose.push_back({transforms[index], state.preview_model->bones[index].visible});
    }
    return pose;
}

ImU32 terrain_height_color(const float height)
{
    struct Stop
    {
        float position;
        std::array<float, 3> color;
    };
    static constexpr std::array stops{Stop{0.00F, {25.0F, 70.0F, 82.0F}},   Stop{0.18F, {38.0F, 105.0F, 83.0F}},   Stop{0.48F, {84.0F, 132.0F, 72.0F}},
                                      Stop{0.72F, {137.0F, 112.0F, 70.0F}}, Stop{0.90F, {151.0F, 143.0F, 120.0F}}, Stop{1.00F, {219.0F, 224.0F, 218.0F}}};
    const auto clamped = std::clamp(height, 0.0F, 1.0F);
    std::size_t upper = 1;
    while (upper + 1 < stops.size() && clamped > stops[upper].position)
    {
        ++upper;
    }
    const auto& left = stops[upper - 1];
    const auto& right = stops[upper];
    const auto mix = (clamped - left.position) / std::max(right.position - left.position, 0.0001F);
    const auto channel = [&](const std::size_t index)
    {
        return static_cast<int>(std::lerp(left.color[index], right.color[index], mix));
    };
    return IM_COL32(channel(0), channel(1), channel(2), 255);
}

ImU32 passability_color(const std::uint32_t value)
{
    static constexpr std::array<ImU32, 8> palette{IM_COL32(48, 170, 117, 255),  IM_COL32(225, 87, 75, 255),  IM_COL32(228, 164, 62, 255),  IM_COL32(76, 140, 207, 255),
                                                  IM_COL32(151, 101, 203, 255), IM_COL32(62, 183, 190, 255), IM_COL32(205, 102, 154, 255), IM_COL32(154, 158, 165, 255)};
    const auto mixed = value ^ (value >> 8U) ^ (value >> 16U) ^ (value >> 24U);
    return palette[mixed % palette.size()];
}

ImU32 object_player_color(const std::uint32_t player)
{
    static constexpr std::array<ImU32, 8> palette{IM_COL32(188, 199, 207, 255), IM_COL32(70, 156, 222, 255),  IM_COL32(228, 88, 76, 255),  IM_COL32(235, 179, 67, 255),
                                                  IM_COL32(112, 195, 130, 255), IM_COL32(166, 111, 215, 255), IM_COL32(67, 190, 194, 255), IM_COL32(219, 116, 175, 255)};
    return palette[player % palette.size()];
}

float object_marker_radius(const TedPlacedObject& object, const float scale)
{
    return std::clamp(std::sqrt(std::max(std::fabs(object.scale), 0.1F)), 0.75F, 2.25F) * 3.5F * scale;
}

void map_object_tooltip(const GuiApp::State& state, const std::size_t object_index)
{
    const auto& object = state.preview_map->placed_objects[object_index];
    ImGui::BeginTooltip();
    const MapObjectPlacement* placement = nullptr;
    if (state.map_object_layout && object_index < state.map_object_placement.size() && state.map_object_placement[object_index] >= 0)
    {
        placement = &state.map_object_layout->placements[static_cast<std::size_t>(state.map_object_placement[object_index])];
    }
    if (placement != nullptr && placement->type != nullptr)
    {
        ImGui::Text("%s  (%s)", placement->type->name.c_str(), placement->type->type.c_str());
        ImGui::TextColored(muted, "%s:%zu", placement->type->source_path.c_str(), placement->type->line);
        if (!placement->model_path.empty())
        {
            ImGui::Text("Model: %s", placement->model_path.c_str());
        }
        else if (!placement->model_reference.empty())
        {
            ImGui::Text("Model: %s (not found)", placement->model_reference.c_str());
        }
        else
        {
            ImGui::TextUnformatted("Model: none for this map type");
        }
    }
    else if (state.map_object_layout)
    {
        ImGui::TextUnformatted("Unresolved object type");
    }
    ImGui::Text("Type CRC: 0x%08X", object.crc);
    ImGui::Text("Instance: %u  |  Player code: %u", object.instance_id, object.player);
    ImGui::Text("Position: %.1f, %.1f, %.1f", object.position.x, object.position.y, object.position.z);
    ImGui::Text("Rotation: %.1f, %.1f, %.1f  |  Scale: %.2f", object.rotation.x, object.rotation.y, object.rotation.z, object.scale);
    ImGui::EndTooltip();
}

// Bounding radius of a placed object's model in world units, or zero without a drawn model.
float map_object_radius(const GuiApp::State& state, const std::size_t object_index)
{
    if (!state.map_object_layout || object_index >= state.map_object_placement.size() || state.map_object_placement[object_index] < 0)
    {
        return 0.0F;
    }
    const auto& placement = state.map_object_layout->placements[static_cast<std::size_t>(state.map_object_placement[object_index])];
    const auto found = state.map_models.find(placement.model_path);
    if (found == state.map_models.end() || !found->second.valid)
    {
        return 0.0F;
    }
    return found->second.radius * std::fabs(state.preview_map->placed_objects[object_index].scale);
}

void render_map_object_status(const GuiApp::State& state)
{
    if (!state.map_object_layout)
    {
        for (const auto& message : state.map_object_warnings)
        {
            ImGui::TextColored(danger, "%s", message.c_str());
        }
        return;
    }
    const auto& layout = *state.map_object_layout;
    const auto drawn = static_cast<std::size_t>(std::ranges::count(state.map_object_drawn, char{1}));
    ImGui::TextColored(muted, "%zu objects drawn as models  |  %zu untyped  |  %zu without model  |  %zu missing model files", drawn, layout.untyped, layout.without_model, layout.missing_model_files);
    if (!state.map_object_warnings.empty() && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        for (std::size_t index = 0; index < std::min<std::size_t>(state.map_object_warnings.size(), 12); ++index)
        {
            ImGui::TextUnformatted(state.map_object_warnings[index].c_str());
        }
        ImGui::EndTooltip();
    }
}

std::vector<PreviewTriangle> map_preview_triangles(const TedMap& map, const int triangle_budget, const float vertical_scale, const bool show_passability)
{
    std::vector<PreviewTriangle> triangles;
    const auto width = map.terrain_width;
    const auto height = map.terrain_height;
    const auto expected = static_cast<std::size_t>(width) * height;
    if (width < 2 || height < 2 || map.elevation_map.size() < expected)
    {
        return triangles;
    }

    const auto budget = static_cast<std::size_t>(std::max(triangle_budget, 2));
    std::uint32_t stride = 1;
    const auto cell_count = [&](const std::uint32_t step)
    {
        const auto columns = (static_cast<std::size_t>(width - 1U) + step - 1U) / step;
        const auto rows = (static_cast<std::size_t>(height - 1U) + step - 1U) / step;
        return columns * rows * 2U;
    };
    while (cell_count(stride) > budget)
    {
        ++stride;
    }
    triangles.reserve(cell_count(stride));

    const auto elevation_range = std::max(1.0F, static_cast<float>(map.maximum_elevation - map.minimum_elevation));
    const auto horizontal_extent = static_cast<float>(std::max(width - 1U, height - 1U));
    const auto vertical_extent = horizontal_extent * 0.36F * vertical_scale;
    const auto center_x = static_cast<float>(width - 1U) * 0.5F;
    const auto center_y = static_cast<float>(height - 1U) * 0.5F;
    const auto point = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto index = static_cast<std::size_t>(y) * width + x;
        const auto normalized = (static_cast<float>(map.elevation_map[index]) - map.minimum_elevation) / elevation_range;
        return AloVector3{static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, (normalized - 0.5F) * vertical_extent};
    };
    const auto color = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto index = static_cast<std::size_t>(y) * width + x;
        if (show_passability && index < map.passability_map.size())
        {
            return passability_color(map.passability_map[index]);
        }
        const auto normalized = (static_cast<float>(map.elevation_map[index]) - map.minimum_elevation) / elevation_range;
        return terrain_height_color(normalized);
    };

    for (std::uint32_t y = 0; y + 1U < height; y += stride)
    {
        const auto next_y = std::min(y + stride, height - 1U);
        for (std::uint32_t x = 0; x + 1U < width; x += stride)
        {
            const auto next_x = std::min(x + stride, width - 1U);
            const auto a = point(x, y);
            const auto b = point(next_x, y);
            const auto c = point(next_x, next_y);
            const auto d = point(x, next_y);
            const auto first_x = (x + 2U * next_x) / 3U;
            const auto first_y = (2U * y + next_y) / 3U;
            const auto second_x = (2U * x + next_x) / 3U;
            const auto second_y = (y + 2U * next_y) / 3U;
            triangles.push_back({{a, b, c}, color(first_x, first_y)});
            triangles.push_back({{a, c, d}, color(second_x, second_y)});
        }
    }
    return triangles;
}

// Uploads per-model instance lists for the current map. `anchor` maps an object position into
// preview space and `unit_scale` converts model/world units to preview units (1 for space maps,
// terrain samples per world unit for land maps).
void update_map_instances(GuiApp::State& state, const std::function<AloVector3(const TedPlacedObject&)>& anchor, const float unit_scale)
{
    const auto& map = *state.preview_map;
    state.map_object_drawn.assign(map.placed_objects.size(), 0);
    std::unordered_map<std::string, std::vector<PreviewInstance>> instances;
    if (state.map_object_layout)
    {
        auto scale = alo_identity_matrix();
        scale.values[0] = scale.values[5] = scale.values[10] = unit_scale;
        for (const auto& placement : state.map_object_layout->placements)
        {
            const auto found = state.map_models.find(placement.model_path);
            if (placement.model_path.empty() || found == state.map_models.end() || !found->second.valid)
            {
                continue;
            }
            const auto& object = map.placed_objects[placement.object_index];
            auto transform = alo_multiply(placement.transform, scale);
            const auto origin = anchor(object);
            transform.values[12] = origin.x;
            transform.values[13] = origin.y;
            transform.values[14] = origin.z;
            PreviewInstance instance;
            std::ranges::copy(transform.values, instance.transform.begin());
            instances[placement.model_path].push_back(instance);
            state.map_object_drawn[placement.object_index] = 1;
        }
    }
    for (auto& [path, model] : state.map_models)
    {
        if (!model.valid)
        {
            continue;
        }
        const auto found = instances.find(path);
        const auto list = found == instances.end() ? std::span<const PreviewInstance>{} : std::span<const PreviewInstance>{found->second};
        static_cast<void>(model.mesh.set_instances(state.renderer.device(), state.renderer.context(), list));
    }
    state.map_object_cached = true;
}

std::vector<const PreviewMesh*> map_object_meshes(const GuiApp::State& state)
{
    std::vector<const PreviewMesh*> meshes;
    for (const auto& [path, model] : state.map_models)
    {
        if (model.valid && model.mesh.ready())
        {
            meshes.push_back(&model.mesh);
        }
    }
    return meshes;
}

// Renders `meshes` into `target` and shows it over the canvas. Returns false (after drawing a
// message) when the GPU preview pipeline is unavailable.
bool draw_gpu_preview(GuiApp::State& state, PreviewTarget& target, const ImVec2 canvas_min, const ImVec2 canvas_max, const AloVector3& center, const float yaw, const float pitch, const float scale,
                      const float depth_radius, const std::span<const PreviewMesh* const> meshes)
{
    auto* draw = ImGui::GetWindowDrawList();
    const auto width = static_cast<std::uint32_t>(std::max(1.0F, std::round(canvas_max.x - canvas_min.x)));
    const auto height = static_cast<std::uint32_t>(std::max(1.0F, std::round(canvas_max.y - canvas_min.y)));
    const auto view_projection = orbit_view_projection({center.x, center.y, center.z}, yaw, pitch, scale, static_cast<float>(width), static_cast<float>(height), depth_radius);
    if (!state.renderer.render(target, width, height, view_projection, meshes, {7.0F / 255.0F, 13.0F / 255.0F, 20.0F / 255.0F, 1.0F}))
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 32.0F}, IM_COL32(230, 120, 110, 255), "GPU preview unavailable on this device");
        return false;
    }
    draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(target.view())), canvas_min, canvas_max);
    return true;
}

void render_space_map_viewport(GuiApp::State& state, const bool reserve_footer)
{
    if (!state.preview_map)
    {
        return;
    }
    const auto& map = *state.preview_map;
    std::vector<std::size_t> positioned;
    positioned.reserve(map.placed_objects.size());
    for (std::size_t index = 0; index < map.placed_objects.size(); ++index)
    {
        if (map.placed_objects[index].has_transform)
        {
            positioned.push_back(index);
        }
    }

    ImGui::Checkbox("Map bounds", &state.map_preview_show_grid);
    ImGui::SameLine();
    ImGui::Checkbox("Objects", &state.map_preview_show_objects);
    ImGui::SameLine();
    if (ImGui::Checkbox("3D altitude", &state.map_preview_show_altitude))
    {
        state.map_object_cached = false;
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("##space-marker-scale", &state.map_preview_marker_scale, 0.5F, 2.5F, "Marker size: %.2fx");
    if (!state.map_object_cached)
    {
        const auto altitude = state.map_preview_show_altitude;
        update_map_instances(
            state,
            [altitude](const TedPlacedObject& object)
            {
                return AloVector3{object.position.x, object.position.y, altitude ? object.position.z : 0.0F};
            },
            1.0F);
    }
    render_map_object_status(state);
    ImGui::TextColored(muted, "%zu positioned objects  |  %zu types  |  %.0f x %.0f bounds", positioned.size(), map.object_types.size(), map.world_width, map.world_height);

    auto canvas_size = ImGui::GetContentRegionAvail();
    if (reserve_footer)
    {
        canvas_size.y -= ImGui::GetFrameHeightWithSpacing();
    }
    canvas_size.x = std::max(canvas_size.x, 120.0F);
    canvas_size.y = std::max(canvas_size.y, 260.0F);
    ImGui::InvisibleButton("space-map-canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const auto canvas_min = ImGui::GetItemRectMin();
    const auto canvas_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        state.map_preview_yaw += ImGui::GetIO().MouseDelta.x * 0.012F;
        state.map_preview_pitch = std::clamp(state.map_preview_pitch - ImGui::GetIO().MouseDelta.y * 0.012F, -1.45F, 1.45F);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
    {
        state.map_preview_zoom = std::clamp(state.map_preview_zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel), 0.15F, 12.0F);
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(7, 13, 20, 255), 6.0F);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(40, 78, 91, 255), 6.0F);
    if (positioned.empty())
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 14.0F}, IM_COL32(150, 164, 174, 255), "No positioned objects were decoded from this space map");
        return;
    }

    AloVector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    AloVector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    const auto preview_point = [&](const TedPlacedObject& object)
    {
        return AloVector3{object.position.x, object.position.y, state.map_preview_show_altitude ? object.position.z : 0.0F};
    };
    for (const auto index : positioned)
    {
        const auto point = preview_point(map.placed_objects[index]);
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
    }
    if (map.world_width > 0.0F && map.world_height > 0.0F)
    {
        minimum.x = std::min(minimum.x, map.world_width * -0.5F);
        maximum.x = std::max(maximum.x, map.world_width * 0.5F);
        minimum.y = std::min(minimum.y, map.world_height * -0.5F);
        maximum.y = std::max(maximum.y, map.world_height * 0.5F);
    }
    minimum.z = std::min(minimum.z, 0.0F);
    maximum.z = std::max(maximum.z, 0.0F);
    const AloVector3 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F};
    const auto extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 0.001F});
    const auto scale = std::min(canvas_size.x, canvas_size.y) * 0.78F / extent * state.map_preview_zoom;
    const auto screen_center = ImVec2{(canvas_min.x + canvas_max.x) * 0.5F, (canvas_min.y + canvas_max.y) * 0.5F};
    const auto yaw_cos = std::cos(state.map_preview_yaw);
    const auto yaw_sin = std::sin(state.map_preview_yaw);
    const auto pitch_cos = std::cos(state.map_preview_pitch);
    const auto pitch_sin = std::sin(state.map_preview_pitch);
    const auto project = [&](const AloVector3& point, float* depth = nullptr)
    {
        const auto x = point.x - center.x;
        const auto y = point.y - center.y;
        const auto z = point.z - center.z;
        const auto horizontal = yaw_cos * x + yaw_sin * y;
        const auto forward = -yaw_sin * x + yaw_cos * y;
        const auto vertical = -pitch_sin * forward + pitch_cos * z;
        if (depth != nullptr)
        {
            *depth = pitch_cos * forward + pitch_sin * z;
        }
        return ImVec2{screen_center.x + horizontal * scale, screen_center.y - vertical * scale};
    };

    float largest_model = 0.0F;
    for (const auto& [path, model] : state.map_models)
    {
        largest_model = std::max(largest_model, model.radius);
    }
    std::vector<const PreviewMesh*> meshes;
    if (state.map_preview_show_objects)
    {
        meshes = map_object_meshes(state);
    }
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw_gpu_preview(state, state.map_target, canvas_min, canvas_max, center, state.map_preview_yaw, state.map_preview_pitch, scale, extent * 1.5F + largest_model * 4.0F, meshes);

    struct SpaceMarker
    {
        ImVec2 point;
        ImVec2 ground;
        float depth{};
        float radius{};
        ImU32 color{};
        ImU32 outline{};
        std::size_t object_index{};
        bool visible{true};
    };
    std::vector<SpaceMarker> markers;
    const auto marker_stride = std::max<std::size_t>(1, (positioned.size() + 5999U) / 6000U);
    markers.reserve((positioned.size() + marker_stride - 1U) / marker_stride);
    for (std::size_t shown = 0; state.map_preview_show_objects && shown < positioned.size(); shown += marker_stride)
    {
        const auto object_index = positioned[shown];
        const auto& object = map.placed_objects[object_index];
        const auto point = preview_point(object);
        SpaceMarker marker;
        marker.point = project(point, &marker.depth);
        marker.ground = project({point.x, point.y, 0.0F});
        marker.radius = object_marker_radius(object, state.map_preview_marker_scale);
        marker.color = passability_color(object.crc);
        marker.outline = object_player_color(object.player);
        marker.object_index = object_index;
        // Objects drawn as models keep an invisible hover target sized to their projected bounds.
        if (object_index < state.map_object_drawn.size() && state.map_object_drawn[object_index] != 0)
        {
            marker.visible = false;
            marker.radius = std::max(marker.radius, map_object_radius(state, object_index) * scale);
        }
        markers.push_back(marker);
    }
    std::ranges::sort(markers, {}, &SpaceMarker::depth);

    if (state.map_preview_show_grid)
    {
        const auto grid_color = IM_COL32(72, 120, 139, 75);
        const auto boundary_color = IM_COL32(91, 169, 184, 155);
        const auto left = map.world_width > 0.0F ? map.world_width * -0.5F : minimum.x;
        const auto right = map.world_width > 0.0F ? map.world_width * 0.5F : maximum.x;
        const auto bottom = map.world_height > 0.0F ? map.world_height * -0.5F : minimum.y;
        const auto top = map.world_height > 0.0F ? map.world_height * 0.5F : maximum.y;
        for (int index = 0; index <= 10; ++index)
        {
            const auto t = static_cast<float>(index) / 10.0F;
            const auto x = std::lerp(left, right, t);
            const auto y = std::lerp(bottom, top, t);
            draw->AddLine(project({x, bottom, 0.0F}), project({x, top, 0.0F}), grid_color);
            draw->AddLine(project({left, y, 0.0F}), project({right, y, 0.0F}), grid_color);
        }
        const std::array corners{project({left, bottom, 0.0F}), project({right, bottom, 0.0F}), project({right, top, 0.0F}), project({left, top, 0.0F})};
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            draw->AddLine(corners[index], corners[(index + 1U) % corners.size()], boundary_color, 1.5F);
        }
    }
    if (state.map_preview_show_altitude && markers.size() <= 2000)
    {
        for (const auto& marker : markers)
        {
            draw->AddLine(marker.ground, marker.point, IM_COL32(105, 145, 158, 55));
        }
    }
    for (const auto& marker : markers)
    {
        if (!marker.visible)
        {
            continue;
        }
        const auto radius = marker.radius;
        const std::array diamond{ImVec2{marker.point.x, marker.point.y - radius}, ImVec2{marker.point.x + radius, marker.point.y}, ImVec2{marker.point.x, marker.point.y + radius},
                                 ImVec2{marker.point.x - radius, marker.point.y}};
        draw->AddConvexPolyFilled(diamond.data(), static_cast<int>(diamond.size()), marker.color);
        draw->AddPolyline(diamond.data(), static_cast<int>(diamond.size()), marker.outline, ImDrawFlags_Closed, 1.25F);
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255), "Drag to orbit  |  Wheel to zoom  |  Hover an object for details");
    draw->PopClipRect();

    if (ImGui::IsItemHovered())
    {
        const auto mouse = ImGui::GetIO().MousePos;
        const SpaceMarker* hovered = nullptr;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& marker : markers)
        {
            const auto dx = mouse.x - marker.point.x;
            const auto dy = mouse.y - marker.point.y;
            const auto distance = dx * dx + dy * dy;
            const auto hit_radius = marker.radius + 4.0F;
            if (distance <= hit_radius * hit_radius && distance < nearest)
            {
                hovered = &marker;
                nearest = distance;
            }
        }
        if (hovered != nullptr)
        {
            map_object_tooltip(state, hovered->object_index);
        }
    }
}

void render_map_viewport(GuiApp::State& state, const bool reserve_footer)
{
    if (!state.preview_map)
    {
        return;
    }
    const auto& map = *state.preview_map;
    if (map.type == TedMapType::space)
    {
        render_space_map_viewport(state, reserve_footer);
        return;
    }
    const auto has_terrain = map.terrain_width >= 2 && map.terrain_height >= 2 && map.elevation_map.size() >= static_cast<std::size_t>(map.terrain_width) * map.terrain_height;

    ImGui::Checkbox("Reference grid", &state.map_preview_show_grid);
    ImGui::SameLine();
    ImGui::Checkbox("Objects", &state.map_preview_show_objects);
    ImGui::SameLine();
    if (!has_terrain || map.passability_map.empty())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Passability", &state.map_preview_show_passability))
    {
        state.map_preview_cached = false;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Color raw passability classes; class meanings vary by map type");
    }
    if (!has_terrain || map.passability_map.empty())
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &state.map_preview_show_wireframe);

    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderFloat("##map-vertical-scale", &state.map_preview_vertical_scale, 0.0F, 3.0F, "Elevation scale: %.2fx"))
    {
        state.map_preview_cached = false;
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderInt("##map-triangle-budget", &state.map_preview_triangle_budget, 2000, 18000, "Terrain detail: up to %d triangles"))
    {
        state.map_preview_cached = false;
    }

    if (!state.map_preview_cached)
    {
        state.map_preview_triangles = map_preview_triangles(map, state.map_preview_triangle_budget, state.map_preview_vertical_scale, state.map_preview_show_passability);
        state.map_preview_cached = true;
        state.terrain_mesh_cached = false;
        state.map_object_cached = false;
    }
    if (!state.terrain_mesh_cached && state.renderer.ready())
    {
        std::vector<PreviewVertex> vertices;
        std::vector<std::uint32_t> indices;
        vertices.reserve(state.map_preview_triangles.size() * 3);
        indices.reserve(state.map_preview_triangles.size() * 3);
        for (const auto& triangle : state.map_preview_triangles)
        {
            for (const auto& point : triangle.points)
            {
                indices.push_back(static_cast<std::uint32_t>(vertices.size()));
                vertices.push_back({{point.x, point.y, point.z}, {}, triangle.color});
            }
        }
        const std::vector<PreviewPart> parts{{0, static_cast<std::uint32_t>(indices.size()), nullptr, PreviewBlend::opaque, 0.5F}};
        const PreviewInstance identity;
        if (!indices.empty() && state.terrain_mesh.upload(state.renderer.device(), state.renderer.context(), vertices, indices, parts))
        {
            static_cast<void>(state.terrain_mesh.set_instances(state.renderer.device(), state.renderer.context(), std::span<const PreviewInstance>{&identity, 1}));
        }
        state.terrain_mesh_cached = true;
    }

    // Placed objects use editor coordinates at 20 world units per terrain sample, with Z elevation
    // in the heightmap's 20x stored scale. Model geometry keeps uniform world proportions.
    const auto elevation_range = std::max(1.0F, static_cast<float>(map.maximum_elevation - map.minimum_elevation));
    const auto horizontal_extent = static_cast<float>(std::max(map.terrain_width - 1U, map.terrain_height - 1U));
    const auto vertical_extent = horizontal_extent * 0.36F * state.map_preview_vertical_scale;
    const auto editor_width = static_cast<float>(map.terrain_width) * 20.0F;
    const auto editor_height = static_cast<float>(map.terrain_height) * 20.0F;
    const auto grid_center_x = static_cast<float>(map.terrain_width - 1U) * 0.5F;
    const auto grid_center_y = static_cast<float>(map.terrain_height - 1U) * 0.5F;
    const auto land_point = [&](const TedPlacedObject& object)
    {
        return AloVector3{object.position.x / editor_width * static_cast<float>(map.terrain_width - 1U) - grid_center_x,
                          object.position.y / editor_height * static_cast<float>(map.terrain_height - 1U) - grid_center_y,
                          ((object.position.z * 20.0F - map.minimum_elevation) / elevation_range - 0.5F) * vertical_extent};
    };
    const auto land_unit_scale = static_cast<float>(map.terrain_width - 1U) / std::max(editor_width, 1.0F);
    const auto has_object_space = map.world_width > 0.0F && map.world_height > 0.0F;
    if (has_object_space && !state.map_object_cached)
    {
        update_map_instances(state, land_point, land_unit_scale);
    }
    ImGui::TextColored(muted, "%u x %u grid  |  %zu terrain triangles  |  elevation %d..%d", map.terrain_width, map.terrain_height, state.map_preview_triangles.size(), map.minimum_elevation,
                       map.maximum_elevation);
    render_map_object_status(state);

    auto canvas_size = ImGui::GetContentRegionAvail();
    if (reserve_footer)
    {
        canvas_size.y -= ImGui::GetFrameHeightWithSpacing();
    }
    canvas_size.x = std::max(canvas_size.x, 120.0F);
    canvas_size.y = std::max(canvas_size.y, 260.0F);
    ImGui::InvisibleButton("map-canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const auto canvas_min = ImGui::GetItemRectMin();
    const auto canvas_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        state.map_preview_yaw += ImGui::GetIO().MouseDelta.x * 0.012F;
        state.map_preview_pitch = std::clamp(state.map_preview_pitch - ImGui::GetIO().MouseDelta.y * 0.012F, -1.45F, 1.45F);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
    {
        state.map_preview_zoom = std::clamp(state.map_preview_zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel), 0.15F, 12.0F);
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(7, 13, 20, 255), 6.0F);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(40, 78, 91, 255), 6.0F);
    if (state.map_preview_triangles.empty())
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 14.0F}, IM_COL32(150, 164, 174, 255), "No renderable terrain heightmap in this map");
        return;
    }

    AloVector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    AloVector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& triangle : state.map_preview_triangles)
    {
        for (const auto& point : triangle.points)
        {
            minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
            maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
        }
    }
    const AloVector3 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F};
    const auto extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 0.001F});
    const auto scale = std::min(canvas_size.x, canvas_size.y) * 0.78F / extent * state.map_preview_zoom;
    const auto screen_center = ImVec2{(canvas_min.x + canvas_max.x) * 0.5F, (canvas_min.y + canvas_max.y) * 0.5F};
    const auto yaw_cos = std::cos(state.map_preview_yaw);
    const auto yaw_sin = std::sin(state.map_preview_yaw);
    const auto pitch_cos = std::cos(state.map_preview_pitch);
    const auto pitch_sin = std::sin(state.map_preview_pitch);
    const auto project = [&](const AloVector3& point, float* depth = nullptr)
    {
        const auto x = point.x - center.x;
        const auto y = point.y - center.y;
        const auto z = point.z - center.z;
        const auto horizontal = yaw_cos * x + yaw_sin * y;
        const auto forward = -yaw_sin * x + yaw_cos * y;
        const auto vertical = -pitch_sin * forward + pitch_cos * z;
        if (depth != nullptr)
        {
            *depth = pitch_cos * forward + pitch_sin * z;
        }
        return ImVec2{screen_center.x + horizontal * scale, screen_center.y - vertical * scale};
    };

    std::vector<const PreviewMesh*> meshes{&state.terrain_mesh};
    if (state.map_preview_show_objects && has_object_space)
    {
        const auto objects = map_object_meshes(state);
        meshes.insert(meshes.end(), objects.begin(), objects.end());
    }
    float largest_model = 0.0F;
    for (const auto& [path, model] : state.map_models)
    {
        largest_model = std::max(largest_model, model.radius * land_unit_scale);
    }
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw_gpu_preview(state, state.map_target, canvas_min, canvas_max, center, state.map_preview_yaw, state.map_preview_pitch, scale, extent * 1.5F + largest_model * 4.0F, meshes);

    struct LandMarker
    {
        ImVec2 point;
        float depth{};
        float radius{};
        ImU32 color{};
        ImU32 outline{};
        std::size_t object_index{};
        bool visible{true};
    };
    std::vector<LandMarker> markers;
    if (state.map_preview_show_objects && has_object_space)
    {
        std::vector<std::size_t> positioned;
        positioned.reserve(map.placed_objects.size());
        for (std::size_t index = 0; index < map.placed_objects.size(); ++index)
        {
            if (map.placed_objects[index].has_transform)
            {
                positioned.push_back(index);
            }
        }
        const auto marker_stride = std::max<std::size_t>(1, (positioned.size() + 5999U) / 6000U);
        markers.reserve((positioned.size() + marker_stride - 1U) / marker_stride);
        for (std::size_t shown = 0; shown < positioned.size(); shown += marker_stride)
        {
            const auto object_index = positioned[shown];
            const auto& object = map.placed_objects[object_index];
            LandMarker marker;
            marker.point = project(land_point(object), &marker.depth);
            marker.radius = object_marker_radius(object, state.map_preview_marker_scale);
            marker.color = passability_color(object.crc);
            marker.outline = object_player_color(object.player);
            marker.object_index = object_index;
            if (object_index < state.map_object_drawn.size() && state.map_object_drawn[object_index] != 0)
            {
                marker.visible = false;
                marker.radius = std::max(marker.radius, map_object_radius(state, object_index) * land_unit_scale * scale);
            }
            markers.push_back(marker);
        }
        std::ranges::sort(markers, {}, &LandMarker::depth);
    }

    if (state.map_preview_show_grid)
    {
        const auto grid_color = IM_COL32(93, 157, 169, 105);
        for (int index = 0; index <= 10; ++index)
        {
            const auto t = static_cast<float>(index) / 10.0F;
            const auto x = std::lerp(minimum.x, maximum.x, t);
            const auto y = std::lerp(minimum.y, maximum.y, t);
            draw->AddLine(project({x, minimum.y, minimum.z}), project({x, maximum.y, minimum.z}), grid_color);
            draw->AddLine(project({minimum.x, y, minimum.z}), project({maximum.x, y, minimum.z}), grid_color);
        }
    }
    if (state.map_preview_show_wireframe)
    {
        const auto wire_stride = std::max<std::size_t>(1, (state.map_preview_triangles.size() + 3999U) / 4000U);
        for (std::size_t index = 0; index < state.map_preview_triangles.size(); index += wire_stride)
        {
            const auto& triangle = state.map_preview_triangles[index];
            draw->AddTriangle(project(triangle.points[0]), project(triangle.points[1]), project(triangle.points[2]), IM_COL32(8, 16, 20, 95));
        }
    }
    for (const auto& marker : markers)
    {
        if (!marker.visible)
        {
            continue;
        }
        const auto radius = marker.radius;
        const std::array diamond{ImVec2{marker.point.x, marker.point.y - radius}, ImVec2{marker.point.x + radius, marker.point.y}, ImVec2{marker.point.x, marker.point.y + radius},
                                 ImVec2{marker.point.x - radius, marker.point.y}};
        draw->AddConvexPolyFilled(diamond.data(), static_cast<int>(diamond.size()), marker.color);
        draw->AddPolyline(diamond.data(), static_cast<int>(diamond.size()), marker.outline, ImDrawFlags_Closed, 1.25F);
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255),
                  state.map_preview_show_objects ? "Drag to orbit  |  Wheel to zoom  |  Hover an object for details" : "Drag to orbit  |  Wheel to zoom");
    draw->PopClipRect();

    if (state.map_preview_show_objects && ImGui::IsItemHovered())
    {
        const auto mouse = ImGui::GetIO().MousePos;
        const LandMarker* hovered = nullptr;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& marker : markers)
        {
            const auto dx = mouse.x - marker.point.x;
            const auto dy = mouse.y - marker.point.y;
            const auto distance = dx * dx + dy * dy;
            const auto hit_radius = marker.radius + 4.0F;
            if (distance <= hit_radius * hit_radius && distance < nearest)
            {
                hovered = &marker;
                nearest = distance;
            }
        }
        if (hovered != nullptr)
        {
            map_object_tooltip(state, hovered->object_index);
        }
    }
}

void render_model_viewport(GuiApp::State& state)
{
    if (!state.preview_model)
    {
        return;
    }

    const char* animation_label = "Bind pose";
    std::string selected_animation_name;
    if (state.preview_animation_index >= 0 && static_cast<std::size_t>(state.preview_animation_index) < state.preview_animation_assets.size())
    {
        selected_animation_name = std::filesystem::path(state.preview_animation_assets[static_cast<std::size_t>(state.preview_animation_index)].virtual_path).filename().string();
        animation_label = selected_animation_name.c_str();
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##preview-animation", animation_label))
    {
        if (ImGui::Selectable("Bind pose", state.preview_animation_index < 0))
        {
            state.select_preview_animation(-1);
            state.model_mesh_cached = false;
        }
        for (std::size_t index = 0; index < state.preview_animation_assets.size(); ++index)
        {
            const auto name = std::filesystem::path(state.preview_animation_assets[index].virtual_path).filename().string();
            if (ImGui::Selectable(name.c_str(), state.preview_animation_index == static_cast<int>(index)))
            {
                try
                {
                    state.select_preview_animation(static_cast<int>(index));
                }
                catch (const std::exception& error)
                {
                    state.fail(error);
                }
                state.model_mesh_cached = false;
            }
        }
        ImGui::EndCombo();
    }

    if (state.preview_animation)
    {
        if (ImGui::Button(state.preview_playing ? "Pause" : "Play"))
        {
            state.preview_playing = !state.preview_playing;
        }
        ImGui::SameLine();
        const auto duration = static_cast<float>(state.preview_animation->duration_seconds());
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::SliderFloat("##animation-time", &state.preview_time, 0.0F, std::max(duration, 0.001F), "%.3f s");
        if (state.preview_playing && duration > 0.0F)
        {
            state.preview_time = std::fmod(state.preview_time + ImGui::GetIO().DeltaTime, duration);
        }
    }

    ImGui::Checkbox("Skeleton", &state.preview_show_skeleton);
    ImGui::SameLine();
    if (ImGui::Checkbox("Collision", &state.preview_show_collision))
    {
        state.model_mesh_cached = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset camera"))
    {
        state.preview_yaw = -0.65F;
        state.preview_pitch = 0.35F;
        state.preview_zoom = 1.0F;
    }
    if (!state.preview_texture_warnings.empty())
    {
        ImGui::TextColored(warning, "%zu texture(s) unavailable", state.preview_texture_warnings.size());
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            for (const auto& warning_text : state.preview_texture_warnings)
            {
                ImGui::TextUnformatted(warning_text.c_str());
            }
            ImGui::EndTooltip();
        }
    }

    // Static models upload once; animated models re-skin on the CPU and re-upload every frame.
    const auto pose = preview_pose(state);
    if ((!state.model_mesh_cached || state.preview_animation || state.model_mesh_collision != state.preview_show_collision) && state.renderer.ready())
    {
        const auto geometry = build_preview_geometry(*state.preview_model, &pose, state.preview_inverse_bind, state.preview_show_collision,
                                                     [&state](const std::string& reference) -> ID3D11ShaderResourceView*
                                                     {
                                                         const auto found = state.preview_textures.find(lower(reference));
                                                         return found == state.preview_textures.end() ? nullptr : found->second.view.Get();
                                                     });
        const PreviewInstance identity;
        if (!geometry.indices.empty() && state.model_mesh.upload(state.renderer.device(), state.renderer.context(), geometry.vertices, geometry.indices, geometry.parts))
        {
            static_cast<void>(state.model_mesh.set_instances(state.renderer.device(), state.renderer.context(), std::span<const PreviewInstance>{&identity, 1}));
            state.model_minimum = geometry.minimum;
            state.model_maximum = geometry.maximum;
        }
        else
        {
            static_cast<void>(state.model_mesh.set_instances(state.renderer.device(), state.renderer.context(), {}));
        }
        state.model_mesh_cached = true;
        state.model_mesh_collision = state.preview_show_collision;
    }
    ImGui::TextColored(muted, "%zu DDS loaded  |  %llu triangles", state.preview_textures.size(), static_cast<unsigned long long>(state.preview_model->triangle_count()));

    auto canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 120.0F);
    canvas_size.y = std::max(canvas_size.y, 260.0F);
    ImGui::InvisibleButton("model-canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const auto canvas_min = ImGui::GetItemRectMin();
    const auto canvas_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        state.preview_yaw += ImGui::GetIO().MouseDelta.x * 0.012F;
        state.preview_pitch = std::clamp(state.preview_pitch - ImGui::GetIO().MouseDelta.y * 0.012F, -1.45F, 1.45F);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
    {
        state.preview_zoom = std::clamp(state.preview_zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel), 0.15F, 12.0F);
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(7, 13, 20, 255), 6.0F);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(40, 78, 91, 255), 6.0F);
    if (!state.model_mesh.ready() || state.model_minimum.x > state.model_maximum.x)
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 14.0F}, IM_COL32(150, 164, 174, 255),
                      state.renderer.ready() ? "No renderable triangle stream in this model" : "GPU preview unavailable on this device");
        return;
    }

    const auto& minimum = state.model_minimum;
    const auto& maximum = state.model_maximum;
    const AloVector3 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F};
    const auto extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 0.001F});
    const auto scale = std::min(canvas_size.x, canvas_size.y) * 0.78F / extent * state.preview_zoom;
    const auto screen_center = ImVec2{(canvas_min.x + canvas_max.x) * 0.5F, (canvas_min.y + canvas_max.y) * 0.5F};
    const auto yaw_cos = std::cos(state.preview_yaw);
    const auto yaw_sin = std::sin(state.preview_yaw);
    const auto pitch_cos = std::cos(state.preview_pitch);
    const auto pitch_sin = std::sin(state.preview_pitch);
    const auto project = [&](const AloVector3& point)
    {
        const auto x = point.x - center.x;
        const auto y = point.y - center.y;
        const auto z = point.z - center.z;
        const auto horizontal = yaw_cos * x + yaw_sin * y;
        const auto forward = -yaw_sin * x + yaw_cos * y;
        const auto vertical = -pitch_sin * forward + pitch_cos * z;
        return ImVec2{screen_center.x + horizontal * scale, screen_center.y - vertical * scale};
    };

    draw->PushClipRect(canvas_min, canvas_max, true);
    const PreviewMesh* meshes[]{&state.model_mesh};
    draw_gpu_preview(state, state.model_target, canvas_min, canvas_max, center, state.preview_yaw, state.preview_pitch, scale, extent * 2.0F, meshes);
    if (state.preview_show_skeleton)
    {
        for (std::size_t index = 0; index < pose.size() && index < state.preview_model->bones.size(); ++index)
        {
            const auto parent = state.preview_model->bones[index].parent;
            if (parent < 0 || static_cast<std::size_t>(parent) >= pose.size())
            {
                continue;
            }
            const AloVector3 point{pose[index].transform.values[12], pose[index].transform.values[13], pose[index].transform.values[14]};
            const AloVector3 parent_point{pose[static_cast<std::size_t>(parent)].transform.values[12], pose[static_cast<std::size_t>(parent)].transform.values[13],
                                          pose[static_cast<std::size_t>(parent)].transform.values[14]};
            draw->AddLine(project(parent_point), project(point), IM_COL32(255, 202, 74, 230), 1.5F);
        }
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255), "Drag to orbit  |  Wheel to zoom");
    draw->PopClipRect();
}

void render_asset_workspace(GuiApp::State& state, HWND owner)
{
    ImGui::TextUnformatted("ASSET INSPECTOR");
    ImGui::SameLine();
    ImGui::TextColored(muted, "Layered game and mod resources");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-125.0F);
    ImGui::InputTextWithHint("##game-root", "Empire at War installation", &state.game_root);
    ImGui::SameLine();
    if (ImGui::Button("Browse...", {110.0F, 0.0F}))
    {
        try
        {
            if (const auto path = pick_folder(owner, L"Select Empire at War installation"))
            {
                state.game_root = path_to_utf8(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }

    if (ImGui::RadioButton("Forces of Corruption", state.variant == GameVariant::forces_of_corruption))
    {
        state.variant = GameVariant::forces_of_corruption;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Empire at War", state.variant == GameVariant::empire_at_war))
    {
        state.variant = GameVariant::empire_at_war;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add mod layer..."))
    {
        try
        {
            if (const auto path = pick_folder(owner, L"Select a mod folder"))
            {
                state.mods.push_back(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load assets"))
    {
        try
        {
            state.load_assets();
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }

    if (!state.mods.empty())
    {
        ImGui::TextColored(muted, "Mod priority (first wins):");
        for (std::size_t index = 0; index < state.mods.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::BulletText("%s", path_to_utf8(state.mods[index]).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                state.mods.erase(state.mods.begin() + static_cast<std::ptrdiff_t>(index));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-405.0F);
    ImGui::InputTextWithHint("##asset-search", "Search virtual paths...", &state.asset_query);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(155.0F);
    ImGui::InputTextWithHint("##extension", "Extension, e.g. .xml", &state.asset_extension);
    ImGui::SameLine();
    auto normalized_extension = lower(state.asset_extension);
    if (!normalized_extension.empty() && normalized_extension.front() != '.')
    {
        normalized_extension.insert(normalized_extension.begin(), '.');
    }
    const char* known_extension_label = normalized_extension.empty() ? known_asset_extensions.front().label : "Custom extension";
    for (const auto& known : known_asset_extensions)
    {
        if (normalized_extension == known.extension)
        {
            known_extension_label = known.label;
            break;
        }
    }
    ImGui::SetNextItemWidth(235.0F);
    if (ImGui::BeginCombo("##known-extension", known_extension_label))
    {
        for (const auto& known : known_asset_extensions)
        {
            const auto selected = normalized_extension == known.extension;
            if (ImGui::Selectable(known.label, selected))
            {
                state.asset_extension = known.extension;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Choose a known game asset type or enter a custom extension");
    }

    std::vector<int> visible;
    visible.reserve(state.assets.size());
    const auto query = lower(state.asset_query);
    auto extension = lower(state.asset_extension);
    if (!extension.empty() && extension.front() != '.')
    {
        extension.insert(extension.begin(), '.');
    }
    std::uint64_t total_bytes = 0;
    for (std::size_t index = 0; index < state.assets.size(); ++index)
    {
        const auto& asset = state.assets[index];
        if (!query.empty() && lower(asset.virtual_path).find(query) == std::string::npos)
        {
            continue;
        }
        if (!extension.empty() && lower(std::filesystem::path(asset.virtual_path).extension().string()) != extension)
        {
            continue;
        }
        visible.push_back(static_cast<int>(index));
        total_bytes += asset.size;
    }

    ImGui::TextColored(accent, "%zu assets", visible.size());
    ImGui::SameLine();
    ImGui::TextColored(muted, "  %s visible  •  %zu mounted layers", human_size(total_bytes).c_str(), state.vfs.mounts().size());

    const float preview_width = std::max(360.0F, ImGui::GetContentRegionAvail().x * ((state.preview_model || state.preview_map) ? 0.52F : 0.40F));
    if (ImGui::BeginTable("asset-layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, {-1.0F, -1.0F}))
    {
        ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch, 0.60F);
        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, preview_width);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginTable("asset-table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                              {-1.0F, -1.0F}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Virtual path", ImGuiTableColumnFlags_WidthStretch, 0.67F);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 82.0F);
            ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch, 0.33F);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const int asset_index = visible[static_cast<std::size_t>(row)];
                    const auto& asset = state.assets[static_cast<std::size_t>(asset_index)];
                    ImGui::PushID(asset_index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(asset.virtual_path.c_str(), state.selected_asset == asset_index, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        try
                        {
                            state.inspect_asset(asset_index);
                        }
                        catch (const std::exception& error)
                        {
                            state.fail(error);
                            state.asset_preview = error.what();
                        }
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(human_size(asset.size).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(asset.mount_name.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("asset-preview", {-1.0F, -1.0F}, ImGuiChildFlags_Borders);
        if (state.asset_preview.empty())
        {
            ImGui::TextColored(muted, "Select an asset to inspect its container, header, and contents.");
        }
        else if (state.preview_model || state.preview_map)
        {
            if (ImGui::BeginTabBar("visual-preview-tabs"))
            {
                if (ImGui::BeginTabItem("3D Preview"))
                {
                    if (state.preview_model)
                    {
                        render_model_viewport(state);
                    }
                    else
                    {
                        if (!state.map_preview_large_window)
                        {
                            ImGui::PushID("embedded-map-preview");
                            render_map_viewport(state, true);
                            ImGui::PopID();
                            if (ImGui::Button("Preview"))
                            {
                                state.map_preview_large_window = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Reset camera"))
                            {
                                state.map_preview_yaw = -0.65F;
                                state.map_preview_pitch = 0.72F;
                                state.map_preview_zoom = 1.0F;
                            }
                        }
                        else
                        {
                            ImGui::TextColored(muted, "Map preview is open in a separate window.");
                            if (ImGui::SmallButton("Close large preview"))
                            {
                                state.map_preview_large_window = false;
                            }
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Metadata"))
                {
                    ImGui::TextUnformatted(state.asset_preview.c_str());
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            ImGui::TextUnformatted(state.asset_preview.c_str());
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

void render_large_map_preview(GuiApp::State& state)
{
    if (!state.map_preview_large_window)
    {
        return;
    }
    if (!state.preview_map)
    {
        state.map_preview_large_window = false;
        return;
    }

    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 window_size{std::max(760.0F, viewport->WorkSize.x * 0.86F), std::max(560.0F, viewport->WorkSize.y * 0.86F)};
    const ImVec2 window_center{viewport->WorkPos.x + viewport->WorkSize.x * 0.5F, viewport->WorkPos.y + viewport->WorkSize.y * 0.5F};
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(window_center, ImGuiCond_Appearing, {0.5F, 0.5F});
    if (ImGui::Begin("Map Preview###large-map-preview", &state.map_preview_large_window, ImGuiWindowFlags_NoCollapse))
    {
        if (state.selected_asset >= 0 && static_cast<std::size_t>(state.selected_asset) < state.assets.size())
        {
            ImGui::TextColored(muted, "%s", state.assets[static_cast<std::size_t>(state.selected_asset)].virtual_path.c_str());
            ImGui::Separator();
        }
        ImGui::PushID("large-map-preview-content");
        render_map_viewport(state, true);
        ImGui::PopID();
        if (ImGui::Button("Reset camera"))
        {
            state.map_preview_yaw = -0.65F;
            state.map_preview_pitch = 0.72F;
            state.map_preview_zoom = 1.0F;
        }
    }
    ImGui::End();
}

bool status_matches(const std::string& filter, const LocalizationRow& row)
{
    return filter == "all" || lower(row.status) == filter;
}

int compare_case_insensitive(const std::string_view left, const std::string_view right)
{
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        const auto left_folded = static_cast<unsigned char>(std::tolower(left_byte));
        const auto right_folded = static_cast<unsigned char>(std::tolower(right_byte));
        if (left_folded != right_folded)
        {
            return left_folded < right_folded ? -1 : 1;
        }
    }
    if (left.size() == right.size())
    {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

std::string_view localization_sort_value(const LocalizationRow& row, const ImGuiID column)
{
    switch (column)
    {
    case 1:
        return row.key;
    case 2:
        return row.text;
    case 3:
        return row.group;
    case 4:
        return row.status;
    default:
        return row.key;
    }
}

void sort_localization_rows(std::vector<int>& visible, const std::vector<LocalizationRow>& rows, const ImGuiTableSortSpecs& specifications)
{
    std::ranges::stable_sort(visible,
                             [&](const int left_index, const int right_index)
                             {
                                 const auto& left = rows[static_cast<std::size_t>(left_index)];
                                 const auto& right = rows[static_cast<std::size_t>(right_index)];
                                 for (int sort_index = 0; sort_index < specifications.SpecsCount; ++sort_index)
                                 {
                                     const auto& specification = specifications.Specs[sort_index];
                                     const auto comparison =
                                         compare_case_insensitive(localization_sort_value(left, specification.ColumnUserID), localization_sort_value(right, specification.ColumnUserID));
                                     if (comparison == 0)
                                     {
                                         continue;
                                     }
                                     return specification.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0;
                                 }
                                 return left_index < right_index;
                             });
}

void render_add_key_popup(GuiApp::State& state)
{
    if (ImGui::BeginPopupModal("Add localization key", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(muted, "Use the same TEXT_* identifier referenced by mod XML.");
        ImGui::SetNextItemWidth(480.0F);
        ImGui::InputTextWithHint("##new-key", "TEXT_MY_NEW_KEY", &state.new_key);
        if (ImGui::Button("Add", {110.0F, 0.0F}))
        {
            try
            {
                state.add_localization_row({state.new_key, {}, "manual", "todo", {}, "Added in EAWTools"});
                state.new_key.clear();
                ImGui::CloseCurrentPopup();
            }
            catch (const std::exception& error)
            {
                state.fail(error);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void render_add_unit_popup(GuiApp::State& state)
{
    if (ImGui::BeginPopupModal("Add unit localization", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(muted, "Creates a display name and encyclopedia description together.");
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint("##unit-id", "Unit ID, e.g. REPUBLIC_VENATOR", &state.unit_id);
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint("##unit-name", "Display name", &state.unit_name);
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextMultiline("##unit-description", &state.unit_description, {520.0F, 130.0F});
        if (ImGui::Button("Create keys", {130.0F, 0.0F}))
        {
            try
            {
                const auto id = normalize_unit_id(state.unit_id);
                if (id.empty())
                {
                    throw std::runtime_error("Unit ID cannot be empty");
                }
                const auto name_key = "TEXT_UNIT_" + id;
                const auto tooltip_key = "TEXT_TOOLTIP_" + id;
                const auto conflicts = [&](const auto& row)
                {
                    const auto key = lower(row.key);
                    return key == lower(name_key) || key == lower(tooltip_key);
                };
                if (std::ranges::any_of(state.rows, conflicts))
                {
                    throw std::runtime_error("One of the generated unit keys already exists");
                }
                state.rows.push_back({name_key, state.unit_name, "unit:" + id, "ready", "Use in <Text_ID>", "Generated unit display-name key"});
                state.rows.push_back({tooltip_key, state.unit_description, "unit:" + id, "ready", "Use in <Encyclopedia_Text>", "Generated unit description key"});
                state.selected_row = static_cast<int>(state.rows.size() - 2);
                state.dirty = true;
                state.unit_id.clear();
                state.unit_name.clear();
                state.unit_description.clear();
                ImGui::CloseCurrentPopup();
            }
            catch (const std::exception& error)
            {
                state.fail(error);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void render_localization_workspace(GuiApp::State& state, HWND owner)
{
    ImGui::TextUnformatted("DAT LOCALIZATION STUDIO");
    ImGui::SameLine();
    ImGui::TextColored(muted, "Edit, scan, validate, and build mod text");
    ImGui::Spacing();

    if (ImGui::Button("Open DAT..."))
    {
        try
        {
            if (const auto path = open_dat_file(owner, L"Open baseline localization DAT"))
            {
                state.open_dat(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open workspace..."))
    {
        try
        {
            if (const auto path = open_workspace_file(owner, L"Open localization workspace"))
            {
                state.open_workspace(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save workspace"))
    {
        try
        {
            auto path = state.workspace_path;
            if (!path)
            {
                path = save_workspace_file(owner, L"Save localization workspace");
            }
            if (path)
            {
                state.save_workspace(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as..."))
    {
        try
        {
            if (const auto path = save_workspace_file(owner, L"Save localization workspace"))
            {
                state.save_workspace(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan mod..."))
    {
        try
        {
            if (const auto path = pick_folder(owner, L"Select mod root to scan"))
            {
                state.scan_mod(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add key"))
    {
        ImGui::OpenPopup("Add localization key");
    }
    ImGui::SameLine();
    if (ImGui::Button("Add unit"))
    {
        ImGui::OpenPopup("Add unit localization");
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate"))
    {
        state.validate_workspace();
    }
    ImGui::SameLine();
    if (ImGui::Button("Build DAT..."))
    {
        try
        {
            if (const auto path = save_dat_file(owner, L"Build localization DAT"))
            {
                state.build_dat(*path);
            }
        }
        catch (const std::exception& error)
        {
            state.fail(error);
        }
    }

    render_add_key_popup(state);
    render_add_unit_popup(state);

    if (state.base_dat_path)
    {
        label_value("Baseline", path_to_utf8(*state.base_dat_path));
    }
    else
    {
        label_value("Baseline", "Not selected (required for scan/build)");
    }
    if (state.workspace_path)
    {
        label_value("Workspace", path_to_utf8(*state.workspace_path));
    }
    ImGui::Checkbox("Skip unresolved TODO rows when building", &state.skip_todo);
    if (state.dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(warning, "Unsaved changes");
    }

    ImGui::SetNextItemWidth(-510.0F);
    ImGui::InputTextWithHint("##row-search", "Search keys, text, group, or context...", &state.row_query);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0F);
    if (ImGui::BeginCombo("##status-filter", state.status_filter.c_str()))
    {
        for (const char* status : {"all", "existing", "new", "modified", "ready", "todo", "deleted"})
        {
            const bool selected = state.status_filter == status;
            if (ImGui::Selectable(status, selected))
            {
                state.status_filter = status;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextColored(accent, "%zu rows", state.rows.size());

    std::vector<int> visible;
    visible.reserve(state.rows.size());
    const auto query = lower(state.row_query);
    for (std::size_t index = 0; index < state.rows.size(); ++index)
    {
        const auto& row = state.rows[index];
        if (!status_matches(state.status_filter, row))
        {
            continue;
        }
        if (!query.empty())
        {
            const auto searchable = lower(row.key + "\n" + row.text + "\n" + row.group + "\n" + row.context);
            if (searchable.find(query) == std::string::npos)
            {
                continue;
            }
        }
        visible.push_back(static_cast<int>(index));
    }

    if (ImGui::BeginTable("localization-layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, {-1.0F, -1.0F}))
    {
        ImGui::TableSetupColumn("Entries", ImGuiTableColumnFlags_WidthStretch, 0.62F);
        ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 0.38F);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginTable("localization-table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable,
                              {-1.0F, -1.0F}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortAscending, 0.44F, 1);
            ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending, 0.36F, 2);
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending, 0.20F, 3);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortAscending, 70.0F, 4);
            if (auto* sort_specs = ImGui::TableGetSortSpecs(); sort_specs != nullptr && sort_specs->SpecsCount > 0)
            {
                sort_localization_rows(visible, state.rows, *sort_specs);
                sort_specs->SpecsDirty = false;
            }
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int table_row = clipper.DisplayStart; table_row < clipper.DisplayEnd; ++table_row)
                {
                    const int index = visible[static_cast<std::size_t>(table_row)];
                    const auto& row = state.rows[static_cast<std::size_t>(index)];
                    ImGui::PushID(index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(row.key.c_str(), state.selected_row == index, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        state.selected_row = index;
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(clean_one_line(row.text, 72).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.group.c_str());
                    ImGui::TableSetColumnIndex(3);
                    const auto color = lower(row.status) == "todo" ? warning : (lower(row.status) == "deleted" ? danger : accent);
                    ImGui::TextColored(color, "%s", row.status.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("row-editor", {-1.0F, -1.0F}, ImGuiChildFlags_Borders);
        if (state.selected_row >= 0 && static_cast<std::size_t>(state.selected_row) < state.rows.size())
        {
            auto& row = state.rows[static_cast<std::size_t>(state.selected_row)];
            ImGui::TextColored(accent, "%s", row.key.c_str());
            ImGui::TextColored(muted, "Key");
            const bool existing = lower(row.status) == "existing";
            if (existing)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::InputText("##edit-key", &row.key))
            {
                state.dirty = true;
            }
            if (existing)
            {
                ImGui::EndDisabled();
            }
            ImGui::TextColored(muted, "Localized text");
            if (ImGui::InputTextMultiline("##edit-text", &row.text, {-1.0F, 150.0F}))
            {
                const auto current_status = lower(row.status);
                if (current_status == "existing")
                {
                    row.status = "modified";
                }
                else if (current_status == "todo" && !row.text.empty())
                {
                    row.status = "ready";
                }
                state.dirty = true;
            }
            ImGui::TextColored(muted, "Group");
            if (ImGui::InputText("##edit-group", &row.group))
            {
                state.dirty = true;
            }
            ImGui::TextColored(muted, "Status");
            if (ImGui::BeginCombo("##edit-status", row.status.c_str()))
            {
                for (const char* status : {"existing", "new", "modified", "ready", "todo", "deleted"})
                {
                    if (ImGui::Selectable(status, row.status == status))
                    {
                        row.status = status;
                        state.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextColored(muted, "Context");
            if (ImGui::InputTextMultiline("##edit-context", &row.context, {-1.0F, 74.0F}))
            {
                state.dirty = true;
            }
            ImGui::TextColored(muted, "Notes");
            if (ImGui::InputTextMultiline("##edit-notes", &row.notes, {-1.0F, 74.0F}))
            {
                state.dirty = true;
            }
            if (ImGui::Button("Delete entry"))
            {
                if (lower(row.status) == "new" || lower(row.status) == "todo")
                {
                    state.rows.erase(state.rows.begin() + state.selected_row);
                    state.selected_row = state.rows.empty() ? -1 : std::min(state.selected_row, static_cast<int>(state.rows.size() - 1));
                }
                else
                {
                    row.status = "deleted";
                }
                state.dirty = true;
            }
        }
        else
        {
            ImGui::TextColored(muted, "Select a localization row to edit it.");
        }
        if (!state.validation_messages.empty())
        {
            ImGui::SeparatorText("Validation");
            for (const auto& item : state.validation_messages)
            {
                ImGui::TextColored(item.starts_with("Error:") ? danger : item.starts_with("TODO:") ? warning : muted, "%s", item.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

} // namespace

void GuiApp::render(const NativeWindowHandle window)
{
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 8.0F;
    style.ChildRounding = 7.0F;
    style.FrameRounding = 5.0F;
    style.PopupRounding = 7.0F;
    style.ScrollbarRounding = 8.0F;
    style.GrabRounding = 5.0F;
    style.WindowPadding = {12.0F, 12.0F};
    style.FramePadding = {9.0F, 6.0F};
    style.ItemSpacing = {8.0F, 7.0F};
    style.Colors[ImGuiCol_WindowBg] = {0.040F, 0.055F, 0.075F, 1.0F};
    style.Colors[ImGuiCol_ChildBg] = {0.055F, 0.073F, 0.095F, 1.0F};
    style.Colors[ImGuiCol_FrameBg] = {0.085F, 0.11F, 0.14F, 1.0F};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.12F, 0.18F, 0.21F, 1.0F};
    style.Colors[ImGuiCol_FrameBgActive] = {0.13F, 0.24F, 0.26F, 1.0F};
    style.Colors[ImGuiCol_Button] = {0.10F, 0.25F, 0.27F, 1.0F};
    style.Colors[ImGuiCol_ButtonHovered] = {0.13F, 0.38F, 0.40F, 1.0F};
    style.Colors[ImGuiCol_ButtonActive] = {0.14F, 0.48F, 0.49F, 1.0F};
    style.Colors[ImGuiCol_Header] = {0.10F, 0.27F, 0.29F, 1.0F};
    style.Colors[ImGuiCol_HeaderHovered] = {0.13F, 0.38F, 0.40F, 1.0F};
    style.Colors[ImGuiCol_HeaderActive] = {0.14F, 0.47F, 0.48F, 1.0F};
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SeparatorActive] = accent;
    style.Colors[ImGuiCol_TabSelected] = {0.10F, 0.31F, 0.33F, 1.0F};

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("EAW Tools Workbench", nullptr, flags);

    ImGui::BeginChild("navigation", {205.0F, -35.0F}, ImGuiChildFlags_Borders);
    ImGui::TextColored(accent, "EAW TOOLS");
    ImGui::TextColored(muted, "Alamo workbench");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Selectable("Asset Inspector", state_->workspace == State::Workspace::assets, 0, {0.0F, 42.0F}))
    {
        state_->workspace = State::Workspace::assets;
    }
    if (ImGui::Selectable("DAT Localization", state_->workspace == State::Workspace::localization, 0, {0.0F, 42.0F}))
    {
        state_->workspace = State::Workspace::localization;
    }
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 68.0F));
    ImGui::TextColored(muted, "Native C++20");
    ImGui::TextColored(muted, "Dear ImGui • DirectX 11");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("workspace", {0.0F, -35.0F}, ImGuiChildFlags_Borders);
    if (state_->workspace == State::Workspace::assets)
    {
        render_asset_workspace(*state_, window);
    }
    else
    {
        render_localization_workspace(*state_, window);
    }
    ImGui::EndChild();
    render_status_bar(*state_);
    ImGui::End();
    render_large_map_preview(*state_);
}

} // namespace eaw::tools
