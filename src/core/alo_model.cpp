#include "eaw/alo_model.hpp"

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

struct Chunk
{
    std::uint32_t type{};
    bool container{};
    std::size_t payload{};
    std::size_t end{};
};

struct MiniChunk
{
    std::uint8_t type{};
    std::size_t payload{};
    std::size_t end{};
};

// Value readers: every caller checks the enclosing chunk or mini-chunk size first, so an
// out-of-range read is a bug.
std::uint32_t read_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    EAW_ASSERT_MSG(offset + 4 <= bytes.size(), "ALO ended while reading a 32-bit value");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

std::int32_t read_i32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return static_cast<std::int32_t>(read_u32(bytes, offset));
}

std::uint16_t read_u16(const std::span<const std::byte> bytes, const std::size_t offset)
{
    EAW_ASSERT_MSG(offset + 2 <= bytes.size(), "ALO ended while reading a 16-bit value");
    return static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset]) | (std::to_integer<unsigned char>(bytes[offset + 1]) << 8U));
}

float read_float(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return std::bit_cast<float>(read_u32(bytes, offset));
}

AloVector3 read_vector3(const std::span<const std::byte> bytes, const std::size_t offset)
{
    return {read_float(bytes, offset), read_float(bytes, offset + 4), read_float(bytes, offset + 8)};
}

std::string read_string(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    EAW_ASSERT_MSG(begin <= end && end <= bytes.size(), "ALO string range is invalid");
    std::string value;
    value.reserve(end - begin);
    for (auto offset = begin; offset < end; ++offset)
    {
        const auto character = std::to_integer<unsigned char>(bytes[offset]);
        if (character == 0)
        {
            break;
        }
        value.push_back(static_cast<char>(character));
    }
    return value;
}

Result<std::vector<Chunk>> read_chunks(const std::span<const std::byte> bytes, const std::size_t begin, const std::size_t end)
{
    if (begin > end || end > bytes.size())
    {
        return make_error("ALO chunk range is invalid");
    }
    std::vector<Chunk> chunks;
    auto cursor = begin;
    while (cursor < end)
    {
        if (end - cursor < 8)
        {
            return make_error("ALO contains a truncated chunk header");
        }
        const auto type = read_u32(bytes, cursor);
        const auto encoded_size = read_u32(bytes, cursor + 4);
        const auto size = static_cast<std::size_t>(encoded_size & 0x7fffffffU);
        const auto payload = cursor + 8;
        if (size > end - payload)
        {
            std::ostringstream message;
            message << "ALO chunk 0x" << std::hex << type << " extends past its parent";
            return make_error(message.str());
        }
        chunks.push_back({type, (encoded_size & 0x80000000U) != 0, payload, payload + size});
        cursor = payload + size;
    }
    return chunks;
}

Result<std::vector<MiniChunk>> read_minis(const std::span<const std::byte> bytes, const Chunk& chunk)
{
    std::vector<MiniChunk> chunks;
    auto cursor = chunk.payload;
    while (cursor < chunk.end)
    {
        if (chunk.end - cursor < 2)
        {
            return make_error("ALO contains a truncated mini-chunk header");
        }
        const auto type = std::to_integer<std::uint8_t>(bytes[cursor]);
        const auto size = std::to_integer<std::uint8_t>(bytes[cursor + 1]);
        const auto payload = cursor + 2;
        if (size > chunk.end - payload)
        {
            return make_error("ALO mini-chunk extends past its parent");
        }
        chunks.push_back({type, payload, payload + size});
        cursor = payload + size;
    }
    return chunks;
}

const Chunk* find_chunk(const std::vector<Chunk>& chunks, const std::uint32_t type)
{
    const auto found = std::ranges::find(chunks, type, &Chunk::type);
    return found == chunks.end() ? nullptr : &*found;
}

const MiniChunk* find_mini(const std::vector<MiniChunk>& chunks, const std::uint8_t type)
{
    const auto found = std::ranges::find(chunks, type, &MiniChunk::type);
    return found == chunks.end() ? nullptr : &*found;
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

Status parse_skeleton(const std::span<const std::byte> bytes, const Chunk& chunk, AloModel& model)
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

Result<AloMesh> parse_mesh(const std::span<const std::byte> bytes, const Chunk& chunk, AloModel& model)
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

Status parse_connections(const std::span<const std::byte> bytes, const Chunk& chunk, AloModel& model, const std::vector<std::pair<bool, std::size_t>>& objects)
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

AloMatrix4 alo_identity_matrix() noexcept
{
    AloMatrix4 matrix;
    matrix.values[0] = 1.0F;
    matrix.values[5] = 1.0F;
    matrix.values[10] = 1.0F;
    matrix.values[15] = 1.0F;
    return matrix;
}

AloNameVariant alo_name_variant(const std::string_view name) noexcept
{
    std::size_t digits = 0;
    while (digits < name.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(name[name.size() - 1 - digits])) != 0)
    {
        ++digits;
    }
    if (digits == 0 || digits == 9 || name.size() < digits + 4)
    {
        return {};
    }
    const auto marker = name.substr(name.size() - digits - 4, 4);
    const auto upper = [](const char character)
    {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    };
    std::string folded(marker);
    std::ranges::transform(folded, folded.begin(), upper);
    AloVariantKind kind = AloVariantKind::none;
    if (folded == "_ALT")
    {
        kind = AloVariantKind::alternate;
    }
    else if (folded == "_LOD")
    {
        kind = AloVariantKind::level_of_detail;
    }
    else
    {
        return {};
    }
    std::uint32_t index = 0;
    for (const auto character : name.substr(name.size() - digits))
    {
        index = index * 10U + static_cast<std::uint32_t>(character - '0');
    }
    return {kind, index};
}

