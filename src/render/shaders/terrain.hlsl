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
    const float3 normal = safe_normalize(tangent_normal.x * tangent + tangent_normal.y * binormal + tangent_normal.z * surface_normal, surface_normal);
    return float4(legacy_lit_color(albedo, normal, input.world_position, gloss, diffuse, specular, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f), input.position.xy), 1.0f);
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
    output.pick = uint2(PICK_TERRAIN, asuint(input.position.z));
    return output;
}
