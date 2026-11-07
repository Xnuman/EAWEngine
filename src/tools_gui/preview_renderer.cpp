#include "preview_renderer.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace eaw::tools {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::string_view shader_source = R"(
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
)";

struct FrameConstants
{
    std::array<float, 16> view_projection{};
};

struct PartConstants
{
    float alpha_cutoff{};
    std::uint32_t mode{};
    float padding[2]{};
};

ComPtr<ID3DBlob> compile(const char* entry, const char* target)
{
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(shader_source.data(), shader_source.size(), "eaw_tools_preview", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(),
                          errors.GetAddressOf())))
    {
        return nullptr;
    }
    return code;
}

// Creates or grows a buffer, then writes `bytes` into it.
bool write_buffer(ID3D11Device* device, ID3D11DeviceContext* context, ComPtr<ID3D11Buffer>& buffer, std::uint32_t& capacity, const UINT bind, const void* data, const std::size_t bytes)
{
    if (bytes == 0 || bytes > 0x7fffffffU)
    {
        return false;
    }
    if (!buffer || capacity < bytes)
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(std::max<std::size_t>(bytes, 64));
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = bind;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        buffer.Reset();
        capacity = 0;
        if (FAILED(device->CreateBuffer(&description, nullptr, buffer.GetAddressOf())))
        {
            return false;
        }
        capacity = description.ByteWidth;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return false;
    }
    std::memcpy(mapped.pData, data, bytes);
    context->Unmap(buffer.Get(), 0);
    return true;
}

} // namespace

bool PreviewMesh::upload(ID3D11Device* device, ID3D11DeviceContext* context, const std::span<const PreviewVertex> vertices, const std::span<const std::uint32_t> indices,
                         std::vector<PreviewPart> parts)
{
    parts_.clear();
    if (vertices.empty() || indices.empty())
    {
        return false;
    }
    if (!write_buffer(device, context, vertex_buffer_, vertex_capacity_, D3D11_BIND_VERTEX_BUFFER, vertices.data(), vertices.size_bytes()) ||
        !write_buffer(device, context, index_buffer_, index_capacity_, D3D11_BIND_INDEX_BUFFER, indices.data(), indices.size_bytes()))
    {
        return false;
    }
    parts_ = std::move(parts);
    return true;
}

bool PreviewMesh::set_instances(ID3D11Device* device, ID3D11DeviceContext* context, const std::span<const PreviewInstance> instances)
{
    instance_count_ = 0;
    if (instances.empty())
    {
        return true;
    }
    // Instance layout matches the INSTANCE0..4 semantics: four matrix rows, then the tint.
    std::vector<float> packed;
    packed.reserve(instances.size() * 20);
    for (const auto& instance : instances)
    {
        packed.insert(packed.end(), instance.transform.begin(), instance.transform.end());
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            packed.push_back(static_cast<float>((instance.tint >> shift) & 0xffU) / 255.0F);
        }
    }
    if (!write_buffer(device, context, instance_buffer_, instance_capacity_, D3D11_BIND_VERTEX_BUFFER, packed.data(), packed.size() * sizeof(float)))
    {
        return false;
    }
    instance_count_ = static_cast<std::uint32_t>(instances.size());
    return true;
}

