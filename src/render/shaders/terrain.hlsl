// EAWEngine D3D12 legacy-mode terrain shaders. Terrain is a heightfield whose vertices each name
// one texture layer (TED sample bits 16-23). Every triangle carries its three corner layers and
// barycentric weights, so a pixel blends the three layers' textures and normals smoothly before
// the shared legacy lighting. Layers are materials at material_index + layer; textures are
// projected from world X/Y and tiled by each layer's texture-coordinate scale. A layer's tint alpha
// is its coverage: untextured layers have none, so the blend uses only covered layers and pixels
// with less than half coverage are discarded.
//
// Written for this engine; compiled offline by DXC (shader model 6.0).

#include "legacy_common.hlsli"

// Terrain decals (render/visual_frame.hpp VisualDecal; d3d12_renderer_impl.hpp GpuDecal), sorted
// into a grid over the terrain: per cell its first index into the grid buffer and its count, then
// the decal indices.
struct TerrainDecal
{
    float2 center;
    float2 half_size;
    float2 axis; // the decal's +X in the world (cos, sin of its turn)
    float opacity;
    uint mode;   // 0 diffuse, 1 bump, 2 additive
    float4 uv_rect;
    uint texture;
    uint3 padding;
};
StructuredBuffer<TerrainDecal> terrain_decals : register(t5);
StructuredBuffer<uint> terrain_decal_grid : register(t6);

static const uint DECAL_DIFFUSE = 0u;
static const uint DECAL_BUMP = 1u;
static const uint DECAL_ADDITIVE = 2u;

// The decals over `world` (X/Y): diffuse ones blend the albedo, bump ones bend the tangent-space
// normal (the terrain's frame follows world X and Y), additive ones return light to add after
// lighting. The atlas's top level is sampled (a shortcut: no mips, so distant marks may shimmer;
// TAA smooths them).
float3 apply_terrain_decals(const float2 world, inout float3 albedo, inout float3 tangent_normal)
{
    float3 added = 0.0f;
    if (decal_grid_size.z == 0u)
    {
        return added;
    }
    const int2 cell = int2(floor((world - decal_grid.xy) * decal_grid.zw));
    if (any(cell < 0) || cell.x >= (int)decal_grid_size.x || cell.y >= (int)decal_grid_size.y)
    {
        return added;
    }
    const uint slot = (uint)cell.y * decal_grid_size.x + (uint)cell.x;
    const uint first = terrain_decal_grid[slot * 2u];
    const uint count = terrain_decal_grid[slot * 2u + 1u];
    for (uint member = 0; member < count; ++member)
    {
        const TerrainDecal decal = terrain_decals[terrain_decal_grid[first + member]];
        const float2 offset = world - decal.center;
        const float2 local = float2(dot(offset, decal.axis), dot(offset, float2(-decal.axis.y, decal.axis.x))) / decal.half_size;
        if (any(abs(local) > 1.0f))
        {
            continue;
        }
        // Texture V runs down the decal's +Y.
        const float2 uv = decal.uv_rect.xy + (float2(local.x, -local.y) * 0.5f + 0.5f) * decal.uv_rect.zw;
        const float4 texel = sample_texture_top(decal.texture, uv);
        const float amount = saturate(texel.a * decal.opacity);
        if (decal.mode == DECAL_BUMP)
        {
            // The texel's normal, its X and Y turned from the decal's frame into the world's.
            float3 bent = texel.rgb * 2.0f - 1.0f;
            bent.y = -bent.y;
            bent.xy = float2(bent.x * decal.axis.x - bent.y * decal.axis.y, bent.x * decal.axis.y + bent.y * decal.axis.x);
            tangent_normal = safe_normalize(lerp(tangent_normal, bent, amount), tangent_normal);
        }
        else if (decal.mode == DECAL_ADDITIVE)
        {
            added += texel.rgb * amount;
        }
        else
        {
            albedo = lerp(albedo, texel.rgb, amount);
        }
    }
    return added;
}

struct TerrainVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    uint3 layers : BLENDINDICES0;
    float3 barycentric : TEXCOORD0;
};

