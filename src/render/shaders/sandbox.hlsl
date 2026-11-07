// EAWEngine D3D12 visual sandbox shaders: instanced, skinned, base-color-textured geometry.
// Compiled offline by DXC (shader model 6.0) into generated headers; never compiled at runtime.
// The root signature below is the single definition of the layout used by d3d12_renderer.cpp.

#define SANDBOX_ROOT_SIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "RootConstants(num32BitConstants = 16, b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "RootConstants(num32BitConstants = 8, b1, visibility = SHADER_VISIBILITY_PIXEL), " \
    "SRV(t1, visibility = SHADER_VISIBILITY_VERTEX), " \
    "SRV(t2, visibility = SHADER_VISIBILITY_VERTEX), " \
    "RootConstants(num32BitConstants = 1, b2, visibility = SHADER_VISIBILITY_VERTEX), " \
    "StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, " \
    "addressW = TEXTURE_ADDRESS_WRAP, comparisonFunc = COMPARISON_ALWAYS, borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, " \
    "visibility = SHADER_VISIBILITY_PIXEL)"

Texture2D SandboxTexture : register(t0);
struct BoneTransform
{
    row_major float4x4 transform;
};
StructuredBuffer<BoneTransform> BoneTransforms : register(t1);
struct InstanceData
{
    row_major float4x4 transform;
    float4 tint;
    uint bone_offset;
    uint flags;
    uint2 padding;
};
StructuredBuffer<InstanceData> Instances : register(t2);
SamplerState SandboxSampler : register(s0);

cbuffer FrameConstants : register(b0)
{
    float4x4 view_projection;
};

cbuffer DrawConstants : register(b2)
{
    uint instance_base;
};

cbuffer MaterialConstants : register(b1)
{
    float4 material_tint;
    uint4 material_flags;
};

struct VsInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bone_indices : BLENDINDICES0;
    float4 bone_weights : BLENDWEIGHT0;
    uint instance_id : SV_InstanceID;
};

struct PsInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tint : COLOR0;
};

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
PsInput vs_main(VsInput input)
{
    // SV_InstanceID restarts at zero for every draw, so the batch's first instance arrives as a
    // root constant.
    const InstanceData instance = Instances[instance_base + input.instance_id];
    float4 skinned_position = 0.0f;
    float3 skinned_normal = 0.0f;
    float total_weight = 0.0f;
    [unroll]
    for (uint influence = 0; influence < 4; ++influence)
    {
        const float weight = input.bone_weights[influence];
        if (weight <= 0.0f)
        {
            continue;
        }
        const row_major float4x4 skin = BoneTransforms[input.bone_indices[influence] + instance.bone_offset].transform;
        skinned_position += mul(float4(input.position, 1.0f), skin) * weight;
        skinned_normal += mul(float4(input.normal, 0.0f), skin).xyz * weight;
        total_weight += weight;
    }
    if (total_weight <= 0.000001f)
    {
        skinned_position = float4(input.position, 1.0f);
        skinned_normal = input.normal;
    }
    else if (abs(total_weight - 1.0f) > 0.0001f)
    {
        skinned_position /= total_weight;
        skinned_normal /= total_weight;
    }
    PsInput output;
    if ((instance.flags & 1u) != 0u)
    {
        // Backgrounds follow the camera: transforming a direction (w = 0) drops the view
        // translation, and the depth is pinned just inside the far plane.
        const float3 direction = mul(float4(skinned_position.xyz, 0.0f), instance.transform).xyz;
        output.position = mul(float4(direction, 0.0f), view_projection);
        output.position.z = output.position.w * 0.999999f;
    }
    else
    {
        output.position = mul(mul(skinned_position, instance.transform), view_projection);
    }
    output.normal = mul(float4(skinned_normal, 0.0f), instance.transform).xyz;
    output.uv = input.uv;
    output.tint = instance.tint;
    return output;
}

[RootSignature(SANDBOX_ROOT_SIGNATURE)]
float4 ps_main(PsInput input) : SV_TARGET
{
    float4 color = SandboxTexture.Sample(SandboxSampler, input.uv) * material_tint * input.tint;
    if (material_flags.x != 0 && color.a < 0.5f)
    {
        discard;
    }
    const float3 light_direction = normalize(float3(0.35f, -0.45f, 0.82f));
    const float lighting = 0.32f + 0.68f * abs(dot(normalize(input.normal), light_direction));
    color.rgb *= lighting;
    return color;
}
