// EAWEngine D3D12 debug lines: world-space colored line segments (bounds, axes) drawn after the
// scene with depth testing but no depth or pick writes. Written for this engine; compiled offline
// by DXC (shader model 6.0).

#include "legacy_common.hlsli"

struct DebugVertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct DebugPixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct DebugPixelOutput
{
    float4 color : SV_TARGET0;
    uint pick_id : SV_TARGET1; // masked by the pipeline; declared to match the bound targets
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
DebugPixelInput debug_vs(DebugVertexInput input)
{
    DebugPixelInput output;
    output.position = mul(float4(input.position, 1.0f), view_projection);
    output.color = input.color;
    return output;
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
DebugPixelOutput debug_ps(DebugPixelInput input)
{
    DebugPixelOutput output;
    output.color = input.color;
    output.pick_id = 0u;
    return output;
}
