#pragma once

#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace eaw::tools {

// Small depth-buffered Direct3D 11 renderer for EAWTools model and map previews. It renders into
// an offscreen texture that the UI shows as an image. It is tool code: the production renderer is
// the Direct3D 12 path in src/render, and this exists only so previews have correct visibility.

enum class PreviewBlend : std::uint8_t
{
    opaque,
    alpha_test,
    blended,
    additive,
};

struct PreviewVertex
{
    Vector3 position{};
    Vector2 texture_coordinates{};
    std::uint32_t color{0xffffffffU};
};

struct PreviewPart
{
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    ID3D11ShaderResourceView* texture{};
    PreviewBlend blend{PreviewBlend::opaque};
    float alpha_cutoff{0.5F};
};

// Row-vector 4x4 transform (translation in elements 12..14) plus an RGBA8 tint.
struct PreviewInstance
{
    Matrix4 transform{Matrix4::identity()};
    std::uint32_t tint{0xffffffffU};
};

// GPU geometry for one model or terrain patch, plus its current instance list.
class PreviewMesh final
{
public:
    // Replaces vertices/indices/parts. Index buffers use 32-bit indices.
    bool upload(ID3D11Device* device, ID3D11DeviceContext* context, std::span<const PreviewVertex> vertices, std::span<const std::uint32_t> indices, std::vector<PreviewPart> parts);
    bool set_instances(ID3D11Device* device, ID3D11DeviceContext* context, std::span<const PreviewInstance> instances);

    [[nodiscard]] bool ready() const noexcept
    {
        return vertex_buffer_ && index_buffer_ && instance_count_ > 0;
    }

private:
    friend class PreviewRenderer;

    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> instance_buffer_;
    std::uint32_t vertex_capacity_{};
    std::uint32_t index_capacity_{};
    std::uint32_t instance_capacity_{};
    std::uint32_t instance_count_{};
    std::vector<PreviewPart> parts_;
};

// Offscreen color/depth target sized to a UI canvas. 4x MSAA is resolved into a texture that the
// UI samples.
class PreviewTarget final
{
public:
    [[nodiscard]] ID3D11ShaderResourceView* view() const noexcept
    {
        return resolved_view_.Get();
    }

private:
    friend class PreviewRenderer;

    std::uint32_t width_{};
    std::uint32_t height_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> color_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> color_view_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resolved_view_;
};

class PreviewRenderer final
{
public:
    // Returns false (and stays unusable) when the device cannot create the pipeline.
    bool initialize(ID3D11Device* device);

    [[nodiscard]] bool ready() const noexcept
    {
        return pixel_shader_ != nullptr;
    }

    // Draws opaque and alpha-tested parts of every mesh with depth writes, then blended and
    // additive parts without depth writes. `view_projection` maps preview space to clip space
    // (row-vector convention). The caller's render targets and viewport are restored afterwards.
    bool render(PreviewTarget& target, std::uint32_t width, std::uint32_t height, const Matrix4& view_projection, std::span<const PreviewMesh* const> meshes, const std::array<float, 4>& clear_color);

    [[nodiscard]] ID3D11Device* device() const noexcept
    {
        return device_.Get();
    }
    [[nodiscard]] ID3D11DeviceContext* context() const noexcept
    {
        return context_.Get();
    }

private:
    bool resize(PreviewTarget& target, std::uint32_t width, std::uint32_t height);
    void draw_parts(const PreviewMesh& mesh, bool transparent_pass);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> frame_constants_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> part_constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_write_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_read_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> opaque_blend_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> alpha_blend_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> additive_blend_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
};

// Orthographic orbit projection shared by the GPU preview and its ImGui overlays: yaw about +Z,
// pitch toward the viewer, `scale` pixels per preview unit, and `depth_radius` bounding the depth
// range. Returns the clip-space transform for a `width` x `height` target centered on `center`.
[[nodiscard]] Matrix4 orbit_view_projection(const Vector3& center, float yaw, float pitch, float scale, float width, float height, float depth_radius) noexcept;

} // namespace eaw::tools