bool PreviewRenderer::initialize(ID3D11Device* device)
{
    if (device == nullptr)
    {
        return false;
    }
    device_ = device;
    device->GetImmediateContext(context_.GetAddressOf());

    const auto vertex_code = compile("vertex_main", "vs_5_0");
    const auto pixel_code = compile("pixel_main", "ps_5_0");
    if (!vertex_code || !pixel_code)
    {
        return false;
    }
    const D3D11_INPUT_ELEMENT_DESC layout[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(PreviewVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PreviewVertex, texture_coordinates), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(PreviewVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    if (FAILED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader_.GetAddressOf())) ||
        FAILED(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), input_layout_.GetAddressOf())))
    {
        return false;
    }

    D3D11_BUFFER_DESC constants{};
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    constants.ByteWidth = sizeof(FrameConstants);
    if (FAILED(device->CreateBuffer(&constants, nullptr, frame_constants_.GetAddressOf())))
    {
        return false;
    }
    constants.ByteWidth = sizeof(PartConstants);
    if (FAILED(device->CreateBuffer(&constants, nullptr, part_constants_.GetAddressOf())))
    {
        return false;
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_ANISOTROPIC;
    sampler.MaxAnisotropy = 8;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    // Two-sided rasterization: winding conventions are not yet verified for every asset, and depth
    // testing (not culling) is what removes hidden surfaces here.
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = FALSE;
    rasterizer.MultisampleEnable = TRUE;
    rasterizer.AntialiasedLineEnable = FALSE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device->CreateSamplerState(&sampler, sampler_.GetAddressOf())) || FAILED(device->CreateRasterizerState(&rasterizer, rasterizer_.GetAddressOf())) ||
        FAILED(device->CreateDepthStencilState(&depth, depth_write_.GetAddressOf())))
    {
        return false;
    }
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device->CreateDepthStencilState(&depth, depth_read_.GetAddressOf())))
    {
        return false;
    }

    D3D11_BLEND_DESC blend{};
    auto& target = blend.RenderTarget[0];
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = D3D11_BLEND_ZERO;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_ZERO;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blend, opaque_blend_.GetAddressOf())))
    {
        return false;
    }
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    if (FAILED(device->CreateBlendState(&blend, alpha_blend_.GetAddressOf())))
    {
        return false;
    }
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_ONE;
    if (FAILED(device->CreateBlendState(&blend, additive_blend_.GetAddressOf())))
    {
        return false;
    }

    const std::uint32_t white = 0xffffffffU;
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = 1;
    texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_IMMUTABLE;
    texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{&white, sizeof(white), 0};
    ComPtr<ID3D11Texture2D> white_texture;
    if (FAILED(device->CreateTexture2D(&texture, &initial, white_texture.GetAddressOf())) || FAILED(device->CreateShaderResourceView(white_texture.Get(), nullptr, white_texture_.GetAddressOf())))
    {
        return false;
    }
    return SUCCEEDED(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, pixel_shader_.GetAddressOf()));
}

bool PreviewRenderer::resize(PreviewTarget& target, const std::uint32_t width, const std::uint32_t height)
{
    if (target.width_ == width && target.height_ == height && target.resolved_view_)
    {
        return true;
    }
    target = {};
    UINT samples = 4;
    UINT quality = 0;
    if (FAILED(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &quality)) || quality == 0)
    {
        samples = 1;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = samples;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, target.color_.GetAddressOf())) ||
        FAILED(device_->CreateRenderTargetView(target.color_.Get(), nullptr, target.color_view_.GetAddressOf())))
    {
        target = {};
        return false;
    }
    description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, target.depth_.GetAddressOf())) ||
        FAILED(device_->CreateDepthStencilView(target.depth_.Get(), nullptr, target.depth_view_.GetAddressOf())))
    {
        target = {};
        return false;
    }
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, target.resolved_.GetAddressOf())) ||
        FAILED(device_->CreateShaderResourceView(target.resolved_.Get(), nullptr, target.resolved_view_.GetAddressOf())))
    {
        target = {};
        return false;
    }
    target.width_ = width;
    target.height_ = height;
    return true;
}

void PreviewRenderer::draw_parts(const PreviewMesh& mesh, const bool transparent_pass)
{
    for (const auto& part : mesh.parts_)
    {
        const bool transparent = part.blend == PreviewBlend::blended || part.blend == PreviewBlend::additive;
        if (transparent != transparent_pass || part.index_count == 0)
        {
            continue;
        }
        const PartConstants constants{part.alpha_cutoff, static_cast<std::uint32_t>(part.blend), {}};
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(part_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            continue;
        }
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context_->Unmap(part_constants_.Get(), 0);
        auto* blend = part.blend == PreviewBlend::additive ? additive_blend_.Get() : part.blend == PreviewBlend::blended ? alpha_blend_.Get() : opaque_blend_.Get();
        context_->OMSetBlendState(blend, nullptr, 0xffffffffU);
        auto* texture = part.texture != nullptr ? part.texture : white_texture_.Get();
        context_->PSSetShaderResources(0, 1, &texture);
        context_->DrawIndexedInstanced(part.index_count, mesh.instance_count_, part.first_index, 0, 0);
    }
}

