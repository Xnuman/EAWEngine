// EAWTools preview shaders (Direct3D 11, shader model 5.0): flat-shaded, textured preview
// geometry with per-instance transforms. Compiled offline by FXC into generated headers. This is
// tool-only rendering; the production renderer lives in src/render.

cbuffer Frame : register(b0)
{
    row_major float4x4 view_projection;
};

cbuffer Part : register(b1)
{
    float alpha_cutoff;
    uint mode;
    float2 part_padding;
};

Texture2D base_texture : register(t0);
SamplerState base_sampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 row0 : INSTANCE0;
    float4 row1 : INSTANCE1;
    float4 row2 : INSTANCE2;
    float4 row3 : INSTANCE3;
    float4 tint : INSTANCE4;
};

struct PixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD1;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PixelInput vertex_main(VertexInput input)
{
    const float4x4 world = float4x4(input.row0, input.row1, input.row2, input.row3);
    const float4 position = mul(float4(input.position, 1.0f), world);
    PixelInput output;
    output.position = mul(position, view_projection);
    output.world = position.xyz;
    output.uv = input.uv;
    output.color = input.color * input.tint;
    return output;
}

float4 pixel_main(PixelInput input) : SV_Target
{
    const float4 texel = base_texture.Sample(base_sampler, input.uv) * input.color;
    // Flat shading from the screen-space derivative of the preview-space position.
    const float3 edge = cross(ddx(input.world), ddy(input.world));
    const float edge_length = length(edge);
    const float facing = edge_length > 1.0e-12f ? abs(dot(edge / edge_length, normalize(float3(0.35f, -0.45f, 0.82f)))) : 1.0f;
    const float light = 0.42f + 0.58f * facing;
    if (mode == 3u)
    {
        return float4(texel.rgb, 1.0f);
    }
    if (mode == 1u)
    {
        clip(texel.a - alpha_cutoff);
    }
    if (mode == 2u)
    {
        clip(texel.a - (1.0f / 255.0f));
        return float4(texel.rgb * light, texel.a);
    }
    return float4(texel.rgb * light, 1.0f);
}
