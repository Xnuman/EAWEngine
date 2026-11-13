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
    float2 uv1 : TEXCOORD1;
    uint instance_id : SV_InstanceID;
};

// Grass (the Grass material; docs/legacy-shader-mapping.md): each vertex turns about its clump's
// center to face the camera around the model's up axis, and the clump's top (texture V 0) waves
// with the wind: faster and further in a stronger wind (10 is strong), phase-shifted across the
// map. The shape of the wave follows the legacy shader; how far its wind vector reached is not
// known, so GRASS_SWAY_SCALE (an engine choice) scales the sway to a few units. `wave` returns the
// 0..1 phase the color swings with.
static const float GRASS_STRONG_WIND = 10.0f;
static const float GRASS_MIN_TIME_SCALE = 0.125f;
static const float GRASS_MAX_TIME_SCALE = 1.0f;
static const float GRASS_SPATIAL_WAVELENGTH = 1.0f / 20.0f;
static const float GRASS_BEND_MIN = 10.0f;
static const float GRASS_BEND_MAX = 20.5f;
static const float GRASS_BEND_BIAS = 10.0f;
static const float GRASS_SWAY_SCALE = 0.15f;

float3 grass_position(const float3 position, const float2 clump_center, const float2 uv, const float4x4 transform, const float bend_scale, out float wave)
{
    // The camera's forward direction in the model's frame (its rows are the model's axes in the world).
    const float3 forward = float3(dot(transform[0].xyz, camera_forward.xyz), dot(transform[1].xyz, camera_forward.xyz), 0.0f);
    const float2 across = safe_normalize(float3(-forward.y, forward.x, 0.0f), float3(1.0f, 0.0f, 0.0f)).xy;
    const float2 offset = position.xy - clump_center;
    const float2 turned = clump_center + float2(offset.x * across.x - offset.y * across.y, offset.x * across.y + offset.y * across.x);
    const float strength = bend_scale * grass_wind.w / GRASS_STRONG_WIND;
    const float time_scale = GRASS_MIN_TIME_SCALE + (GRASS_MAX_TIME_SCALE - GRASS_MIN_TIME_SCALE) * strength;
    wave = 0.5f + 0.5f * sin(6.2831853f * frac(time_scale * camera_position_time.w + GRASS_SPATIAL_WAVELENGTH * (position.x + position.y)));
    const float bend = GRASS_BEND_BIAS * strength + (GRASS_BEND_MIN + (GRASS_BEND_MAX - GRASS_BEND_MIN) * strength) * wave;
    float3 world = mul(float4(turned, position.z, 1.0f), transform).xyz;
    world += (1.0f - uv.y) * bend * GRASS_SWAY_SCALE * grass_wind.xyz;
    return world;
}

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

    // Nebula (the legacy Nebula shader): the vertex wobbles in its model's space by a wave of its
    // world position and time; the brightness follows below.
    const MaterialData vertex_material = materials[material_index];
    if (vertex_material.lighting == LIGHTING_NEBULA)
    {
        const float3 placed = mul(float4(position.xyz, 1.0f), instance.transform).xyz;
        const float3 phase = vertex_material.layer_a.y * placed + vertex_material.layer_a.z * camera_position_time.w;
        position.xyz += vertex_material.layer_a.x * sin(6.28f * frac(phase));
    }

    // Last frame's skinned position, from last frame's palette.
    const float4 previous = skinned_position(input, instance.previous_bone_offset);
    float3 grass_tint = float3(1.0f, 1.0f, 1.0f);

    PixelInput output;
    if ((instance.flags & INSTANCE_BACKGROUND) != 0u)
    {
        // Backgrounds follow the camera: transforming a direction (w = 0) drops the view
        // translation, and the depth is pinned just inside the far plane.
        const float3 direction = mul(float4(position.xyz, 0.0f), instance.transform).xyz;
        // The sky capture draws backgrounds into its cube faces.
        const float4x4 projection = pass_index >= SKY_FACE_PASS ? sky_face_view_projection[pass_index - SKY_FACE_PASS] : view_projection;
        output.position = mul(float4(direction, 0.0f), projection);
        output.position.z = output.position.w * 0.999999f;
        output.world_position = camera_position_time.xyz + direction;
        // No motion: backgrounds mask the motion target.
        output.current_clip = output.position;
        output.previous_clip = output.position;
    }
    else
    {
        const MaterialData material = materials[material_index];
        const float bend_scale = material.bend_scale;
        float4 world = mul(position, instance.transform);
        world.xyz += wind_offset(wind_bend, position.z, bend_scale);
        float4 previous_world = mul(previous, instance.previous_transform);
        previous_world.xyz += wind_offset(previous_wind_bend, previous.z, bend_scale);
        if ((material.flags & MATERIAL_GRASS) != 0u)
        {
            // Motion vectors of waving grass are left to the depth reprojection (grass is blended).
            float wave = 0.0f;
            world.xyz = grass_position(position.xyz, input.uv1, input.uv, instance.transform, bend_scale, wave);
            previous_world = world;
            // The color swings between the two diffuse colors; the pixel shader multiplies by the
            // first, so the vertex color carries their ratio.
            grass_tint = lerp(float3(1.0f, 1.0f, 1.0f), material.grass_diffuse_alt.rgb / max(material.diffuse.rgb, 1.0e-3f), wave);
            // Grass is lit as if facing straight up from its model.
            normal = float3(0.0f, 0.0f, 1.0f);
        }
        output.position = mul(world, view_projection);
        output.world_position = world.xyz;
        output.current_clip = mul(world, unjittered_view_projection);
        output.previous_clip = mul(previous_world, previous_view_projection);
    }
    output.normal = mul(float4(normal, 0.0f), instance.transform).xyz;
    output.tangent = mul(float4(tangent, 0.0f), instance.transform).xyz;
    output.binormal = mul(float4(binormal, 0.0f), instance.transform).xyz;
    output.uv = input.uv;
    output.tint = instance.tint;
    output.team_color = instance.team_color;
    // Shields soften toward vertical edges: the vertex color times saturate(object normal z + edge
    // brightness). Nebulae brighten steeply where their surface faces along the view (or away:
    // the power is even): 2 x (view-space normal z)^8 + 0.1.
    float vertex_scale = 1.0f;
    if (vertex_material.lighting == LIGHTING_SHIELD)
    {
        vertex_scale = saturate(normal.z + vertex_material.layer_a.w);
    }
    else if (vertex_material.lighting == LIGHTING_NEBULA)
    {
        const float facing = dot(safe_normalize(output.normal, float3(0.0f, 0.0f, 1.0f)), camera_forward.xyz);
        vertex_scale = pow(facing, 8.0f) * 2.0f + 0.1f;
    }
    output.vertex_color = float4(input.color.rgb * grass_tint * vertex_scale, input.color.a);
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
        cloud = sample_texture(material.layer_texture_a, cloud_uv);
        const float3 bent = sample_texture(material.layer_texture_b, cloud_uv).xyz * 2.0f - 1.0f;
        const float3 cloud_normal = safe_normalize(bent.x * tangent + bent.y * binormal + bent.z * sphere_normal, sphere_normal);
        cloud.rgb = (cloud.rgb * saturate(dot(cloud_normal, light)) * key_diffuse.rgb + fill_light) * 2.0f;
    }

    const float4 normal_sample = sample_texture(material.normal_texture, uv);
    const float3 bent = normal_sample.xyz * 2.0f - 1.0f;
    const float3 normal = safe_normalize(bent.x * tangent + bent.y * binormal + bent.z * sphere_normal, sphere_normal);
    const float rim = pow(saturate(1.0f - dot(sphere_normal, view)), material.layer_b.w) * 2.0f * material.layer_a.a;
    const float3 surface = base.rgb + material.layer_a.rgb * rim;
    const float3 half_vector = safe_normalize(light + view, normal);
    float3 color = surface * (saturate(dot(normal, light)) * material.diffuse.rgb * key_diffuse.rgb + fill_light) * 2.0f;
    // City lights switch on sharply past the terminator (a 32x steep ramp of the sphere's N.L).
    const float night = saturate(-32.0f * dot(sphere_normal, light));
    color += base.a * night * (1.0f - cloud.a) * material.layer_b.rgb;
    color += key_specular.rgb * material.specular.rgb * pow(saturate(dot(normal, half_vector)), 16.0f) * normal_sample.a;
    return lerp(color, cloud.rgb, cloud.a);
}

