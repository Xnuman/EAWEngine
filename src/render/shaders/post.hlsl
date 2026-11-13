// EAWEngine D3D12 post-processing: bloom (a bright pass at reduced resolution, separable Gaussian
// blurs, and a composite of scene plus bloom), edge anti-aliasing, and temporal anti-aliasing. Fullscreen passes draw one
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

// Heat distortion: the scene copy (texture instance_base) sampled shifted by the heat buffer
// (texture material_index; 0.5 shifts nothing), a bump color of 1 or 0 moving the sample by
// HEAT_SHIFT of the screen's height. How far the original shifted is not known: an engine choice.
static const float HEAT_SHIFT = 0.015f;

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 heat_ps(PostInput input) : SV_TARGET
{
    Texture2D scene_copy = scene_textures[instance_base];
    uint width = 1u;
    uint height = 1u;
    scene_copy.GetDimensions(width, height);
    // 128/255 (the 8-bit buffer's 0.5) is exactly no shift.
    const float2 bend = (scene_textures[material_index].Load(int3(input.position.xy, 0)).rg - 128.0f / 255.0f) * 2.0f;
    const float2 shift = bend * HEAT_SHIFT * float2((float)height / (float)width, 1.0f);
    return float4(scene_copy.Sample(clamp_sampler, input.uv + shift).rgb, 1.0f);
}

// Enhanced heat: the scene copy (texture instance_base) shifted by the signed buffer (texture
// material_index, shifts in texture units), the shift faded near the screen's edges and where the
// shifted sample shows something nearer the camera than this pixel (the foreground would be dragged
// into the heat: "leaking"), softly over HEAT_FOREGROUND_FADE world units. Engine choices.
static const float HEAT_EDGE_FADE = 0.03f;
static const float HEAT_FOREGROUND_FADE = 4.0f;

float heat_view_distance(const Texture2D depth, const float2 uv)
{
    uint width = 1u;
    uint height = 1u;
    depth.GetDimensions(width, height);
    const float stored = depth.SampleLevel(clamp_sampler, uv, 0.0f).r;
    const float4 world = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, stored, 1.0f), inverse_view_projection);
    return stored >= 1.0f ? 1.0e6f : distance(world.xyz / world.w, camera_position_time.xyz);
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 heat_enhanced_ps(PostInput input) : SV_TARGET
{
    Texture2D scene_copy = scene_textures[instance_base];
    float2 shift = scene_textures[material_index].Load(int3(input.position.xy, 0)).rg;
    const float2 edge = min(input.uv, 1.0f - input.uv);
    shift *= saturate(min(edge.x, edge.y) / HEAT_EDGE_FADE);
    Texture2D depth = scene_textures[water_parameters.y];
    const float here = heat_view_distance(depth, input.uv);
    const float there = heat_view_distance(depth, input.uv + shift);
    shift *= saturate((there - here) / HEAT_FOREGROUND_FADE + 1.0f);
    return float4(scene_copy.Sample(clamp_sampler, input.uv + shift).rgb, 1.0f);
}

// The occlusion silhouettes' mask (texture instance_base) over the scene: its color at its alpha.
[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 silhouette_composite_ps(PostInput input) : SV_TARGET
{
    return scene_textures[instance_base].Load(int3(input.position.xy, 0));
}

// Edge anti-aliasing: a post pass in the manner of FXAA, written for this engine. A pixel on a
// contrasting edge is resampled across the edge, as far as its place along the edge (found by
// walking to both ends) says the stair step covers it, or by its contrast with its 3x3 average for
// details smaller than a pixel. The thresholds are engine choices (common FXAA-style defaults).
static const float edge_contrast_minimum = 0.0312f;
static const float edge_contrast_relative = 0.125f;
static const float subpixel_strength = 0.75f;
// Where particles marked the reactive mask (the motion target's blue; material_index is its slot),
// the edge pass keeps the pixel: thin bolts and sparks are not jagged edges to blur (engine choice).
static const float REACTIVE_GAIN = 2.0f;

float reactive_at(const float2 position)
{
    return saturate(scene_textures[material_index].Load(int3(position, 0)).b * REACTIVE_GAIN);
}
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
    return float4(lerp(source.SampleLevel(clamp_sampler, uv + across * offset, 0.0f).rgb, color, reactive_at(input.position.xy)), 1.0f);
}