struct TerrainPixelInput
{
    float4 position : SV_POSITION;
    float3 world_position : POSITION1;
    float3 normal : NORMAL;
    nointerpolation uint3 layers : BLENDINDICES0;
    float3 barycentric : TEXCOORD0;
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
TerrainPixelInput terrain_vs(TerrainVertexInput input)
{
    TerrainPixelInput output;
    output.position = mul(float4(input.position, 1.0f), view_projection);
    output.world_position = input.position;
    output.normal = input.normal;
    output.layers = input.layers;
    output.barycentric = input.barycentric;
    return output;
}

// The corner layers' blend weights, by coverage; discards pixels less than half covered (holes in
// the terrain, such as crater openings under placed models).
float3 covered_layer_weights(const TerrainPixelInput input)
{
    float3 weights = input.barycentric / max(input.barycentric.x + input.barycentric.y + input.barycentric.z, 1.0e-6f);
    [unroll]
    for (uint layer_corner = 0; layer_corner < 3; ++layer_corner)
    {
        weights[layer_corner] *= saturate(materials[material_index + input.layers[layer_corner]].tint.a);
    }
    const float coverage = weights.x + weights.y + weights.z;
    if (coverage < 0.5f)
    {
        discard;
    }
    return weights / coverage;
}

// The terrain into a shadow cascade (depth only); holes cast nothing, as they draw nothing.
[RootSignature(SANDBOX_ROOT_SIGNATURE)]
TerrainPixelInput terrain_shadow_vs(TerrainVertexInput input)
{
    TerrainPixelInput output;
    output.position = mul(float4(input.position, 1.0f), shadow_view_projection[pass_index]);
    output.world_position = input.position;
    output.normal = input.normal;
    output.layers = input.layers;
    output.barycentric = input.barycentric;
    return output;
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
void terrain_shadow_ps(TerrainPixelInput input)
{
    (void)covered_layer_weights(input);
}

float4 shade_terrain(TerrainPixelInput input)
{
    const float3 surface_normal = safe_normalize(input.normal, float3(0.0f, 0.0f, 1.0f));
    // Planar projection: texture U follows world +X and V follows world +Y on the surface.
    const float3 tangent = safe_normalize(float3(1.0f, 0.0f, 0.0f) - surface_normal * surface_normal.x, float3(1.0f, 0.0f, 0.0f));
    const float3 binormal = cross(surface_normal, tangent);
    const float3 weights = covered_layer_weights(input);

    float3 albedo = 0.0f;
    float3 tangent_normal = 0.0f;
    float3 diffuse = 0.0f;
    float3 specular = 0.0f;
    float gloss = 0.0f;
    [unroll]
    for (uint corner = 0; corner < 3; ++corner)
    {
        const MaterialData layer = materials[material_index + input.layers[corner]];
        const float2 uv = input.world_position.xy * layer.specular.w;
        const float4 base = sample_texture(layer.base_texture, uv);
        float3 layer_normal = float3(0.0f, 0.0f, 1.0f);
        if (layer.lighting == LIGHTING_NORMAL_MAPPED)
        {
            layer_normal = sample_texture(layer.normal_texture, uv).xyz * 2.0f - 1.0f;
        }
        const float weight = weights[corner];
        albedo += base.rgb * weight;
        gloss += base.a * weight;
        tangent_normal += layer_normal * weight;
        diffuse += layer.diffuse.rgb * weight;
        specular += layer.specular.rgb * weight;
    }
    const float3 added = apply_terrain_decals(input.world_position.xy, albedo, tangent_normal);
    const float3 normal = safe_normalize(tangent_normal.x * tangent + tangent_normal.y * binormal + tangent_normal.z * surface_normal, surface_normal);
    return float4(added + legacy_lit_color(albedo, normal, input.world_position, gloss, diffuse, specular, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f), input.position.xy, true), 1.0f);
}

struct TerrainPixelOutput
{
    float4 color : SV_TARGET0;
    uint2 pick : SV_TARGET1; // (pick id, depth bits)
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
TerrainPixelOutput terrain_ps(TerrainPixelInput input)
{
    TerrainPixelOutput output;
    output.color = shade_terrain(input);
    output.color.rgb = apply_fog(output.color.rgb * cloud_shadow_color(input.world_position), input.world_position);
    output.pick = uint2(PICK_TERRAIN, asuint(input.position.z));
    return output;
}