// A shield (docs/legacy-shader-mapping.md, MeshShield): an energy texture whose coordinates a
// distortion texture shifts (a quarter of its value), times a wave texture, each layer scaled and
// scrolling along V at its own rate, times the vertex color (softened toward vertical edges in the
// vertex shader) and the material color; added to the frame, unlit. As in the legacy shader the
// distortion layer takes the scale and scroll named for the wave and the other way round (the
// observed pairing; VisualShield names the layers by what they do).
static const float SHIELD_DISTORTION_SHIFT = 0.25f;

float3 shade_shield(const PixelInput input, const MaterialData material, const float3 tint)
{
    const float time = camera_position_time.w;
    const float2 base_uv = input.uv * material.layer_a.x + float2(0.0f, time * material.layer_b.x);
    const float2 distortion_uv = input.uv * material.layer_a.y + float2(0.0f, time * material.layer_b.y);
    const float2 wave_uv = input.uv * material.layer_a.z + float2(0.0f, time * material.layer_b.z);
    const float2 shift = sample_texture(material.layer_texture_b, distortion_uv).xy * SHIELD_DISTORTION_SHIFT;
    const float3 energy = sample_texture(material.base_texture, base_uv + shift).rgb;
    const float3 wave = sample_texture(material.layer_texture_a, wave_uv).rgb;
    return energy * wave * tint * material.diffuse.rgb;
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

    if (material.lighting == LIGHTING_SHIELD)
    {
        return float4(shade_shield(input, material, tint.rgb), 1.0f);
    }

    if (material.lighting == LIGHTING_SKY)
    {
        // The sky dome (the legacy Skydome shader): clouds over the base by cloud alpha x vertex
        // alpha (cloud coordinates scroll, then scale), lit by every light through the turned-around
        // normal, times the vertex color, plus Emissive; never shadowed.
        float3 surface = base.rgb;
        if ((material.flags & MATERIAL_CLOUDS) != 0u)
        {
            const float4 cloud = sample_texture(material.layer_texture_a, (input.uv + float2(material.layer_a.y * camera_position_time.w, 0.0f)) * material.layer_a.x);
            surface = lerp(surface, cloud.rgb, cloud.a * input.vertex_color.a);
        }
        const float3 inward = -safe_normalize(input.normal, float3(0.0f, 0.0f, 1.0f));
        const float3 lit = legacy_lit_color(surface, inward, input.world_position, 0.0f, input.vertex_color.rgb, float3(0.0f, 0.0f, 0.0f), material.emissive.rgb,
                                            (material.tint * input.tint).rgb, input.position.xy, false);
        return float4(lit, 1.0f);
    }

    if (material.lighting == LIGHTING_UNLIT || material.lighting == LIGHTING_NEBULA)
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
    if ((materials[material_index].flags & MATERIAL_CLOUD_SHADOW) != 0u)
    {
        output.color.rgb *= cloud_shadow_color(input.world_position);
    }
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
static const float WATER_REFRACTION = 0.02f;       // screen share the bent normal shifts what lies beneath
static const float WATER_REFRACTION_DEPTH = 4.0f;  // depth (units) over which refraction grows to full
// Foam: along the shore within this depth, broken up by the normal layers, stronger where the bottom
// is steep (breakers) than over flat shallows (the 0 A.D. developers' finding), with lines rolling in
// toward the shore.
static const float WATER_FOAM_DEPTH = 6.0f;
static const float WATER_FOAM_FLAT_SHARE = 0.35f;   // foam over a flat bottom, against a steep one's 1
static const float WATER_FOAM_LINE_SPACING = 2.5f;  // depth (units) between rolling lines
static const float WATER_FOAM_LINE_SPEED = 1.2f;    // their travel (units of depth per second)
static const float3 WATER_FOAM_COLOR = float3(0.9f, 0.92f, 0.95f);
static const float WATER_REFLECTION_DISTORTION = 0.015f; // screen share the bent normal shifts the reflection

// Planar reflections (water_reflections.hlsl): per screen pixel 0, or the source pixel the
// mirrored scene shows there as ((row + 1) << 16) | column.
StructuredBuffer<uint> water_reflections : register(t7);

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
    const float3 sky = sky_parameters.y != 0u ? sample_sky(reflect(-view, normal)) : ambient.rgb + key_diffuse.rgb * 0.5f;
    const float3 light = safe_normalize(key_direction.xyz, float3(0.0f, 0.0f, 1.0f));
    const float3 half_vector = safe_normalize(light + view, normal);
    const float lit = dot(surface_normal, light) > 0.0f ? key_light_visibility(input.world_position, surface_normal, input.position.xy) : 0.0f;
    const float3 glint = key_specular.rgb * material.specular.rgb * pow(saturate(dot(normal, half_vector)), WATER_GLINT_POWER) * WATER_GLINT_STRENGTH * lit;

    // Foam, from the depth beneath and the bottom's slope (from the beneath position's screen
    // derivatives).
    const float3 bottom_normal = safe_normalize(cross(ddx(beneath), ddy(beneath)), float3(0.0f, 0.0f, 1.0f));
    const float steepness = saturate(1.0f - abs(bottom_normal.z));
    const float shallow = saturate(1.0f - below / WATER_FOAM_DEPTH);
    const float rolling = pow(saturate(sin((below / WATER_FOAM_LINE_SPACING + time * WATER_FOAM_LINE_SPEED / WATER_FOAM_LINE_SPACING) * 6.2831853f) * 0.5f + 0.5f), 4.0f);
    const float breakup = saturate((first.x + second.y) * 1.5f + 0.5f);
    const float foam = bottomless ? 0.0f
                                  : saturate(shallow * shallow * (WATER_FOAM_FLAT_SHARE + (1.0f - WATER_FOAM_FLAT_SHARE) * steepness) * (0.6f + rolling) * breakup * 1.5f);
    const float3 foam_lit = legacy_lit_color(WATER_FOAM_COLOR, surface_normal, input.world_position, 0.0f, float3(1.0f, 1.0f, 1.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f),
                                             float3(1.0f, 1.0f, 1.0f), input.position.xy, true);

    PixelOutput output;
    const float shore = saturate(below / WATER_SHORE_FADE);
    const float transmittance = exp(-path / WATER_CLARITY);
    if (water_parameters.w != 0u)
    {
        // Refraction: what lies beneath, seen through the bent surface; where the shifted pixel
        // shows something standing above the water, the unshifted one is used instead.
        Texture2D refraction = scene_textures[water_parameters.z];
        const float2 size = float2(width, height);
        const float2 shifted = input.position.xy + bent.xy * WATER_REFRACTION * size.y * saturate(below / WATER_REFRACTION_DEPTH);
        const int2 shifted_pixel = int2(clamp(shifted, float2(0.0f, 0.0f), size - 1.0f));
        const float shifted_depth = scene_depth.Load(int3(shifted_pixel, 0)).r;
        const float2 shifted_ndc = float2((shifted_pixel.x + 0.5f) / width * 2.0f - 1.0f, 1.0f - (shifted_pixel.y + 0.5f) / height * 2.0f);
        const float4 shifted_h = mul(float4(shifted_ndc, shifted_depth, 1.0f), inverse_view_projection);
        const bool above = shifted_depth < 1.0f && shifted_h.z / shifted_h.w > input.world_position.z;
        const float3 seen = refraction.Load(int3(above ? int2(input.position.xy) : shifted_pixel, 0)).rgb;
        const float3 water = lerp(seen, body, 1.0f - transmittance);
        // What the surface mirrors: the scene where planar reflections found it (this row or the next
        // ones, to fill one-pixel holes), else the sky.
        float3 reflection = sky;
        if (water_plane.y != 0.0f)
        {
            const int2 at = int2(clamp(input.position.xy + bent.xy * WATER_REFLECTION_DISTORTION * size.y, float2(0.0f, 0.0f), size - 1.0f));
            uint source = water_reflections[uint(at.y) * width + uint(at.x)];
            source = source != 0u ? source : water_reflections[uint(min(at.y + 1, int(height) - 1)) * width + uint(at.x)];
            source = source != 0u ? source : water_reflections[uint(max(at.y - 1, 0)) * width + uint(at.x)];
            if (source != 0u)
            {
                reflection = refraction.Load(int3(int(source & 0xFFFFu), int(source >> 16) - 1, 0)).rgb;
            }
        }
        output.color = float4(apply_fog(lerp(lerp(water, reflection, fresnel) + glint, foam_lit, foam), input.world_position), shore);
    }
    else
    {
        const float alpha = max(max(1.0f - transmittance, fresnel) * shore, foam);
        output.color = float4(apply_fog(lerp(lerp(body, sky, fresnel) + glint, foam_lit, foam), input.world_position), alpha);
    }
    output.pick = uint2(0u, 0u);
    output.motion = float2(MOTION_UNKNOWN, MOTION_UNKNOWN);
    return output;
}

