// Pipeline states and the optional pipeline library cache.

#include "d3d12_renderer_impl.hpp"

#include "core/file_io.hpp"

#include <array>
#include <cstring>

// Offline-compiled shader model 6.0 bytecode (see src/render/shaders/sandbox.hlsl). Both entry
// points embed the root signature, which the renderer creates from the vertex shader blob.
#include "g_eaw_debug_ps.h"
#include "g_eaw_debug_vs.h"
#include "g_eaw_particle_heat_enhanced_ps.h"
#include "g_eaw_particle_heat_ps.h"
#include "g_eaw_particle_modulate_ps.h"
#include "g_eaw_particle_ps.h"
#include "g_eaw_particle_vs.h"
#include "g_eaw_post_blur_ps.h"
#include "g_eaw_post_composite_ps.h"
#include "g_eaw_post_edge_aa_ps.h"
#include "g_eaw_post_extract_ps.h"
#include "g_eaw_post_heat_enhanced_ps.h"
#include "g_eaw_post_heat_ps.h"
#include "g_eaw_post_taa_ps.h"
#include "g_eaw_post_vs.h"
#include "g_eaw_sandbox_ps.h"
#include "g_eaw_sandbox_vs.h"
#include "g_eaw_shadow_ps.h"
#include "g_eaw_shadow_vs.h"
#include "g_eaw_silhouette_composite_ps.h"
#include "g_eaw_silhouette_ps.h"
#include "g_eaw_sky_light_cs.h"
#include "g_eaw_terrain_ps.h"
#include "g_eaw_terrain_shadow_ps.h"
#include "g_eaw_terrain_shadow_vs.h"
#include "g_eaw_terrain_vs.h"
#include "g_eaw_water_clear_cs.h"
#include "g_eaw_water_project_cs.h"
#include "g_eaw_water_ps.h"

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
    key = fnv1a(key, std::as_bytes(std::span(g_eaw_water_ps)));
    key = fnv1a(key, std::as_bytes(std::span(&pipeline_layout_version, 1)));
    pipeline_cache_key = key;
    pipeline_cache_file = options.pipeline_cache_directory / "d3d12_pipelines.bin";

    pipeline_cache_status = "created";
    // A missing, oversized, or unreadable cache is simply not used.
    constexpr std::size_t header_bytes = pipeline_cache_magic.size() + 2 * sizeof(std::uint64_t);
    const auto cached = read_file(pipeline_cache_file, maximum_pipeline_cache_bytes);
    if (cached && cached->size() > header_bytes)
    {
        const auto& bytes = *cached;
        std::array<char, 8> magic{};
        std::uint64_t stored_key{};
        std::uint64_t payload_size{};
        std::memcpy(magic.data(), bytes.data(), magic.size());
        std::memcpy(&stored_key, bytes.data() + magic.size(), sizeof(stored_key));
        std::memcpy(&payload_size, bytes.data() + magic.size() + sizeof(stored_key), sizeof(payload_size));
        if (magic != pipeline_cache_magic || payload_size != bytes.size() - header_bytes)
        {
            pipeline_cache_status = "discarded (unrecognized or truncated file)";
        }
        else if (stored_key != pipeline_cache_key)
        {
            pipeline_cache_status = "discarded (shaders changed)";
        }
        else
        {
            pipeline_library_blob.assign(bytes.begin() + static_cast<std::ptrdiff_t>(header_bytes), bytes.end());
            const auto result = device1->CreatePipelineLibrary(pipeline_library_blob.data(), pipeline_library_blob.size(), IID_PPV_ARGS(&pipeline_library));
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
        check_device_hresult(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline)), device, "CreateGraphicsPipelineState(" + to_utf8(name) + ")");
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
    const std::uint64_t size = payload.size();
    std::vector<std::byte> file(pipeline_cache_magic.size() + 2 * sizeof(std::uint64_t) + payload.size());
    std::memcpy(file.data(), pipeline_cache_magic.data(), pipeline_cache_magic.size());
    std::memcpy(file.data() + pipeline_cache_magic.size(), &pipeline_cache_key, sizeof(pipeline_cache_key));
    std::memcpy(file.data() + pipeline_cache_magic.size() + sizeof(pipeline_cache_key), &size, sizeof(size));
    std::memcpy(file.data() + pipeline_cache_magic.size() + 2 * sizeof(std::uint64_t), payload.data(), payload.size());
    EAW_TRY(write_file(pipeline_cache_file, file, WriteMode::replace));
    return Status::success();
}