std::vector<bool> alo_default_mesh_visibility(const AloModel& model, const bool apply_bind_pose_bones)
{
    const auto variant_of = [&](const AloMesh& mesh)
    {
        auto variant = alo_name_variant(mesh.name);
        if (variant.kind == AloVariantKind::none && mesh.attached_bone && *mesh.attached_bone < model.bones.size())
        {
            variant = alo_name_variant(model.bones[*mesh.attached_bone].name);
        }
        return variant;
    };
    std::uint32_t highest_lod = 0;
    for (const auto& mesh : model.meshes)
    {
        if (const auto variant = variant_of(mesh); variant.kind == AloVariantKind::level_of_detail)
        {
            highest_lod = std::max(highest_lod, variant.index);
        }
    }
    std::vector<bool> visible;
    visible.reserve(model.meshes.size());
    for (const auto& mesh : model.meshes)
    {
        const auto variant = variant_of(mesh);
        const bool hidden_bone = apply_bind_pose_bones && mesh.attached_bone && *mesh.attached_bone < model.bones.size() && !model.bones[*mesh.attached_bone].visible;
        const bool other_state = variant.kind == AloVariantKind::alternate && variant.index != 0;
        const bool other_lod = variant.kind == AloVariantKind::level_of_detail && variant.index != highest_lod;
        visible.push_back(!mesh.hidden && !hidden_bone && !other_state && !other_lod);
    }
    return visible;
}

AloMatrix4 alo_multiply(const AloMatrix4& left, const AloMatrix4& right) noexcept
{
    AloMatrix4 result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                value += left.values[row * 4 + inner] * right.values[inner * 4 + column];
            }
            result.values[row * 4 + column] = value;
        }
    }
    return result;
}

Result<AloMatrix4> alo_inverse(const AloMatrix4& matrix)
{
    float augmented[4][8]{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = matrix.values[row * 4 + column];
        }
        augmented[row][row + 4] = 1.0F;
    }
    for (std::size_t pivot = 0; pivot < 4; ++pivot)
    {
        auto best = pivot;
        for (std::size_t row = pivot + 1; row < 4; ++row)
        {
            if (std::fabs(augmented[row][pivot]) > std::fabs(augmented[best][pivot]))
            {
                best = row;
            }
        }
        if (std::fabs(augmented[best][pivot]) < 1.0e-8F)
        {
            return make_error("ALO bone transform is singular");
        }
        if (best != pivot)
        {
            for (std::size_t column = 0; column < 8; ++column)
            {
                std::swap(augmented[pivot][column], augmented[best][column]);
            }
        }
        const auto divisor = augmented[pivot][pivot];
        for (float& value : augmented[pivot])
        {
            value /= divisor;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == pivot)
            {
                continue;
            }
            const auto factor = augmented[row][pivot];
            for (std::size_t column = 0; column < 8; ++column)
            {
                augmented[row][column] -= factor * augmented[pivot][column];
            }
        }
    }
    AloMatrix4 result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.values[row * 4 + column] = augmented[row][column + 4];
        }
    }
    return result;
}

AloVector3 alo_transform_point(const AloVector3& point, const AloMatrix4& matrix) noexcept
{
    return {point.x * matrix.values[0] + point.y * matrix.values[4] + point.z * matrix.values[8] + matrix.values[12],
            point.x * matrix.values[1] + point.y * matrix.values[5] + point.z * matrix.values[9] + matrix.values[13],
            point.x * matrix.values[2] + point.y * matrix.values[6] + point.z * matrix.values[10] + matrix.values[14]};
}

