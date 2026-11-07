#include "eaw/d3d12_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

namespace eaw {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t frame_count = 3;
constexpr std::uint32_t maximum_scene_bones = 4096;
constexpr std::uint32_t maximum_scene_textures = 4096;
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

std::array<float, 16> identity_matrix()
{
    return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
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

DXGI_FORMAT dxgi_format(const VisualTextureFormat format)
{
    switch (format)
    {
    case VisualTextureFormat::bc1_unorm:
        return DXGI_FORMAT_BC1_UNORM;
    case VisualTextureFormat::bc1_srgb:
        return DXGI_FORMAT_BC1_UNORM_SRGB;
    case VisualTextureFormat::bc2_unorm:
        return DXGI_FORMAT_BC2_UNORM;
    case VisualTextureFormat::bc2_srgb:
        return DXGI_FORMAT_BC2_UNORM_SRGB;
    case VisualTextureFormat::bc3_unorm:
        return DXGI_FORMAT_BC3_UNORM;
    case VisualTextureFormat::bc3_srgb:
        return DXGI_FORMAT_BC3_UNORM_SRGB;
    case VisualTextureFormat::bc4_unorm:
        return DXGI_FORMAT_BC4_UNORM;
    case VisualTextureFormat::bc4_snorm:
        return DXGI_FORMAT_BC4_SNORM;
    case VisualTextureFormat::bc5_unorm:
        return DXGI_FORMAT_BC5_UNORM;
    case VisualTextureFormat::bc5_snorm:
        return DXGI_FORMAT_BC5_SNORM;
    case VisualTextureFormat::rgba8_unorm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VisualTextureFormat::rgba8_srgb:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case VisualTextureFormat::bgra8_unorm:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VisualTextureFormat::bgra8_srgb:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case VisualTextureFormat::bgrx8_unorm:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case VisualTextureFormat::bgrx8_srgb:
        return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    case VisualTextureFormat::r8_unorm:
        return DXGI_FORMAT_R8_UNORM;
    case VisualTextureFormat::a8_unorm:
        return DXGI_FORMAT_A8_UNORM;
    case VisualTextureFormat::rg8_unorm:
        return DXGI_FORMAT_R8G8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
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
        ComPtr<ID3D12Resource> bone_buffer;
        std::byte* bone_buffer_mapped{};
        std::uint32_t bone_capacity{};
        std::uint64_t fence_value{};
    };

    struct UploadBatch
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::vector<ComPtr<ID3D12Resource>> resources;
        std::vector<D3D12_RESOURCE_BARRIER> transitions;
        std::uint64_t fence_value{};
    };

    struct GpuDraw
    {
        VisualDraw geometry;
        std::array<float, 4> tint{};
        std::uint32_t descriptor_index{};
        VisualBlendMode blend{VisualBlendMode::opaque};
    };

    HWND window{};
    D3d12RendererOptions options;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t swap_chain_flags{};
    bool tearing_supported{};
    bool occluded{};
    std::uint64_t frame_number{};
    std::string adapter_name;
    float camera_yaw{-0.65F};
    float camera_pitch{0.35F};
    float camera_zoom{1.0F};
    std::array<float, 3> scene_center{};
    float scene_radius{1.0F};

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
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    ComPtr<ID3D12Resource> depth_buffer;
    std::uint32_t rtv_increment{};

    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> opaque_pipeline;
    ComPtr<ID3D12PipelineState> alpha_pipeline;
    ComPtr<ID3D12PipelineState> additive_pipeline;
    ComPtr<ID3D12DescriptorHeap> shader_heap;
    ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> scene_vertex_buffer;
    ComPtr<ID3D12Resource> scene_index_buffer;
    D3D12_VERTEX_BUFFER_VIEW scene_vertex_view{};
    D3D12_INDEX_BUFFER_VIEW scene_index_view{};
    std::vector<ComPtr<ID3D12Resource>> scene_textures;
    std::vector<GpuDraw> scene_draws;
    std::vector<std::array<float, 16>> bone_transforms;
    std::vector<UploadBatch> pending_uploads;
    std::vector<D3D12_RESOURCE_BARRIER> pending_transitions;

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
        for (auto& frame : frames)
        {
            if (frame.bone_buffer && frame.bone_buffer_mapped != nullptr)
            {
                frame.bone_buffer->Unmap(0, nullptr);
                frame.bone_buffer_mapped = nullptr;
            }
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
        create_depth_buffer();
        create_bone_buffers(1);
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

    void create_depth_buffer()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
        heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heap_description.NumDescriptors = 1;
        if (!dsv_heap)
        {
            check_hresult(device->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&dsv_heap)), "CreateDescriptorHeap(DSV)");
            set_name(dsv_heap.Get(), L"EAWEngine depth DSV heap");
        }

        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = width;
        description.Height = height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0F;
        check_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_buffer)),
                      "CreateCommittedResource(depth buffer)");
        set_name(depth_buffer.Get(), L"EAWEngine depth buffer");
        device->CreateDepthStencilView(depth_buffer.Get(), nullptr, dsv_heap->GetCPUDescriptorHandleForHeapStart());
    }

    void create_bone_buffers(const std::uint32_t requested_count)
    {
        const auto count = std::max(requested_count, 1U);
        if (count > maximum_scene_bones)
        {
            throw std::invalid_argument("visual scene exceeds the D3D12 bone palette capacity");
        }
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto description = buffer_description(static_cast<std::uint64_t>(count) * sizeof(std::array<float, 16>));
        for (std::uint32_t index = 0; index < frame_count; ++index)
        {
            auto& frame = frames[index];
            if (frame.bone_buffer && frame.bone_buffer_mapped != nullptr)
            {
                frame.bone_buffer->Unmap(0, nullptr);
                frame.bone_buffer_mapped = nullptr;
            }
            frame.bone_buffer.Reset();
            check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.bone_buffer)),
                          "CreateCommittedResource(frame bone palette)");
            set_name(frame.bone_buffer.Get(), L"EAWEngine frame bone palette " + std::to_wstring(index));
            const D3D12_RANGE no_read{0, 0};
            check_hresult(frame.bone_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.bone_buffer_mapped)), "Map(frame bone palette)");
            frame.bone_capacity = count;
        }
        bone_transforms.assign(count, identity_matrix());
    }

    void create_pipeline()
    {
        constexpr std::string_view shader_source = R"(
Texture2D SandboxTexture : register(t0);
struct BoneTransform
{
    row_major float4x4 transform;
};
StructuredBuffer<BoneTransform> BoneTransforms : register(t1);
SamplerState SandboxSampler : register(s0);

cbuffer FrameConstants : register(b0)
{
    float4x4 model_view_projection;
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
};

struct PsInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

PsInput vs_main(VsInput input)
{
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
        const row_major float4x4 skin = BoneTransforms[input.bone_indices[influence]].transform;
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
    output.position = mul(skinned_position, model_view_projection);
    output.normal = skinned_normal;
    output.uv = input.uv;
    return output;
}

float4 ps_main(PsInput input) : SV_TARGET
{
    float4 color = SandboxTexture.Sample(SandboxSampler, input.uv) * material_tint;
    if (material_flags.x != 0 && color.a < 0.5f)
    {
        discard;
    }
    const float3 light_direction = normalize(float3(0.35f, -0.45f, 0.82f));
    const float lighting = 0.32f + 0.68f * abs(dot(normalize(input.normal), light_direction));
    color.rgb *= lighting;
    return color;
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

        std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.ShaderRegister = 0;
        parameters[0].Constants.Num32BitValues = 16;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants.ShaderRegister = 1;
        parameters[2].Constants.Num32BitValues = 8;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[3].Descriptor.ShaderRegister = 1;
        parameters[3].Descriptor.RegisterSpace = 0;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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
        root_description.NumParameters = static_cast<UINT>(parameters.size());
        root_description.pParameters = parameters.data();
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

        constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 5> input_layout{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VisualVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(VisualVertex, texture_coordinates), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(VisualVertex, bone_indices), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VisualVertex, bone_weights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
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
        description.DepthStencilState.DepthEnable = TRUE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        description.DepthStencilState.StencilEnable = FALSE;
        description.InputLayout = {input_layout.data(), static_cast<UINT>(input_layout.size())};
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = back_buffer_format;
        description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        description.SampleDesc.Count = 1;
        check_hresult(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&opaque_pipeline)), "CreateGraphicsPipelineState(opaque)");
        set_name(opaque_pipeline.Get(), L"EAWEngine opaque pipeline state");

        description.BlendState.RenderTarget[0].BlendEnable = TRUE;
        description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        check_hresult(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&alpha_pipeline)), "CreateGraphicsPipelineState(alpha)");
        set_name(alpha_pipeline.Get(), L"EAWEngine alpha pipeline state");

        description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        check_hresult(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&additive_pipeline)), "CreateGraphicsPipelineState(additive)");
        set_name(additive_pipeline.Get(), L"EAWEngine additive pipeline state");

        D3D12_DESCRIPTOR_HEAP_DESC shader_heap_description{};
        shader_heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        shader_heap_description.NumDescriptors = maximum_scene_textures;
        shader_heap_description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check_hresult(device->CreateDescriptorHeap(&shader_heap_description, IID_PPV_ARGS(&shader_heap)), "CreateDescriptorHeap(shader visible)");
        set_name(shader_heap.Get(), L"EAWEngine sandbox shader descriptor heap");
    }

    void create_initial_assets()
    {
        auto upload = begin_upload(L"EAWEngine validation upload");
        constexpr std::array<VisualVertex, 3> vertices{{
            {{-0.78F, -0.68F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
            {{0.0F, 0.78F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.5F, 0.0F}},
            {{0.78F, -0.68F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
        }};
        const auto vertex_bytes = std::as_bytes(std::span(vertices));
        vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine validation vertex buffer");
        vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
        vertex_buffer_view.SizeInBytes = static_cast<UINT>(vertex_bytes.size());
        vertex_buffer_view.StrideInBytes = sizeof(VisualVertex);

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
        VisualTexture fallback;
        fallback.name = "validation checker";
        fallback.format = VisualTextureFormat::rgba8_unorm;
        VisualTextureMip mip;
        mip.width = texture_width;
        mip.height = texture_height;
        mip.row_pitch = texture_width * sizeof(std::uint32_t);
        mip.slice_pitch = sizeof(pixels);
        const auto pixel_bytes = std::as_bytes(std::span(pixels));
        mip.data.assign(pixel_bytes.begin(), pixel_bytes.end());
        fallback.mips.push_back(std::move(mip));
        texture = upload_texture(upload, fallback, 0);
        submit_upload(std::move(upload));
    }

    UploadBatch begin_upload(const std::wstring_view name)
    {
        UploadBatch result;
        check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&result.allocator)), "CreateCommandAllocator(copy)");
        check_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, result.allocator.Get(), nullptr, IID_PPV_ARGS(&result.list)), "CreateCommandList(copy)");
        set_name(result.allocator.Get(), std::wstring(name) + L" allocator");
        set_name(result.list.Get(), std::wstring(name) + L" command list");
        return result;
    }

    void submit_upload(UploadBatch upload)
    {
        pending_uploads.reserve(pending_uploads.size() + 1U);
        pending_transitions.reserve(pending_transitions.size() + upload.transitions.size());
        check_hresult(upload.list->Close(), "close upload command list");
        ID3D12CommandList* lists[]{upload.list.Get()};
        copy_queue.execute(lists, 1);
        upload.fence_value = copy_queue.signal();

        // Queue-to-queue synchronization keeps upload completion off the CPU's
        // critical path. The direct queue transitions and consumes the data.
        direct_queue.wait_gpu(copy_queue, upload.fence_value);
        pending_transitions.insert(pending_transitions.end(), upload.transitions.begin(), upload.transitions.end());
        pending_uploads.push_back(std::move(upload));
    }

    ComPtr<ID3D12Resource> upload_buffer(UploadBatch& upload, const std::span<const std::byte> bytes, const D3D12_RESOURCE_STATES final_state, const std::wstring_view name)
    {
        if (bytes.empty())
        {
            throw std::invalid_argument("cannot upload an empty D3D12 buffer");
        }
        const auto description = buffer_description(bytes.size());
        const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> staging;
        check_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)),
                      "CreateCommittedResource(default buffer)");
        check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)),
                      "CreateCommittedResource(upload buffer)");
        set_name(resource.Get(), name);
        set_name(staging.Get(), std::wstring(name) + L" staging");
        void* mapped{};
        const D3D12_RANGE no_read{0, 0};
        check_hresult(staging->Map(0, &no_read, &mapped), "Map(upload buffer)");
        std::memcpy(mapped, bytes.data(), bytes.size());
        staging->Unmap(0, nullptr);
        upload.list->CopyBufferRegion(resource.Get(), 0, staging.Get(), 0, bytes.size());
        upload.resources.push_back(std::move(staging));
        upload.transitions.push_back(transition_barrier(resource.Get(), D3D12_RESOURCE_STATE_COMMON, final_state));
        return resource;
    }

    ComPtr<ID3D12Resource> upload_texture(UploadBatch& upload, const VisualTexture& source, const std::uint32_t descriptor_index)
    {
        if (source.mips.empty() || descriptor_index >= maximum_scene_textures)
        {
            throw std::invalid_argument("invalid visual texture upload");
        }
        const auto format = dxgi_format(source.format);
        if (format == DXGI_FORMAT_UNKNOWN || source.mips.size() > std::numeric_limits<std::uint16_t>::max())
        {
            throw std::invalid_argument("invalid visual texture format or mip count");
        }
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = source.mips.front().width;
        description.Height = source.mips.front().height;
        description.DepthOrArraySize = 1;
        description.MipLevels = static_cast<std::uint16_t>(source.mips.size());
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> resource;
        check_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)),
                      "CreateCommittedResource(scene texture)");
        set_name(resource.Get(), std::wstring(source.name.begin(), source.name.end()));

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(source.mips.size());
        std::vector<UINT> row_counts(source.mips.size());
        std::vector<UINT64> row_sizes(source.mips.size());
        UINT64 upload_size{};
        device->GetCopyableFootprints(&description, 0, static_cast<UINT>(source.mips.size()), 0, footprints.data(), row_counts.data(), row_sizes.data(), &upload_size);
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto upload_description = buffer_description(upload_size);
        ComPtr<ID3D12Resource> staging;
        check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)),
                      "CreateCommittedResource(texture staging)");
        set_name(staging.Get(), L"EAWEngine texture staging");

        std::byte* mapped{};
        const D3D12_RANGE no_read{0, 0};
        check_hresult(staging->Map(0, &no_read, reinterpret_cast<void**>(&mapped)), "Map(texture staging)");
        for (std::size_t mip_index = 0; mip_index < source.mips.size(); ++mip_index)
        {
            const auto& mip = source.mips[mip_index];
            const auto required_size = mip.row_pitch * row_counts[mip_index];
            if (mip.data.size() < required_size || mip.row_pitch < row_sizes[mip_index])
            {
                staging->Unmap(0, nullptr);
                throw std::runtime_error("visual texture mip data is shorter than its D3D12 footprint");
            }
            for (UINT row = 0; row < row_counts[mip_index]; ++row)
            {
                std::memcpy(mapped + footprints[mip_index].Offset + static_cast<std::size_t>(row) * footprints[mip_index].Footprint.RowPitch,
                            mip.data.data() + static_cast<std::size_t>(row) * mip.row_pitch, static_cast<std::size_t>(row_sizes[mip_index]));
            }

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = resource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = static_cast<UINT>(mip_index);
            D3D12_TEXTURE_COPY_LOCATION source_location{};
            source_location.pResource = staging.Get();
            source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source_location.PlacedFootprint = footprints[mip_index];
            upload.list->CopyTextureRegion(&destination, 0, 0, 0, &source_location, nullptr);
        }
        staging->Unmap(0, nullptr);
        upload.resources.push_back(std::move(staging));
        upload.transitions.push_back(transition_barrier(resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = static_cast<UINT>(source.mips.size());
        auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_index) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateShaderResourceView(resource.Get(), &view, handle);
        return resource;
    }

    void load_scene(const VisualScene& scene)
    {
        if (frame_number != 0 || !scene_draws.empty())
        {
            throw std::logic_error("the initial visual scene must be loaded before rendering begins");
        }
        if (scene.vertices.empty() || scene.indices.empty() || scene.draws.empty())
        {
            throw std::invalid_argument("visual scene contains no renderable geometry");
        }
        if (scene.textures.size() + 1U > maximum_scene_textures)
        {
            throw std::invalid_argument("visual scene exceeds the D3D12 texture descriptor capacity");
        }
        create_bone_buffers(scene.bone_count);

        auto upload = begin_upload(L"EAWEngine scene upload");
        const auto vertex_bytes = std::as_bytes(std::span(scene.vertices));
        const auto index_bytes = std::as_bytes(std::span(scene.indices));
        auto new_vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine scene vertex buffer");
        auto new_index_buffer = upload_buffer(upload, index_bytes, D3D12_RESOURCE_STATE_INDEX_BUFFER, L"EAWEngine scene index buffer");

        std::vector<ComPtr<ID3D12Resource>> new_textures;
        new_textures.reserve(scene.textures.size());
        for (std::size_t index = 0; index < scene.textures.size(); ++index)
        {
            new_textures.push_back(upload_texture(upload, scene.textures[index], static_cast<std::uint32_t>(index + 1U)));
        }

        std::vector<GpuDraw> new_draws;
        new_draws.reserve(scene.draws.size());
        for (const auto& draw : scene.draws)
        {
            if (draw.material >= scene.materials.size() || static_cast<std::uint64_t>(draw.first_index) + draw.index_count > scene.indices.size())
            {
                throw std::invalid_argument("visual scene draw references data outside its buffers");
            }
            const auto& material = scene.materials[draw.material];
            std::uint32_t descriptor_index = 0;
            if (material.base_color_texture != invalid_visual_texture)
            {
                if (material.base_color_texture >= scene.textures.size())
                {
                    throw std::invalid_argument("visual material references a missing texture");
                }
                descriptor_index = material.base_color_texture + 1U;
            }
            new_draws.push_back({draw, material.tint, descriptor_index, material.blend});
        }
        std::ranges::stable_sort(new_draws,
                                 [](const GpuDraw& left, const GpuDraw& right)
                                 {
                                     return static_cast<unsigned>(left.blend) < static_cast<unsigned>(right.blend);
                                 });

        submit_upload(std::move(upload));
        scene_vertex_buffer = std::move(new_vertex_buffer);
        scene_index_buffer = std::move(new_index_buffer);
        scene_textures = std::move(new_textures);
        scene_draws = std::move(new_draws);
        scene_vertex_view.BufferLocation = scene_vertex_buffer->GetGPUVirtualAddress();
        scene_vertex_view.SizeInBytes = static_cast<UINT>(vertex_bytes.size());
        scene_vertex_view.StrideInBytes = sizeof(VisualVertex);
        scene_index_view.BufferLocation = scene_index_buffer->GetGPUVirtualAddress();
        scene_index_view.SizeInBytes = static_cast<UINT>(index_bytes.size());
        scene_index_view.Format = DXGI_FORMAT_R32_UINT;

        for (std::size_t axis = 0; axis < scene_center.size(); ++axis)
        {
            scene_center[axis] = (scene.bounds_minimum[axis] + scene.bounds_maximum[axis]) * 0.5F;
        }
        const auto extent_x = (scene.bounds_maximum[0] - scene.bounds_minimum[0]) * 0.5F;
        const auto extent_y = (scene.bounds_maximum[1] - scene.bounds_minimum[1]) * 0.5F;
        const auto extent_z = (scene.bounds_maximum[2] - scene.bounds_minimum[2]) * 0.5F;
        scene_radius = std::max(std::sqrt(extent_x * extent_x + extent_y * extent_y + extent_z * extent_z), 0.001F);
        reset_camera();
    }

    void set_bone_transforms(const std::span<const std::array<float, 16>> transforms)
    {
        if (transforms.size() > bone_transforms.size())
        {
            throw std::invalid_argument("bone palette update exceeds the loaded visual scene capacity");
        }
        std::ranges::fill(bone_transforms, identity_matrix());
        std::ranges::copy(transforms, bone_transforms.begin());
    }

    void orbit_camera(const float yaw_delta, const float pitch_delta)
    {
        camera_yaw += yaw_delta;
        camera_pitch = std::clamp(camera_pitch + pitch_delta, -1.45F, 1.45F);
    }

    void zoom_camera(const float wheel_delta)
    {
        camera_zoom = std::clamp(camera_zoom * std::pow(1.12F, wheel_delta), 0.15F, 12.0F);
    }

    void reset_camera()
    {
        camera_yaw = -0.65F;
        camera_pitch = 0.35F;
        camera_zoom = 1.0F;
    }

    [[nodiscard]] DirectX::XMFLOAT4X4 camera_matrix() const
    {
        using namespace DirectX;
        const auto distance = scene_radius * 2.6F / camera_zoom;
        const auto horizontal = std::cos(camera_pitch) * distance;
        const auto target = XMVectorSet(scene_center[0], scene_center[1], scene_center[2], 1.0F);
        const auto eye =
            XMVectorSet(scene_center[0] + std::cos(camera_yaw) * horizontal, scene_center[1] + std::sin(camera_yaw) * horizontal, scene_center[2] + std::sin(camera_pitch) * distance, 1.0F);
        const auto view = XMMatrixLookAtLH(eye, target, XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F));
        const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1U));
        const auto near_plane = std::max(scene_radius * 0.01F, 0.001F);
        const auto far_plane = std::max(scene_radius * 20.0F, near_plane + 1.0F);
        const auto projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(55.0F), aspect, near_plane, far_plane);
        DirectX::XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, XMMatrixTranspose(view * projection));
        return result;
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
        if (bone_transforms.size() > frame.bone_capacity)
        {
            throw std::runtime_error("frame bone palette is smaller than the active visual scene");
        }
        std::memcpy(frame.bone_buffer_mapped, bone_transforms.data(), bone_transforms.size() * sizeof(bone_transforms.front()));

        check_hresult(frame.allocator->Reset(), "ID3D12CommandAllocator::Reset");
        check_hresult(command_list->Reset(frame.allocator.Get(), opaque_pipeline.Get()), "ID3D12GraphicsCommandList::Reset");

        if (!pending_transitions.empty())
        {
            command_list->ResourceBarrier(static_cast<UINT>(pending_transitions.size()), pending_transitions.data());
            pending_transitions.clear();
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
        const auto depth_target = dsv_heap->GetCPUDescriptorHandleForHeapStart();
        command_list->OMSetRenderTargets(1, &render_target, FALSE, &depth_target);
        constexpr std::array clear_color{0.018F, 0.031F, 0.043F, 1.0F};
        command_list->ClearRenderTargetView(render_target, clear_color.data(), 0, nullptr);
        command_list->ClearDepthStencilView(depth_target, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);

        ID3D12DescriptorHeap* descriptor_heaps[]{shader_heap.Get()};
        command_list->SetDescriptorHeaps(1, descriptor_heaps);
        command_list->SetGraphicsRootSignature(root_signature.Get());
        const auto matrix = camera_matrix();
        command_list->SetGraphicsRoot32BitConstants(0, 16, &matrix, 0);
        command_list->SetGraphicsRootShaderResourceView(3, frame.bone_buffer->GetGPUVirtualAddress());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        struct MaterialConstants
        {
            std::array<float, 4> tint;
            std::array<std::uint32_t, 4> flags;
        };
        const auto descriptor_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto descriptor_start = shader_heap->GetGPUDescriptorHandleForHeapStart();
        const auto bind_material = [&](const std::uint32_t descriptor_index, const std::array<float, 4>& tint, const bool alpha_test)
        {
            auto descriptor = descriptor_start;
            descriptor.ptr += static_cast<UINT64>(descriptor_index) * descriptor_increment;
            command_list->SetGraphicsRootDescriptorTable(1, descriptor);
            const MaterialConstants constants{tint, {alpha_test ? 1U : 0U, 0U, 0U, 0U}};
            command_list->SetGraphicsRoot32BitConstants(2, 8, &constants, 0);
        };

        if (scene_draws.empty())
        {
            command_list->SetPipelineState(opaque_pipeline.Get());
            bind_material(0, {1.0F, 1.0F, 1.0F, 1.0F}, false);
            command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
            command_list->DrawInstanced(3, 1, 0, 0);
        }
        else
        {
            command_list->IASetVertexBuffers(0, 1, &scene_vertex_view);
            command_list->IASetIndexBuffer(&scene_index_view);
            for (const auto& draw : scene_draws)
            {
                ID3D12PipelineState* pipeline = opaque_pipeline.Get();
                if (draw.blend == VisualBlendMode::alpha_blend)
                {
                    pipeline = alpha_pipeline.Get();
                }
                else if (draw.blend == VisualBlendMode::additive)
                {
                    pipeline = additive_pipeline.Get();
                }
                command_list->SetPipelineState(pipeline);
                bind_material(draw.descriptor_index, draw.tint, draw.blend == VisualBlendMode::alpha_test);
                command_list->DrawIndexedInstanced(draw.geometry.index_count, 1, draw.geometry.first_index, draw.geometry.base_vertex, 0);
            }
        }

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
        depth_buffer.Reset();
        for (auto& frame : frames)
        {
            frame.back_buffer.Reset();
            frame.fence_value = 0;
        }

        width = new_width;
        height = new_height;
        check_device_result(swap_chain->ResizeBuffers(frame_count, width, height, back_buffer_format, swap_chain_flags), "IDXGISwapChain::ResizeBuffers");
        acquire_back_buffers();
        create_depth_buffer();
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
        const auto completed = copy_queue.completed_value();
        std::erase_if(pending_uploads,
                      [completed](const UploadBatch& upload)
                      {
                          return upload.fence_value <= completed;
                      });
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

void D3d12Renderer::load_scene(const VisualScene& scene)
{
    impl_->load_scene(scene);
}

void D3d12Renderer::set_bone_transforms(const std::span<const std::array<float, 16>> transforms)
{
    impl_->set_bone_transforms(transforms);
}

void D3d12Renderer::orbit_camera(const float yaw_delta, const float pitch_delta)
{
    impl_->orbit_camera(yaw_delta, pitch_delta);
}

void D3d12Renderer::zoom_camera(const float wheel_delta)
{
    impl_->zoom_camera(wheel_delta);
}

void D3d12Renderer::reset_camera()
{
    impl_->reset_camera();
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
