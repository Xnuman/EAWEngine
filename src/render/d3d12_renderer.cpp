#include "eaw/d3d12_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

namespace eaw {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t frame_count = 3;
constexpr DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

std::string hresult_text(const HRESULT result)
{
    std::ostringstream stream;
    stream << "HRESULT 0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(result);
    return stream.str();
}

void check_hresult(const HRESULT result, const std::string_view operation)
{
    if (FAILED(result))
    {
        throw std::runtime_error(std::string(operation) + " failed (" + hresult_text(result) + ")");
    }
}

void set_name(ID3D12Object* object, const std::wstring_view name)
{
    if (object != nullptr)
    {
        object->SetName(std::wstring(name).c_str());
    }
}

std::string to_utf8(const std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

class UniqueHandle final
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(const HANDLE value) : value_(value)
    {}
    ~UniqueHandle()
    {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : value_(std::exchange(other.value_, nullptr))
    {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    void reset(const HANDLE value = nullptr)
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value_);
        }
        value_ = value;
    }

    [[nodiscard]] HANDLE get() const
    {
        return value_;
    }

private:
    HANDLE value_{};
};

class CommandQueue final
{
public:
    void initialize(ID3D12Device* device, const D3D12_COMMAND_LIST_TYPE type, const wchar_t* name)
    {
        D3D12_COMMAND_QUEUE_DESC description{};
        description.Type = type;
        description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        check_hresult(device->CreateCommandQueue(&description, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
        check_hresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");

        set_name(queue_.Get(), name);
        const std::wstring fence_name = std::wstring(name) + L" fence";
        set_name(fence_.Get(), fence_name);

        event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (event_.get() == nullptr)
        {
            throw std::runtime_error("CreateEventW failed for a D3D12 fence");
        }
    }

    void execute(ID3D12CommandList* const* lists, const std::uint32_t count) const
    {
        queue_->ExecuteCommandLists(count, lists);
    }

    std::uint64_t signal()
    {
        const auto value = next_fence_value_++;
        check_hresult(queue_->Signal(fence_.Get(), value), "ID3D12CommandQueue::Signal");
        return value;
    }

    void wait_gpu(const CommandQueue& producer, const std::uint64_t value) const
    {
        check_hresult(queue_->Wait(producer.fence_.Get(), value), "ID3D12CommandQueue::Wait");
    }

    void wait_cpu(const std::uint64_t value) const
    {
        if (completed_value() >= value)
        {
            return;
        }
        check_hresult(fence_->SetEventOnCompletion(value, event_.get()), "ID3D12Fence::SetEventOnCompletion");
        const auto wait_result = WaitForSingleObjectEx(event_.get(), INFINITE, FALSE);
        if (wait_result != WAIT_OBJECT_0)
        {
            throw std::runtime_error("waiting for a D3D12 fence failed");
        }
    }

    void flush()
    {
        wait_cpu(signal());
    }

    [[nodiscard]] std::uint64_t completed_value() const
    {
        return fence_->GetCompletedValue();
    }

    [[nodiscard]] ID3D12CommandQueue* get() const
    {
        return queue_.Get();
    }

    [[nodiscard]] bool initialized() const
    {
        return queue_ && fence_;
    }

private:
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    UniqueHandle event_;
    std::uint64_t next_fence_value_{1};
};

D3D12_HEAP_PROPERTIES heap_properties(const D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC buffer_description(const std::uint64_t size)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource, const D3D12_RESOURCE_STATES before, const D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

ComPtr<ID3DBlob> compile_shader(const std::string_view source, const char* entry_point, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const auto result = D3DCompile(source.data(), source.size(), "visual_sandbox.hlsl", nullptr, nullptr, entry_point, target, flags, 0, &bytecode, &errors);
    if (FAILED(result))
    {
        std::string message = "D3DCompile failed";
        if (errors)
        {
            message += ": ";
            message.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        }
        throw std::runtime_error(message);
    }
    return bytecode;
}

bool query_tearing_support(IDXGIFactory7* factory)
{
    BOOL supported = FALSE;
    const auto result = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported));
    return SUCCEEDED(result) && supported == TRUE;
}

ComPtr<IDXGIAdapter4> select_adapter(IDXGIFactory7* factory, const bool use_warp)
{
    ComPtr<IDXGIAdapter4> adapter;
    if (use_warp)
    {
        check_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "IDXGIFactory::EnumWarpAdapter");
        return adapter;
    }

    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter4> candidate;
        const auto result = factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        check_hresult(result, "IDXGIFactory6::EnumAdapterByGpuPreference");

