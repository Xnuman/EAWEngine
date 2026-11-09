// EAWEngine D3D12 particles: one quad per particle, expanded in the vertex shader from a structured
// buffer (no vertex input), drawn after the scene without depth or pick writes. Written for this
// engine; compiled offline by DXC (shader model 6.0).

#include "legacy_common.hlsli"

// Keep in sync with GpuParticle in d3d12_renderer_impl.hpp and VisualParticleShape.
struct ParticleData
{
    float3 position;
    float half_size;
    float4 color;
    float4 uv_rect;
    float3 axis;
    float rotation;
    float tail;
    uint texture;
    uint shape;
    uint flags; // bit 0: monochrome
};

static const uint SHAPE_BILLBOARD = 0u;
static const uint SHAPE_GROUND = 1u;
static const uint SHAPE_KITE = 2u;
static const uint SHAPE_BEAM = 3u;
static const uint SHAPE_TEARDROP = 4u;
static const uint FLAG_MONOCHROME = 1u;
static const uint NO_TEXTURE = 0xFFFFFFFFu;

// Bound to the instance slot (root parameter 3) while particles draw.
StructuredBuffer<ParticleData> particles : register(t2);

struct ParticlePixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation uint texture : TEXTURE;
    nointerpolation uint flags : FLAGS;
    // The quad corner (x across, y along), for shapes drawn procedurally.
    float2 local : LOCAL;
    nointerpolation uint shape : SHAPE;
};

