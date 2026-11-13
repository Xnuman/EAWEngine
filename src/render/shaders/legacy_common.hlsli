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
    "RootConstants(num32BitConstants = 3, b1), " \
    "SRV(t4, visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t5, visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t6, visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t7, visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t8, visibility = SHADER_VISIBILITY_PIXEL), " \
    "DescriptorTable(SRV(t9, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, filter = FILTER_ANISOTROPIC, maxAnisotropy = 8, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, " \
    "addressW = TEXTURE_ADDRESS_WRAP, comparisonFunc = COMPARISON_ALWAYS, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, " \
    "visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s1, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, " \
    "addressW = TEXTURE_ADDRESS_CLAMP, comparisonFunc = COMPARISON_ALWAYS, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, " \
    "visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s2, filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_BORDER, addressV = TEXTURE_ADDRESS_BORDER, " \
    "addressW = TEXTURE_ADDRESS_BORDER, comparisonFunc = COMPARISON_LESS_EQUAL, borderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE, " \
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
static const uint LIGHTING_PLANET = 3u;
static const uint LIGHTING_SHIELD = 4u;
static const uint LIGHTING_SKY = 5u;
static const uint LIGHTING_NEBULA = 6u;
static const uint MATERIAL_CLOUDS = 1u << 5;
static const uint MATERIAL_GRASS = 1u << 6;
static const uint MATERIAL_CLOUD_SHADOW = 1u << 7;
static const uint INSTANCE_BACKGROUND = 1u;
static const uint INSTANCE_HIGHLIGHTED = 2u;
static const uint INSTANCE_NO_SHADOWS = 4u;
static const uint INSTANCE_SILHOUETTE = 8u;

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
    float4 post_parameters; // x bloom threshold, y bloom strength, z bloom knee
    // Temporal anti-aliasing: view_projection is jittered then; these are not. The inverse is the
    // current frame's, the previous the last frame's.
    float4x4 unjittered_view_projection;
    float4x4 inverse_view_projection;
    float4x4 previous_view_projection; // the last frame's camera (the current one in the first frame)
    float4 taa_parameters; // xy jitter (NDC), z history valid (1) or not (0), w weight of the new frame
    // Key-light shadows (render/shadow_cascades.hpp): each cascade's world-to-clip matrix, the far
    // end of its slice along the view, its texel's world size, and its map's texture slot.
    float4x4 shadow_view_projection[4];
    float4 shadow_splits;
    float4 shadow_texels;
    float4 camera_forward; // xyz the view direction
    uint4 shadow_slots;
    float4 shadow_parameters; // x cascade count (0: no shadows), y normal offset in texels, z blend band share, w filter radius in UV
    float4 fog_color; // rgb the map's fog color, w the mode (0 off, 1 legacy, 2 enhanced)
    float4 fog_range; // x start, y end (distance), z the height enhanced fog is densest at, w its falloff per unit of height
    float4 decal_grid;     // xy the terrain decal grid's corner, zw its cells per world unit
    uint4 decal_grid_size; // x, y cells per side, z the decal count
    uint4 water_parameters; // x enhanced water (1) or legacy (0), y the depth buffer's texture slot, z the refraction copy's, w refraction on (1)
    float4 water_plane;     // x the water plane's height, y planar reflections on (1)
    float4 wind_bend;          // xyz the wind's bend offset, w 1 / height^2 (render/wind_bend.hpp)
    float4 previous_wind_bend; // the same last frame
    float4 grass_wind;         // xyz the wind's direction, w its speed (1-10)
    float4 cloud_shadow;       // x 1 / the cloud texture's repeat, yz its drift so far
    uint4 cloud_parameters;    // x the cloud texture's logical id, y cloud shadows on
    // The sky capture: each cube face's matrix for the background draws (pass index SKY_FACE_PASS +
    // face); x the atlas's texture slot, y a sky was captured, z the ambient takes its light, w the
    // face size.
    float4x4 sky_face_view_projection[6];
    uint4 sky_parameters;
    float4 sky_visibility_map;        // world X, Y to the sky visibility texture: scale xy, offset zw
    uint4 sky_visibility_parameters;  // x its logical id, y on
    float4 heat_parameters; // x the projection's vertical scale, y width over height, z the height in pixels
    uint4 heat_slots;       // x the signed heat buffer's slot, y TAA weighs heat (1)
    uint4 particle_parameters; // x soft particles on
};

