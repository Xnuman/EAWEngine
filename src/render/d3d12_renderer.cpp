#include "eaw/d3d12_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
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
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

// Offline-compiled shader model 6.0 bytecode (see src/render/shaders/sandbox.hlsl). Both entry
// points embed the root signature, which the renderer creates from the vertex shader blob.
#include "g_eaw_sandbox_ps.h"
#include "g_eaw_sandbox_vs.h"

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

    // One draw of one mesh, issued once for every instance of that mesh. Instances are sorted by
    // mesh at load, so each batch reads a contiguous instance range.
    struct GpuDraw
    {
        VisualDraw geometry;
        std::array<float, 4> tint{};
        std::uint32_t descriptor_index{};
        VisualBlendMode blend{VisualBlendMode::opaque};
        std::uint32_t first_instance{};
        std::uint32_t instance_count{1};
        bool background{};
    };

    // Structured-buffer layout shared with the vertex shader (96 bytes).
    struct GpuInstance
    {
        std::array<float, 16> transform{};
        std::array<float, 4> tint{};
        std::uint32_t bone_offset{};
        std::uint32_t flags{};
        std::array<std::uint32_t, 2> padding{};
    };

    static constexpr std::uint32_t background_instance_flag = 1U;

    HWND window{};
    D3d12RendererOptions options;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t swap_chain_flags{};
    bool tearing_supported{};
    bool occluded{};
    std::uint64_t frame_number{};
    std::string adapter_name;
    VisualCamera camera;

    ComPtr<IDXGIFactory7> factory;
    ComPtr<IDXGIAdapter4> adapter;
    ComPtr<ID3D12Device> device;
    // Optional pipeline library. The serialized blob backs the library, so it is declared first
    // and therefore destroyed after it.
    std::vector<std::byte> pipeline_library_blob;
    ComPtr<ID3D12PipelineLibrary> pipeline_library;
    std::filesystem::path pipeline_cache_file;
    std::uint64_t pipeline_cache_key{};
    bool pipeline_library_loaded{};
    bool pipeline_library_dirty{};
    bool pipeline_library_stale{};
    std::uint32_t pipeline_cache_hits{};
    std::uint32_t pipeline_cache_misses{};
    std::string pipeline_cache_status{"disabled"};
    std::vector<std::pair<std::wstring, ComPtr<ID3D12PipelineState>>> created_pipelines;
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
    ComPtr<ID3D12PipelineState> background_opaque_pipeline;
    ComPtr<ID3D12PipelineState> background_alpha_pipeline;
    ComPtr<ID3D12PipelineState> background_additive_pipeline;
    ComPtr<ID3D12DescriptorHeap> shader_heap;
    ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    ComPtr<ID3D12Resource> texture;
    // The validation fallback keeps its own identity instance; its pending COMMON-to-SRV transition
    // holds a raw resource pointer, so it must outlive any scene that replaces the scene buffer.
    ComPtr<ID3D12Resource> identity_instance_buffer;
    ComPtr<ID3D12Resource> scene_instance_buffer;
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
        open_pipeline_cache();
        create_pipeline();
        save_pipeline_cache();
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

    // --- Pipeline library cache -----------------------------------------------------------------
    // File layout: "EAWPSO01", u64 key (shader bytecode + layout version), u64 payload size,
    // payload (ID3D12PipelineLibrary::Serialize output). Any mismatch or driver rejection discards
    // the cache; failures never stop rendering.
    static constexpr std::array<char, 8> pipeline_cache_magic{'E', 'A', 'W', 'P', 'S', 'O', '0', '1'};
    static constexpr std::uint64_t pipeline_layout_version = 1;
    static constexpr std::uint64_t maximum_pipeline_cache_bytes = 256ULL * 1024ULL * 1024ULL;

    static std::uint64_t fnv1a(std::uint64_t hash, const std::span<const std::byte> bytes) noexcept
    {
        for (const auto byte : bytes)
        {
            hash = (hash ^ static_cast<std::uint8_t>(byte)) * 1099511628211ULL;
        }
        return hash;
    }

    void open_pipeline_cache()
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

    void create_pipeline_state(const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description, ComPtr<ID3D12PipelineState>& pipeline)
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

    void save_pipeline_cache()
    {
        if (!pipeline_library || (!pipeline_library_dirty && !pipeline_library_stale))
        {
            created_pipelines.clear();
            return;
        }
        try
        {
            ComPtr<ID3D12PipelineLibrary> library = pipeline_library;
            if (pipeline_library_stale)
            {
                ComPtr<ID3D12Device1> device1;
                check_hresult(device.As(&device1), "query ID3D12Device1");
                library.Reset();
                check_hresult(device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library)), "CreatePipelineLibrary(rebuild)");
                for (const auto& [name, pipeline] : created_pipelines)
                {
                    check_hresult(library->StorePipeline(name.c_str(), pipeline.Get()), "ID3D12PipelineLibrary::StorePipeline(rebuild)");
                }
            }
            const auto payload_size = library->GetSerializedSize();
            std::vector<std::byte> payload(payload_size);
            check_hresult(library->Serialize(payload.data(), payload.size()), "ID3D12PipelineLibrary::Serialize");
            std::filesystem::create_directories(options.pipeline_cache_directory);
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
                    throw std::runtime_error("could not write " + temporary.string());
                }
            }
            std::filesystem::rename(temporary, pipeline_cache_file);
            pipeline_cache_status += pipeline_library_stale ? ", rebuilt and saved" : ", saved";
        }
        catch (const std::exception& error)
        {
            pipeline_cache_status += std::string(", not saved: ") + error.what();
        }
        created_pipelines.clear();
    }

    void create_pipeline()
    {
        D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
        {
            throw std::runtime_error("the D3D12 device does not support shader model 6.0, which the offline-compiled sandbox shaders require");
        }
        check_hresult(device->CreateRootSignature(0, g_eaw_sandbox_vs, sizeof(g_eaw_sandbox_vs), IID_PPV_ARGS(&root_signature)), "CreateRootSignature(embedded sandbox root signature)");
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
        description.VS = {g_eaw_sandbox_vs, sizeof(g_eaw_sandbox_vs)};
        description.PS = {g_eaw_sandbox_ps, sizeof(g_eaw_sandbox_ps)};
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
        create_pipeline_state(L"opaque", description, opaque_pipeline);

        description.BlendState.RenderTarget[0].BlendEnable = TRUE;
        description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
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
        const GpuInstance identity{visual_identity_transform, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, {}};
        identity_instance_buffer = upload_buffer(upload, std::as_bytes(std::span(&identity, 1)), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"EAWEngine identity instance buffer");
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
        if (scene.meshes.empty() || scene.instances.empty())
        {
            throw std::invalid_argument("visual scene has no meshes or instances");
        }
        if (scene.instances.size() > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("visual scene has too many instances for one D3D12 instance buffer");
        }
        for (std::size_t index = 0; index < scene.meshes.size(); ++index)
        {
            const auto& mesh = scene.meshes[index];
            if (static_cast<std::uint64_t>(mesh.first_draw) + mesh.draw_count > scene.draws.size())
            {
                throw std::invalid_argument("visual mesh " + std::to_string(index) + " (" + mesh.name + ") references draws outside the scene");
            }
        }
        for (std::size_t index = 0; index < scene.instances.size(); ++index)
        {
            const auto& instance = scene.instances[index];
            if (instance.mesh >= scene.meshes.size())
            {
                throw std::invalid_argument("visual instance " + std::to_string(index) + " references a missing mesh");
            }
            if (static_cast<std::uint64_t>(instance.bone_offset) + scene.meshes[instance.mesh].bone_count > std::max(scene.bone_count, 1U))
            {
                throw std::invalid_argument("visual instance " + std::to_string(index) + " bone range exceeds the scene bone palette");
            }
            if (!std::ranges::all_of(instance.transform,
                                     [](const float value)
                                     {
                                         return std::isfinite(value);
                                     }))
            {
                throw std::invalid_argument("visual instance " + std::to_string(index) + " has a non-finite transform");
            }
        }
        create_bone_buffers(scene.bone_count);

        // Group instances by mesh so each mesh's draws cover one contiguous instance range.
        std::vector<std::uint32_t> order(scene.instances.size());
        for (std::uint32_t index = 0; index < order.size(); ++index)
        {
            order[index] = index;
        }
        // Ranges are keyed by mesh and layer: range index = mesh * 2 + (background ? 1 : 0).
        const auto range_key = [&](const std::uint32_t index)
        {
            const auto& instance = scene.instances[index];
            return static_cast<std::size_t>(instance.mesh) * 2U + (instance.background ? 1U : 0U);
        };
        std::ranges::stable_sort(order, {}, range_key);
        std::vector<GpuInstance> gpu_instances;
        gpu_instances.reserve(order.size());
        std::vector<std::pair<std::uint32_t, std::uint32_t>> mesh_ranges(scene.meshes.size() * 2U, {0U, 0U});
        for (const auto index : order)
        {
            const auto& instance = scene.instances[index];
            auto& range = mesh_ranges[range_key(index)];
            if (range.second == 0)
            {
                range.first = static_cast<std::uint32_t>(gpu_instances.size());
            }
            ++range.second;
            gpu_instances.push_back({instance.transform, instance.tint, instance.bone_offset, instance.background ? background_instance_flag : 0U, {}});
        }

        auto upload = begin_upload(L"EAWEngine scene upload");
        const auto vertex_bytes = std::as_bytes(std::span(scene.vertices));
        const auto index_bytes = std::as_bytes(std::span(scene.indices));
        auto new_vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine scene vertex buffer");
        auto new_index_buffer = upload_buffer(upload, index_bytes, D3D12_RESOURCE_STATE_INDEX_BUFFER, L"EAWEngine scene index buffer");
        auto new_instance_buffer = upload_buffer(upload, std::as_bytes(std::span(gpu_instances)), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"EAWEngine scene instance buffer");

        std::vector<ComPtr<ID3D12Resource>> new_textures;
        new_textures.reserve(scene.textures.size());
        for (std::size_t index = 0; index < scene.textures.size(); ++index)
        {
            new_textures.push_back(upload_texture(upload, scene.textures[index], static_cast<std::uint32_t>(index + 1U)));
        }

        std::vector<GpuDraw> new_draws;
        new_draws.reserve(scene.draws.size());
        for (std::size_t range_index = 0; range_index < mesh_ranges.size(); ++range_index)
        {
            const auto mesh_index = range_index / 2U;
            const bool background = (range_index % 2U) != 0;
            const auto& mesh = scene.meshes[mesh_index];
            const auto [first_instance, instance_count] = mesh_ranges[range_index];
            if (instance_count == 0)
            {
                continue;
            }
            for (auto draw_index = mesh.first_draw; draw_index < mesh.first_draw + mesh.draw_count; ++draw_index)
            {
                const auto& draw = scene.draws[draw_index];
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
                new_draws.push_back({draw, material.tint, descriptor_index, material.blend, first_instance, instance_count, background});
            }
        }
        // Backgrounds first in scene (mesh) order so later layers paint over earlier ones; then the
        // world by blend mode (opaque, alpha-tested, blended, additive).
        std::ranges::stable_sort(new_draws, {},
                                 [](const GpuDraw& draw)
                                 {
                                     return draw.background ? 0U : 1U + static_cast<unsigned>(draw.blend);
                                 });

        submit_upload(std::move(upload));
        scene_vertex_buffer = std::move(new_vertex_buffer);
        scene_index_buffer = std::move(new_index_buffer);
        scene_instance_buffer = std::move(new_instance_buffer);
        scene_textures = std::move(new_textures);
        scene_draws = std::move(new_draws);
        scene_vertex_view.BufferLocation = scene_vertex_buffer->GetGPUVirtualAddress();
        scene_vertex_view.SizeInBytes = static_cast<UINT>(vertex_bytes.size());
        scene_vertex_view.StrideInBytes = sizeof(VisualVertex);
        scene_index_view.BufferLocation = scene_index_buffer->GetGPUVirtualAddress();
        scene_index_view.SizeInBytes = static_cast<UINT>(index_bytes.size());
        scene_index_view.Format = DXGI_FORMAT_R32_UINT;
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

    void set_camera(const VisualCamera& next)
    {
        const auto finite = [](const std::array<float, 3>& value)
        {
            return std::ranges::all_of(value,
                                       [](const float component)
                                       {
                                           return std::isfinite(component);
                                       });
        };
        if (!finite(next.eye) || !finite(next.target) || !finite(next.up) || !std::isfinite(next.vertical_fov_degrees) || !std::isfinite(next.near_plane) || !std::isfinite(next.far_plane))
        {
            throw std::invalid_argument("visual camera contains non-finite values");
        }
        if (!(next.near_plane > 0.0F) || !(next.far_plane > next.near_plane * 1.0001F) || next.vertical_fov_degrees <= 1.0F || next.vertical_fov_degrees >= 179.0F)
        {
            throw std::invalid_argument("visual camera has an invalid projection (near " + std::to_string(next.near_plane) + ", far " + std::to_string(next.far_plane) + ", fov " +
                                        std::to_string(next.vertical_fov_degrees) + ")");
        }
        const std::array<float, 3> forward{next.target[0] - next.eye[0], next.target[1] - next.eye[1], next.target[2] - next.eye[2]};
        const std::array<float, 3> side{next.up[1] * forward[2] - next.up[2] * forward[1], next.up[2] * forward[0] - next.up[0] * forward[2], next.up[0] * forward[1] - next.up[1] * forward[0]};
        if (forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2] <= 1.0e-12F || side[0] * side[0] + side[1] * side[1] + side[2] * side[2] <= 1.0e-12F)
        {
            throw std::invalid_argument("visual camera eye, target, and up do not define a view");
        }
        camera = next;
    }

    [[nodiscard]] DirectX::XMFLOAT4X4 camera_matrix() const
    {
        using namespace DirectX;
        const auto eye = XMVectorSet(camera.eye[0], camera.eye[1], camera.eye[2], 1.0F);
        const auto target = XMVectorSet(camera.target[0], camera.target[1], camera.target[2], 1.0F);
        const auto up = XMVectorSet(camera.up[0], camera.up[1], camera.up[2], 0.0F);
        const auto view = XMMatrixLookAtLH(eye, target, up);
        const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1U));
        const auto projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.vertical_fov_degrees), aspect, camera.near_plane, camera.far_plane);
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
        command_list->SetGraphicsRootShaderResourceView(4, (scene_draws.empty() ? identity_instance_buffer : scene_instance_buffer)->GetGPUVirtualAddress());
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
            const std::uint32_t instance_base = 0;
            command_list->SetGraphicsRoot32BitConstants(5, 1, &instance_base, 0);
            command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
            command_list->DrawInstanced(3, 1, 0, 0);
        }
        else
        {
            command_list->IASetVertexBuffers(0, 1, &scene_vertex_view);
            command_list->IASetIndexBuffer(&scene_index_view);
            for (const auto& draw : scene_draws)
            {
                ID3D12PipelineState* pipeline = draw.background ? background_opaque_pipeline.Get() : opaque_pipeline.Get();
                if (draw.blend == VisualBlendMode::alpha_blend)
                {
                    pipeline = draw.background ? background_alpha_pipeline.Get() : alpha_pipeline.Get();
                }
                else if (draw.blend == VisualBlendMode::additive)
                {
                    pipeline = draw.background ? background_additive_pipeline.Get() : additive_pipeline.Get();
                }
                command_list->SetPipelineState(pipeline);
                bind_material(draw.descriptor_index, draw.tint, draw.blend == VisualBlendMode::alpha_test);
                command_list->SetGraphicsRoot32BitConstants(5, 1, &draw.first_instance, 0);
                command_list->DrawIndexedInstanced(draw.geometry.index_count, draw.instance_count, draw.geometry.first_index, draw.geometry.base_vertex, 0);
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

void D3d12Renderer::set_camera(const VisualCamera& camera)
{
    impl_->set_camera(camera);
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
        .pipeline_cache_hits = impl_->pipeline_cache_hits,
        .pipeline_cache_misses = impl_->pipeline_cache_misses,
        .pipeline_cache_status = impl_->pipeline_cache_status,
    };
}

} // namespace eaw
