#include "formats/alamo_chunks.hpp"
#include "formats/alo_model.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace eaw {
namespace {

// The shared chunk container (formats/alamo_chunks.hpp), with this format named in diagnostics.
using Chunk = ChunkRange;
using MiniChunk = MiniChunkRange;
using chunk::find_chunk;
using chunk::find_mini;
using chunk::read_float;
using chunk::read_i32;
using chunk::read_string;
using chunk::read_u16;
using chunk::read_u32;

Result<std::vector<Chunk>> read_chunks(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    return chunk::read_chunks(bytes, begin, end, "ALO");
}

Result<std::vector<MiniChunk>> read_minis(const std::span<const std::byte> bytes, const Chunk& parent)
{
    return chunk::read_minis(bytes, parent, "ALO");
}

Vector3 read_vector3(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return {read_float(bytes, offset), read_float(bytes, offset + 4), read_float(bytes, offset + 8)};
}

// Decodes numeric parameter components from the parameter's own data chunk. A chunk shorter than
// the type requires yields no values (it never borrows bytes from the following chunk).
std::vector<float> numeric_values(const std::span<const std::byte> bytes, const Chunk& chunk, const AloShaderParameterType type)
{
    std::size_t count = 0;
    switch (type)
    {
    case AloShaderParameterType::integer:
    case AloShaderParameterType::scalar:
        count = 1;
        break;
    case AloShaderParameterType::vector3:
        count = 3;
        break;
    case AloShaderParameterType::vector4:
        count = 4;
        break;
    default:
        return {};
    }
    if (chunk.end < chunk.payload || chunk.end - chunk.payload < count * 4U)
    {
        return {};
    }
    std::vector<float> values;
    for (std::size_t index = 0; index < count; ++index)
    {
        values.push_back(type == AloShaderParameterType::integer ? static_cast<float>(read_i32(bytes, chunk.payload)) : read_float(bytes, chunk.payload + index * 4U));
    }
    return values;
}

std::string numeric_text(const std::vector<float>& values, const AloShaderParameterType type)
{
    std::ostringstream text;
    text << std::setprecision(9);
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0)
        {
            text << ", ";
        }
        if (type == AloShaderParameterType::integer)
        {
            text << static_cast<std::int64_t>(values[index]);
        }
        else
        {
            text << values[index];
        }
    }
    return text.str();
}

Result<AloShaderParameter> parse_parameter(const std::span<const std::byte> bytes, const Chunk& chunk)
{
    AloShaderParameter parameter;
    switch (chunk.type)
    {
    case 0x10102:
        parameter.type = AloShaderParameterType::integer;
        break;
    case 0x10103:
        parameter.type = AloShaderParameterType::scalar;
        break;
    case 0x10104:
        parameter.type = AloShaderParameterType::vector3;
        break;
    case 0x10105:
        parameter.type = AloShaderParameterType::texture;
        break;
    case 0x10106:
        parameter.type = AloShaderParameterType::vector4;
        break;
    default:
        return parameter;
    }
    EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, chunk));
    if (const auto* name = find_mini(minis, 1))
    {
        parameter.name = read_string(bytes, name->payload, name->end);
    }
    if (const auto* data = find_mini(minis, 2))
    {
        const Chunk data_chunk{0, false, data->payload, data->end};
        if (parameter.type == AloShaderParameterType::texture)
        {
            parameter.value = read_string(bytes, data->payload, data->end);
        }
        else
        {
            parameter.values = numeric_values(bytes, data_chunk, parameter.type);
            parameter.value = numeric_text(parameter.values, parameter.type);
        }
    }
    return parameter;
}

struct Material
{
    std::string shader;
    std::vector<AloShaderParameter> parameters;
};

Result<Material> parse_material(const std::span<const std::byte> bytes, const Chunk& chunk)
{
    Material material;
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& child : children)
    {
        if (child.type == 0x10101)
        {
            material.shader = read_string(bytes, child.payload, child.end);
        }
        else if (child.type >= 0x10102 && child.type <= 0x10106)
        {
            EAW_TRY_ASSIGN(auto parameter, parse_parameter(bytes, child));
            material.parameters.push_back(std::move(parameter));
        }
    }
    return material;
}