// Temporal anti-aliasing, written for this engine: every frame is rendered with a sub-pixel jitter,
// and this pass blends it with the accumulated history. Each pixel finds where its surface was last
// frame from its motion vector (models that write one) or else from the depth buffer and the
// previous camera, and the history is clipped to the colour range of the current 3x3 neighbourhood
// so stale colours do not ghost. Root constants: instance_base is the frame's colour slot (low 16
// bits) and the motion slot (high 16 bits), material_index the history slot (low 16 bits) and the
// depth slot (high 16 bits).
static const float taa_clip_spread = 1.25f;

struct TaaOutput
{
    float4 frame : SV_TARGET0;
    float4 history : SV_TARGET1;
};

static const float TAA_REACTIVE_WEIGHT = 0.85f;

float3 to_ycocg(const float3 rgb)
{
    return float3(0.25f * rgb.r + 0.5f * rgb.g + 0.25f * rgb.b, 0.5f * rgb.r - 0.5f * rgb.b, -0.25f * rgb.r + 0.5f * rgb.g - 0.25f * rgb.b);
}

float3 from_ycocg(const float3 ycocg)
{
    return float3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
TaaOutput taa_resolve_ps(PostInput input)
{
    Texture2D frame = scene_textures[instance_base & 0xFFFFu];
    Texture2D motion = scene_textures[instance_base >> 16u];
    Texture2D history = scene_textures[material_index & 0xFFFFu];
    Texture2D depth = scene_textures[material_index >> 16u];
    const int2 pixel = int2(input.position.xy);
    const float3 current = frame.Load(int3(pixel, 0)).rgb;

    // The colour range of the neighbourhood (mean and spread in YCoCg), and its nearest depth, so
    // an edge reprojects with its foreground.
    float3 mean = 0.0f;
    float3 square = 0.0f;
    float nearest = 1.0f;
    int2 nearest_at = pixel;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const int2 at = pixel + int2(x, y);
            const float3 sample = to_ycocg(frame.Load(int3(at, 0)).rgb);
            mean += sample;
            square += sample * sample;
            const float at_depth = depth.Load(int3(at, 0)).r;
            if (at_depth < nearest)
            {
                nearest = at_depth;
                nearest_at = at;
            }
        }
    }
    mean /= 9.0f;
    const float3 spread = sqrt(max(square / 9.0f - mean * mean, 0.0f)) * taa_clip_spread;

    // Where this surface was last frame: by its motion vector when it wrote one, else back to the
    // world through the unjittered camera and into the previous frame.
    const float2 moved = motion.Load(int3(nearest_at, 0)).rg;
    const float2 ndc = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f) - taa_parameters.xy;
    float4 world = mul(float4(ndc, nearest, 1.0f), inverse_view_projection);
    world /= world.w;
    const float4 before = mul(world, previous_view_projection);
    // The jitter goes back on, so only real motion moves the sample: a still camera reads the
    // history at this very pixel instead of a fraction off it (which would blur every frame).
    const float2 before_ndc = before.xy / before.w + taa_parameters.xy;
    const bool known = moved.x < MOTION_UNKNOWN * 0.5f;
    const float2 before_uv = known ? input.uv - moved : float2(before_ndc.x * 0.5f + 0.5f, 0.5f - before_ndc.y * 0.5f);
    const bool inside = (known || before.w > 0.0f) && all(before_uv >= 0.0f) && all(before_uv <= 1.0f);

    TaaOutput output;
    float3 result = current;
    if (taa_parameters.z > 0.0f && inside)
    {
        const float3 past = from_ycocg(clamp(to_ycocg(history.SampleLevel(clamp_sampler, before_uv, 0.0f).rgb), mean - spread, mean + spread));
        // Where enhanced heat shifts the image, the new frame weighs more (a reactive mask), so TAA
        // neither smears the distortion nor averages its shimmer away (an engine choice).
        float weight = taa_parameters.w;
        // Particles (bolts, sparks) write no motion: where they lit the pixel, the new frame weighs
        // up to TAA_REACTIVE_WEIGHT so they are neither smeared nor averaged away (engine choice).
        weight = lerp(weight, TAA_REACTIVE_WEIGHT, saturate(motion.Load(int3(pixel, 0)).b * REACTIVE_GAIN));
        if (heat_slots.y != 0u)
        {
            const float2 heat = scene_textures[heat_slots.x].Load(int3(input.position.xy, 0)).rg;
            weight = lerp(weight, 0.6f, saturate(length(heat) * 400.0f));
        }
        result = lerp(past, current, weight);
    }
    output.frame = float4(result, 1.0f);
    output.history = float4(result, 1.0f);
    return output;
}
