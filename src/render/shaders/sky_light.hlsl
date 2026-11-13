// The sky's light (docs/sky-research.md): the captured sky (the background draws rendered into six
// cube faces, laid out three by two in the sky atlas) projected to second-order spherical harmonics,
// nine colors that give the light arriving from the whole sky in any direction. One group of 64
// threads sums the texels, each weighted by the solid angle it covers, and writes the nine
// coefficients.

#include "legacy_common.hlsli"

#define SKY_LIGHT_ROOT_SIGNATURE \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, space = 1, numDescriptors = 4096)), " \
    "UAV(u0)"

RWStructuredBuffer<float4> sky_light_out : register(u0);

static const uint SKY_LIGHT_THREADS = 64u;
groupshared float3 partial[SKY_LIGHT_THREADS][9];

[RootSignature(SKY_LIGHT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void sky_light_cs(const uint3 thread : SV_GroupThreadID)
{
    Texture2D atlas = scene_textures[sky_parameters.x];
    const uint face_size = sky_parameters.w;
    const uint texels = face_size * face_size * 6u;
    float3 sums[9];
    for (uint term = 0u; term < 9u; ++term)
    {
        sums[term] = float3(0.0f, 0.0f, 0.0f);
    }
    for (uint texel = thread.x; texel < texels; texel += SKY_LIGHT_THREADS)
    {
        const uint face = texel / (face_size * face_size);
        const uint within = texel % (face_size * face_size);
        const uint2 local = uint2(within % face_size, within / face_size);
        const float2 ndc = float2((local.x + 0.5f) / face_size * 2.0f - 1.0f, 1.0f - (local.y + 0.5f) / face_size * 2.0f);
        const float3 direction = normalize(sky_face_forward(face) + ndc.x * sky_face_right(face) + ndc.y * sky_face_up(face));
        // The solid angle a cube texel covers: its area over the cube of its distance.
        const float texel_area = 4.0f / (face_size * face_size);
        const float weight = texel_area / pow(1.0f + dot(ndc, ndc), 1.5f);
        const float3 color = atlas.Load(int3(sky_face_cell(face) * face_size + local, 0)).rgb * weight;
        float basis[9];
        sh_basis(direction, basis);
        for (uint term = 0u; term < 9u; ++term)
        {
            sums[term] += color * basis[term];
        }
    }
    for (uint term = 0u; term < 9u; ++term)
    {
        partial[thread.x][term] = sums[term];
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread.x == 0u)
    {
        for (uint term = 0u; term < 9u; ++term)
        {
            float3 total = float3(0.0f, 0.0f, 0.0f);
            for (uint other = 0u; other < SKY_LIGHT_THREADS; ++other)
            {
                total += partial[other][term];
            }
            sky_light_out[term] = float4(total, 0.0f);
        }
    }
}