void D3d12Renderer::Impl::create_pipeline()
{
    check_device_hresult(device->CreateRootSignature(0, g_eaw_sandbox_vs, sizeof(g_eaw_sandbox_vs), IID_PPV_ARGS(&root_signature)), device, "CreateRootSignature(embedded sandbox root signature)");
    set_name(root_signature.Get(), L"EAWEngine sandbox root signature");

    constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 9> input_layout{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(VisualVertex, texture_coordinates), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(VisualVertex, bone_indices), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VisualVertex, bone_weights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, binormal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VisualVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(VisualVertex, texture_coordinates_1), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
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
    // Target 2 is the motion target: its motion (red, green) written by opaque and alpha-tested
    // models only; its reactive mask (blue) by particles.
    description.BlendState.RenderTarget[2] = description.BlendState.RenderTarget[1];
    description.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN;
    description.NumRenderTargets = 3;
    description.RTVFormats[0] = back_buffer_format;
    description.RTVFormats[1] = pick_format;
    description.RTVFormats[2] = motion_format;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    create_pipeline_state(L"opaque", description, opaque_pipeline);
    // Occlusion silhouettes: the pixel shader keeps only parts the scene's depth shows something
    // clearly nearer than (a hardware depth-greater test let every visible unit through: redrawn
    // geometry does not reproduce its stored depth exactly), no depth writes, and a max blend so
    // overlapping parts of a unit never stack. Back faces are dropped in the shader (models are
    // drawn two-sided).
    {
        auto silhouette = description;
        silhouette.PS = {g_eaw_silhouette_ps, sizeof(g_eaw_silhouette_ps)};
        silhouette.NumRenderTargets = 1;
        silhouette.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
        silhouette.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
        silhouette.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        silhouette.DepthStencilState.DepthEnable = FALSE;
        auto& target = silhouette.BlendState.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_ONE;
        target.BlendOp = D3D12_BLEND_OP_MAX;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_ONE;
        target.BlendOpAlpha = D3D12_BLEND_OP_MAX;
        create_pipeline_state(L"occlusion silhouette", silhouette, silhouette_pipeline);
    }

    // Shadow casters: depth only, biased by slope; alpha-tested ones cut their holes.
    const auto shadow_variant = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC variant)
    {
        variant.NumRenderTargets = 0;
        for (auto& format : variant.RTVFormats)
        {
            format = DXGI_FORMAT_UNKNOWN;
        }
        variant.PS = {};
        variant.RasterizerState.SlopeScaledDepthBias = shadow_slope_bias;
        return variant;
    };
    auto shadow = shadow_variant(description);
    shadow.VS = {g_eaw_shadow_vs, sizeof(g_eaw_shadow_vs)};
    create_pipeline_state(L"shadow", shadow, shadow_pipeline);
    shadow.PS = {g_eaw_shadow_ps, sizeof(g_eaw_shadow_ps)};
    create_pipeline_state(L"shadow alpha test", shadow, shadow_alpha_test_pipeline);

    description.BlendState.RenderTarget[0].BlendEnable = TRUE;
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
    description.BlendState.RenderTarget[2].RenderTargetWriteMask = 0;
    create_pipeline_state(L"alpha", description, alpha_pipeline);
    auto water = description;
    water.PS = {g_eaw_water_ps, sizeof(g_eaw_water_ps)};
    create_pipeline_state(L"water", water, water_pipeline);
    check_device_hresult(device->CreateRootSignature(0, g_eaw_water_clear_cs, sizeof(g_eaw_water_clear_cs), IID_PPV_ARGS(&water_reflection_root_signature)), device,
                         "CreateRootSignature(water reflections)");
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
    compute.pRootSignature = water_reflection_root_signature.Get();
    compute.CS = {g_eaw_water_clear_cs, sizeof(g_eaw_water_clear_cs)};
    check_device_hresult(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&water_reflection_clear_pipeline)), device, "CreateComputePipelineState(water reflection clear)");
    compute.CS = {g_eaw_sky_light_cs, sizeof(g_eaw_sky_light_cs)};
    check_device_hresult(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&sky_light_pipeline)), device, "CreateComputePipelineState(sky light)");
    compute.CS = {g_eaw_water_project_cs, sizeof(g_eaw_water_project_cs)};
    check_device_hresult(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&water_reflection_project_pipeline)), device, "CreateComputePipelineState(water reflection projection)");

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
    // The sky capture: the background pipelines into one target without depth, both faces drawn
    // (the capture's face bases need not match the camera's handedness).
    {
        auto sky = description;
        sky.NumRenderTargets = 1;
        sky.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
        sky.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
        sky.DSVFormat = DXGI_FORMAT_UNKNOWN;
        sky.DepthStencilState.DepthEnable = FALSE;
        sky.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        create_pipeline_state(L"sky capture opaque", sky, sky_capture_opaque_pipeline);
        sky.BlendState.RenderTarget[0].BlendEnable = TRUE;
        sky.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        sky.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        sky.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        sky.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        create_pipeline_state(L"sky capture alpha", sky, sky_capture_alpha_pipeline);
        sky.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        sky.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        create_pipeline_state(L"sky capture additive", sky, sky_capture_additive_pipeline);
    }

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
    auto terrain_shadow = shadow_variant(description);
    terrain_shadow.VS = {g_eaw_terrain_shadow_vs, sizeof(g_eaw_terrain_shadow_vs)};
    terrain_shadow.PS = {g_eaw_terrain_shadow_ps, sizeof(g_eaw_terrain_shadow_ps)};
    create_pipeline_state(L"terrain shadow", terrain_shadow, terrain_shadow_pipeline);

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
    // Drawn after the scene pass, over the frame and the pick target only.
    description.NumRenderTargets = 2;
    description.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
    create_pipeline_state(L"debug lines", description, debug_pipeline);
    description.NumRenderTargets = 3;
    description.RTVFormats[2] = motion_format;

    // Particles: no vertex input, no culling, depth-tested (or not) without depth or pick writes;
    // each raises the reactive mask (the motion target's blue) to its own strength.
    description.VS = {g_eaw_particle_vs, sizeof(g_eaw_particle_vs)};
    {
        auto& reactive = description.BlendState.RenderTarget[2];
        reactive.BlendEnable = TRUE;
        reactive.SrcBlend = D3D12_BLEND_ONE;
        reactive.DestBlend = D3D12_BLEND_ONE;
        reactive.BlendOp = D3D12_BLEND_OP_MAX;
        reactive.SrcBlendAlpha = D3D12_BLEND_ONE;
        reactive.DestBlendAlpha = D3D12_BLEND_ONE;
        reactive.BlendOpAlpha = D3D12_BLEND_OP_MAX;
        reactive.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_BLUE;
    }
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
            // Colour x texture added as is: additive particles ignore alpha (the GlyphX particle
            // editor's blend setup, and its fork's guide).
            target.SrcBlend = D3D12_BLEND_ONE;
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
        case VisualParticleBlend::additive_alpha:
            target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            target.DestBlend = D3D12_BLEND_ONE;
            target.SrcBlendAlpha = D3D12_BLEND_ZERO;
            target.DestBlendAlpha = D3D12_BLEND_ONE;
            name = L"particles additive with alpha";
            break;
        }
        description.DepthStencilState.DepthEnable = TRUE;
        create_pipeline_state(name, description, particle_pipelines[blend_index][0]);
        description.DepthStencilState.DepthEnable = FALSE;
        create_pipeline_state((std::wstring(name) + L" (no depth test)").c_str(), description, particle_pipelines[blend_index][1]);
    }
    // Heat particles: their bump texture blended by alpha into the heat buffer, depth tested against
    // the scene (read-only), as the legacy heat shaders blend into their distortion texture.
    {
        auto heat = description;
        heat.PS = {g_eaw_particle_heat_ps, sizeof(g_eaw_particle_heat_ps)};
        heat.NumRenderTargets = 1;
        heat.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
        heat.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
        heat.DepthStencilState.DepthEnable = TRUE;
        heat.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        auto& target = heat.BlendState.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        create_pipeline_state(L"heat particles", heat, heat_particle_pipeline);
        // Enhanced: signed shifts added up in 16-bit float.
        heat.PS = {g_eaw_particle_heat_enhanced_ps, sizeof(g_eaw_particle_heat_enhanced_ps)};
        heat.RTVFormats[0] = heat_signed_format;
        target.SrcBlend = D3D12_BLEND_ONE;
        target.DestBlend = D3D12_BLEND_ONE;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_ONE;
        create_pipeline_state(L"heat particles (enhanced)", heat, heat_particle_enhanced_pipeline);
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
        post.PS = {g_eaw_post_edge_aa_ps, sizeof(g_eaw_post_edge_aa_ps)};
        create_pipeline_state(L"edge anti-aliasing", post, edge_aa_pipeline);
        // The silhouette mask blended over the scene.
        {
            auto composite = post;
            composite.PS = {g_eaw_silhouette_composite_ps, sizeof(g_eaw_silhouette_composite_ps)};
            composite.BlendState.RenderTarget[0].BlendEnable = TRUE;
            composite.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            composite.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            composite.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            create_pipeline_state(L"silhouette composite", composite, silhouette_composite_pipeline);
        }
        post.PS = {g_eaw_post_heat_ps, sizeof(g_eaw_post_heat_ps)};
        create_pipeline_state(L"heat distortion", post, heat_apply_pipeline);
        post.PS = {g_eaw_post_heat_enhanced_ps, sizeof(g_eaw_post_heat_enhanced_ps)};
        create_pipeline_state(L"heat distortion (enhanced)", post, heat_apply_enhanced_pipeline);
        // The resolve writes the frame and the next history at once.
        post.NumRenderTargets = 2;
        post.RTVFormats[1] = back_buffer_format;
        post.PS = {g_eaw_post_taa_ps, sizeof(g_eaw_post_taa_ps)};
        create_pipeline_state(L"temporal anti-aliasing", post, taa_pipeline);
    }

    D3D12_DESCRIPTOR_HEAP_DESC shader_heap_description{};
    shader_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    shader_heap_description.NumDescriptors = maximum_scene_textures;
    shader_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check_device_hresult(device->CreateDescriptorHeap(&shader_heap_description, IID_PPV_ARGS(&shader_heap)), device, "CreateDescriptorHeap(shader visible)");
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