// Pick ids written to the selection target: 0 is nothing, scene instance index + 1 an object, and
// PICK_TERRAIN the terrain. Keep in sync with d3d12_renderer.cpp.
static const uint PICK_TERRAIN = 0xFFFFFFFFu;

// The motion target is cleared to this where nothing wrote motion (backgrounds, blended surfaces,
// terrain, particles); keep in sync with d3d12_renderer_frame.cpp. Real motion stays far below.
static const float MOTION_UNKNOWN = 1000.0f;

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
    // The shadow cascade a shadow pass draws into.
    uint pass_index;
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
    // Extra layers by lighting model (d3d12_renderer_impl.hpp, GpuMaterial). Planets: layer_a the
    // atmosphere rgb and strength, layer_b the city lights rgb and atmosphere power, textures the
    // clouds and their normal map. Shields: layer_a the base, distortion, and wave UV scales and the
    // edge brightness, layer_b their scroll rates, textures the wave and the distortion.
    float4 layer_a;
    float4 layer_b;
    uint layer_texture_a;
    uint layer_texture_b;
    float cloud_scroll;
    float bend_scale; // wind bend (trees), 0 none
    float4 grass_diffuse_alt;
    uint billboard;          // VisualBillboard (render/visual_material.hpp)
    float3 billboard_anchor; // the bone's bind position in the model
};

// Wind bend (the Tree material): a vertex sways by its height above the model's origin, squared.
float3 wind_offset(const float4 bend, const float height, const float scale)
{
    return scale * bend.xyz * (height * height * bend.w);
}

Texture2D scene_textures[4096] : register(t0, space1);
StructuredBuffer<MaterialData> materials : register(t3);
// Logical texture id -> descriptor slot for this frame. Materials name logical ids so a texture can
// stream in (or fall back) without rewriting descriptors or materials.
StructuredBuffer<uint> texture_residency : register(t4);
SamplerState surface_sampler : register(s0);
SamplerComparisonState shadow_sampler : register(s2);

// A texture's top level (sampling inside branches, where derivatives are not defined).
float4 sample_texture_top(const uint texture, const float2 uv)
{
    return scene_textures[NonUniformResourceIndex(texture_residency[texture])].SampleLevel(surface_sampler, uv, 0.0f);
}

float4 sample_texture(const uint texture, const float2 uv)
{
    return scene_textures[NonUniformResourceIndex(texture_residency[texture])].Sample(surface_sampler, uv);
}

// The sky capture's cube faces (keep in step with sky_faces in d3d12_renderer_frame.cpp): forward,
// right, and up of each, and its cell in the three-by-two atlas.
static const uint SKY_FACE_PASS = 16u;
float3 sky_face_forward(const uint face)
{
    const float3 forwards[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
    return forwards[face];
}
float3 sky_face_right(const uint face)
{
    const float3 rights[6] = {float3(0, 1, 0), float3(0, -1, 0), float3(-1, 0, 0), float3(1, 0, 0), float3(1, 0, 0), float3(1, 0, 0)};
    return rights[face];
}
float3 sky_face_up(const uint face)
{
    const float3 ups[6] = {float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1), float3(0, 0, 1), float3(0, 1, 0), float3(0, -1, 0)};
    return ups[face];
}
uint2 sky_face_cell(const uint face)
{
    return uint2(face % 3u, face / 3u);
}

// The captured sky in a direction (inside the face, a texel from its edges so filtering stays on it).
float3 sample_sky(const float3 direction)
{
    const float3 magnitude = abs(direction);
    uint face = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? (direction.x >= 0.0f ? 0u : 1u)
                : magnitude.y >= magnitude.z                            ? (direction.y >= 0.0f ? 2u : 3u)
                                                                        : (direction.z >= 0.0f ? 4u : 5u);
    const float along = dot(direction, sky_face_forward(face));
    const float2 ndc = float2(dot(direction, sky_face_right(face)), dot(direction, sky_face_up(face))) / max(along, 1.0e-4f);
    const float size = (float)sky_parameters.w;
    const float2 local = clamp(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f), 1.0f / size, 1.0f - 1.0f / size);
    const float2 uv = (float2(sky_face_cell(face)) + local) / float2(3.0f, 2.0f);
    return scene_textures[sky_parameters.x].SampleLevel(surface_sampler, uv, 0.0f).rgb;
}

// Second-order spherical harmonics' nine real basis functions in a unit direction.
void sh_basis(const float3 n, out float basis[9])
{
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);
}

