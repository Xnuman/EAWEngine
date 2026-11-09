// Pipeline states and the optional pipeline library cache.

#include "d3d12_renderer_impl.hpp"

// Offline-compiled shader model 6.0 bytecode (see src/render/shaders/sandbox.hlsl). Both entry
// points embed the root signature, which the renderer creates from the vertex shader blob.
#include "g_eaw_debug_ps.h"
#include "g_eaw_debug_vs.h"
#include "g_eaw_particle_modulate_ps.h"
#include "g_eaw_particle_ps.h"
#include "g_eaw_particle_vs.h"
#include "g_eaw_post_blur_ps.h"
#include "g_eaw_post_composite_ps.h"
#include "g_eaw_post_extract_ps.h"
#include "g_eaw_post_vs.h"
#include "g_eaw_sandbox_ps.h"
#include "g_eaw_sandbox_vs.h"
#include "g_eaw_terrain_ps.h"
#include "g_eaw_terrain_vs.h"

namespace eaw {

std::uint64_t D3d12Renderer::Impl::fnv1a(std::uint64_t hash, const std::span<const std::byte> bytes) noexcept
{
    for (const auto byte : bytes)
    {
        hash = (hash ^ static_cast<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    return hash;
}

void D3d12Renderer::Impl::open_pipeline_cache()
{
    if (options.pipeline_cache_directory.empty())
    {
        return;
    }
    ComPtr<ID3D12Device1> device1;
    if (FAILED(device.As(&device1)))
    {
        pipeline_cache_status = "unsupported (ID3D12Device1 unavailable)";
        return;
    }
    auto key = fnv1a(1469598103934665603ULL, std::as_bytes(std::span(g_eaw_sandbox_vs)));
    key = fnv1a(key, std::as_bytes(std::span(g_eaw_sandbox_ps)));
    key = fnv1a(key, std::as_bytes(std::span(g_eaw_terrain_vs)));
    key = fnv1a(key, std::as_bytes(std::span(g_eaw_terrain_ps)));
    key = fnv1a(key, std::as_bytes(std::span(&pipeline_layout_version, 1)));
    pipeline_cache_key = key;
    pipeline_cache_file = options.pipeline_cache_directory / "d3d12_pipelines.bin";

    pipeline_cache_status = "created";
    std::error_code error;
    const auto size = std::filesystem::file_size(pipeline_cache_file, error);
    if (!error && size > 24 && size <= maximum_pipeline_cache_bytes)
    {
        std::ifstream file(pipeline_cache_file, std::ios::binary);
        std::array<char, 8> magic{};
        std::uint64_t stored_key{};
        std::uint64_t payload_size{};
        file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        file.read(reinterpret_cast<char*>(&stored_key), sizeof(stored_key));
        file.read(reinterpret_cast<char*>(&payload_size), sizeof(payload_size));
        if (!file || magic != pipeline_cache_magic || payload_size != size - 24)
        {
            pipeline_cache_status = "discarded (unrecognized or truncated file)";
        }
        else if (stored_key != pipeline_cache_key)
        {
            pipeline_cache_status = "discarded (shaders changed)";
        }
        else
        {
            pipeline_library_blob.resize(static_cast<std::size_t>(payload_size));
            file.read(reinterpret_cast<char*>(pipeline_library_blob.data()), static_cast<std::streamsize>(payload_size));
            const auto result = file ? device1->CreatePipelineLibrary(pipeline_library_blob.data(), pipeline_library_blob.size(), IID_PPV_ARGS(&pipeline_library)) : E_FAIL;
            if (SUCCEEDED(result))
            {
                pipeline_cache_status = "loaded";
                pipeline_library_loaded = true;
            }
            else
            {
                pipeline_library.Reset();
                pipeline_library_blob.clear();
                pipeline_cache_status = "discarded (driver rejected library, " + hresult_text(result) + ")";
            }
        }
    }
    if (!pipeline_library && FAILED(device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&pipeline_library))))
    {
        pipeline_library.Reset();
        pipeline_cache_status = "unsupported (CreatePipelineLibrary failed)";
        return;
    }
    set_name(pipeline_library.Get(), L"EAWEngine pipeline library");
}

void D3d12Renderer::Impl::create_pipeline_state(const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description, ComPtr<ID3D12PipelineState>& pipeline)
{
    // Only a library read from disk can contain the pipeline; probing a fresh one just makes
    // the debug layer warn about every miss.
    if (pipeline_library_loaded && SUCCEEDED(pipeline_library->LoadGraphicsPipeline(name, &description, IID_PPV_ARGS(&pipeline))))
    {
        ++pipeline_cache_hits;
    }
    else
    {
        check_hresult(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline)), "CreateGraphicsPipelineState(" + to_utf8(name) + ")");
        if (pipeline_library)
        {
            ++pipeline_cache_misses;
            // A name that is already stored with a different description cannot be replaced
            // in place, so the whole library is rebuilt when saving.
            if (SUCCEEDED(pipeline_library->StorePipeline(name, pipeline.Get())))
            {
                pipeline_library_dirty = true;
            }
            else
            {
                pipeline_library_stale = true;
            }
        }
    }
    set_name(pipeline.Get(), std::wstring(L"EAWEngine ") + name + L" pipeline state");
    created_pipelines.emplace_back(name, pipeline);
}