        DXGI_ADAPTER_DESC3 description{};
        check_hresult(candidate->GetDesc3(&description), "IDXGIAdapter4::GetDesc3");
        if ((description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            return candidate;
        }
    }
    throw std::runtime_error("no Direct3D 12 feature-level 12_0 adapter was found (use --warp to test with WARP)");
}

void configure_debugging(const bool gpu_validation)
{
#if !defined(_DEBUG)
    if (!gpu_validation)
    {
        return;
    }
#endif

    ComPtr<ID3D12Debug> debug;
    check_hresult(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12GetDebugInterface");
    debug->EnableDebugLayer();
    if (gpu_validation)
    {
        ComPtr<ID3D12Debug1> validation;
        check_hresult(debug.As(&validation), "query ID3D12Debug1");
        validation->SetEnableGPUBasedValidation(TRUE);
        validation->SetEnableSynchronizedCommandQueueValidation(TRUE);
    }

    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings))))
    {
        dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
}

#if defined(_DEBUG)
void CALLBACK debug_message_callback(const D3D12_MESSAGE_CATEGORY, const D3D12_MESSAGE_SEVERITY severity, const D3D12_MESSAGE_ID id, const LPCSTR description, void*)
{
    std::fprintf(stderr, "D3D12 validation [%u, severity %u]: %s\n", static_cast<unsigned>(id), static_cast<unsigned>(severity), description != nullptr ? description : "(no description)");
}
#endif

} // namespace

struct D3d12Renderer::Impl
{
    struct FrameContext
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> back_buffer;
        std::uint64_t fence_value{};
    };

    struct Vertex
    {
        float position[3];
        float uv[2];
    };

    HWND window{};
    D3d12RendererOptions options;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t swap_chain_flags{};
    bool tearing_supported{};
    bool occluded{};
    bool initial_transitions_pending{true};
    std::uint64_t frame_number{};
    std::uint64_t upload_fence_value{};
    std::string adapter_name;

    ComPtr<IDXGIFactory7> factory;
    ComPtr<IDXGIAdapter4> adapter;
    ComPtr<ID3D12Device> device;
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue1> debug_info_queue;
    DWORD debug_callback_cookie{};
    bool debug_callback_registered{};