bool PreviewRenderer::render(PreviewTarget& target, const std::uint32_t width, const std::uint32_t height, const std::array<float, 16>& view_projection,
                             const std::span<const PreviewMesh* const> meshes, const std::array<float, 4>& clear_color)
{
    if (!ready() || width == 0 || height == 0 || width > 16384 || height > 16384 || !resize(target, width, height))
    {
        return false;
    }

    // Preserve whatever the UI frame has bound; ImGui rebinds its own state when it renders.
    ComPtr<ID3D11RenderTargetView> previous_target;
    ComPtr<ID3D11DepthStencilView> previous_depth;
    context_->OMGetRenderTargets(1, previous_target.GetAddressOf(), previous_depth.GetAddressOf());
    UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT previous_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    context_->RSGetViewports(&viewport_count, previous_viewports);

    FrameConstants frame{view_projection};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context_->Map(frame_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &frame, sizeof(frame));
        context_->Unmap(frame_constants_.Get(), 0);
    }

    auto* color_view = target.color_view_.Get();
    context_->OMSetRenderTargets(1, &color_view, target.depth_view_.Get());
    context_->ClearRenderTargetView(color_view, clear_color.data());
    context_->ClearDepthStencilView(target.depth_view_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0F, 0);
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F};
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(rasterizer_.Get());
    context_->IASetInputLayout(input_layout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->GSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* constant_buffers[]{frame_constants_.Get(), part_constants_.Get()};
    context_->VSSetConstantBuffers(0, 2, constant_buffers);
    context_->PSSetConstantBuffers(0, 2, constant_buffers);
    auto* sampler = sampler_.Get();
    context_->PSSetSamplers(0, 1, &sampler);

    for (const bool transparent_pass : {false, true})
    {
        context_->OMSetDepthStencilState(transparent_pass ? depth_read_.Get() : depth_write_.Get(), 0);
        for (const auto* mesh : meshes)
        {
            if (mesh == nullptr || !mesh->ready())
            {
                continue;
            }
            ID3D11Buffer* buffers[]{mesh->vertex_buffer_.Get(), mesh->instance_buffer_.Get()};
            const UINT strides[]{sizeof(PreviewVertex), 20 * sizeof(float)};
            const UINT offsets[]{0, 0};
            context_->IASetVertexBuffers(0, 2, buffers, strides, offsets);
            context_->IASetIndexBuffer(mesh->index_buffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
            draw_parts(*mesh, transparent_pass);
        }
    }

    context_->ResolveSubresource(target.resolved_.Get(), 0, target.color_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3D11ShaderResourceView* no_texture = nullptr;
    context_->PSSetShaderResources(0, 1, &no_texture);
    auto* restored_target = previous_target.Get();
    context_->OMSetRenderTargets(1, &restored_target, previous_depth.Get());
    if (viewport_count > 0)
    {
        context_->RSSetViewports(viewport_count, previous_viewports);
    }
    return true;
}

std::array<float, 16> orbit_view_projection(const std::array<float, 3>& center, const float yaw, const float pitch, const float scale, const float width, const float height,
                                            const float depth_radius) noexcept
{
    const auto yc = std::cos(yaw);
    const auto ys = std::sin(yaw);
    const auto pc = std::cos(pitch);
    const auto ps = std::sin(pitch);
    const auto sx = 2.0F * scale / std::max(width, 1.0F);
    const auto sy = 2.0F * scale / std::max(height, 1.0F);
    const auto sz = 1.0F / (2.0F * std::max(depth_radius, 1.0e-3F));
    // Matches the overlay projection: horizontal = yc*x + ys*y; forward = -ys*x + yc*y;
    // vertical = -ps*forward + pc*z; depth = pc*forward + ps*z (larger depth is nearer).
    const std::array<float, 3> x{yc * sx, ys * sx, 0.0F};
    const std::array<float, 3> y{ps * ys * sy, -ps * yc * sy, pc * sy};
    const std::array<float, 3> z{pc * ys * sz, -pc * yc * sz, -ps * sz};
    const auto dot = [&](const std::array<float, 3>& row)
    {
        return row[0] * center[0] + row[1] * center[1] + row[2] * center[2];
    };
    return {x[0], y[0], z[0], 0.0F, x[1], y[1], z[1], 0.0F, x[2], y[2], z[2], 0.0F, -dot(x), -dot(y), 0.5F - dot(z), 1.0F};
}

} // namespace eaw::tools
