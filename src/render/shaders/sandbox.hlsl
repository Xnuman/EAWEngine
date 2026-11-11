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
    uint pick_id;
    // Detail-level cross-fade (d3d12_renderer_impl.hpp GpuInstance::fade).
    float fade;
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
    float4 color : COLOR;
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
    float4 vertex_color : COLOR2;
    nointerpolation uint pick_id : PICKID;
    nointerpolation uint instance_flags : INSTANCEFLAGS;
    nointerpolation float fade : FADE;
};

struct PixelOutput
{
    float4 color : SV_TARGET0;
    uint2 pick : SV_TARGET1; // (pick id, depth bits)
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
    output.vertex_color = input.color;
    output.pick_id = instance.pick_id;
    output.instance_flags = instance.flags;
    output.fade = instance.fade;
    return output;
}


float4 shade(PixelInput input)
{
    const MaterialData material = materials[material_index];
    const float2 uv = input.uv * material.specular.w + material.uv_scroll * camera_position_time.w;
    const float4 base = sample_texture(material.base_texture, uv);
    const float4 tint = material.tint * input.tint * input.vertex_color;
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
        normal_sample = sample_texture(material.normal_texture, uv);
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
        gloss = sample_texture(material.gloss_texture, uv).r;
    }
    float3 color = legacy_lit_color(albedo, normal, input.world_position, gloss, material.diffuse.rgb, material.specular.rgb, material.emissive.rgb, tint.rgb);
    if ((material.flags & MATERIAL_EMISSIVE_MASK) != 0u)
    {
        color += material.specular.rgb * base.a;
    }
    const float alpha = (material.flags & MATERIAL_ALPHA_BLEND) != 0u ? base.a * material.diffuse.a * tint.a : 1.0f;
    return float4(color, alpha);
}

// The ordered (Bayer) 4x4 dither matrix: every threshold once, neighbours far apart.
static const uint dither_4x4[16] = {0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
PixelOutput ps_main(PixelInput input)
{
    // A detail level cross-fading in keeps the pixels whose dither threshold lies below its share;
    // the level fading out keeps exactly the others, so each pixel shows one of the two.
    if (input.fade != 0.0f)
    {
        const uint2 cell = uint2(input.position.xy) & 3u;
        const float threshold = (dither_4x4[cell.y * 4u + cell.x] + 0.5f) / 16.0f;
        if ((input.fade > 0.0f) ? (threshold >= input.fade) : (threshold < -input.fade))
        {
            discard;
        }
    }
    // Blended pipelines mask the pick target, so only opaque and alpha-tested surfaces are pickable.
    PixelOutput output;
    output.color = shade(input);
    output.color.rgb = apply_selection_highlight(output.color.rgb, input.pick_id, input.instance_flags);
    output.pick = uint2(input.pick_id, asuint(input.position.z));
    return output;
}
