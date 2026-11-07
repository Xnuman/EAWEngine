// EAWEngine D3D12 legacy-mode surface shaders: instanced, skinned geometry with engine-native
// materials (see VisualMaterial). The lighting approximates how Alamo materials were meant to
// look -- ambient plus two fill lights and a directional key light, a 2x diffuse convention, and a
// fixed exponent-16 highlight -- as documented in docs/legacy-shader-mapping.md. This file is
// written for this engine; no legacy shader code is used.
//
// Compiled offline by DXC (shader model 6.0) into generated headers. The root signature and shared
// lighting live in legacy_common.hlsli.

#include "legacy_common.hlsli"

struct BoneTransform
{
    row_major float4x4 transform;
};

struct InstanceData
{
    row_major float4x4 transform;
    float4 tint;
    float4 team_color;
    uint bone_offset;
    uint flags;
    uint2 padding;
};

StructuredBuffer<BoneTransform> bones : register(t1);
StructuredBuffer<InstanceData> instances : register(t2);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_indices : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    uint instance_id : SV_InstanceID;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 world_position : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 uv : TEXCOORD0;
    float4 tint : COLOR0;
    float4 team_color : COLOR1;
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
PixelInput vs_main(VertexInput input)
{
    // SV_InstanceID restarts at zero for every draw; the batch's first instance is a root constant.
    const InstanceData instance = instances[instance_base + input.instance_id];

    float4 position = 0.0f;
    float3 normal = 0.0f;
    float3 tangent = 0.0f;
    float3 binormal = 0.0f;
    float total_weight = 0.0f;
    [unroll]
    for (uint influence = 0; influence < 4; ++influence)
    {
        const float weight = input.bone_weights[influence];
        if (weight <= 0.0f)
        {
            continue;
        }
        const row_major float4x4 skin = bones[input.bone_indices[influence] + instance.bone_offset].transform;
        position += mul(float4(input.position, 1.0f), skin) * weight;
        normal += mul(float4(input.normal, 0.0f), skin).xyz * weight;
        tangent += mul(float4(input.tangent, 0.0f), skin).xyz * weight;
        binormal += mul(float4(input.binormal, 0.0f), skin).xyz * weight;
        total_weight += weight;
    }
    if (total_weight <= 0.000001f)
    {
        position = float4(input.position, 1.0f);
        normal = input.normal;
        tangent = input.tangent;
        binormal = input.binormal;
    }
    else if (abs(total_weight - 1.0f) > 0.0001f)
    {
        position /= total_weight;
        normal /= total_weight;
        tangent /= total_weight;
        binormal /= total_weight;
    }

    PixelInput output;
    if ((instance.flags & INSTANCE_BACKGROUND) != 0u)
    {
        // Backgrounds follow the camera: transforming a direction (w = 0) drops the view
        // translation, and the depth is pinned just inside the far plane.
        const float3 direction = mul(float4(position.xyz, 0.0f), instance.transform).xyz;
        output.position = mul(float4(direction, 0.0f), view_projection);
        output.position.z = output.position.w * 0.999999f;
        output.world_position = camera_position_time.xyz + direction;
    }
    else
    {
        const float4 world = mul(position, instance.transform);
        output.position = mul(world, view_projection);
        output.world_position = world.xyz;
    }
    output.normal = mul(float4(normal, 0.0f), instance.transform).xyz;
    output.tangent = mul(float4(tangent, 0.0f), instance.transform).xyz;
    output.binormal = mul(float4(binormal, 0.0f), instance.transform).xyz;
    output.uv = input.uv;
    output.tint = instance.tint;
    output.team_color = instance.team_color;
    return output;
}


[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 ps_main(PixelInput input) : SV_TARGET
{
    const MaterialData material = materials[material_index];
    const float2 uv = input.uv * material.specular.w + material.uv_scroll * camera_position_time.w;
    const float4 base = scene_textures[material.base_texture].Sample(surface_sampler, uv);
    const float4 tint = material.tint * input.tint;
    if ((material.flags & MATERIAL_ALPHA_TEST) != 0u)
    {
        clip(base.a - material.alpha_cutoff);
    }

    if (material.lighting == LIGHTING_UNLIT)
    {
        const float3 color = base.rgb * material.diffuse.rgb * tint.rgb;
        if ((material.flags & MATERIAL_ADDITIVE) != 0u)
        {
            return float4(color, 1.0f);
        }
        const float alpha = (material.flags & MATERIAL_ALPHA_BLEND) != 0u ? base.a * material.diffuse.a * tint.a : 1.0f;
        return float4(color, alpha);
    }

    float3 albedo = base.rgb;
    if ((material.flags & MATERIAL_TEAM_COLOR) != 0u)
    {
        albedo = lerp(albedo, albedo * input.team_color.rgb, base.a);
    }

    const float3 view = safe_normalize(camera_position_time.xyz - input.world_position, float3(0.0f, 0.0f, 1.0f));
    float3 normal = safe_normalize(input.normal, float3(0.0f, 0.0f, 1.0f));
    // Surfaces are drawn two-sided; light the side that faces the viewer.
    if (dot(normal, view) < 0.0f)
    {
        normal = -normal;
    }
    float4 normal_sample = float4(0.5f, 0.5f, 1.0f, 1.0f);
    if (material.lighting == LIGHTING_NORMAL_MAPPED)
    {
        normal_sample = scene_textures[material.normal_texture].Sample(surface_sampler, uv);
        const float3 tangent_normal = normal_sample.xyz * 2.0f - 1.0f;
        const float3 tangent = safe_normalize(input.tangent, float3(1.0f, 0.0f, 0.0f));
        const float3 binormal = safe_normalize(input.binormal, float3(0.0f, 1.0f, 0.0f));
        normal = safe_normalize(tangent_normal.x * tangent + tangent_normal.y * binormal + tangent_normal.z * normal, normal);
    }

    float gloss = 0.0f;
    const uint gloss_source = (material.flags >> GLOSS_SHIFT) & 3u;
    if (gloss_source == GLOSS_BASE_ALPHA)
    {
        gloss = base.a;
    }
    else if (gloss_source == GLOSS_NORMAL_ALPHA)
    {
        gloss = normal_sample.a;
    }
    else if (gloss_source == GLOSS_RED)
    {
        gloss = scene_textures[material.gloss_texture].Sample(surface_sampler, uv).r;
    }
    float3 color = legacy_lit_color(albedo, normal, input.world_position, gloss, material.diffuse.rgb, material.specular.rgb, material.emissive.rgb, tint.rgb);
    if ((material.flags & MATERIAL_EMISSIVE_MASK) != 0u)
    {
        color += material.specular.rgb * base.a;
    }
    const float alpha = (material.flags & MATERIAL_ALPHA_BLEND) != 0u ? base.a * material.diffuse.a * tint.a : 1.0f;
    return float4(color, alpha);
}