// The environment's cloud shadows at a world position: the cloud texture's color, mapped over the
// world's X and Y and drifting (the legacy terrain shaders multiply their lighting by it); white
// when off.
float3 cloud_shadow_color(const float3 world)
{
    if (cloud_parameters.y == 0u)
    {
        return float3(1.0f, 1.0f, 1.0f);
    }
    return sample_texture(cloud_parameters.x, world.xy * cloud_shadow.x - cloud_shadow.yz).rgb;
}

float3 safe_normalize(const float3 value, const float3 fallback)
{
    const float length_squared = dot(value, value);
    return length_squared > 1.0e-12f ? value * rsqrt(length_squared) : fallback;
}

// Enhanced fog (engine choices): its density at the floor makes level fog nearly opaque (95 %) at
// the map's end distance, and the part toward the key light glows with the light's color.
static const float FOG_DENSITY_AT_END = 3.0f;
static const float FOG_SUN_GLOW = 0.5f;
static const float FOG_SUN_EXPONENT = 8.0f;

// The fog a ray meets, in lengths of floor-density fog: density is constant below the floor (so
// deep pits fill but do not turn opaque without end) and thins by e per fog_range.w^-1 of height
// above it. The ray starts `height` above the floor, rises `rise` per unit, and runs `travel`.
float height_fog_length(const float height, const float rise, const float travel)
{
    const float falloff = fog_range.w;
    float below_length = 0.0f;
    float above_start = height;
    float above_length = travel;
    if (height < 0.0f)
    {
        // Starts below: below until it climbs out (if it does within the travel).
        const float climbs_out = rise > 1.0e-6f ? -height / rise : travel;
        below_length = min(climbs_out, travel);
        above_start = 0.0f;
        above_length = travel - below_length;
    }
    else if (rise < -1.0e-6f)
    {
        // Starts above and descends: above until it reaches the floor, below after.
        const float down = height / -rise;
        above_length = min(down, travel);
        below_length = travel - above_length;
    }
    const float climb = falloff * rise * above_length;
    const float along = abs(climb) > 1.0e-4f ? (1.0f - exp(-climb)) / (falloff * rise) : above_length;
    // Above the floor: the closed form of the exponential, linear when level.
    return below_length + exp(-falloff * above_start) * along;
}

// How much fog lies between the camera and `world`, 0..1. Legacy: linear in view distance from
// start to end (the legacy shaders' model). Enhanced: exponential height fog, its density falling
// off with height above the floor, integrated in closed form along the ray beyond the start.
float fog_amount(const float3 world)
{
    const uint mode = (uint)fog_color.w;
    if (mode == 0u)
    {
        return 0.0f;
    }
    const float3 offset = world - camera_position_time.xyz;
    if (mode == 1u)
    {
        const float distance = dot(offset, camera_forward.xyz);
        return saturate((distance - fog_range.x) / max(fog_range.y - fog_range.x, 1.0e-3f));
    }
    const float total = length(offset);
    const float travel = total - fog_range.x;
    if (travel <= 0.0f)
    {
        return 0.0f;
    }
    const float3 direction = offset / max(total, 1.0e-3f);
    const float density = FOG_DENSITY_AT_END / max(fog_range.y - fog_range.x, 1.0f);
    // The ray from the start point: its height above the floor at the start and its rise per unit.
    const float height = camera_position_time.z + direction.z * fog_range.x - fog_range.z;
    return 1.0f - exp(-density * height_fog_length(height, direction.z, travel));
}

// The fog's color toward `world`: enhanced fog brightens toward the key light.
// Enhanced fog leans toward the captured sky just above the horizon in the view's direction, so far
// terrain meets the sky without a seam (docs/sky-research.md): this share of the way, and that far
// above the horizon (engine choices).
static const float FOG_HORIZON_SHARE = 0.5f;
static const float FOG_HORIZON_LIFT = 0.05f;

float3 fog_tint(const float3 world)
{
    if ((uint)fog_color.w != 2u)
    {
        return fog_color.rgb;
    }
    const float3 direction = safe_normalize(world - camera_position_time.xyz, camera_forward.xyz);
    const float toward = saturate(dot(direction, safe_normalize(key_direction.xyz, float3(0.0f, 0.0f, 1.0f))));
    float3 base = fog_color.rgb;
    if (sky_parameters.y != 0u)
    {
        const float3 horizon = safe_normalize(float3(direction.xy, FOG_HORIZON_LIFT * length(direction.xy)), float3(1.0f, 0.0f, 0.0f));
        base = lerp(base, sample_sky(horizon), FOG_HORIZON_SHARE);
    }
    return base + key_diffuse.rgb * pow(toward, FOG_SUN_EXPONENT) * FOG_SUN_GLOW;
}

