// The FFT ocean's compute passes (docs/water-research.md, "The ocean plan", stage 2): each frame the
// cascades' starting spectra (render/ocean_spectrum.hpp) evolve to the scene's time (deep-water
// dispersion), eight real fields are carried two to a complex value through inverse FFTs along the
// rows and then the columns (256 points in shared memory, radix 2), and the result is packed as
// half floats for the copy into each cascade's displacement and derivative textures. Written from
// Tessendorf's "Simulating Ocean Water" (2002): the choppy horizontal displacement -i k/|k| h, the
// slopes i k h, and the Jacobian of the displaced surface for folding (foam). Foam persists: each
// cascade's foam lives in the packed buffer between frames, fades by its half-life, and gathers where
// the cascade folds (the surface's Jacobian below the threshold), as Crest, Atlas, and Sea of Thieves
// keep theirs.

#define OCEAN_ROOT_SIGNATURE \
    "RootConstants(num32BitConstants = 12, b0), " \
    "SRV(t0), " \
    "UAV(u0), " \
    "UAV(u1)"

static const uint OCEAN_N = 256u;
static const uint OCEAN_LOG2_N = 8u;
static const float OCEAN_TAU = 6.2831853f;
// Foam: a crest foams fully this far below the threshold; foam halves in this many seconds (engine
// choices, by eye).
static const float OCEAN_FOAM_JACOBIAN_RANGE = 0.5f;
static const float OCEAN_FOAM_HALF_LIFE = 2.0f;

cbuffer OceanConstants : register(b0)
{
    float ocean_time;
    float ocean_gravity;
    float ocean_choppiness;
    uint ocean_cascade_count;
    float4 ocean_patch_sizes; // world units, per cascade
    float ocean_delta;          // seconds since the last update (foam's fading)
    float ocean_foam_threshold; // the Jacobian below which crests foam
    float2 ocean_unused;
};

// Per cascade, N x N cells (row-major, wave numbers -N/2 .. N/2 - 1): h0(k) in xy, conj(h0(-k)) in zw.
StructuredBuffer<float4> spectrum : register(t0);
// Per cascade and cell, two values of two complex numbers each: (Dx + i Dy, h + i Dxy) and
// (hx + i hy, Dxx + i Dyy).
RWStructuredBuffer<float4> fields : register(u0);
// Per cascade, two blocks of N x N half4 texels: displacement (x, y, height) and derivatives
// (slope x, slope y, Jacobian, foam). The foam is read back the next frame.
RWStructuredBuffer<uint2> packed : register(u1);

