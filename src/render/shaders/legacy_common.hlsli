// Shared declarations for EAWEngine legacy-mode D3D12 shaders: the root signature, per-frame and
// per-draw constants, the material table, and the lighting model. Written for this engine; no
// legacy shader code is used.

#ifndef EAW_LEGACY_COMMON_HLSLI
#define EAW_LEGACY_COMMON_HLSLI

#define SANDBOX_ROOT_SIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = 4096), visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t1, visibility = SHADER_VISIBILITY_VERTEX), " \
    "SRV(t2, visibility = SHADER_VISIBILITY_VERTEX), " \
    "SRV(t3), " \
    "RootConstants(num32BitConstants = 2, b1), " \
    "SRV(t4, visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, filter = FILTER_ANISOTROPIC, maxAnisotropy = 8, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, " \
    "addressW = TEXTURE_ADDRESS_WRAP, comparisonFunc = COMPARISON_ALWAYS, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, " \
    "visibility = SHADER_VISIBILITY_PIXEL)"

// Material flags (keep in sync with d3d12_renderer.cpp).
static const uint MATERIAL_ALPHA_TEST = 1u << 0;
static const uint MATERIAL_ALPHA_BLEND = 1u << 1;
static const uint MATERIAL_ADDITIVE = 1u << 2;
static const uint MATERIAL_TEAM_COLOR = 1u << 3;
static const uint MATERIAL_EMISSIVE_MASK = 1u << 4;
static const uint GLOSS_SHIFT = 8u;
static const uint GLOSS_NONE = 0u;
static const uint GLOSS_BASE_ALPHA = 1u;
static const uint GLOSS_NORMAL_ALPHA = 2u;
static const uint GLOSS_RED = 3u;
static const uint LIGHTING_UNLIT = 0u;
static const uint LIGHTING_LIT = 1u;
static const uint LIGHTING_NORMAL_MAPPED = 2u;
static const uint INSTANCE_BACKGROUND = 1u;
static const uint INSTANCE_HIGHLIGHTED = 2u;

cbuffer FrameConstants : register(b0)
{
    float4x4 view_projection;
    float4 camera_position_time; // xyz camera position, w scene time in seconds
    float4 key_direction;        // xyz unit vector from surface toward the light
    float4 key_diffuse;
    float4 key_specular;
    float4 ambient;
    float4 fill_direction[2];    // unit vectors toward the fill lights
    float4 fill_diffuse[2];
    uint4 selection; // x: pick id drawn highlighted (0 = none)
    float4 camera_right; // world-space view basis for camera-facing particles
    float4 camera_up;
};

// Pick ids written to the selection target: 0 is nothing, scene instance index + 1 an object, and
// PICK_TERRAIN the terrain. Keep in sync with d3d12_renderer.cpp.
static const uint PICK_TERRAIN = 0xFFFFFFFFu;

// Highlighted when the instance is flagged (selection groups) or is the single highlighted pick id.
float3 apply_selection_highlight(const float3 color, const uint pick_id, const uint instance_flags)
{
    const bool highlighted = (instance_flags & INSTANCE_HIGHLIGHTED) != 0u || (pick_id != 0u && pick_id == selection.x);
    return highlighted ? lerp(color, float3(1.0f, 0.82f, 0.25f), 0.35f) : color;
}

cbuffer DrawConstants : register(b1)
{
    uint instance_base;
    uint material_index;
};

struct MaterialData
{
    float4 tint;
    float4 diffuse;
    float4 specular; // rgb highlight color, w texture-coordinate scale
    float4 emissive;
    float2 uv_scroll;
    float alpha_cutoff;
    uint flags;
    uint base_texture;
    uint normal_texture;
    uint gloss_texture;
    uint lighting;
};

Texture2D scene_textures[4096] : register(t0, space1);
StructuredBuffer<MaterialData> materials : register(t3);
// Logical texture id -> descriptor slot for this frame. Materials name logical ids so a texture can
// stream in (or fall back) without rewriting descriptors or materials.
StructuredBuffer<uint> texture_residency : register(t4);
SamplerState surface_sampler : register(s0);

float4 sample_texture(const uint texture, const float2 uv)
{
    return scene_textures[NonUniformResourceIndex(texture_residency[texture])].Sample(surface_sampler, uv);
}

float3 safe_normalize(const float3 value, const float3 fallback)
{
    const float length_squared = dot(value, value);
    return length_squared > 1.0e-12f ? value * rsqrt(length_squared) : fallback;
}

// Legacy-mode surface lighting shared by meshes and terrain: ambient plus two diffuse fill lights
// and a key light, the 2x diffuse convention, and an exponent-16 highlight scaled by gloss.
float3 legacy_lit_color(const float3 albedo, const float3 normal, const float3 world_position, const float gloss, const float3 diffuse, const float3 specular,
                        const float3 emissive, const float3 tint)
{
    const float3 view = safe_normalize(camera_position_time.xyz - world_position, float3(0.0f, 0.0f, 1.0f));
    const float3 light = safe_normalize(key_direction.xyz, float3(0.0f, 0.0f, 1.0f));
    const float3 half_vector = safe_normalize(light + view, normal);
    const float3 fill = ambient.rgb + saturate(dot(normal, fill_direction[0].xyz)) * fill_diffuse[0].rgb + saturate(dot(normal, fill_direction[1].xyz)) * fill_diffuse[1].rgb;
    const float3 diffuse_light = diffuse * (fill + saturate(dot(normal, light)) * key_diffuse.rgb) + emissive;
    const float3 highlight = specular * key_specular.rgb * pow(saturate(dot(normal, half_vector)), 16.0f) * gloss;
    // Legacy 2x diffuse convention: lighting near 0.5 reproduces the texture color.
    return albedo * diffuse_light * 2.0f * tint + highlight;
}

#endif
