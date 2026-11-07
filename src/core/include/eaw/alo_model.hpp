#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct AloVector3
{
    float x{};
    float y{};
    float z{};
};

struct AloVector2
{
    float x{};
    float y{};
};

struct AloVector4
{
    float x{};
    float y{};
    float z{};
    float w{};
};

struct AloQuaternion
{
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

struct AloMatrix4
{
    std::array<float, 16> values{};
};

// Mesh/bone naming convention for state variants: a trailing "_ALT<n>" marks alternate states
// (stock models use ALT0 for intact and higher numbers for progressively damaged states) and
// "_LOD<n>" marks detail levels. Matching is case-insensitive and needs at least one digit.
enum class AloVariantKind : std::uint8_t
{
    none,
    alternate,
    level_of_detail,
};

struct AloNameVariant
{
    AloVariantKind kind{AloVariantKind::none};
    std::uint32_t index{};
};

[[nodiscard]] AloNameVariant alo_name_variant(std::string_view name) noexcept;

[[nodiscard]] AloMatrix4 alo_identity_matrix() noexcept;
[[nodiscard]] AloMatrix4 alo_multiply(const AloMatrix4& left, const AloMatrix4& right) noexcept;
[[nodiscard]] AloMatrix4 alo_inverse(const AloMatrix4& matrix);
[[nodiscard]] AloVector3 alo_transform_point(const AloVector3& point, const AloMatrix4& matrix) noexcept;
[[nodiscard]] AloMatrix4 alo_compose_transform(const AloVector3& scale, const AloQuaternion& rotation, const AloVector3& translation) noexcept;

struct AloVertex
{
    AloVector3 position;
    AloVector3 normal;
    std::array<AloVector2, 4> texture_coordinates{};
    AloVector3 tangent;
    AloVector3 binormal;
    AloVector4 color;
    std::array<std::uint32_t, 4> bone_indices{};
    std::array<float, 4> bone_weights{};
};

struct AloBounds
{
    AloVector3 minimum;
    AloVector3 maximum;
};

struct AloBone
{
    std::string name;
    std::int32_t parent{-1};
    bool visible{};
    std::uint32_t billboard{};
    std::array<float, 12> transform{};
};

enum class AloShaderParameterType
{
    integer,
    scalar,
    vector3,
    texture,
    vector4,
    unknown,
};

struct AloShaderParameter
{
    std::string name;
    AloShaderParameterType type{AloShaderParameterType::unknown};
    // Texture name, or the numeric components as text (for display and diagnostics).
    std::string value;
    // Exact numeric components (integers converted to float). Empty for textures and for data
    // chunks shorter than the parameter type requires.
    std::vector<float> values;
};

struct AloSubMesh
{
    std::string shader;
    std::vector<AloShaderParameter> parameters;
    std::string vertex_format;
    std::uint32_t vertex_count{};
    std::uint32_t triangle_count{};
    std::uint32_t vertex_stride{};
    std::uint32_t skin_bone_count{};
    std::vector<AloVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<std::uint32_t> skin_bones;
};

struct AloMesh
{
    std::string name;
    AloBounds bounds;
    bool hidden{};
    bool collision_enabled{};
    std::optional<std::uint32_t> attached_bone;
    std::vector<AloSubMesh> submeshes;
};

struct AloLight
{
    std::string name;
    std::uint32_t type{};
    AloVector3 color;
    float intensity{};
    std::optional<std::uint32_t> attached_bone;
};

struct AloProxy
{
    std::string name;
    std::uint32_t bone{};
    bool hidden{};
    bool alt_decrease_stay_hidden{};
};

struct AloDazzle
{
    std::string name;
    std::string texture;
    std::uint32_t bone{};
    bool hidden{};
};

class AloModel final
{
public:
    [[nodiscard]] static AloModel parse(std::span<const std::byte> bytes);

    std::vector<AloBone> bones;
    std::vector<AloMesh> meshes;
    std::vector<AloLight> lights;
    std::vector<AloProxy> proxies;
    std::vector<AloDazzle> dazzles;
    std::vector<std::string> warnings;

    [[nodiscard]] std::size_t submesh_count() const noexcept;
    [[nodiscard]] std::uint64_t vertex_count() const noexcept;
    [[nodiscard]] std::uint64_t triangle_count() const noexcept;
    [[nodiscard]] std::vector<AloMatrix4> local_bind_pose() const;
    [[nodiscard]] std::vector<AloMatrix4> bind_pose() const;
};

// Which meshes form a model's default appearance: not flagged hidden, not attached to a bone
// hidden in the bind pose, alternate (damage) state ALT0, and the model's highest LOD index. In the
// stock corpus higher LOD indices carry more triangles (LOD0 is the sparsest level in 137 of 145
// models with LOD meshes). The variant name is taken from the mesh, or from its attached bone when
// the mesh name carries none. Callers that animate bone visibility pass apply_bind_pose_bones =
// false and test the sampled pose instead.
[[nodiscard]] std::vector<bool> alo_default_mesh_visibility(const AloModel& model, bool apply_bind_pose_bones = true);

} // namespace eaw