#endif
    CommandQueue direct_queue;
    CommandQueue copy_queue;
    ComPtr<IDXGISwapChain3> swap_chain;
    UniqueHandle frame_latency_event;

    std::array<FrameContext, frame_count> frames;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    std::uint32_t rtv_increment{};

    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> pipeline_state;
    ComPtr<ID3D12DescriptorHeap> shader_heap;
    ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    ComPtr<ID3D12Resource> texture;

    ComPtr<ID3D12CommandAllocator> upload_allocator;
    ComPtr<ID3D12GraphicsCommandList> upload_list;
    std::vector<ComPtr<ID3D12Resource>> upload_resources;

    Impl(void* native_window, const std::uint32_t initial_width, const std::uint32_t initial_height, const D3d12RendererOptions& renderer_options)
        : window(static_cast<HWND>(native_window)), options(renderer_options), width(std::max(initial_width, 1U)), height(std::max(initial_height, 1U))
    {
        if (window == nullptr)
        {
            throw std::invalid_argument("D3d12Renderer requires a valid HWND");
        }
        options.maximum_frame_latency = std::clamp(options.maximum_frame_latency, 1U, frame_count - 1);
        try
        {
            initialize();
        }
        catch (...)
        {
            try
            {
                wait_idle();
            }
            catch (...)
            {}
            throw;
        }
    }

    ~Impl()
    {
        try
        {
            wait_idle();
        }
        catch (...)
        {
            // Destructors must not mask an active rendering or device-removal error.
        }
#if defined(_DEBUG)
        if (debug_info_queue && debug_callback_registered)
        {
            debug_info_queue->UnregisterMessageCallback(debug_callback_cookie);
        }
#endif
    }

    void initialize()
    {
        configure_debugging(options.gpu_validation);

#if defined(_DEBUG)
        constexpr UINT factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#else
        const UINT factory_flags = options.gpu_validation ? DXGI_CREATE_FACTORY_DEBUG : 0;
#endif
        check_hresult(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        adapter = select_adapter(factory.Get(), options.use_warp);
        check_hresult(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
        set_name(device.Get(), L"EAWEngine D3D12 device");

        DXGI_ADAPTER_DESC3 adapter_description{};
        check_hresult(adapter->GetDesc3(&adapter_description), "IDXGIAdapter4::GetDesc3");
        adapter_name = to_utf8(adapter_description.Description);

#if defined(_DEBUG)
        if (SUCCEEDED(device.As(&debug_info_queue)))
        {
            debug_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            check_hresult(debug_info_queue->RegisterMessageCallback(debug_message_callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &debug_callback_cookie),
                          "ID3D12InfoQueue1::RegisterMessageCallback");
            debug_callback_registered = true;
        }
#endif

        direct_queue.initialize(device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"EAWEngine direct queue");
        copy_queue.initialize(device.Get(), D3D12_COMMAND_LIST_TYPE_COPY, L"EAWEngine copy queue");
        create_swap_chain();
        create_frame_resources();
        create_pipeline();
        create_initial_assets();
    }

    void create_swap_chain()
    {
        tearing_supported = query_tearing_support(factory.Get());
        swap_chain_flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (tearing_supported)
        {
            swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = back_buffer_format;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = frame_count;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        description.Flags = swap_chain_flags;

        ComPtr<IDXGISwapChain1> created;
        check_hresult(factory->CreateSwapChainForHwnd(direct_queue.get(), window, &description, nullptr, nullptr, &created), "IDXGIFactory2::CreateSwapChainForHwnd");
        check_hresult(created.As(&swap_chain), "query IDXGISwapChain3");
        check_hresult(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory::MakeWindowAssociation");
        check_hresult(swap_chain->SetMaximumFrameLatency(options.maximum_frame_latency), "IDXGISwapChain2::SetMaximumFrameLatency");

        frame_latency_event.reset(swap_chain->GetFrameLatencyWaitableObject());
        if (frame_latency_event.get() == nullptr)
        {
            throw std::runtime_error("DXGI did not create the frame-latency waitable object");
        }
    }

    void create_frame_resources()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
        heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap_description.NumDescriptors = frame_count;
        check_hresult(device->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&rtv_heap)), "CreateDescriptorHeap(RTV)");
        set_name(rtv_heap.Get(), L"EAWEngine back-buffer RTV heap");
        rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (std::uint32_t index = 0; index < frame_count; ++index)
        {
            check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[index].allocator)), "CreateCommandAllocator(direct)");
            set_name(frames[index].allocator.Get(), L"EAWEngine frame allocator " + std::to_wstring(index));
        }
        check_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&command_list)), "CreateCommandList(direct)");
        set_name(command_list.Get(), L"EAWEngine primary graphics command list");
        check_hresult(command_list->Close(), "close initial direct command list");
        acquire_back_buffers();
    }

    void acquire_back_buffers()
    {
        auto handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t index = 0; index < frame_count; ++index)
        {
            check_hresult(swap_chain->GetBuffer(index, IID_PPV_ARGS(&frames[index].back_buffer)), "IDXGISwapChain::GetBuffer");
            set_name(frames[index].back_buffer.Get(), L"EAWEngine back buffer " + std::to_wstring(index));
            device->CreateRenderTargetView(frames[index].back_buffer.Get(), nullptr, handle);
            handle.ptr += rtv_increment;
        }
    }

    void create_pipeline()
    {
        constexpr std::string_view shader_source = R"(
Texture2D SandboxTexture : register(t0);
SamplerState SandboxSampler : register(s0);

struct VsInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct PsInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PsInput vs_main(VsInput input)
{
    PsInput output;
    output.position = float4(input.position, 1.0f);
    output.uv = input.uv;
    return output;
}

float4 ps_main(PsInput input) : SV_TARGET
{
    return SandboxTexture.Sample(SandboxSampler, input.uv);
}
)";
        const auto vertex_shader = compile_shader(shader_source, "vs_main", "vs_5_1");
        const auto pixel_shader = compile_shader(shader_source, "ps_main", "ps_5_1");

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.RegisterSpace = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges = &range;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0.0F;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0F;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_description{};
        root_description.NumParameters = 1;
        root_description.pParameters = &parameter;
        root_description.NumStaticSamplers = 1;
        root_description.pStaticSamplers = &sampler;
        root_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        const auto serialize_result = D3D12SerializeRootSignature(&root_description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if (FAILED(serialize_result))
        {
            std::string message = "D3D12SerializeRootSignature failed";
            if (errors)
            {
                message += ": ";
                message.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            }
            throw std::runtime_error(message);
        }
        check_hresult(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root_signature)), "CreateRootSignature");
        set_name(root_signature.Get(), L"EAWEngine sandbox root signature");

        constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 2> input_layout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        }};

        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = root_signature.Get();
        description.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
        description.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
        description.BlendState.AlphaToCoverageEnable = FALSE;
        description.BlendState.IndependentBlendEnable = FALSE;
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
        description.DepthStencilState.DepthEnable = FALSE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        description.DepthStencilState.StencilEnable = FALSE;
        description.InputLayout = {input_layout.data(), static_cast<UINT>(input_layout.size())};
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = back_buffer_format;
        description.SampleDesc.Count = 1;
        check_hresult(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline_state)), "CreateGraphicsPipelineState");
        set_name(pipeline_state.Get(), L"EAWEngine sandbox pipeline state");

        D3D12_DESCRIPTOR_HEAP_DESC shader_heap_description{};
        shader_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        shader_heap_description.NumDescriptors = 1;
        shader_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check_hresult(device->CreateDescriptorHeap(&shader_heap_description, IID_PPV_ARGS(&shader_heap)), "CreateDescriptorHeap(shader visible)");
        set_name(shader_heap.Get(), L"EAWEngine sandbox shader descriptor heap");
    }

    void create_initial_assets()
    {
        check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&upload_allocator)), "CreateCommandAllocator(copy)");
        check_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, upload_allocator.Get(), nullptr, IID_PPV_ARGS(&upload_list)), "CreateCommandList(copy)");
        set_name(upload_allocator.Get(), L"EAWEngine initial upload allocator");
        set_name(upload_list.Get(), L"EAWEngine initial upload command list");

        create_vertex_buffer();
        create_texture();

        check_hresult(upload_list->Close(), "close initial upload command list");
        ID3D12CommandList* lists[]{upload_list.Get()};
        copy_queue.execute(lists, 1);
        upload_fence_value = copy_queue.signal();

        // The dependency stays entirely on the GPU. The CPU is free to continue
        // preparing the first frame while the copy engine performs the uploads.
        direct_queue.wait_gpu(copy_queue, upload_fence_value);
    }

    void create_vertex_buffer()
    {
        constexpr std::array<Vertex, 3> vertices{{
            {{-0.78F, -0.68F, 0.0F}, {0.0F, 1.0F}},
            {{0.0F, 0.78F, 0.0F}, {0.5F, 0.0F}},
            {{0.78F, -0.68F, 0.0F}, {1.0F, 1.0F}},
        }};
        constexpr auto buffer_size = static_cast<std::uint64_t>(sizeof(vertices));
        const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto description = buffer_description(buffer_size);

        check_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&vertex_buffer)),
                      "CreateCommittedResource(vertex buffer)");
        ComPtr<ID3D12Resource> upload;
        check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
                      "CreateCommittedResource(vertex upload)");
        set_name(vertex_buffer.Get(), L"EAWEngine sandbox vertex buffer");
        set_name(upload.Get(), L"EAWEngine sandbox vertex upload");

        void* mapped{};
        D3D12_RANGE no_read{0, 0};
        check_hresult(upload->Map(0, &no_read, &mapped), "Map(vertex upload)");
        std::memcpy(mapped, vertices.data(), sizeof(vertices));
        upload->Unmap(0, nullptr);
        upload_list->CopyBufferRegion(vertex_buffer.Get(), 0, upload.Get(), 0, buffer_size);
        upload_resources.push_back(std::move(upload));

        vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
        vertex_buffer_view.SizeInBytes = static_cast<UINT>(buffer_size);
        vertex_buffer_view.StrideInBytes = sizeof(Vertex);
    }

    void create_texture()
    {
        constexpr std::uint32_t texture_width = 8;
        constexpr std::uint32_t texture_height = 8;
        std::array<std::uint32_t, texture_width * texture_height> pixels{};
        for (std::uint32_t y = 0; y < texture_height; ++y)
        {
            for (std::uint32_t x = 0; x < texture_width; ++x)
            {
                const bool bright = ((x / 2) + (y / 2)) % 2 == 0;
                pixels[y * texture_width + x] = bright ? 0xFF50D5DBU : 0xFF17242EU;
            }
        }

        D3D12_RESOURCE_DESC texture_description{};
        texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_description.Width = texture_width;
        texture_description.Height = texture_height;
        texture_description.DepthOrArraySize = 1;
        texture_description.MipLevels = 1;
        texture_description.Format = back_buffer_format;
        texture_description.SampleDesc.Count = 1;
        texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        check_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture)),
                      "CreateCommittedResource(texture)");
        set_name(texture.Get(), L"EAWEngine sandbox texture");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT row_count{};
        UINT64 row_size{};
        UINT64 upload_size{};
        device->GetCopyableFootprints(&texture_description, 0, 1, 0, &footprint, &row_count, &row_size, &upload_size);

        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto upload_description = buffer_description(upload_size);
        ComPtr<ID3D12Resource> upload;
        check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
                      "CreateCommittedResource(texture upload)");
        set_name(upload.Get(), L"EAWEngine sandbox texture upload");

        std::byte* mapped{};
        D3D12_RANGE no_read{0, 0};
        check_hresult(upload->Map(0, &no_read, reinterpret_cast<void**>(&mapped)), "Map(texture upload)");
        const auto* source = reinterpret_cast<const std::byte*>(pixels.data());
        const auto source_row_pitch = texture_width * sizeof(std::uint32_t);
        for (UINT row = 0; row < row_count; ++row)
        {
            std::memcpy(mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch, source + static_cast<std::size_t>(row) * source_row_pitch,
                        static_cast<std::size_t>(row_size));
        }
        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = upload.Get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source_location.PlacedFootprint = footprint;
        upload_list->CopyTextureRegion(&destination, 0, 0, 0, &source_location, nullptr);
        upload_resources.push_back(std::move(upload));

        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = texture_description.Format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.Get(), &view, shader_heap->GetCPUDescriptorHandleForHeapStart());
    }

    bool render()
    {
        retire_completed_uploads();
        if (occluded)
        {
            const auto test_result = swap_chain->Present(0, DXGI_PRESENT_TEST);
            if (test_result == DXGI_STATUS_OCCLUDED)
            {
                return false;
            }
            check_device_result(test_result, "IDXGISwapChain::Present(test)");
            occluded = false;
        }
        if (!wait_for_frame_latency())
        {
            return false;
        }

        const auto frame_index = swap_chain->GetCurrentBackBufferIndex();
        auto& frame = frames[frame_index];
        if (frame.fence_value != 0 && direct_queue.completed_value() < frame.fence_value)
        {
            direct_queue.wait_cpu(frame.fence_value);
        }

        check_hresult(frame.allocator->Reset(), "ID3D12CommandAllocator::Reset");
        check_hresult(command_list->Reset(frame.allocator.Get(), pipeline_state.Get()), "ID3D12GraphicsCommandList::Reset");

        if (initial_transitions_pending)
        {
            const std::array barriers{
                transition_barrier(vertex_buffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
                transition_barrier(texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            };
            command_list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            initial_transitions_pending = false;
        }

        auto to_render_target = transition_barrier(frame.back_buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        command_list->ResourceBarrier(1, &to_render_target);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0F;
        D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        command_list->RSSetViewports(1, &viewport);
        command_list->RSSetScissorRects(1, &scissor);

        auto render_target = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        render_target.ptr += static_cast<SIZE_T>(frame_index) * rtv_increment;
        command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
        constexpr std::array clear_color{0.018F, 0.031F, 0.043F, 1.0F};
        command_list->ClearRenderTargetView(render_target, clear_color.data(), 0, nullptr);

        ID3D12DescriptorHeap* descriptor_heaps[]{shader_heap.Get()};
        command_list->SetDescriptorHeaps(1, descriptor_heaps);
        command_list->SetGraphicsRootSignature(root_signature.Get());
        command_list->SetGraphicsRootDescriptorTable(0, shader_heap->GetGPUDescriptorHandleForHeapStart());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
        command_list->DrawInstanced(3, 1, 0, 0);

        auto to_present = transition_barrier(frame.back_buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        command_list->ResourceBarrier(1, &to_present);
        check_hresult(command_list->Close(), "ID3D12GraphicsCommandList::Close");
        ID3D12CommandList* lists[]{command_list.Get()};
        direct_queue.execute(lists, 1);

        const UINT sync_interval = options.vsync ? 1U : 0U;
        const UINT present_flags = !options.vsync && tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0U;
        const auto present_result = swap_chain->Present(sync_interval, present_flags);
        check_device_result(present_result, "IDXGISwapChain::Present");
        frame.fence_value = direct_queue.signal();
        ++frame_number;
        if (present_result == DXGI_STATUS_OCCLUDED)
        {
            occluded = true;
            return false;
        }
        return true;
    }

    bool wait_for_frame_latency()
    {
        for (;;)
        {
            const auto result = WaitForSingleObjectEx(frame_latency_event.get(), 1000, TRUE);
            if (result == WAIT_OBJECT_0)
            {
                return true;
            }
            if (result == WAIT_IO_COMPLETION)
            {
                continue;
            }
            if (result == WAIT_TIMEOUT)
            {
                const auto removed = device->GetDeviceRemovedReason();
                if (FAILED(removed))
                {
                    check_device_result(removed, "waiting for the DXGI frame-latency object");
                }
                return false;
            }
            throw std::runtime_error("waiting for the DXGI frame-latency object failed (Windows error " + std::to_string(GetLastError()) + ")");
        }
    }

    void resize(const std::uint32_t new_width, const std::uint32_t new_height)
    {
        if (new_width == 0 || new_height == 0 || (new_width == width && new_height == height))
        {
            return;
        }

        // ResizeBuffers invalidates every back buffer, so this is an intentional
        // synchronization point. Normal frame submission never takes this path.
        direct_queue.flush();
        for (auto& frame : frames)
        {
            frame.back_buffer.Reset();
            frame.fence_value = 0;
        }

        width = new_width;
        height = new_height;
        check_device_result(swap_chain->ResizeBuffers(frame_count, width, height, back_buffer_format, swap_chain_flags), "IDXGISwapChain::ResizeBuffers");
        acquire_back_buffers();
        occluded = false;
    }

    void wait_idle()
    {
        std::exception_ptr error;
        if (direct_queue.initialized())
        {
            try
            {
                direct_queue.flush();
            }
            catch (...)
            {
                error = std::current_exception();
            }
        }
        if (copy_queue.initialized())
        {
            try
            {
                copy_queue.flush();
            }
            catch (...)
            {
                if (!error)
                {
                    error = std::current_exception();
                }
            }
        }
        if (direct_queue.initialized() && copy_queue.initialized())
        {
            retire_completed_uploads();
        }
        if (error)
        {
            std::rethrow_exception(error);
        }
    }

    void retire_completed_uploads()
    {
        if (upload_fence_value == 0 || copy_queue.completed_value() < upload_fence_value)
        {
            return;
        }
        upload_resources.clear();
        upload_list.Reset();
        upload_allocator.Reset();
        upload_fence_value = 0;
    }

    void check_device_result(const HRESULT result, const std::string_view operation) const
    {
        if (SUCCEEDED(result))
        {
            return;
        }
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        {
            throw std::runtime_error(std::string(operation) + " failed: " + device_removed_diagnostics());
        }
        check_hresult(result, operation);
    }

    [[nodiscard]] std::string device_removed_diagnostics() const
    {
        std::ostringstream stream;
        const auto reason = device->GetDeviceRemovedReason();
        stream << "D3D12 device removed (" << hresult_text(reason) << ')';

        ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        if (FAILED(device.As(&dred)))
        {
            return stream.str();
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
        {
            std::size_t node_count = 0;
            for (auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext)
            {
                ++node_count;
            }
            stream << "; DRED breadcrumb nodes: " << node_count;
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT1 page_fault{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&page_fault)) && page_fault.PageFaultVA != 0)
        {
            stream << "; page-fault VA: 0x" << std::hex << page_fault.PageFaultVA;
        }
        return stream.str();
    }
};

D3d12Renderer::D3d12Renderer(void* native_window, const std::uint32_t width, const std::uint32_t height, const D3d12RendererOptions& options)
    : impl_(std::make_unique<Impl>(native_window, width, height, options))
{}

D3d12Renderer::~D3d12Renderer() = default;
D3d12Renderer::D3d12Renderer(D3d12Renderer&&) noexcept = default;
D3d12Renderer& D3d12Renderer::operator=(D3d12Renderer&&) noexcept = default;

bool D3d12Renderer::render()
{
    return impl_->render();
}

void D3d12Renderer::resize(const std::uint32_t width, const std::uint32_t height)
{
    impl_->resize(width, height);
}

void D3d12Renderer::wait_idle()
{
    impl_->wait_idle();
}

const std::string& D3d12Renderer::adapter_name() const
{
    return impl_->adapter_name;
}

D3d12RendererStats D3d12Renderer::stats() const
{
    return {
        .frame_number = impl_->frame_number,
        .direct_fence_completed = impl_->direct_queue.completed_value(),
        .copy_fence_completed = impl_->copy_queue.completed_value(),
    };
}

} // namespace eaw
