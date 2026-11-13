// EAWEngine D3D12 post-processing: bloom (a bright pass at reduced resolution, separable Gaussian
// blurs, and a composite of scene plus bloom) and edge anti-aliasing. Fullscreen passes draw one
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

// Edge anti-aliasing: a post pass in the manner of FXAA, written for this engine. A pixel on a
// contrasting edge is resampled across the edge, as far as its place along the edge (found by
// walking to both ends) says the stair step covers it, or by its contrast with its 3x3 average for
// details smaller than a pixel. The thresholds are engine choices (common FXAA-style defaults).
static const float edge_contrast_minimum = 0.0312f;
static const float edge_contrast_relative = 0.125f;
static const float subpixel_strength = 0.75f;
static const int edge_search_steps = 10;
static const float edge_search_stride[edge_search_steps] = {1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.0f, 2.0f, 4.0f, 8.0f};

float edge_luma(const float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float luma_at(Texture2D source, const float2 uv)
{
    return edge_luma(source.SampleLevel(clamp_sampler, uv, 0.0f).rgb);
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 edge_aa_ps(PostInput input) : SV_TARGET
{
    Texture2D source = scene_textures[instance_base];
    float width;
    float height;
    source.GetDimensions(width, height);
    const float2 texel = 1.0f / float2(width, height);
    const float2 uv = input.uv;
    const float3 color = source.SampleLevel(clamp_sampler, uv, 0.0f).rgb;
    const float center = edge_luma(color);
    const float north = luma_at(source, uv + float2(0.0f, -texel.y));
    const float south = luma_at(source, uv + float2(0.0f, texel.y));
    const float west = luma_at(source, uv + float2(-texel.x, 0.0f));
    const float east = luma_at(source, uv + float2(texel.x, 0.0f));
    const float lowest = min(center, min(min(north, south), min(west, east)));
    const float highest = max(center, max(max(north, south), max(west, east)));
    const float range = highest - lowest;
    if (range < max(edge_contrast_minimum, highest * edge_contrast_relative))
    {
        return float4(color, 1.0f);
    }
    const float north_west = luma_at(source, uv + float2(-texel.x, -texel.y));
    const float north_east = luma_at(source, uv + float2(texel.x, -texel.y));
    const float south_west = luma_at(source, uv + float2(-texel.x, texel.y));
    const float south_east = luma_at(source, uv + float2(texel.x, texel.y));

    // A horizontal edge changes most from north to south.
    const float across_rows = abs(north + south - 2.0f * center) * 2.0f + abs(north_west + south_west - 2.0f * west) + abs(north_east + south_east - 2.0f * east);
    const float across_columns = abs(west + east - 2.0f * center) * 2.0f + abs(north_west + north_east - 2.0f * north) + abs(south_west + south_east - 2.0f * south);
    const bool horizontal = across_rows >= across_columns;

    // The neighbour across the edge with the larger step, and the step toward it.
    const float before = horizontal ? north : west;
    const float after = horizontal ? south : east;
    const bool toward_before = abs(before - center) >= abs(after - center);
    const float neighbour = toward_before ? before : after;
    const float step_length = (horizontal ? texel.y : texel.x) * (toward_before ? -1.0f : 1.0f);
    const float gradient = abs(neighbour - center) * 0.25f;
    const float edge_middle = (center + neighbour) * 0.5f;

    // Walk along the edge, half a pixel toward the neighbour, until the luma leaves the edge's.
    const float2 across = horizontal ? float2(0.0f, step_length) : float2(step_length, 0.0f);
    const float2 along = horizontal ? float2(texel.x, 0.0f) : float2(0.0f, texel.y);
    const float2 edge_uv = uv + across * 0.5f;
    float2 back_uv = edge_uv - along;
    float2 ahead_uv = edge_uv + along;
    float back_end = luma_at(source, back_uv) - edge_middle;
    float ahead_end = luma_at(source, ahead_uv) - edge_middle;
    bool back_done = abs(back_end) >= gradient;
    bool ahead_done = abs(ahead_end) >= gradient;
    for (int step = 0; step < edge_search_steps && !(back_done && ahead_done); ++step)
    {
        if (!back_done)
        {
            back_uv -= along * edge_search_stride[step];
            back_end = luma_at(source, back_uv) - edge_middle;
            back_done = abs(back_end) >= gradient;
        }
        if (!ahead_done)
        {
            ahead_uv += along * edge_search_stride[step];
            ahead_end = luma_at(source, ahead_uv) - edge_middle;
            ahead_done = abs(ahead_end) >= gradient;
        }
    }
    const float back_distance = horizontal ? uv.x - back_uv.x : uv.y - back_uv.y;
    const float ahead_distance = horizontal ? ahead_uv.x - uv.x : ahead_uv.y - uv.y;
    const bool back_nearer = back_distance < ahead_distance;
    const float nearest = min(back_distance, ahead_distance);
    // Blend only when the nearer end turns away from the center's side of the edge.
    const bool center_darker = center < edge_middle;
    const bool turns_away = ((back_nearer ? back_end : ahead_end) < 0.0f) != center_darker;
    const float edge_offset = turns_away ? 0.5f - nearest / max(back_distance + ahead_distance, 1.0e-6f) : 0.0f;

    // Details smaller than a pixel: by the contrast with the 3x3 average.
    const float average = (2.0f * (north + south + west + east) + north_west + north_east + south_west + south_east) / 12.0f;
    const float sub = smoothstep(0.0f, 1.0f, saturate(abs(average - center) / range));
    const float offset = max(edge_offset, sub * sub * subpixel_strength);
    return float4(source.SampleLevel(clamp_sampler, uv + across * offset, 0.0f).rgb, 1.0f);
}