void D3d12Renderer::Impl::save_pipeline_cache()
{
    if (!pipeline_library || (!pipeline_library_dirty && !pipeline_library_stale))
    {
        created_pipelines.clear();
        return;
    }
    if (const auto saved = write_pipeline_cache(); saved)
    {
        pipeline_cache_status += pipeline_library_stale ? ", rebuilt and saved" : ", saved";
    }
    else
    {
        pipeline_cache_status += ", not saved: " + saved.error().message;
    }
    created_pipelines.clear();
}

Status D3d12Renderer::Impl::write_pipeline_cache() const
{
    ComPtr<ID3D12PipelineLibrary> library = pipeline_library;
    if (pipeline_library_stale)
    {
        ComPtr<ID3D12Device1> device1;
        EAW_TRY(hresult_status(device.As(&device1), "query ID3D12Device1"));
        library.Reset();
        EAW_TRY(hresult_status(device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library)), "CreatePipelineLibrary(rebuild)"));
        for (const auto& [name, pipeline] : created_pipelines)
        {
            EAW_TRY(hresult_status(library->StorePipeline(name.c_str(), pipeline.Get()), "ID3D12PipelineLibrary::StorePipeline(rebuild)"));
        }
    }
    const auto payload_size = library->GetSerializedSize();
    std::vector<std::byte> payload(payload_size);
    EAW_TRY(hresult_status(library->Serialize(payload.data(), payload.size()), "ID3D12PipelineLibrary::Serialize"));
    std::error_code error;
    std::filesystem::create_directories(options.pipeline_cache_directory, error);
    if (error)
    {
        return make_error("could not create " + options.pipeline_cache_directory.string() + ": " + error.message());
    }
    auto temporary = pipeline_cache_file;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        const std::uint64_t size = payload.size();
        file.write(pipeline_cache_magic.data(), static_cast<std::streamsize>(pipeline_cache_magic.size()));
        file.write(reinterpret_cast<const char*>(&pipeline_cache_key), sizeof(pipeline_cache_key));
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (!file)
        {
            return make_error("could not write " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, pipeline_cache_file, error);
    if (error)
    {
        return make_error("could not replace " + pipeline_cache_file.string() + ": " + error.message());
    }
    return Status::success();
}

