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
    uint padding;
};

static const uint SHAPE_BILLBOARD = 0u;
static const uint SHAPE_GROUND = 1u;
static const uint SHAPE_KITE = 2u;
static const uint NO_TEXTURE = 0xFFFFFFFFu;

// Bound to the instance slot (root parameter 3) while particles draw.
StructuredBuffer<ParticleData> particles : register(t2);

struct ParticlePixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation uint texture : TEXTURE;
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
    if (particle.shape == SHAPE_KITE && axis_length > 1.0e-4f)
    {
        // Stretched along the direction of travel, turned toward the camera around it; the tail
        // extends behind the particle.
        along = particle.axis / axis_length;
        const float3 to_camera = camera_position_time.xyz - particle.position;
        across = safe_normalize(cross(along, to_camera), camera_right.xyz);
        extent.y = particle.half_size + particle.tail * 0.5f;
        center -= along * (particle.tail * 0.5f);
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
    output.uv = particle.uv_rect.xy + float2(corner.x * 0.5f + 0.5f, 0.5f - corner.y * 0.5f) * particle.uv_rect.zw;
    output.color = particle.color;
    output.texture = particle.texture;
    return output;
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
ParticlePixelOutput particle_ps(ParticlePixelInput input)
{
    const float4 texel = input.texture == NO_TEXTURE ? float4(1.0f, 1.0f, 1.0f, 1.0f) : sample_texture(input.texture, input.uv);
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