float2 complex_multiply(const float2 a, const float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// i z and -i z.
float2 times_i(const float2 z)
{
    return float2(-z.y, z.x);
}

uint cell_index(const uint cascade, const uint2 cell)
{
    return (cascade * OCEAN_N + cell.y) * OCEAN_N + cell.x;
}

[RootSignature(OCEAN_ROOT_SIGNATURE)]
[numthreads(16, 16, 1)]
void evolve_cs(const uint3 thread : SV_DispatchThreadID)
{
    const uint cascade = thread.z;
    if (cascade >= ocean_cascade_count || any(thread.xy >= OCEAN_N))
    {
        return;
    }
    const uint index = cell_index(cascade, thread.xy);
    const float2 n = float2(thread.xy) - float(OCEAN_N / 2u);
    const float2 k = n * (OCEAN_TAU / ocean_patch_sizes[cascade]);
    const float length_k = length(k);
    if (length_k < 1.0e-6f)
    {
        fields[index * 2u] = 0.0f;
        fields[index * 2u + 1u] = 0.0f;
        return;
    }
    const float omega = sqrt(ocean_gravity * length_k);
    float phase_sin;
    float phase_cos;
    sincos(omega * ocean_time, phase_sin, phase_cos);
    const float4 start = spectrum[index];
    const float2 h = complex_multiply(start.xy, float2(phase_cos, phase_sin)) + complex_multiply(start.zw, float2(phase_cos, -phase_sin));
    const float2 unit_k = k / length_k;
    const float2 minus_i_h = -times_i(h);
    const float2 dx = unit_k.x * minus_i_h;
    const float2 dy = unit_k.y * minus_i_h;
    const float2 hx = k.x * times_i(h);
    const float2 hy = k.y * times_i(h);
    const float2 dxx = (k.x * unit_k.x) * h;
    const float2 dyy = (k.y * unit_k.y) * h;
    const float2 dxy = (k.x * unit_k.y) * h;
    // Two real fields per complex value: A + i B, whose inverse transform is a + i b.
    fields[index * 2u] = float4(dx + times_i(dy), h + times_i(dxy));
    fields[index * 2u + 1u] = float4(hx + times_i(hy), dxx + times_i(dyy));
}

groupshared float4 line_first[2][OCEAN_N];
groupshared float4 line_second[2][OCEAN_N];

float4 multiply_pair(const float4 pair, const float2 w)
{
    return float4(complex_multiply(pair.xy, w), complex_multiply(pair.zw, w));
}

// One inverse FFT of N points along a line (a row, or a column when `columns`): 128 threads, each a
// butterfly per stage, on bit-reversed input.
void inverse_fft_line(const uint thread, const uint line_index, const uint cascade, const bool columns)
{
    for (uint copy = 0u; copy < 2u; ++copy)
    {
        const uint position = thread + copy * (OCEAN_N / 2u);
        const uint2 cell = columns ? uint2(line_index, position) : uint2(position, line_index);
        const uint index = cell_index(cascade, cell);
        const uint reversed = reversebits(position) >> (32u - OCEAN_LOG2_N);
        line_first[0][reversed] = fields[index * 2u];
        line_second[0][reversed] = fields[index * 2u + 1u];
    }
    GroupMemoryBarrierWithGroupSync();
    uint source = 0u;
    for (uint stage = 0u; stage < OCEAN_LOG2_N; ++stage)
    {
        const uint half_span = 1u << stage;
        const uint group = thread / half_span;
        const uint offset = thread % half_span;
        const uint top = group * half_span * 2u + offset;
        const uint bottom = top + half_span;
        float twiddle_sin;
        float twiddle_cos;
        sincos(OCEAN_TAU * float(offset) / float(half_span * 2u), twiddle_sin, twiddle_cos);
        const float2 twiddle = float2(twiddle_cos, twiddle_sin);
        const float4 first_top = line_first[source][top];
        const float4 first_bottom = multiply_pair(line_first[source][bottom], twiddle);
        const float4 second_top = line_second[source][top];
        const float4 second_bottom = multiply_pair(line_second[source][bottom], twiddle);
        line_first[1u - source][top] = first_top + first_bottom;
        line_first[1u - source][bottom] = first_top - first_bottom;
        line_second[1u - source][top] = second_top + second_bottom;
        line_second[1u - source][bottom] = second_top - second_bottom;
        source = 1u - source;
        GroupMemoryBarrierWithGroupSync();
    }
    for (uint copy = 0u; copy < 2u; ++copy)
    {
        const uint position = thread + copy * (OCEAN_N / 2u);
        const uint2 cell = columns ? uint2(line_index, position) : uint2(position, line_index);
        const uint index = cell_index(cascade, cell);
        fields[index * 2u] = line_first[source][position];
        fields[index * 2u + 1u] = line_second[source][position];
    }
}

[RootSignature(OCEAN_ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
void fft_rows_cs(const uint3 group : SV_GroupID, const uint3 thread : SV_GroupThreadID)
{
    inverse_fft_line(thread.x, group.x, group.y, false);
}

[RootSignature(OCEAN_ROOT_SIGNATURE)]
[numthreads(128, 1, 1)]
void fft_columns_cs(const uint3 group : SV_GroupID, const uint3 thread : SV_GroupThreadID)
{
    inverse_fft_line(thread.x, group.x, group.y, true);
}

uint pack_halves(const float low, const float high)
{
    return f32tof16(low) | (f32tof16(high) << 16u);
}

[RootSignature(OCEAN_ROOT_SIGNATURE)]
[numthreads(16, 16, 1)]
void pack_cs(const uint3 thread : SV_DispatchThreadID)
{
    const uint cascade = thread.z;
    if (cascade >= ocean_cascade_count || any(thread.xy >= OCEAN_N))
    {
        return;
    }
    const uint index = cell_index(cascade, thread.xy);
    // The spectrum is stored from -N/2: shifting it to the start multiplies the result by (-1)^(x + y).
    const float sign = ((thread.x + thread.y) & 1u) != 0u ? -1.0f : 1.0f;
    const float4 first = fields[index * 2u] * sign;
    const float4 second = fields[index * 2u + 1u] * sign;
    const float lambda = ocean_choppiness;
    const float dxx = lambda * second.z;
    const float dyy = lambda * second.w;
    const float dxy = lambda * first.w;
    const float jacobian = (1.0f + dxx) * (1.0f + dyy) - dxy * dxy;
    const uint texel = thread.y * OCEAN_N + thread.x;
    const uint block = OCEAN_N * OCEAN_N;
    packed[cascade * 2u * block + texel] = uint2(pack_halves(lambda * first.x, lambda * first.y), pack_halves(first.z, 0.0f));
    const uint derivative_texel = (cascade * 2u + 1u) * block + texel;
    const float previous = f16tof32(packed[derivative_texel].y >> 16u);
    const float gathered = saturate((ocean_foam_threshold - jacobian) / OCEAN_FOAM_JACOBIAN_RANGE);
    const float foam = max(previous * exp2(-ocean_delta / OCEAN_FOAM_HALF_LIFE), gathered);
    packed[derivative_texel] = uint2(pack_halves(second.x, second.y), pack_halves(jacobian, foam));
}