Result<AloSubMesh> parse_submesh(const std::span<const std::byte> bytes, const Chunk& chunk, Material material, std::vector<std::string>& warnings)
{
    AloSubMesh submesh;
    submesh.shader = std::move(material.shader);
    submesh.parameters = std::move(material.parameters);
    std::size_t vertex_bytes = 0;
    Chunk vertex_data{};
    Chunk index_data{};
    Chunk skin_data{};
    bool has_vertex_data = false;
    bool has_index_data = false;
    bool has_skin_data = false;
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& child : children)
    {
        switch (child.type)
        {
        case 0x10001:
            if (child.end - child.payload >= 8)
            {
                submesh.vertex_count = read_u32(bytes, child.payload);
                submesh.triangle_count = read_u32(bytes, child.payload + 4);
            }
            break;
        case 0x10002:
            submesh.vertex_format = read_string(bytes, child.payload, child.end);
            break;
        case 0x10005:
        case 0x10007:
            vertex_bytes = child.end - child.payload;
            vertex_data = child;
            has_vertex_data = true;
            break;
        case 0x10004:
            index_data = child;
            has_index_data = true;
            break;
        case 0x10006:
            submesh.skin_bone_count = static_cast<std::uint32_t>((child.end - child.payload) / sizeof(std::uint32_t));
            skin_data = child;
            has_skin_data = true;
            break;
        default:
            break;
        }
    }
    if (submesh.vertex_count != 0 && vertex_bytes % submesh.vertex_count == 0)
    {
        submesh.vertex_stride = static_cast<std::uint32_t>(vertex_bytes / submesh.vertex_count);
    }
    if (has_vertex_data && submesh.vertex_count != 0 && (submesh.vertex_stride == 128 || submesh.vertex_stride == 144))
    {
        submesh.vertices.reserve(submesh.vertex_count);
        const auto bone_offset = submesh.vertex_stride == 144 ? 112U : 96U;
        const auto weight_offset = bone_offset + 16U;
        for (std::uint32_t vertex_index = 0; vertex_index < submesh.vertex_count; ++vertex_index)
        {
            const auto base = vertex_data.payload + static_cast<std::size_t>(vertex_index) * submesh.vertex_stride;
            AloVertex vertex;
            vertex.position = read_vector3(bytes, base);
            vertex.normal = read_vector3(bytes, base + 12);
            for (std::size_t coordinate = 0; coordinate < 4; ++coordinate)
            {
                vertex.texture_coordinates[coordinate] = {read_float(bytes, base + 24 + coordinate * 8), read_float(bytes, base + 28 + coordinate * 8)};
            }
            vertex.tangent = read_vector3(bytes, base + 56);
            vertex.binormal = read_vector3(bytes, base + 68);
            vertex.color = {read_float(bytes, base + 80), read_float(bytes, base + 84), read_float(bytes, base + 88), read_float(bytes, base + 92)};
            for (std::size_t influence = 0; influence < 4; ++influence)
            {
                vertex.bone_indices[influence] = read_u32(bytes, base + bone_offset + influence * 4);
                vertex.bone_weights[influence] = read_float(bytes, base + weight_offset + influence * 4);
            }
            submesh.vertices.push_back(vertex);
        }
    }
    else if (submesh.vertex_count != 0)
    {
        warnings.push_back("Unsupported ALO vertex stride " + std::to_string(submesh.vertex_stride));
    }

    if (has_index_data)
    {
        const auto available = (index_data.end - index_data.payload) / 2U;
        const auto expected = static_cast<std::size_t>(submesh.triangle_count) * 3U;
        const auto count = std::min(available, expected);
        submesh.indices.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto value = read_u16(bytes, index_data.payload + index * 2U);
            submesh.indices.push_back(value);
            if (value >= submesh.vertex_count)
            {
                warnings.emplace_back("ALO submesh contains an out-of-range vertex index");
                break;
            }
        }
        if (available != expected)
        {
            warnings.push_back("ALO index count does not match its triangle count");
        }
    }
    if (has_skin_data)
    {
        submesh.skin_bones.reserve(submesh.skin_bone_count);
        for (std::uint32_t index = 0; index < submesh.skin_bone_count; ++index)
        {
            submesh.skin_bones.push_back(read_u32(bytes, skin_data.payload + index * 4U));
        }
    }
    return submesh;
}

