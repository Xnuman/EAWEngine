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
    // Last frame's palette offset and transform, for motion vectors (the current ones when new).
    uint previous_bone_offset;
    row_major float4x4 previous_transform;
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
    // Unjittered clip positions now and last frame (motion vectors).
    float4 current_clip : TEXCOORD1;
    float4 previous_clip : TEXCOORD2;
};

struct PixelOutput
{
    float4 color : SV_TARGET0;
    uint2 pick : SV_TARGET1; // (pick id, depth bits)
    // Screen motion since last frame in UV units (masked for blended surfaces and backgrounds).
    float2 motion : SV_TARGET2;
};

// A vertex's position skinned by the palette at `bone_offset` (its own position without weights).
float4 skinned_position(const VertexInput input, const uint bone_offset)
{
    float4 position = 0.0f;
    float total_weight = 0.0f;
    [unroll]
    for (uint influence = 0; influence < 4; ++influence)
    {
        const float weight = input.bone_weights[influence];
        if (weight <= 0.0f)
        {
            continue;
        }
        position += mul(float4(input.position, 1.0f), bones[input.bone_indices[influence] + bone_offset].transform) * weight;
        total_weight += weight;
    }
    if (total_weight <= 0.000001f)
    {
        return float4(input.position, 1.0f);
    }
    return abs(total_weight - 1.0f) > 0.0001f ? position / total_weight : position;
}

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

    // Last frame's skinned position, from last frame's palette.
    const float4 previous = skinned_position(input, instance.previous_bone_offset);

    PixelInput output;
    if ((instance.flags & INSTANCE_BACKGROUND) != 0u)
    {
        // Backgrounds follow the camera: transforming a direction (w = 0) drops the view
        // translation, and the depth is pinned just inside the far plane.
        const float3 direction = mul(float4(position.xyz, 0.0f), instance.transform).xyz;
        output.position = mul(float4(direction, 0.0f), view_projection);
        output.position.z = output.position.w * 0.999999f;
        output.world_position = camera_position_time.xyz + direction;
        // No motion: backgrounds mask the motion target.
        output.current_clip = output.position;
        output.previous_clip = output.position;
    }
    else
    {
        const float4 world = mul(position, instance.transform);
        output.position = mul(world, view_projection);
        output.world_position = world.xyz;
        output.current_clip = mul(world, unjittered_view_projection);
        output.previous_clip = mul(mul(previous, instance.previous_transform), previous_view_projection);
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


// A planet (docs/legacy-shader-mapping.md, Planet): the surface lit through its normal map, brightened
// toward the atmosphere color at the rim, with city lights (base alpha) on the night side under a
// cloud layer that scrolls along U and is lit by its own normal map. The fill comes from the
// sphere's own normal, as the legacy planet lit it per vertex; planets take no shadows.
float3 shade_planet(const PixelInput input, const MaterialData material, const float2 uv, const float4 base)
{
    const float3 view = safe_normalize(camera_position_time.xyz - input.world_position, float3(0.0f, 0.0f, 1.0f));
    const float3 sphere_normal = safe_normalize(input.normal, float3(0.0f, 0.0f, 1.0f));
    const float3 tangent = safe_normalize(input.tangent, float3(1.0f, 0.0f, 0.0f));
    const float3 binormal = safe_normalize(input.binormal, float3(0.0f, 1.0f, 0.0f));
    const float3 light = safe_normalize(key_direction.xyz, float3(0.0f, 0.0f, 1.0f));
    const float3 fill = ambient.rgb + saturate(dot(sphere_normal, fill_direction[0].xyz)) * fill_diffuse[0].rgb + saturate(dot(sphere_normal, fill_direction[1].xyz)) * fill_diffuse[1].rgb;
    const float3 fill_light = material.diffuse.rgb * fill + material.emissive.rgb;

    float4 cloud = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if ((material.flags & MATERIAL_CLOUDS) != 0u)
    {
        const float2 cloud_uv = uv + float2(material.cloud_scroll * camera_position_time.w, 0.0f);
        cloud = sample_texture(material.cloud_texture, cloud_uv);
        const float3 bent = sample_texture(material.cloud_normal_texture, cloud_uv).xyz * 2.0f - 1.0f;
        const float3 cloud_normal = safe_normalize(bent.x * tangent + bent.y * binormal + bent.z * sphere_normal, sphere_normal);
        cloud.rgb = (cloud.rgb * saturate(dot(cloud_normal, light)) * key_diffuse.rgb + fill_light) * 2.0f;
    }

    const float4 normal_sample = sample_texture(material.normal_texture, uv);
    const float3 bent = normal_sample.xyz * 2.0f - 1.0f;
    const float3 normal = safe_normalize(bent.x * tangent + bent.y * binormal + bent.z * sphere_normal, sphere_normal);
    const float rim = pow(saturate(1.0f - dot(sphere_normal, view)), material.city_color_power.w) * 2.0f * material.atmosphere.a;
    const float3 surface = base.rgb + material.atmosphere.rgb * rim;
    const float3 half_vector = safe_normalize(light + view, normal);
    float3 color = surface * (saturate(dot(normal, light)) * material.diffuse.rgb * key_diffuse.rgb + fill_light) * 2.0f;
    // City lights switch on sharply past the terminator (a 32x steep ramp of the sphere's N.L).
    const float night = saturate(-32.0f * dot(sphere_normal, light));
    color += base.a * night * (1.0f - cloud.a) * material.city_color_power.rgb;
    color += key_specular.rgb * material.specular.rgb * pow(saturate(dot(normal, half_vector)), 16.0f) * normal_sample.a;
    return lerp(color, cloud.rgb, cloud.a);
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

    if (material.lighting == LIGHTING_PLANET)
    {
        const float alpha = (material.flags & MATERIAL_ALPHA_BLEND) != 0u ? base.a * material.diffuse.a * tint.a : 1.0f;
        return float4(shade_planet(input, material, uv, base) * tint.rgb, alpha);
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
    float3 color = legacy_lit_color(albedo, normal, input.world_position, gloss, material.diffuse.rgb, material.specular.rgb, material.emissive.rgb, tint.rgb, input.position.xy,
                                   (input.instance_flags & (INSTANCE_NO_SHADOWS | INSTANCE_BACKGROUND)) == 0u);
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
    // Fog, never on backgrounds; additive surfaces fade instead of taking the fog's color.
    if ((input.instance_flags & INSTANCE_BACKGROUND) == 0u)
    {
        output.color.rgb = (materials[material_index].flags & MATERIAL_ADDITIVE) != 0u ? fade_added_light(output.color.rgb, input.world_position)
                                                                                         : apply_fog(output.color.rgb, input.world_position);
    }
    output.color.rgb = apply_selection_highlight(output.color.rgb, input.pick_id, input.instance_flags);
    output.pick = uint2(input.pick_id, asuint(input.position.z));
    output.motion = (input.current_clip.xy / input.current_clip.w - input.previous_clip.xy / input.previous_clip.w) * float2(0.5f, -0.5f);
    return output;
}

// Enhanced water (docs/water-research.md): the surface's color and opacity come from what lies
// beneath it. The view ray's path through the water, from the opaque depth, hides the bottom by
// Beer-Lambert absorption (clear in the shallows, the water's own color when deep); the surface
// fades in over the first units of depth at the shore; two normal layers (the map's bump map, the
// second finer, turned, and drifting another way) bend the light; reflectance follows Schlick's
// Fresnel toward a sky color; and the key light leaves a sharp glint. All constants are engine
// choices, tuned by eye.
static const float WATER_CLARITY = 20.0f;          // path through water (units) that hides 63 % of the bottom
static const float WATER_SHORE_FADE = 3.0f;        // depth (units) over which the surface fades in
static const float WATER_DETAIL_SCALE = 2.7f;      // second normal layer's repeats per first-layer repeat
static const float WATER_DETAIL_DRIFT = 1.7f;      // its scroll relative to the first layer's
static const float WATER_GLINT_POWER = 256.0f;
static const float WATER_GLINT_STRENGTH = 1.5f;
static const float WATER_BASE_REFLECTANCE = 0.02f; // water's reflectance looking straight down

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
PixelOutput water_ps(PixelInput input)
{
    const MaterialData material = materials[material_index];
    const float time = camera_position_time.w;
    const float2 tiled = input.uv * material.specular.w;
    const float2 uv = tiled + material.uv_scroll * time;
    const float2 detail_uv = mul(float2x2(0.8f, -0.6f, 0.6f, 0.8f), tiled * WATER_DETAIL_SCALE) + float2(-material.uv_scroll.y, material.uv_scroll.x) * WATER_DETAIL_DRIFT * time;
    const float3 first = sample_texture(material.normal_texture, uv).xyz * 2.0f - 1.0f;
    const float3 second = sample_texture(material.normal_texture, detail_uv).xyz * 2.0f - 1.0f;
    const float3 bent = safe_normalize(float3(first.xy + second.xy, first.z * second.z), float3(0.0f, 0.0f, 1.0f));
    const float3 surface_normal = safe_normalize(input.normal, float3(0.0f, 0.0f, 1.0f));
    const float3 tangent = safe_normalize(input.tangent, float3(1.0f, 0.0f, 0.0f));
    const float3 binormal = safe_normalize(input.binormal, float3(0.0f, 1.0f, 0.0f));
    const float3 normal = safe_normalize(bent.x * tangent + bent.y * binormal + bent.z * surface_normal, surface_normal);

    // What lies beneath this pixel: the opaque scene's depth, back to a world position.
    Texture2D scene_depth = scene_textures[water_parameters.y];
    uint width = 1u;
    uint height = 1u;
    scene_depth.GetDimensions(width, height);
    const float depth = scene_depth.Load(int3(input.position.xy, 0)).r;
    const float2 ndc = float2(input.position.x / width * 2.0f - 1.0f, 1.0f - input.position.y / height * 2.0f);
    const float4 beneath_h = mul(float4(ndc, depth, 1.0f), inverse_view_projection);
    const float3 beneath = beneath_h.xyz / beneath_h.w;
    const bool bottomless = depth >= 1.0f;
    const float path = bottomless ? 1.0e6f : distance(input.world_position, beneath);
    const float below = bottomless ? 1.0e6f : max(input.world_position.z - beneath.z, 0.0f);

    const float3 view = safe_normalize(camera_position_time.xyz - input.world_position, float3(0.0f, 0.0f, 1.0f));
    const float fresnel = WATER_BASE_REFLECTANCE + (1.0f - WATER_BASE_REFLECTANCE) * pow(1.0f - saturate(dot(normal, view)), 5.0f);
    const float4 base = sample_texture(material.base_texture, uv);
    const float3 tint = (material.tint * input.tint * input.vertex_color).rgb;
    const float3 body = legacy_lit_color(base.rgb, normal, input.world_position, 0.0f, material.diffuse.rgb, float3(0.0f, 0.0f, 0.0f), material.emissive.rgb, tint, input.position.xy, true);
    // The sky the surface reflects, from the map's lights (an engine choice until there is a sky
    // probe).
    const float3 sky = ambient.rgb + key_diffuse.rgb * 0.5f;
    const float3 light = safe_normalize(key_direction.xyz, float3(0.0f, 0.0f, 1.0f));
    const float3 half_vector = safe_normalize(light + view, normal);
    const float lit = dot(surface_normal, light) > 0.0f ? key_light_visibility(input.world_position, surface_normal, input.position.xy) : 0.0f;
    const float3 glint = key_specular.rgb * material.specular.rgb * pow(saturate(dot(normal, half_vector)), WATER_GLINT_POWER) * WATER_GLINT_STRENGTH * lit;

    PixelOutput output;
    const float alpha = max(1.0f - exp(-path / WATER_CLARITY), fresnel) * saturate(below / WATER_SHORE_FADE);
    output.color = float4(apply_fog(lerp(body, sky, fresnel) + glint, input.world_position), alpha);
    output.pick = uint2(0u, 0u);
    output.motion = float2(MOTION_UNKNOWN, MOTION_UNKNOWN);
    return output;
}

// Shadow casting: a model into the cascade `pass_index` (depth only); alpha-tested surfaces cut
// their holes with the same test as when drawn.
struct ShadowPixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
ShadowPixelInput shadow_vs(VertexInput input)
{
    const InstanceData instance = instances[instance_base + input.instance_id];
    ShadowPixelInput output;
    output.position = mul(mul(skinned_position(input, instance.bone_offset), instance.transform), shadow_view_projection[pass_index]);
    output.uv = input.uv;
    return output;
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
void shadow_ps(ShadowPixelInput input)
{
    const MaterialData material = materials[material_index];
    const float2 uv = input.uv * material.specular.w + material.uv_scroll * camera_position_time.w;
    clip(sample_texture(material.base_texture, uv).a - material.alpha_cutoff);
}
