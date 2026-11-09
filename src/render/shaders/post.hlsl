// EAWEngine D3D12 post-processing: bloom (a bright pass at reduced resolution, separable Gaussian
// blurs, and a composite of scene plus bloom into the back buffer). Fullscreen passes draw one
// triangle from SV_VertexID. Written for this engine; compiled offline by DXC (shader model 6.0).
//
// Root constants (b1): instance_base is the source texture's descriptor slot; material_index is the
// pass parameter (blur: 0 horizontal, 1 vertical; composite: the bloom texture's slot).

#include "legacy_common.hlsli"

SamplerState clamp_sampler : register(s1);

struct PostInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
PostInput fullscreen_vs(const uint vertex : SV_VertexID)
{
    // One triangle covering the screen: uv (0,0), (2,0), (0,2).
    PostInput output;
    output.uv = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

// The part of `color` above the threshold, with a soft knee so bright edges fade in rather than pop
// (a quadratic ramp over [threshold - knee, threshold + knee], linear above).
float3 bright_part(const float3 color)
{
    const float threshold = post_parameters.x;
    const float knee = post_parameters.z;
    const float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1.0e-4f);
    return color * (max(soft, brightness - threshold) / max(brightness, 1.0e-4f));
}

// Downsamples the scene by the bloom factor: every texel of the block is thresholded on its own (so a
// small bright source is not averaged below the threshold first), and the result mixes the block's
// mean with its brightest texel, so engine glows a few pixels wide still bloom.
[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 bloom_extract_ps(PostInput input) : SV_TARGET
{
    Texture2D source = scene_textures[instance_base];
    float width;
    float height;
    source.GetDimensions(width, height);
    const float2 texel = 1.0f / float2(width, height);
    float3 sum = 0.0f;
    float3 brightest = 0.0f;
    [unroll] for (int y = 0; y < 4; ++y)
    {
        [unroll] for (int x = 0; x < 4; ++x)
        {
            const float2 offset = (float2(x, y) - 1.5f) * texel;
            const float3 part = bright_part(source.SampleLevel(clamp_sampler, input.uv + offset, 0.0f).rgb);
            sum += part;
            brightest = max(brightest, part);
        }
    }
    return float4(lerp(sum / 16.0f, brightest, 0.5f), 1.0f);
}

// Nine-tap Gaussian along one axis (sigma about 2 texels of the bloom target).
[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 bloom_blur_ps(PostInput input) : SV_TARGET
{
    Texture2D source = scene_textures[instance_base];
    float width;
    float height;
    source.GetDimensions(width, height);
    const float2 step = (material_index == 0u ? float2(1.0f / width, 0.0f) : float2(0.0f, 1.0f / height));
    static const float weights[5] = {0.2270270f, 0.1945946f, 0.1216216f, 0.0540541f, 0.0162162f};
    float3 color = source.Sample(clamp_sampler, input.uv).rgb * weights[0];
    [unroll] for (int tap = 1; tap < 5; ++tap)
    {
        color += source.Sample(clamp_sampler, input.uv + step * tap).rgb * weights[tap];
        color += source.Sample(clamp_sampler, input.uv - step * tap).rgb * weights[tap];
    }
    return float4(color, 1.0f);
}

// Scene plus bloom scaled by the strength, into the back buffer.
[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 bloom_composite_ps(PostInput input) : SV_TARGET
{
    const float3 scene = scene_textures[instance_base].Sample(clamp_sampler, input.uv).rgb;
    const float3 bloom = scene_textures[material_index].Sample(clamp_sampler, input.uv).rgb;
    return float4(scene + bloom * post_parameters.y, 1.0f);
}