Result<AloBone> parse_bone(const std::span<const std::byte> bytes, const Chunk& chunk)
{
    AloBone bone;
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    if (const auto* name = find_chunk(children, 0x203))
    {
        bone.name = read_string(bytes, name->payload, name->end);
    }
    auto* data = find_chunk(children, 0x206);
    if (data == nullptr)
    {
        data = find_chunk(children, 0x205);
    }
    const auto required_size = data != nullptr && data->type == 0x206 ? 60U : 56U;
    if (data == nullptr || data->end - data->payload < required_size)
    {
        return make_error("ALO bone is missing its transform data");
    }
    bone.parent = read_i32(bytes, data->payload);
    bone.visible = read_u32(bytes, data->payload + 4) != 0;
    const bool version_two = data->type == 0x206;
    bone.billboard = version_two ? read_u32(bytes, data->payload + 8) : 0;
    const auto transform = data->payload + (version_two ? 12U : 8U);
    for (std::size_t index = 0; index < bone.transform.size(); ++index)
    {
        bone.transform[index] = read_float(bytes, transform + index * 4U);
    }
    return bone;
}

Status parse_skeleton(const std::span<const std::byte> bytes, const Chunk& chunk, AloModelData& model)
{
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    std::uint32_t declared = 0;
    if (const auto* info = find_chunk(children, 0x201); info != nullptr && info->end - info->payload >= 4)
    {
        declared = read_u32(bytes, info->payload);
    }
    for (const auto& child : children)
    {
        if (child.type == 0x202)
        {
            EAW_TRY_ASSIGN(auto bone, parse_bone(bytes, child));
            model.bones.push_back(std::move(bone));
        }
    }
    if (declared != model.bones.size())
    {
        model.warnings.push_back("Skeleton declares " + std::to_string(declared) + " bones but contains " + std::to_string(model.bones.size()));
    }
    return Status::success();
}

Result<AloMesh> parse_mesh(const std::span<const std::byte> bytes, const Chunk& chunk, AloModelData& model)
{
    AloMesh mesh;
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    std::uint32_t declared_materials = 0;
    if (const auto* name = find_chunk(children, 0x401))
    {
        mesh.name = read_string(bytes, name->payload, name->end);
    }
    if (const auto* info = find_chunk(children, 0x402); info != nullptr && info->end - info->payload >= 40)
    {
        declared_materials = read_u32(bytes, info->payload);
        mesh.bounds.minimum = read_vector3(bytes, info->payload + 4);
        mesh.bounds.maximum = read_vector3(bytes, info->payload + 16);
        mesh.hidden = read_u32(bytes, info->payload + 32) != 0;
        mesh.collision_enabled = read_u32(bytes, info->payload + 36) != 0;
    }

    Material pending;
    bool has_material = false;
    for (const auto& child : children)
    {
        if (child.type == 0x10100)
        {
            EAW_TRY_ASSIGN(pending, parse_material(bytes, child));
            has_material = true;
        }
        else if (child.type == 0x10000)
        {
            EAW_TRY_ASSIGN(auto submesh, parse_submesh(bytes, child, has_material ? std::move(pending) : Material{}, model.warnings));
            mesh.submeshes.push_back(std::move(submesh));
            has_material = false;
        }
    }
    if (declared_materials != mesh.submeshes.size())
    {
        model.warnings.push_back("Mesh " + mesh.name + " declares " + std::to_string(declared_materials) + " submeshes but contains " + std::to_string(mesh.submeshes.size()));
    }
    return mesh;
}

Result<AloLight> parse_light(const std::span<const std::byte> bytes, const Chunk& chunk)
{
    AloLight light;
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    if (const auto* name = find_chunk(children, 0x1301))
    {
        light.name = read_string(bytes, name->payload, name->end);
    }
    if (const auto* data = find_chunk(children, 0x1302); data != nullptr && data->end - data->payload >= 20)
    {
        light.type = read_u32(bytes, data->payload);
        light.color = read_vector3(bytes, data->payload + 4);
        light.intensity = read_float(bytes, data->payload + 16);
    }
    return light;
}