// Occlusion silhouettes (the legacy OccludedUnit shaders): where a flagged unit is hidden, its team
// color at half strength (the legacy default color's alpha; the original's color for players is not
// established: an engine choice). Hidden means the pixel shows another object, at least
// SILHOUETTE_TOLERANCE world units nearer the camera than the unit's surface (an engine choice).
// Alpha-tested parts keep their holes.
static const float SILHOUETTE_ALPHA = 0.5f;
static const float SILHOUETTE_TOLERANCE = 0.5f;
// The pick target (pick id, depth bits per pixel): where the pixel shows the unit itself, it is not
// hidden, however its own parts overlap (the legacy shaders' stencil served the same end).
Texture2D<uint2> pick_view : register(t9);

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 silhouette_ps(PixelInput input) : SV_TARGET
{
    // Models are drawn two-sided (ALO winding is not reliable), so back faces, which a unit's own
    // front hides, are told apart by the normal, as the shading does.
    if ((input.instance_flags & INSTANCE_SILHOUETTE) == 0u || dot(input.normal, camera_position_time.xyz - input.world_position) < 0.0f)
    {
        discard;
    }
    if (pick_view.Load(int3(input.position.xy, 0)).x == input.pick_id)
    {
        discard;
    }
    Texture2D scene_depth = scene_textures[water_parameters.y];
    uint width = 1u;
    uint height = 1u;
    scene_depth.GetDimensions(width, height);
    const float stored = scene_depth.Load(int3(input.position.xy, 0)).r;
    const float2 ndc = float2(input.position.x / width * 2.0f - 1.0f, 1.0f - input.position.y / height * 2.0f);
    const float4 stored_h = mul(float4(ndc, stored, 1.0f), inverse_view_projection);
    const float stored_distance = distance(stored_h.xyz / stored_h.w, camera_position_time.xyz);
    if (stored >= 1.0f || stored_distance > distance(input.world_position, camera_position_time.xyz) - SILHOUETTE_TOLERANCE)
    {
        discard;
    }
    const MaterialData material = materials[material_index];
    if ((material.flags & MATERIAL_ALPHA_TEST) != 0u)
    {
        clip(sample_texture(material.base_texture, input.uv * material.specular.w + material.uv_scroll * camera_position_time.w).a - material.alpha_cutoff);
    }
    return float4(input.team_color.rgb, SILHOUETTE_ALPHA);
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
    const float4 position = skinned_position(input, instance.bone_offset);
    float4 world = mul(position, instance.transform);
    world.xyz += wind_offset(wind_bend, position.z, materials[material_index].bend_scale);
    output.position = mul(world, shadow_view_projection[pass_index]);
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