// A surface seen through the fog.
float3 apply_fog(const float3 color, const float3 world)
{
    return lerp(color, fog_tint(world), fog_amount(world));
}

// Light added to the frame (additive surfaces and particles): legacy fog leaves it alone, as the
// legacy shaders do; enhanced fog lets less of it through.
float3 fade_added_light(const float3 color, const float3 world)
{
    return (uint)fog_color.w == 2u ? color * (1.0f - fog_amount(world)) : color;
}

// Shadow filtering: taps of a Poisson disk (unit radius), turned per pixel and frame by a noise
// angle so that temporal anti-aliasing averages the pattern away (engine choices).
static const uint SHADOW_TAPS = 12;
static const float2 shadow_disk[SHADOW_TAPS] = {float2(-0.326f, -0.406f), float2(-0.840f, -0.074f), float2(-0.696f, 0.457f), float2(-0.203f, 0.621f),
                                                float2(0.962f, -0.195f), float2(0.473f, -0.480f), float2(0.519f, 0.767f), float2(0.185f, -0.893f),
                                                float2(0.507f, 0.064f), float2(0.896f, 0.412f), float2(-0.322f, -0.933f), float2(-0.792f, -0.598f)};

// Interleaved gradient noise in 0..1, moved every frame by the scene clock.
float shadow_noise(const float2 pixel)
{
    const float2 moved = pixel + 5.588238f * floor(frac(camera_position_time.w * 7.0f) * 64.0f);
    return frac(52.9829189f * frac(dot(moved, float2(0.06711056f, 0.00583715f))));
}

// How much of the key light reaches `world` through one cascade (1 outside it).
float cascade_visibility(const uint cascade, const float3 world, const float3 normal, const float angle)
{
    // Pushed out along the normal by a few texels of this cascade, against self-shadowing acne.
    const float3 offset = world + normal * shadow_texels[cascade] * shadow_parameters.y;
    const float4 clip = mul(float4(offset, 1.0f), shadow_view_projection[cascade]);
    const float2 uv = float2(clip.x * 0.5f + 0.5f, 0.5f - clip.y * 0.5f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || clip.z > 1.0f)
    {
        return 1.0f;
    }
    Texture2D map = scene_textures[NonUniformResourceIndex(shadow_slots[cascade])];
    float s;
    float c;
    sincos(angle, s, c);
    float lit = 0.0f;
    [unroll]
    for (uint tap = 0; tap < SHADOW_TAPS; ++tap)
    {
        const float2 d = shadow_disk[tap];
        const float2 turned = float2(d.x * c - d.y * s, d.x * s + d.y * c) * shadow_parameters.w;
        lit += map.SampleCmpLevelZero(shadow_sampler, uv + turned, clip.z);
    }
    return lit / SHADOW_TAPS;
}

// How much of the key light reaches a surface: from the cascade its view depth falls in, dithered
// into the next cascade over the last share of each slice, and fading out at the far end.
float key_light_visibility(const float3 world, const float3 normal, const float2 pixel)
{
    const uint count = (uint)shadow_parameters.x;
    if (count == 0u)
    {
        return 1.0f;
    }
    const float depth = dot(world - camera_position_time.xyz, camera_forward.xyz);
    uint cascade = count;
    for (uint index = 0; index < count; ++index)
    {
        if (depth <= shadow_splits[index])
        {
            cascade = index;
            break;
        }
    }
    if (cascade >= count)
    {
        return 1.0f;
    }
    const float noise = shadow_noise(pixel);
    const float angle = noise * 6.2831853f;
    const float start = cascade == 0u ? 0.0f : shadow_splits[cascade - 1u];
    const float band = max((shadow_splits[cascade] - start) * shadow_parameters.z, 1.0e-3f);
    const float into = (shadow_splits[cascade] - depth) / band;
    if (into < 1.0f && cascade + 1u < count && noise > into)
    {
        return cascade_visibility(cascade + 1u, world, normal, angle);
    }
    const float visibility = cascade_visibility(cascade, world, normal, angle);
    return cascade + 1u == count ? lerp(1.0f, visibility, saturate(into)) : visibility;
}