void D3d12Renderer::Impl::create_pipeline()
{
    check_hresult(device->CreateRootSignature(0, g_eaw_sandbox_vs, sizeof(g_eaw_sandbox_vs), IID_PPV_ARGS(&root_signature)), "CreateRootSignature(embedded sandbox root signature)");
    set_name(root_signature.Get(), L"EAWEngine sandbox root signature");

    constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 8> input_layout{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(VisualVertex, texture_coordinates), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(VisualVertex, bone_indices), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VisualVertex, bone_weights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, binormal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VisualVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root_signature.Get();
    description.VS = {g_eaw_sandbox_vs, sizeof(g_eaw_sandbox_vs)};
    description.PS = {g_eaw_sandbox_ps, sizeof(g_eaw_sandbox_ps)};
    description.BlendState.AlphaToCoverageEnable = FALSE;
    description.BlendState.IndependentBlendEnable = TRUE;
    auto& blend = description.BlendState.RenderTarget[0];
    blend.BlendEnable = FALSE;
    blend.LogicOpEnable = FALSE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    description.SampleMask = UINT_MAX;
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.FrontCounterClockwise = FALSE;
    description.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    description.DepthStencilState.StencilEnable = FALSE;
    description.InputLayout = {input_layout.data(), static_cast<UINT>(input_layout.size())};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    // Target 1 is the pick target: written (never blended) by opaque, alpha-tested, and terrain
    // surfaces; masked for blended effects and backgrounds so they cannot be selected.
    description.BlendState.RenderTarget[1] = blend;
    description.BlendState.RenderTarget[1].BlendEnable = FALSE;
    description.NumRenderTargets = 2;
    description.RTVFormats[0] = back_buffer_format;
    description.RTVFormats[1] = pick_format;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    create_pipeline_state(L"opaque", description, opaque_pipeline);

    description.BlendState.RenderTarget[0].BlendEnable = TRUE;
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
    create_pipeline_state(L"alpha", description, alpha_pipeline);

    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    create_pipeline_state(L"additive", description, additive_pipeline);

    // Background variants: painted first without depth testing or writes.
    description.DepthStencilState.DepthEnable = FALSE;
    create_pipeline_state(L"background additive", description, background_additive_pipeline);
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    create_pipeline_state(L"background alpha", description, background_alpha_pipeline);
    description.BlendState.RenderTarget[0].BlendEnable = FALSE;
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    create_pipeline_state(L"background opaque", description, background_opaque_pipeline);

    // Terrain: opaque, depth-written, with its own vertex layout and shaders.
    constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 4> terrain_layout{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualTerrainVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualTerrainVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32_UINT, 0, offsetof(VisualTerrainVertex, layers), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualTerrainVertex, barycentric), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};
    description.VS = {g_eaw_terrain_vs, sizeof(g_eaw_terrain_vs)};
    description.PS = {g_eaw_terrain_ps, sizeof(g_eaw_terrain_ps)};
    description.InputLayout = {terrain_layout.data(), static_cast<UINT>(terrain_layout.size())};
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    create_pipeline_state(L"terrain", description, terrain_pipeline);

    // Debug lines: alpha-blended over the scene, depth-tested without depth or pick writes.
    constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 2> debug_layout{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DebugVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DebugVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};
    description.VS = {g_eaw_debug_vs, sizeof(g_eaw_debug_vs)};
    description.PS = {g_eaw_debug_ps, sizeof(g_eaw_debug_ps)};
    description.InputLayout = {debug_layout.data(), static_cast<UINT>(debug_layout.size())};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    description.BlendState.RenderTarget[0].BlendEnable = TRUE;
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    description.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
    create_pipeline_state(L"debug lines", description, debug_pipeline);

    // Particles: no vertex input, no culling, depth-tested (or not) without depth or pick writes.
    description.VS = {g_eaw_particle_vs, sizeof(g_eaw_particle_vs)};
    description.InputLayout = {nullptr, 0};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    for (std::size_t blend_index = 0; blend_index < particle_pipelines.size(); ++blend_index)
    {
        auto& target = description.BlendState.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        description.PS = {g_eaw_particle_ps, sizeof(g_eaw_particle_ps)};
        const wchar_t* name = L"particles additive";
        switch (static_cast<VisualParticleBlend>(blend_index))
        {
        case VisualParticleBlend::additive:
            target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            target.DestBlend = D3D12_BLEND_ONE;
            target.SrcBlendAlpha = D3D12_BLEND_ZERO;
            target.DestBlendAlpha = D3D12_BLEND_ONE;
            break;
        case VisualParticleBlend::alpha:
            target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            target.SrcBlendAlpha = D3D12_BLEND_ONE;
            target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            name = L"particles alpha";
            break;
        case VisualParticleBlend::modulate:
            target.SrcBlend = D3D12_BLEND_ZERO;
            target.DestBlend = D3D12_BLEND_SRC_COLOR;
            target.SrcBlendAlpha = D3D12_BLEND_ZERO;
            target.DestBlendAlpha = D3D12_BLEND_ONE;
            description.PS = {g_eaw_particle_modulate_ps, sizeof(g_eaw_particle_modulate_ps)};
            name = L"particles modulate";
            break;
        }
        description.DepthStencilState.DepthEnable = TRUE;
        create_pipeline_state(name, description, particle_pipelines[blend_index][0]);
        description.DepthStencilState.DepthEnable = FALSE;
        create_pipeline_state((std::wstring(name) + L" (no depth test)").c_str(), description, particle_pipelines[blend_index][1]);
    }

    // Post-processing: fullscreen triangles into one target, no depth, no blending.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC post{};
        post.pRootSignature = root_signature.Get();
        post.VS = {g_eaw_post_vs, sizeof(g_eaw_post_vs)};
        post.BlendState.RenderTarget[0].BlendEnable = FALSE;
        post.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        post.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        post.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        post.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        post.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        post.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        post.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        post.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        post.SampleMask = UINT_MAX;
        post.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        post.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        post.RasterizerState.DepthClipEnable = TRUE;
        post.DepthStencilState.DepthEnable = FALSE;
        post.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        post.NumRenderTargets = 1;
        post.SampleDesc.Count = 1;
        post.RTVFormats[0] = bloom_format;
        post.PS = {g_eaw_post_extract_ps, sizeof(g_eaw_post_extract_ps)};
        create_pipeline_state(L"bloom extract", post, bloom_extract_pipeline);
        post.PS = {g_eaw_post_blur_ps, sizeof(g_eaw_post_blur_ps)};
        create_pipeline_state(L"bloom blur", post, bloom_blur_pipeline);
        post.RTVFormats[0] = back_buffer_format;
        post.PS = {g_eaw_post_composite_ps, sizeof(g_eaw_post_composite_ps)};
        create_pipeline_state(L"bloom composite", post, bloom_composite_pipeline);
    }

    D3D12_DESCRIPTOR_HEAP_DESC shader_heap_description{};
    shader_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    shader_heap_description.NumDescriptors = maximum_scene_textures;
    shader_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check_hresult(device->CreateDescriptorHeap(&shader_heap_description, IID_PPV_ARGS(&shader_heap)), "CreateDescriptorHeap(shader visible)");
    set_name(shader_heap.Get(), L"EAWEngine sandbox shader descriptor heap");
    // Every slot starts as a valid null view so unused table entries are well defined.
    D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};
    null_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_view.Texture2D.MipLevels = 1;
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t slot = 0; slot < maximum_scene_textures; ++slot)
    {
        device->CreateShaderResourceView(nullptr, &null_view, handle);
        handle.ptr += increment;
    }
}

} // namespace eaw