struct ParticlePixelOutput
{
    float4 color : SV_TARGET0;
    uint2 pick : SV_TARGET1; // masked by the pipeline; declared to match the bound targets
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
ParticlePixelInput particle_vs(const uint vertex : SV_VertexID, const uint instance : SV_InstanceID)
{
    const ParticleData particle = particles[instance_base + instance];
    // Two triangles: corners in -1..1, x across, y up the quad.
    static const float2 corners[6] = {float2(-1.0f, -1.0f), float2(1.0f, -1.0f), float2(1.0f, 1.0f), float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(-1.0f, 1.0f)};
    const float2 corner = corners[vertex];

    float3 across;
    float3 along;
    float2 extent = float2(particle.half_size, particle.half_size);
    float3 center = particle.position;
    const float axis_length = length(particle.axis);
    float2 uv = particle.uv_rect.xy + float2(corner.x * 0.5f + 0.5f, 0.5f - corner.y * 0.5f) * particle.uv_rect.zw;
    if ((particle.shape == SHAPE_BEAM || particle.shape == SHAPE_TEARDROP) && axis_length > 1.0e-4f)
    {
        along = particle.axis / axis_length;
        const float3 to_camera = camera_position_time.xyz - particle.position;
        across = safe_normalize(cross(along, to_camera), camera_right.xyz);
        extent.y = particle.tail * 0.5f;
        // The cell's bolt runs diagonally from bottom-left to top-right (+u, -v): map the quad's
        // long axis along it and its width across it, staying inside the cell (0.38 + 0.10 < 0.5) so
        // neighbouring pills never bleed in.
        const float2 cell_center = particle.uv_rect.xy + particle.uv_rect.zw * 0.5f;
        uv = cell_center + particle.uv_rect.zw * (corner.y * float2(0.38f, -0.38f) + corner.x * float2(0.10f, 0.10f));
    }
    else if (particle.shape == SHAPE_KITE && axis_length > 1.0e-4f)
    {
        // A diamond facing the camera: the quad turned 45 degrees so its top-left corner (texture
        // u = 0, v = 0) trails the velocity as seen on screen, and that corner pushed out by
        // max(1, tail x screen fraction / sqrt 2), as the GlyphX particle editor draws kites.
        const float3 view = safe_normalize(particle.position - camera_position_time.xyz, float3(0.0f, 1.0f, 0.0f));
        const float3 velocity = particle.axis / axis_length;
        const float3 on_screen = velocity - view * dot(velocity, view);
        const float screen_fraction = length(on_screen);
        const float3 back = -safe_normalize(on_screen, camera_up.xyz);
        const float3 side = cross(view, back);
        const float3 e1 = (side - back) * 0.70710678f;
        const float3 e2 = (side + back) * 0.70710678f;
        float stretch = 1.0f;
        if (corner.x < 0.0f && corner.y > 0.0f)
        {
            stretch = max(1.0f, particle.tail * screen_fraction * 0.70710678f);
        }
        const float3 kite_world = center + (e1 * corner.x + e2 * corner.y) * (particle.half_size * stretch);
        ParticlePixelInput kite;
        kite.position = mul(float4(kite_world, 1.0f), view_projection);
        kite.uv = uv;
        kite.color = particle.color;
        kite.texture = particle.texture;
        kite.flags = particle.flags;
        kite.local = corner;
        kite.shape = particle.shape;
        return kite;
    }
    else
    {
        float3 right = camera_right.xyz;
        float3 up = camera_up.xyz;
        if (particle.shape == SHAPE_GROUND)
        {
            right = float3(1.0f, 0.0f, 0.0f);
            up = float3(0.0f, 1.0f, 0.0f);
        }
        const float c = cos(particle.rotation);
        const float s = sin(particle.rotation);
        across = right * c + up * s;
        along = up * c - right * s;
    }
    const float3 world = center + across * (corner.x * extent.x) + along * (corner.y * extent.y);

    ParticlePixelInput output;
    output.position = mul(float4(world, 1.0f), view_projection);
    output.uv = uv;
    output.color = particle.color;
    output.texture = particle.texture;
    output.flags = particle.flags;
    output.local = corner;
    output.shape = particle.shape;
    return output;
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
// A teardrop bolt (tinted custom-render projectiles): a round, full-width head at the front (+y)
// tapering to a thin, fading tail, with a white-hot core. Drawn procedurally; no stock texture holds
// this shape (docs/compatibility-research.md, "Projectile visuals").
float4 teardrop(const float2 local, const float4 tint)
{
    const float along = local.y * 0.5f + 0.5f;     // 0 at the tail, 1 at the front
    const float head = 0.82f;                       // where the head is widest
    float half_width = along < head ? pow(along / head, 0.75f) : sqrt(saturate(1.0f - pow((along - head) / (1.0f - head), 2.0f)));
    half_width = max(half_width, 1.0e-3f);
    const float across = abs(local.x) / half_width;
    const float body = saturate(1.0f - across);
    const float fade = smoothstep(0.0f, 0.35f, along);
    const float glow = body * body * fade;
    const float core = pow(body, 6.0f) * smoothstep(0.3f, 0.9f, along);
    // Additive particles ignore alpha, so the fade is carried in the colour.
    const float strength = glow * tint.a;
    return float4(lerp(tint.rgb, float3(1.0f, 1.0f, 1.0f), core) * strength, strength);
}

ParticlePixelOutput particle_ps(ParticlePixelInput input)
{
    if (input.shape == SHAPE_TEARDROP)
    {
        ParticlePixelOutput drop;
        drop.color = teardrop(input.local, input.color);
        drop.pick = uint2(0u, 0u);
        return drop;
    }
    float4 texel = input.texture == NO_TEXTURE ? float4(1.0f, 1.0f, 1.0f, 1.0f) : sample_texture(input.texture, input.uv);
    if ((input.flags & FLAG_MONOCHROME) != 0u)
    {
        // Tinted bolts: the texel's brightness carries the shape, the colour comes from the particle.
        texel.rgb = max(texel.r, max(texel.g, texel.b));
    }
    ParticlePixelOutput output;
    output.color = texel * input.color;
    output.pick = uint2(0u, 0u);
    return output;
}

// Modulate: destination times a color faded toward white by the particle's alpha.
[RootSignature(SANDBOX_ROOT_SIGNATURE)]
ParticlePixelOutput particle_modulate_ps(ParticlePixelInput input)
{
    const float4 texel = input.texture == NO_TEXTURE ? float4(1.0f, 1.0f, 1.0f, 1.0f) : sample_texture(input.texture, input.uv);
    const float4 color = texel * input.color;
    ParticlePixelOutput output;
    output.color = float4(lerp(float3(1.0f, 1.0f, 1.0f), color.rgb, color.a), 1.0f);
    output.pick = uint2(0u, 0u);
    return output;
}
