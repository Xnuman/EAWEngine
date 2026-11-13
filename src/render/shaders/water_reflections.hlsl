// Planar screen-space reflections for the water plane (docs/water-research.md; the technique of
// Ghost Recon Wildlands' screen-space planar reflections, written anew): every opaque pixel above
// the plane is mirrored across it and projected back to the screen, and the pixel it lands on
// remembers the source. Among several sources landing on one pixel the one lowest on the screen
// wins (InterlockedMax on the row): seen from above, it lies nearest the water along the reflected
// ray, so it is the one the reflection shows. The water's pixel shader reads the source back and
// samples the copy of the opaque scene there; pixels nothing lands on fall back to the sky.

#include "legacy_common.hlsli"

#define WATER_REFLECTION_ROOT_SIGNATURE \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = 4096)), " \
    "UAV(u0)"

// One per screen pixel: 0 when nothing landed there, else ((source row + 1) << 16) | source column.
RWStructuredBuffer<uint> reflection_sources : register(u0);

uint2 screen_size()
{
    uint width = 1u;
    uint height = 1u;
    scene_textures[water_parameters.y].GetDimensions(width, height);
    return uint2(width, height);
}

[RootSignature(WATER_REFLECTION_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void clear_cs(const uint3 thread : SV_DispatchThreadID)
{
    const uint2 size = screen_size();
    if (thread.x < size.x * size.y)
    {
        reflection_sources[thread.x] = 0u;
    }
}

[RootSignature(WATER_REFLECTION_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void project_cs(const uint3 thread : SV_DispatchThreadID)
{
    const uint2 size = screen_size();
    if (thread.x >= size.x || thread.y >= size.y)
    {
        return;
    }
    const float depth = scene_textures[water_parameters.y].Load(int3(thread.xy, 0)).r;
    if (depth >= 1.0f)
    {
        return;
    }
    const float2 ndc = float2((thread.x + 0.5f) / size.x * 2.0f - 1.0f, 1.0f - (thread.y + 0.5f) / size.y * 2.0f);
    const float4 world_h = mul(float4(ndc, depth, 1.0f), inverse_view_projection);
    const float3 world = world_h.xyz / world_h.w;
    const float level = water_plane.x;
    if (world.z <= level)
    {
        return;
    }
    const float4 mirrored = mul(float4(world.x, world.y, 2.0f * level - world.z, 1.0f), unjittered_view_projection);
    if (mirrored.w <= 0.0f)
    {
        return;
    }
    const float2 mirrored_ndc = mirrored.xy / mirrored.w;
    const int2 target = int2(floor(float2(mirrored_ndc.x * 0.5f + 0.5f, 0.5f - mirrored_ndc.y * 0.5f) * float2(size)));
    if (any(target < 0) || target.x >= int(size.x) || target.y >= int(size.y))
    {
        return;
    }
    uint previous = 0u;
    InterlockedMax(reflection_sources[uint(target.y) * size.x + uint(target.x)], ((thread.y + 1u) << 16) | thread.x, previous);
}