Status parse_connections(const std::span<const std::byte> bytes, const Chunk& chunk, AloModelData& model, const std::vector<std::pair<bool, std::size_t>>& objects)
{
    EAW_TRY_ASSIGN(const auto children, read_chunks(bytes, chunk.payload, chunk.end));
    for (const auto& child : children)
    {
        if (child.type == 0x602)
        {
            EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, child));
            const auto* object = find_mini(minis, 2);
            const auto* bone = find_mini(minis, 3);
            if (object == nullptr || bone == nullptr || object->end - object->payload < 4 || bone->end - bone->payload < 4)
            {
                continue;
            }
            const auto object_index = read_u32(bytes, object->payload);
            const auto bone_index = read_u32(bytes, bone->payload);
            if (object_index >= objects.size())
            {
                model.warnings.push_back("Object connection references an invalid object");
                continue;
            }
            const auto [is_mesh, index] = objects[object_index];
            if (is_mesh)
            {
                model.meshes[index].attached_bone = bone_index;
            }
            else
            {
                model.lights[index].attached_bone = bone_index;
            }
        }
        else if (child.type == 0x603)
        {
            EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, child));
            AloProxy proxy;
            if (const auto* value = find_mini(minis, 5))
            {
                proxy.name = read_string(bytes, value->payload, value->end);
            }
            if (const auto* value = find_mini(minis, 6); value != nullptr && value->end - value->payload >= 4)
            {
                proxy.bone = read_u32(bytes, value->payload);
            }
            if (const auto* value = find_mini(minis, 7); value != nullptr && value->end - value->payload >= 4)
            {
                proxy.hidden = read_u32(bytes, value->payload) != 0;
            }
            if (const auto* value = find_mini(minis, 8); value != nullptr && value->end - value->payload >= 4)
            {
                proxy.alt_decrease_stay_hidden = read_u32(bytes, value->payload) != 0;
            }
            model.proxies.push_back(std::move(proxy));
        }
        else if (child.type == 0x604)
        {
            EAW_TRY_ASSIGN(const auto nested, read_chunks(bytes, child.payload, child.end));
            const auto* data = find_chunk(nested, 0);
            if (data == nullptr)
            {
                continue;
            }
            EAW_TRY_ASSIGN(const auto minis, read_minis(bytes, *data));
            AloDazzle dazzle;
            if (const auto* value = find_mini(minis, 11))
            {
                dazzle.name = read_string(bytes, value->payload, value->end);
            }
            if (const auto* value = find_mini(minis, 5))
            {
                dazzle.texture = read_string(bytes, value->payload, value->end);
            }
            if (const auto* value = find_mini(minis, 10); value != nullptr && value->end - value->payload >= 4)
            {
                dazzle.bone = read_u32(bytes, value->payload);
            }
            if (const auto* value = find_mini(minis, 12); value != nullptr && value->end - value->payload >= 4)
            {
                dazzle.hidden = read_u32(bytes, value->payload) != 0;
            }
            model.dazzles.push_back(std::move(dazzle));
        }
    }
    return Status::success();
}

} // namespace

template <> Result<AloModelData> read_alo<AloModelData>(const std::span<const std::byte> bytes)
{
    AloModelData model;
    EAW_TRY_ASSIGN(const auto chunks, read_chunks(bytes, 0, bytes.size()));
    const auto* skeleton = find_chunk(chunks, 0x200);
    if (skeleton == nullptr)
    {
        return make_error("ALO is not a semantic model file (it may be a particle ALO)");
    }
    EAW_TRY(parse_skeleton(bytes, *skeleton, model));

    std::vector<std::pair<bool, std::size_t>> objects;
    const Chunk* connections = nullptr;
    for (const auto& chunk : chunks)
    {
        if (chunk.type == 0x400)
        {
            EAW_TRY_ASSIGN(auto mesh, parse_mesh(bytes, chunk, model));
            model.meshes.push_back(std::move(mesh));
            objects.emplace_back(true, model.meshes.size() - 1);
        }
        else if (chunk.type == 0x1300)
        {
            EAW_TRY_ASSIGN(auto light, parse_light(bytes, chunk));
            model.lights.push_back(std::move(light));
            objects.emplace_back(false, model.lights.size() - 1);
        }
        else if (chunk.type == 0x600)
        {
            connections = &chunk;
        }
    }
    if (connections != nullptr)
    {
        EAW_TRY(parse_connections(bytes, *connections, model, objects));
    }
    return model;
}

} // namespace eaw