// Legacy-mode surface lighting shared by meshes and terrain: ambient plus two diffuse fill lights
// and a key light, the 2x diffuse convention, and an exponent-16 highlight scaled by gloss. Shadows
// take away the key light only (diffuse and highlight), as the original's shadow volumes did.
// The sky's light (sky_light.hlsl), nine spherical-harmonic colors.
StructuredBuffer<float4> sky_light : register(t8);
// How much of the ambient follows the sky when sky ambient is on (an engine choice).
static const float SKY_AMBIENT_SHARE = 0.75f;

// The irradiance the sky gives a surface facing `normal`, over pi (radiance units, so a uniform sky
// of color c gives c): the cosine-convolved harmonics.
float3 sky_irradiance(const float3 normal)
{
    float basis[9];
    sh_basis(normal, basis);
    const float bands[9] = {3.141593f, 2.094395f, 2.094395f, 2.094395f, 0.785398f, 0.785398f, 0.785398f, 0.785398f, 0.785398f};
    float3 total = float3(0.0f, 0.0f, 0.0f);
    for (uint term = 0u; term < 9u; ++term)
    {
        total += sky_light[term].rgb * bands[term] * basis[term];
    }
    return max(total, 0.0f) / 3.141593f;
}

// The fill light with the sky's color and direction blended in, at the map's own fill brightness:
// the sky's average (its first harmonic) is scaled to the legacy fill's average brightness.
float3 sky_fill(const float3 normal, const float3 legacy_fill)
{
    if (sky_parameters.z == 0u)
    {
        return legacy_fill;
    }
    // Balanced where most of the scene faces, straight up: upward surfaces keep the legacy fill's
    // brightness and the sky only reshapes the light on slopes and sides (balancing the average
    // instead brightened the ground by a tenth under bright domes).
    const float3 up = float3(0.0f, 0.0f, 1.0f);
    const float3 upward_sky = sky_irradiance(up);
    const float3 upward_fill = ambient.rgb + saturate(dot(up, fill_direction[0].xyz)) * fill_diffuse[0].rgb + saturate(dot(up, fill_direction[1].xyz)) * fill_diffuse[1].rgb;
    const float3 luminance = float3(0.2126f, 0.7152f, 0.0722f);
    const float scale = dot(upward_fill, luminance) / max(dot(upward_sky, luminance), 1.0e-4f);
    return lerp(legacy_fill, sky_irradiance(normal) * scale, SKY_AMBIENT_SHARE);
}

float3 legacy_lit_color(const float3 albedo, const float3 normal, const float3 world_position, const float gloss, const float3 diffuse, const float3 specular,
                        const float3 emissive, const float3 tint, const float2 pixel, const bool receives_shadows)
{
    const float3 view = safe_normalize(camera_position_time.xyz - world_position, float3(0.0f, 0.0f, 1.0f));
    const float3 light = safe_normalize(key_direction.xyz, float3(0.0f, 0.0f, 1.0f));
    const float3 half_vector = safe_normalize(light + view, normal);
    const float3 legacy_fill = ambient.rgb + saturate(dot(normal, fill_direction[0].xyz)) * fill_diffuse[0].rgb + saturate(dot(normal, fill_direction[1].xyz)) * fill_diffuse[1].rgb;
    // The sky's light shapes the fill of the world's surfaces (never the backgrounds it was captured
    // from), dimmed by how much sky the terrain there sees.
    float3 fill = receives_shadows ? sky_fill(normal, legacy_fill) : legacy_fill;
    if (receives_shadows && sky_visibility_parameters.y != 0u)
    {
        fill *= sample_texture_top(sky_visibility_parameters.x, world_position.xy * sky_visibility_map.xy + sky_visibility_map.zw).r;
    }
    const float facing = saturate(dot(normal, light));
    // Surfaces facing away get no key light anyway; their highlight stays as it was without shadows.
    const float key = facing > 0.0f && receives_shadows ? key_light_visibility(world_position, normal, pixel) : 1.0f;
    const float3 diffuse_light = diffuse * (fill + facing * key * key_diffuse.rgb) + emissive;
    const float3 highlight = specular * key_specular.rgb * pow(saturate(dot(normal, half_vector)), 16.0f) * gloss * key;
    // Legacy 2x diffuse convention: lighting near 0.5 reproduces the texture color.
    return albedo * diffuse_light * 2.0f * tint + highlight;
}

#endif