AloTransformParts alo_decompose_transform(const AloMatrix4& matrix) noexcept
{
    const auto& m = matrix.values;
    AloTransformParts parts;
    parts.translation = {m[12], m[13], m[14]};
    const auto length = [](const float x, const float y, const float z)
    {
        return std::sqrt(x * x + y * y + z * z);
    };
    parts.scale = {length(m[0], m[1], m[2]), length(m[4], m[5], m[6]), length(m[8], m[9], m[10])};
    const auto determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (determinant < 0.0F)
    {
        parts.scale.x = -parts.scale.x;
    }
    if (parts.scale.x == 0.0F || parts.scale.y == 0.0F || parts.scale.z == 0.0F || !std::isfinite(determinant))
    {
        return parts;
    }
    // Rows of the rotation (row-vector convention, as alo_compose_transform writes them).
    const std::array<float, 9> r{m[0] / parts.scale.x, m[1] / parts.scale.x, m[2] / parts.scale.x, m[4] / parts.scale.y, m[5] / parts.scale.y,
                                 m[6] / parts.scale.y, m[8] / parts.scale.z, m[9] / parts.scale.z, m[10] / parts.scale.z};
    const auto trace = r[0] + r[4] + r[8];
    AloQuaternion q;
    if (trace > 0.0F)
    {
        const auto s = 0.5F / std::sqrt(trace + 1.0F);
        q = {(r[5] - r[7]) * s, (r[6] - r[2]) * s, (r[1] - r[3]) * s, 0.25F / s};
    }
    else if (r[0] > r[4] && r[0] > r[8])
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r[0] - r[4] - r[8], 0.0F));
        q = {0.25F * s, (r[1] + r[3]) / s, (r[2] + r[6]) / s, (r[5] - r[7]) / s};
    }
    else if (r[4] > r[8])
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r[4] - r[0] - r[8], 0.0F));
        q = {(r[1] + r[3]) / s, 0.25F * s, (r[5] + r[7]) / s, (r[6] - r[2]) / s};
    }
    else
    {
        const auto s = 2.0F * std::sqrt(std::max(1.0F + r[8] - r[0] - r[4], 0.0F));
        q = {(r[2] + r[6]) / s, (r[5] + r[7]) / s, 0.25F * s, (r[1] - r[3]) / s};
    }
    const auto norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (norm > 0.0F && std::isfinite(norm))
    {
        parts.rotation = {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
    }
    return parts;
}

AloMatrix4 alo_compose_transform(const AloVector3& scale, const AloQuaternion& rotation, const AloVector3& translation) noexcept
{
    const auto xx = rotation.x * rotation.x;
    const auto yy = rotation.y * rotation.y;
    const auto zz = rotation.z * rotation.z;
    const auto xy = rotation.x * rotation.y;
    const auto xz = rotation.x * rotation.z;
    const auto yz = rotation.y * rotation.z;
    const auto wx = rotation.w * rotation.x;
    const auto wy = rotation.w * rotation.y;
    const auto wz = rotation.w * rotation.z;
    auto matrix = alo_identity_matrix();
    matrix.values[0] = (1.0F - 2.0F * (yy + zz)) * scale.x;
    matrix.values[1] = (2.0F * (xy + wz)) * scale.x;
    matrix.values[2] = (2.0F * (xz - wy)) * scale.x;
    matrix.values[4] = (2.0F * (xy - wz)) * scale.y;
    matrix.values[5] = (1.0F - 2.0F * (xx + zz)) * scale.y;
    matrix.values[6] = (2.0F * (yz + wx)) * scale.y;
    matrix.values[8] = (2.0F * (xz + wy)) * scale.z;
    matrix.values[9] = (2.0F * (yz - wx)) * scale.z;
    matrix.values[10] = (1.0F - 2.0F * (xx + yy)) * scale.z;
    matrix.values[12] = translation.x;
    matrix.values[13] = translation.y;
    matrix.values[14] = translation.z;
    return matrix;
}

Result<AloModel> AloModel::parse(const std::span<const std::byte> bytes)
{
    AloModel model;
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

std::size_t AloModel::submesh_count() const noexcept
{
    std::size_t count = 0;
    for (const auto& mesh : meshes)
    {
        count += mesh.submeshes.size();
    }
    return count;
}

std::uint64_t AloModel::vertex_count() const noexcept
{
    std::uint64_t count = 0;
    for (const auto& mesh : meshes)
    {
        for (const auto& submesh : mesh.submeshes)
        {
            count += submesh.vertex_count;
        }
    }
    return count;
}

std::uint64_t AloModel::triangle_count() const noexcept
{
    std::uint64_t count = 0;
    for (const auto& mesh : meshes)
    {
        for (const auto& submesh : mesh.submeshes)
        {
            count += submesh.triangle_count;
        }
    }
    return count;
}

std::vector<AloMatrix4> AloModel::local_bind_pose() const
{
    std::vector<AloMatrix4> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
    {
        auto local = alo_identity_matrix();
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                local.values[column * 4 + row] = bone.transform[row * 4 + column];
            }
        }
        pose.push_back(local);
    }
    return pose;
}

std::vector<AloMatrix4> AloModel::bind_pose() const
{
    auto local_pose = local_bind_pose();
    std::vector<AloMatrix4> pose;
    pose.reserve(bones.size());
    for (std::size_t index = 0; index < bones.size(); ++index)
    {
        if (bones[index].parent >= 0 && static_cast<std::size_t>(bones[index].parent) < pose.size())
        {
            pose.push_back(alo_multiply(local_pose[index], pose[static_cast<std::size_t>(bones[index].parent)]));
        }
        else
        {
            pose.push_back(local_pose[index]);
        }
    }
    return pose;
}

} // namespace eaw
