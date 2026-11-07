#include "eaw/d3d12_renderer.hpp"

#include "eaw/gpu_resource_lifetime.hpp"

#include "imgui.h"
#include "imgui_impl_dx12.h"

#include <algorithm>
#include <array>
#include <chrono>
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
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

// Offline-compiled shader model 6.0 bytecode (see src/render/shaders/sandbox.hlsl). Both entry
// points embed the root signature, which the renderer creates from the vertex shader blob.
#include "g_eaw_debug_ps.h"
#include "g_eaw_debug_vs.h"
#include "g_eaw_sandbox_ps.h"
#include "g_eaw_sandbox_vs.h"
#include "g_eaw_terrain_ps.h"
#include "g_eaw_terrain_vs.h"

namespace eaw {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t frame_count = 3;
constexpr std::uint32_t maximum_scene_bones = 4096;
// Size of the shader-visible texture table (matches numDescriptors in sandbox.hlsl). The first
// fallback_texture_count slots hold the validation checker, white, and a flat normal map.
constexpr std::uint32_t maximum_scene_textures = 4096;
constexpr std::uint32_t checker_texture_slot = 0;
constexpr std::uint32_t white_texture_slot = 1;
constexpr std::uint32_t flat_normal_texture_slot = 2;
constexpr std::uint32_t fallback_texture_count = 3;
// Per-frame constant buffers are 256-byte aligned, as D3D12 requires for CBVs.
constexpr std::uint64_t frame_constant_bytes = 256;
constexpr DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT pick_format = DXGI_FORMAT_R32_UINT;
// Temporary per-frame debug line capacity (an implementation limit, diagnosed through stats).
constexpr std::size_t maximum_debug_lines = 32768;
constexpr std::uint32_t pick_terrain = 0xFFFFFFFFU;

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

    // The value the next signal() will write. Work recorded now is submitted before that signal,
    // so resources it references may be retired once this value completes.
    [[nodiscard]] std::uint64_t next_value() const noexcept
    {
        return next_fence_value_;
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
        ComPtr<ID3D12Resource> constant_buffer;
        std::byte* constant_buffer_mapped{};
        // Logical texture id -> descriptor slot for this frame (sandbox shaders' texture_residency).
        ComPtr<ID3D12Resource> residency_buffer;
        std::uint32_t* residency_mapped{};
        // One pick-target texel copied by this frame, read once its fence completes.
        ComPtr<ID3D12Resource> pick_readback;
        std::optional<std::uint32_t> pick_request;
        // Debug line vertices written once this context is reusable.
        ComPtr<ID3D12Resource> debug_vertex_buffer;
        std::byte* debug_vertices_mapped{};
        // Begin/end GPU timestamps of this context's last frame, resolved for readback.
        ComPtr<ID3D12Resource> timestamp_readback;
        bool timestamps_pending{};
        // Instances submitted through set_instances, written once this context is reusable.
        ComPtr<ID3D12Resource> instance_buffer;
        std::byte* instance_buffer_mapped{};
        std::size_t instance_capacity{};
        std::uint64_t fence_value{};
    };

    // Vertex layout of debug.hlsl.
    struct DebugVertex
    {
        std::array<float, 3> position;
        std::array<float, 4> color;
    };

    // Layout of FrameConstants in sandbox.hlsl.
    struct FrameConstants
    {
        DirectX::XMFLOAT4X4 view_projection;
        std::array<float, 4> camera_position_time;
        std::array<float, 4> key_direction;
        std::array<float, 4> key_diffuse;
        std::array<float, 4> key_specular;
        std::array<float, 4> ambient;
        std::array<std::array<float, 4>, 2> fill_direction;
        std::array<std::array<float, 4>, 2> fill_diffuse;
        std::array<std::uint32_t, 4> selection{};
    };
    static_assert(sizeof(FrameConstants) <= frame_constant_bytes);

    // Layout of MaterialData in sandbox.hlsl (96 bytes) and its flag bits.
    struct GpuMaterial
    {
        std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
        std::array<float, 4> diffuse{1.0F, 1.0F, 1.0F, 1.0F};
        std::array<float, 4> specular{};
        std::array<float, 4> emissive{};
        std::array<float, 2> uv_scroll{};
        float alpha_cutoff{0.5F};
        std::uint32_t flags{};
        std::uint32_t base_texture{white_texture_slot};
        std::uint32_t normal_texture{flat_normal_texture_slot};
        std::uint32_t gloss_texture{white_texture_slot};
        std::uint32_t lighting{};
    };
    static_assert(sizeof(GpuMaterial) == 96);
    static constexpr std::uint32_t material_alpha_test = 1U << 0U;
    static constexpr std::uint32_t material_alpha_blend = 1U << 1U;
    static constexpr std::uint32_t material_additive = 1U << 2U;
    static constexpr std::uint32_t material_team_color = 1U << 3U;
    static constexpr std::uint32_t material_emissive_mask = 1U << 4U;
    static constexpr std::uint32_t material_gloss_shift = 8U;

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
        std::uint32_t material{};
        VisualBlendMode blend{VisualBlendMode::opaque};
        std::uint32_t first_instance{};
        std::uint32_t instance_count{1};
        bool background{};
    };

    // Structured-buffer layout shared with the vertex shader (112 bytes).
    struct GpuInstance
    {
        std::array<float, 16> transform{};
        std::array<float, 4> tint{};
        std::array<float, 4> team_color{};
        std::uint32_t bone_offset{};
        std::uint32_t flags{};
        // Scene instance index + 1; 0 for instances that are not pickable.
        std::uint32_t pick_id{};
        std::uint32_t padding{};
    };

    static constexpr std::uint32_t background_instance_flag = 1U;
    static constexpr std::uint32_t highlighted_instance_flag = 2U;

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
    ComPtr<ID3D12DescriptorHeap> pick_rtv_heap;
    ComPtr<ID3D12Resource> pick_target;
    // Requested pixel and selection state (see request_pick / set_highlight).
    std::optional<std::array<std::uint32_t, 2>> pending_pick;
    std::uint32_t next_pick_request{1};
    std::optional<VisualPick> completed_pick;
    std::uint32_t highlighted_pick_id{};
    std::vector<VisualDebugLine> debug_lines;
    ComPtr<ID3D12PipelineState> debug_pipeline;

    // Debug overlay: a display-only Dear ImGui panel drawn last into the back buffer. Its font
    // texture takes slots from texture_slots through the backend's allocation callbacks.
    ImGuiContext* overlay_context{};
    bool overlay_backend_initialized{};
    std::vector<std::string> overlay_lines;
    std::chrono::steady_clock::time_point overlay_tick{std::chrono::steady_clock::now()};
    VisualOverlayInput overlay_input;
    ImVec2 overlay_position{10.0F, 10.0F};
    bool overlay_minimized{};
    bool overlay_wants_mouse{};

    // Frame statistics of the last recorded frame and of the last frame the GPU finished.
    ComPtr<ID3D12QueryHeap> timestamp_heap;
    std::uint64_t timestamp_frequency{};
    double last_gpu_milliseconds{};
    std::uint32_t last_draw_calls{};
    std::uint64_t last_triangles{};
    std::uint64_t last_instances{};
    std::uint64_t video_memory_usage{};
    std::uint64_t video_memory_budget{};
    std::uint32_t rtv_increment{};

    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> opaque_pipeline;
    ComPtr<ID3D12PipelineState> alpha_pipeline;
    ComPtr<ID3D12PipelineState> additive_pipeline;
    ComPtr<ID3D12PipelineState> background_opaque_pipeline;
    ComPtr<ID3D12PipelineState> terrain_pipeline;
    ComPtr<ID3D12Resource> terrain_vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW terrain_vertex_view{};
    std::uint32_t terrain_vertex_count{};
    std::uint32_t terrain_first_material{};
    // True once load_scene succeeded; before that the validation triangle is drawn.
    bool scene_loaded{};
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
    // Scene textures are pool entries owning their resource and shader-table slot; the scene keeps
    // generation-checked handles. Replaced resources retire on the direct queue's timeline.
    struct GpuTexture
    {
        ComPtr<ID3D12Resource> resource;
        std::uint32_t descriptor{};
    };
    struct TextureTag
    {};
    using TextureHandle = ResourceHandle<TextureTag>;
    HandlePool<TextureTag, GpuTexture> textures;
    DescriptorSlotAllocator texture_slots{fallback_texture_count, maximum_scene_textures - fallback_texture_count};
    FenceRetirementQueue<std::vector<ComPtr<ID3D12Resource>>> retired_resources;
    // Materials name logical textures: 0..fallback_texture_count-1 are the fallbacks, then one per
    // scene texture. Scene textures stream in under the per-frame upload budget; until a texture is
    // resident its logical id maps to a role-appropriate fallback in the frame's residency table.
    struct StreamingTexture
    {
        VisualTexture source;
        TextureHandle handle;
        std::uint32_t fallback_slot{white_texture_slot};
        std::uint64_t bytes{};
        std::string identity;
    };
    std::vector<StreamingTexture> scene_textures;
    // Scan position of the streaming queue; entries before it are resident or reused.
    std::size_t next_streaming_texture{};
    std::size_t pending_texture_count{};
    std::size_t last_reused_textures{};
    std::vector<GpuDraw> scene_draws;
    // Mesh library of the loaded scene: each mesh's draws (instance range unset) and bone count.
    std::vector<std::vector<GpuDraw>> scene_mesh_draws;
    std::vector<std::uint32_t> scene_mesh_bone_counts;
    std::uint32_t scene_bone_capacity{1};
    // Runtime instance list (set_instances); when present it replaces the scene's instances.
    std::optional<std::vector<VisualInstance>> dynamic_instances;
    std::vector<GpuInstance> dynamic_gpu_instances;
    std::vector<GpuDraw> dynamic_draws;
    std::vector<std::array<float, 16>> bone_transforms;
    ComPtr<ID3D12Resource> fallback_white_texture;
    ComPtr<ID3D12Resource> fallback_normal_texture;
    // The validation fallback keeps its own material; like the identity instance it must outlive
    // any scene because its pending state transition holds a raw resource pointer.
    ComPtr<ID3D12Resource> identity_material_buffer;
    ComPtr<ID3D12Resource> scene_material_buffer;
    VisualLightRig lighting;
    float scene_time{};
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
        shutdown_overlay();
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
            if (frame.constant_buffer && frame.constant_buffer_mapped != nullptr)
            {
                frame.constant_buffer->Unmap(0, nullptr);
                frame.constant_buffer_mapped = nullptr;
            }
            if (frame.residency_buffer && frame.residency_mapped != nullptr)
            {
                frame.residency_buffer->Unmap(0, nullptr);
                frame.residency_mapped = nullptr;
            }
            if (frame.debug_vertex_buffer && frame.debug_vertices_mapped != nullptr)
            {
                frame.debug_vertex_buffer->Unmap(0, nullptr);
                frame.debug_vertices_mapped = nullptr;
            }
            if (frame.instance_buffer && frame.instance_buffer_mapped != nullptr)
            {
                frame.instance_buffer->Unmap(0, nullptr);
                frame.instance_buffer_mapped = nullptr;
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
        create_statistics_queries();
        create_overlay();
    }

    void create_statistics_queries()
    {
        D3D12_QUERY_HEAP_DESC description{};
        description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        description.Count = frame_count * 2U;
        check_hresult(device->CreateQueryHeap(&description, IID_PPV_ARGS(&timestamp_heap)), "CreateQueryHeap(timestamps)");
        set_name(timestamp_heap.Get(), L"EAWEngine frame timestamps");
        check_hresult(direct_queue.get()->GetTimestampFrequency(&timestamp_frequency), "ID3D12CommandQueue::GetTimestampFrequency");
    }

    // The ImGui DX12 backend uploads its font texture on the direct queue and waits for that upload
    // on the CPU. This happens only when the font atlas changes (startup, first use of new glyphs),
    // never per frame; it is a documented exception to the no-CPU-wait rule for this debug overlay.
    void create_overlay()
    {
        IMGUI_CHECKVERSION();
        overlay_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(overlay_context);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplDX12_InitInfo info;
        info.Device = device.Get();
        info.CommandQueue = direct_queue.get();
        info.NumFramesInFlight = static_cast<int>(frame_count);
        info.RTVFormat = back_buffer_format;
        info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        info.UserData = this;
        info.SrvDescriptorHeap = shader_heap.Get();
        info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
        {
            static_cast<Impl*>(init->UserData)->allocate_overlay_descriptor(*cpu, *gpu);
        };
        info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init, const D3D12_CPU_DESCRIPTOR_HANDLE cpu, const D3D12_GPU_DESCRIPTOR_HANDLE)
        {
            static_cast<Impl*>(init->UserData)->free_overlay_descriptor(cpu);
        };
        if (!ImGui_ImplDX12_Init(&info))
        {
            ImGui::DestroyContext(overlay_context);
            overlay_context = nullptr;
            throw std::runtime_error("the debug overlay (Dear ImGui DX12 backend) could not be initialized");
        }
        overlay_backend_initialized = true;
    }

    void allocate_overlay_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
    {
        reclaim_texture_slots();
        const auto slot = texture_slots.allocate();
        if (!slot)
        {
            throw std::runtime_error("the D3D12 texture table has no slot for a debug overlay texture");
        }
        const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cpu = shader_heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(*slot) * increment;
        gpu = shader_heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(*slot) * increment;
    }

    void free_overlay_descriptor(const D3D12_CPU_DESCRIPTOR_HANDLE cpu)
    {
        const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto slot = static_cast<std::uint32_t>((cpu.ptr - shader_heap->GetCPUDescriptorHandleForHeapStart().ptr) / increment);
        texture_slots.release(slot, direct_queue.next_value());
    }

    void shutdown_overlay() noexcept
    {
        if (overlay_context == nullptr)
        {
            return;
        }
        ImGui::SetCurrentContext(overlay_context);
        if (overlay_backend_initialized)
        {
            try
            {
                ImGui_ImplDX12_Shutdown();
            }
            catch (...)
            {
                // Destructors must not throw; slot bookkeeping is discarded with the renderer.
            }
            overlay_backend_initialized = false;
        }
        ImGui::DestroyContext(overlay_context);
        overlay_context = nullptr;
    }

    // Draws the overlay panel into the current back buffer (call after every scene pass).
    void render_overlay(const D3D12_CPU_DESCRIPTOR_HANDLE render_target)
    {
        ImGui::SetCurrentContext(overlay_context);
        const auto now = std::chrono::steady_clock::now();
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DeltaTime = std::max(std::chrono::duration<float>(now - overlay_tick).count(), 1.0e-4F);
        overlay_tick = now;
        io.AddMousePosEvent(overlay_input.mouse_x, overlay_input.mouse_y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, overlay_input.left_button);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, overlay_input.right_button);
        if (overlay_input.wheel != 0.0F)
        {
            io.AddMouseWheelEvent(0.0F, overlay_input.wheel);
        }
        overlay_input.wheel = 0.0F;
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();

        // Expanded: a movable, resizable panel with a "-" button in its top-right corner. Minimized:
        // a compact tab at the same place with a "+" button.
        constexpr ImGuiWindowFlags common = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse;
        const auto corner_button = [](const char* label)
        {
            const auto width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0F;
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - width));
            return ImGui::SmallButton(label);
        };
        ImGui::SetNextWindowBgAlpha(0.62F);
        if (!overlay_minimized)
        {
            ImGui::SetNextWindowPos(overlay_position, ImGuiCond_Once);
            // Zero on an axis fits the contents the first time; afterwards the user's size wins.
            ImGui::SetNextWindowSize(ImVec2(0.0F, 0.0F), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("##eaw-statistics", nullptr, common | ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::TextUnformatted("Statistics");
                if (corner_button("-"))
                {
                    overlay_minimized = true;
                }
                ImGui::Separator();
                for (const auto& line : overlay_lines)
                {
                    if (line.empty())
                    {
                        ImGui::Separator();
                    }
                    else
                    {
                        ImGui::TextUnformatted(line.c_str());
                    }
                }
                overlay_position = ImGui::GetWindowPos();
            }
            ImGui::End();
        }
        else
        {
            ImGui::SetNextWindowPos(overlay_position, ImGuiCond_Always);
            if (ImGui::Begin("##eaw-statistics-minimized", nullptr, common | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
            {
                ImGui::TextUnformatted("Statistics");
                if (corner_button("+"))
                {
                    overlay_minimized = false;
                    // Reopen the panel where the tab is.
                    ImGui::SetWindowPos("##eaw-statistics", overlay_position);
                }
            }
            ImGui::End();
        }
        overlay_wants_mouse = io.WantCaptureMouse;
        ImGui::Render();
        // The overlay pipeline has one color target and no depth.
        command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());
    }

    // Reads the GPU time of the frame this context recorded last; its fence has completed.
    void collect_timestamps(FrameContext& frame)
    {
        if (!frame.timestamps_pending || timestamp_frequency == 0)
        {
            return;
        }
        frame.timestamps_pending = false;
        std::array<std::uint64_t, 2> ticks{};
        const D3D12_RANGE read_range{0, sizeof(ticks)};
        void* mapped{};
        check_hresult(frame.timestamp_readback->Map(0, &read_range, &mapped), "Map(frame timestamps)");
        std::memcpy(ticks.data(), mapped, sizeof(ticks));
        const D3D12_RANGE no_write{0, 0};
        frame.timestamp_readback->Unmap(0, &no_write);
        if (ticks[1] >= ticks[0])
        {
            last_gpu_milliseconds = static_cast<double>(ticks[1] - ticks[0]) * 1000.0 / static_cast<double>(timestamp_frequency);
        }
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
        create_constant_buffers();
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

        // Selection target: the pick id of the surface in front at every pixel.
        if (!pick_rtv_heap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC pick_heap{};
            pick_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            pick_heap.NumDescriptors = 1;
            check_hresult(device->CreateDescriptorHeap(&pick_heap, IID_PPV_ARGS(&pick_rtv_heap)), "CreateDescriptorHeap(pick RTV)");
            set_name(pick_rtv_heap.Get(), L"EAWEngine pick RTV heap");
        }
        description.Format = pick_format;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE pick_clear{};
        pick_clear.Format = pick_format;
        check_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_RENDER_TARGET, &pick_clear, IID_PPV_ARGS(&pick_target)),
                      "CreateCommittedResource(pick target)");
        set_name(pick_target.Get(), L"EAWEngine pick target");
        device->CreateRenderTargetView(pick_target.Get(), nullptr, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart());
    }

    void create_constant_buffers()
    {
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto description = buffer_description(frame_constant_bytes);
        for (std::uint32_t index = 0; index < frame_count; ++index)
        {
            auto& frame = frames[index];
            check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.constant_buffer)),
                          "CreateCommittedResource(frame constants)");
            set_name(frame.constant_buffer.Get(), L"EAWEngine frame constants " + std::to_wstring(index));
            const D3D12_RANGE no_read{0, 0};
            check_hresult(frame.constant_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.constant_buffer_mapped)), "Map(frame constants)");

            const auto residency_description = buffer_description(static_cast<std::uint64_t>(maximum_scene_textures) * sizeof(std::uint32_t));
            check_hresult(
                device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &residency_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.residency_buffer)),
                "CreateCommittedResource(frame texture residency)");
            set_name(frame.residency_buffer.Get(), L"EAWEngine frame texture residency " + std::to_wstring(index));
            check_hresult(frame.residency_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.residency_mapped)), "Map(frame texture residency)");

            const auto readback_heap = heap_properties(D3D12_HEAP_TYPE_READBACK);
            const auto readback_description = buffer_description(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            check_hresult(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&frame.pick_readback)),
                          "CreateCommittedResource(frame pick readback)");
            set_name(frame.pick_readback.Get(), L"EAWEngine frame pick readback " + std::to_wstring(index));

            const auto debug_description = buffer_description(static_cast<std::uint64_t>(maximum_debug_lines) * 2U * sizeof(DebugVertex));
            check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &debug_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.debug_vertex_buffer)),
                          "CreateCommittedResource(frame debug lines)");
            set_name(frame.debug_vertex_buffer.Get(), L"EAWEngine frame debug lines " + std::to_wstring(index));
            check_hresult(frame.debug_vertex_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.debug_vertices_mapped)), "Map(frame debug lines)");

            const auto timestamp_description = buffer_description(2U * sizeof(std::uint64_t));
            check_hresult(
                device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &timestamp_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&frame.timestamp_readback)),
                "CreateCommittedResource(frame timestamps)");
            set_name(frame.timestamp_readback.Get(), L"EAWEngine frame timestamps " + std::to_wstring(index));
        }
    }

    void create_bone_buffers(const std::uint32_t requested_count)
    {
        const auto count = std::max(requested_count, 1U);
        if (count > maximum_scene_bones)
        {
            throw std::invalid_argument("visual scene exceeds the D3D12 bone palette capacity");
        }
        bone_transforms.assign(count, identity_matrix());
        if (std::ranges::all_of(frames,
                                [count](const FrameContext& frame)
                                {
                                    return frame.bone_buffer && frame.bone_capacity >= count;
                                }))
        {
            return;
        }
        // In-flight frames may still read the old palettes, so they retire on the direct timeline.
        std::vector<ComPtr<ID3D12Resource>> replaced;
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
            if (frame.bone_buffer)
            {
                replaced.push_back(std::move(frame.bone_buffer));
            }
            check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.bone_buffer)),
                          "CreateCommittedResource(frame bone palette)");
            set_name(frame.bone_buffer.Get(), L"EAWEngine frame bone palette " + std::to_wstring(index));
            const D3D12_RANGE no_read{0, 0};
            check_hresult(frame.bone_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.bone_buffer_mapped)), "Map(frame bone palette)");
            frame.bone_capacity = count;
        }
        if (!replaced.empty())
        {
            retired_resources.push(std::move(replaced), direct_queue.next_value());
        }
    }

    // --- Pipeline library cache -----------------------------------------------------------------
    // File layout: "EAWPSO01", u64 key (shader bytecode + layout version), u64 payload size,
    // payload (ID3D12PipelineLibrary::Serialize output). Any mismatch or driver rejection discards
    // the cache; failures never stop rendering.
    static constexpr std::array<char, 8> pipeline_cache_magic{'E', 'A', 'W', 'P', 'S', 'O', '0', '1'};
    static constexpr std::uint64_t pipeline_layout_version = 2;
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
        // The material shader indexes a 4096-entry texture table, which needs resource binding
        // tier 2 (tier 1 limits a stage to 128 shader resource views).
        D3D12_FEATURE_DATA_D3D12_OPTIONS binding_options{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &binding_options, sizeof(binding_options))) || binding_options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2)
        {
            throw std::runtime_error("the D3D12 device does not support resource binding tier 2, which the sandbox texture table requires");
        }
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
        texture = upload_texture(upload, fallback, checker_texture_slot);
        const auto solid_texture = [](const std::string& name, const std::uint32_t pixel)
        {
            VisualTexture result;
            result.name = name;
            result.format = VisualTextureFormat::rgba8_unorm;
            VisualTextureMip level;
            level.width = level.height = 1;
            level.row_pitch = level.slice_pitch = sizeof(pixel);
            const auto bytes = std::as_bytes(std::span(&pixel, 1));
            level.data.assign(bytes.begin(), bytes.end());
            result.mips.push_back(std::move(level));
            return result;
        };
        fallback_white_texture = upload_texture(upload, solid_texture("fallback white", 0xFFFFFFFFU), white_texture_slot);
        // (0.5, 0.5, 1.0) encodes an unperturbed tangent-space normal; alpha 1 keeps full gloss.
        fallback_normal_texture = upload_texture(upload, solid_texture("fallback flat normal", 0xFFFF8080U), flat_normal_texture_slot);
        GpuMaterial checker_material;
        checker_material.base_texture = checker_texture_slot;
        identity_material_buffer = upload_buffer(upload, std::as_bytes(std::span(&checker_material, 1)), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, L"EAWEngine validation material buffer");
        const GpuInstance identity{visual_identity_transform, {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0};
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

    // Checks a texture's format, mip chain, and data sizes against its D3D12 copy footprint so a
    // streamed upload cannot fail on malformed data after the scene was accepted.
    D3D12_RESOURCE_DESC texture_description(const VisualTexture& source) const
    {
        if (source.mips.empty() || source.mips.front().width == 0 || source.mips.front().height == 0)
        {
            throw std::invalid_argument("visual texture '" + source.name + "' has no mip levels or a zero extent");
        }
        const auto format = dxgi_format(source.format);
        if (format == DXGI_FORMAT_UNKNOWN || source.mips.size() > std::numeric_limits<std::uint16_t>::max())
        {
            throw std::invalid_argument("visual texture '" + source.name + "' has an unsupported format or mip count");
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
        std::vector<UINT> row_counts(source.mips.size());
        std::vector<UINT64> row_sizes(source.mips.size());
        UINT64 upload_size{};
        device->GetCopyableFootprints(&description, 0, static_cast<UINT>(source.mips.size()), 0, nullptr, row_counts.data(), row_sizes.data(), &upload_size);
        if (upload_size == std::numeric_limits<UINT64>::max())
        {
            throw std::invalid_argument("visual texture '" + source.name + "' has a mip chain D3D12 cannot lay out");
        }
        for (std::size_t mip_index = 0; mip_index < source.mips.size(); ++mip_index)
        {
            const auto& mip = source.mips[mip_index];
            if (mip.row_pitch < row_sizes[mip_index] || mip.data.size() < static_cast<std::uint64_t>(mip.row_pitch) * row_counts[mip_index])
            {
                throw std::invalid_argument("visual texture '" + source.name + "' mip " + std::to_string(mip_index) + " is shorter than its D3D12 footprint");
            }
        }
        return description;
    }

    ComPtr<ID3D12Resource> upload_texture(UploadBatch& upload, const VisualTexture& source, const std::uint32_t descriptor_index)
    {
        if (descriptor_index >= maximum_scene_textures)
        {
            throw std::invalid_argument("texture descriptor slot is outside the shader table");
        }
        const auto description = texture_description(source);
        const auto format = description.Format;
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

    void load_scene(VisualScene scene)
    {
        if (scene.vertices.empty() || scene.indices.empty() || scene.draws.empty())
        {
            throw std::invalid_argument("visual scene contains no renderable geometry");
        }
        if (scene.textures.size() > texture_slots.capacity())
        {
            throw std::invalid_argument("visual scene has " + std::to_string(scene.textures.size()) + " textures; the D3D12 texture table holds " + std::to_string(texture_slots.capacity()));
        }
        if (scene.bone_count > maximum_scene_bones)
        {
            throw std::invalid_argument("visual scene has " + std::to_string(scene.bone_count) + " bones; the D3D12 bone palette holds " + std::to_string(maximum_scene_bones));
        }
        if ((scene.meshes.empty() || scene.instances.empty()) && scene.terrain.vertices.empty())
        {
            throw std::invalid_argument("visual scene has no meshes, instances, or terrain");
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
            gpu_instances.push_back({instance.transform, instance.tint, instance.team_color, instance.bone_offset,
                                     (instance.background ? background_instance_flag : 0U) | (instance.highlighted ? highlighted_instance_flag : 0U), index + 1U, 0U});
        }

        auto upload = begin_upload(L"EAWEngine scene upload");
        const auto vertex_bytes = std::as_bytes(std::span(scene.vertices));
        const auto index_bytes = std::as_bytes(std::span(scene.indices));
        auto new_vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine scene vertex buffer");
        auto new_index_buffer = upload_buffer(upload, index_bytes, D3D12_RESOURCE_STATE_INDEX_BUFFER, L"EAWEngine scene index buffer");
        if (gpu_instances.empty())
        {
            gpu_instances.push_back({visual_identity_transform, {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0});
        }
        auto new_instance_buffer = upload_buffer(upload, std::as_bytes(std::span(gpu_instances)), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"EAWEngine scene instance buffer");

        // Textures are validated now and stream in from render(). A normal map that has not arrived
        // yet samples the flat normal; everything else samples white.
        std::vector<StreamingTexture> new_textures(scene.textures.size());
        for (std::size_t index = 0; index < scene.textures.size(); ++index)
        {
            (void)texture_description(scene.textures[index]);
        }
        for (const auto& material : scene.materials)
        {
            if (material.normal_texture != invalid_visual_texture && material.normal_texture < new_textures.size())
            {
                new_textures[material.normal_texture].fallback_slot = flat_normal_texture_slot;
            }
        }

        std::vector<GpuMaterial> gpu_materials;
        gpu_materials.reserve(std::max<std::size_t>(scene.materials.size(), 1U));
        for (std::size_t index = 0; index < scene.materials.size(); ++index)
        {
            gpu_materials.push_back(gpu_material(scene, index));
        }
        if (gpu_materials.empty())
        {
            gpu_materials.emplace_back();
        }
        commit_scene(scene, std::move(upload), std::move(gpu_materials), std::move(new_textures), std::move(mesh_ranges), std::move(new_vertex_buffer), std::move(new_index_buffer),
                     std::move(new_instance_buffer), vertex_bytes.size(), index_bytes.size());
    }

    // Second half of load_scene: validates terrain and draws, submits the upload, and swaps the new
    // scene in. The replaced scene's resources retire once the next direct-queue signal completes,
    // which covers in-flight frames and the pending transitions that still name them.
    void commit_scene(VisualScene& scene, UploadBatch upload, std::vector<GpuMaterial> gpu_materials, std::vector<StreamingTexture> new_textures,
                      std::vector<std::pair<std::uint32_t, std::uint32_t>> mesh_ranges, ComPtr<ID3D12Resource> new_vertex_buffer, ComPtr<ID3D12Resource> new_index_buffer,
                      ComPtr<ID3D12Resource> new_instance_buffer, const std::size_t vertex_byte_count, const std::size_t index_byte_count)
    {
        auto new_material_buffer = upload_buffer(upload, std::as_bytes(std::span(gpu_materials)), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, L"EAWEngine scene material buffer");

        ComPtr<ID3D12Resource> new_terrain_buffer;
        const auto& terrain = scene.terrain;
        if (!terrain.vertices.empty())
        {
            if (terrain.vertices.size() % 3U != 0 || terrain.vertices.size() > std::numeric_limits<std::uint32_t>::max() || terrain.layer_count == 0 ||
                static_cast<std::uint64_t>(terrain.first_material) + terrain.layer_count > scene.materials.size())
            {
                throw std::invalid_argument("visual terrain has an invalid triangle count or layer range");
            }
            for (std::size_t index = 0; index < terrain.vertices.size(); ++index)
            {
                const auto& vertex = terrain.vertices[index];
                const bool finite = std::ranges::all_of(vertex.position,
                                                        [](const float value)
                                                        {
                                                            return std::isfinite(value);
                                                        }) &&
                                    std::ranges::all_of(vertex.normal,
                                                        [](const float value)
                                                        {
                                                            return std::isfinite(value);
                                                        }) &&
                                    std::ranges::all_of(vertex.barycentric,
                                                        [](const float value)
                                                        {
                                                            return std::isfinite(value);
                                                        });
                if (!finite || std::ranges::any_of(vertex.layers,
                                                   [&](const std::uint32_t layer)
                                                   {
                                                       return layer >= terrain.layer_count;
                                                   }))
                {
                    throw std::invalid_argument("visual terrain vertex " + std::to_string(index) + " has non-finite data or a layer outside the terrain");
                }
            }
            new_terrain_buffer = upload_buffer(upload, std::as_bytes(std::span(terrain.vertices)), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine terrain vertex buffer");
        }

        std::vector<std::vector<GpuDraw>> new_mesh_draws(scene.meshes.size());
        std::vector<std::uint32_t> new_mesh_bone_counts(scene.meshes.size());
        for (std::size_t mesh_index = 0; mesh_index < scene.meshes.size(); ++mesh_index)
        {
            const auto& mesh = scene.meshes[mesh_index];
            new_mesh_bone_counts[mesh_index] = mesh.bone_count;
            for (auto draw_index = mesh.first_draw; draw_index < mesh.first_draw + mesh.draw_count; ++draw_index)
            {
                const auto& draw = scene.draws[draw_index];
                if (draw.material >= scene.materials.size() || static_cast<std::uint64_t>(draw.first_index) + draw.index_count > scene.indices.size())
                {
                    throw std::invalid_argument("visual scene draw references data outside its buffers");
                }
                new_mesh_draws[mesh_index].push_back({draw, draw.material, scene.materials[draw.material].blend, 0, 0, false});
            }
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
                new_draws.push_back({draw, draw.material, scene.materials[draw.material].blend, first_instance, instance_count, background});
            }
        }
        // Backgrounds first in scene (mesh) order so later layers paint over earlier ones; then the
        // world by blend mode (opaque, alpha-tested, blended, additive).
        std::ranges::stable_sort(new_draws, {},
                                 [](const GpuDraw& draw)
                                 {
                                     return draw.background ? 0U : 1U + static_cast<unsigned>(draw.blend);
                                 });

        // After every validation: a failed load leaves the current scene and its palette untouched.
        create_bone_buffers(scene.bone_count);
        submit_upload(std::move(upload));
        const auto retire_value = direct_queue.next_value();
        std::vector<ComPtr<ID3D12Resource>> replaced{std::move(scene_vertex_buffer), std::move(scene_index_buffer), std::move(scene_instance_buffer), std::move(scene_material_buffer),
                                                     std::move(terrain_vertex_buffer)};
        std::erase(replaced, nullptr);
        if (!replaced.empty())
        {
            retired_resources.push(std::move(replaced), retire_value);
        }
        // A texture whose identity a resident texture of the outgoing scene already has keeps that
        // texture and descriptor slot instead of uploading again; the rest of the outgoing scene's
        // textures retire.
        std::unordered_map<std::string, std::size_t> resident_by_identity;
        for (std::size_t index = 0; index < scene_textures.size(); ++index)
        {
            const auto& entry = scene_textures[index];
            if (!entry.identity.empty() && textures.contains(entry.handle))
            {
                resident_by_identity.try_emplace(entry.identity, index);
            }
        }
        last_reused_textures = 0;
        pending_texture_count = 0;
        // Validation is complete, so the CPU texture data moves into the streaming queue.
        for (std::size_t index = 0; index < new_textures.size(); ++index)
        {
            auto& entry = new_textures[index];
            entry.identity = scene.textures[index].identity;
            if (const auto reused = resident_by_identity.find(entry.identity); !entry.identity.empty() && reused != resident_by_identity.end())
            {
                entry.handle = std::exchange(scene_textures[reused->second].handle, {});
                resident_by_identity.erase(reused);
                ++last_reused_textures;
                continue;
            }
            entry.source = std::move(scene.textures[index]);
            for (const auto& mip : entry.source.mips)
            {
                entry.bytes += mip.data.size();
            }
            ++pending_texture_count;
        }
        retire_textures(scene_textures);
        scene_textures = std::move(new_textures);
        next_streaming_texture = 0;
        scene_vertex_buffer = std::move(new_vertex_buffer);
        scene_index_buffer = std::move(new_index_buffer);
        scene_instance_buffer = std::move(new_instance_buffer);
        scene_material_buffer = std::move(new_material_buffer);
        terrain_vertex_buffer = std::move(new_terrain_buffer);
        terrain_vertex_count = static_cast<std::uint32_t>(terrain.vertices.size());
        terrain_first_material = terrain.first_material;
        scene_loaded = true;
        if (terrain_vertex_buffer)
        {
            terrain_vertex_view.BufferLocation = terrain_vertex_buffer->GetGPUVirtualAddress();
            terrain_vertex_view.SizeInBytes = static_cast<UINT>(terrain.vertices.size() * sizeof(VisualTerrainVertex));
            terrain_vertex_view.StrideInBytes = sizeof(VisualTerrainVertex);
        }
        scene_draws = std::move(new_draws);
        scene_mesh_draws = std::move(new_mesh_draws);
        scene_mesh_bone_counts = std::move(new_mesh_bone_counts);
        scene_bone_capacity = std::max(scene.bone_count, 1U);
        dynamic_instances.reset();
        highlighted_pick_id = 0;
        scene_vertex_view.BufferLocation = scene_vertex_buffer->GetGPUVirtualAddress();
        scene_vertex_view.SizeInBytes = static_cast<UINT>(vertex_byte_count);
        scene_vertex_view.StrideInBytes = sizeof(VisualVertex);
        scene_index_view.BufferLocation = scene_index_buffer->GetGPUVirtualAddress();
        scene_index_view.SizeInBytes = static_cast<UINT>(index_byte_count);
        scene_index_view.Format = DXGI_FORMAT_R32_UINT;
    }

    // Removes textures from the pool and retires their resources and descriptor slots on the
    // direct queue's next signal. Entries that never became resident are dropped; the vector is cleared.
    void retire_textures(std::vector<StreamingTexture>& entries)
    {
        if (entries.empty())
        {
            return;
        }
        const auto retire_value = direct_queue.next_value();
        std::vector<ComPtr<ID3D12Resource>> resources;
        resources.reserve(entries.size());
        for (auto& entry : entries)
        {
            if (auto texture_entry = textures.erase(entry.handle))
            {
                texture_slots.release(texture_entry->descriptor, retire_value);
                resources.push_back(std::move(texture_entry->resource));
            }
            entry.handle = {};
        }
        entries.clear();
        if (!resources.empty())
        {
            retired_resources.push(std::move(resources), retire_value);
        }
    }

    // Uploads queued scene textures in order until the per-frame byte budget is spent (always at
    // least one, so a texture larger than the budget still arrives). A texture counts as resident
    // from the frame that submits it: the direct queue waits for the copy and applies its
    // transition before that frame's draws. When the shader table is full the rest wait on their
    // fallbacks and retry next frame.
    void stream_scene_textures()
    {
        if (next_streaming_texture >= scene_textures.size())
        {
            return;
        }
        reclaim_texture_slots();
        const auto first = next_streaming_texture;
        std::vector<std::size_t> streamed;
        std::optional<UploadBatch> upload;
        std::uint64_t submitted_bytes = 0;
        try
        {
            while (next_streaming_texture < scene_textures.size())
            {
                auto& entry = scene_textures[next_streaming_texture];
                if (textures.contains(entry.handle))
                {
                    // Reused from the previous scene.
                    ++next_streaming_texture;
                    continue;
                }
                if (options.texture_upload_budget_bytes != 0 && submitted_bytes != 0 && submitted_bytes + entry.bytes > options.texture_upload_budget_bytes)
                {
                    break;
                }
                const auto slot = texture_slots.allocate();
                if (!slot)
                {
                    break;
                }
                if (!upload)
                {
                    upload = begin_upload(L"EAWEngine texture stream");
                }
                try
                {
                    auto resource = upload_texture(*upload, entry.source, *slot);
                    entry.handle = textures.insert({std::move(resource), *slot});
                }
                catch (...)
                {
                    texture_slots.release(*slot, direct_queue.next_value());
                    throw;
                }
                submitted_bytes += entry.bytes;
                streamed.push_back(next_streaming_texture);
                ++next_streaming_texture;
            }
            if (upload)
            {
                submit_upload(std::move(*upload));
            }
        }
        catch (...)
        {
            // Nothing from this batch was submitted: undo residency so no draw samples it.
            std::vector<StreamingTexture> unsent;
            for (const auto index : streamed)
            {
                unsent.push_back({{}, std::exchange(scene_textures[index].handle, {}), 0, 0, {}});
            }
            retire_textures(unsent);
            next_streaming_texture = first;
            throw;
        }
        for (const auto index : streamed)
        {
            scene_textures[index].source = {};
        }
        pending_texture_count -= std::min(pending_texture_count, streamed.size());
    }

    // Reads a pick this frame context copied earlier; its fence has completed when this runs.
    void collect_pick(FrameContext& frame)
    {
        if (!frame.pick_request)
        {
            return;
        }
        frame.pick_request.reset();
        std::uint32_t value{};
        const D3D12_RANGE read_range{0, sizeof(value)};
        void* mapped{};
        check_hresult(frame.pick_readback->Map(0, &read_range, &mapped), "Map(pick readback)");
        std::memcpy(&value, mapped, sizeof(value));
        const D3D12_RANGE no_write{0, 0};
        frame.pick_readback->Unmap(0, &no_write);
        VisualPick result;
        if (value == pick_terrain)
        {
            result.terrain = true;
        }
        else if (value != 0)
        {
            result.instance = value - 1U;
        }
        completed_pick = result;
    }

    void request_pick(const std::uint32_t x, const std::uint32_t y)
    {
        pending_pick = std::array<std::uint32_t, 2>{x, y};
    }

    void set_debug_lines(const std::span<const VisualDebugLine> lines)
    {
        const auto finite = [](const auto& values)
        {
            return std::ranges::all_of(values,
                                       [](const float value)
                                       {
                                           return std::isfinite(value);
                                       });
        };
        if (!std::ranges::all_of(lines,
                                 [&](const VisualDebugLine& line)
                                 {
                                     return finite(line.from) && finite(line.to) && finite(line.color);
                                 }))
        {
            throw std::invalid_argument("debug lines contain non-finite values");
        }
        debug_lines.assign(lines.begin(), lines.end());
    }

    void set_instances(const std::span<const VisualInstance> instances)
    {
        if (!scene_loaded)
        {
            throw std::logic_error("set_instances needs a loaded scene (its meshes are the mesh library)");
        }
        if (instances.size() >= std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("too many visual instances for one frame");
        }
        for (std::size_t index = 0; index < instances.size(); ++index)
        {
            const auto& instance = instances[index];
            if (instance.mesh >= scene_mesh_draws.size())
            {
                throw std::invalid_argument("visual instance " + std::to_string(index) + " references mesh " + std::to_string(instance.mesh) + ", but the scene has " +
                                            std::to_string(scene_mesh_draws.size()));
            }
            if (static_cast<std::uint64_t>(instance.bone_offset) + scene_mesh_bone_counts[instance.mesh] > scene_bone_capacity)
            {
                throw std::invalid_argument("visual instance " + std::to_string(index) + " bone range exceeds the scene bone palette");
            }
            const auto finite = [](const auto& values)
            {
                return std::ranges::all_of(values,
                                           [](const float value)
                                           {
                                               return std::isfinite(value);
                                           });
            };
            if (!finite(instance.transform) || !finite(instance.tint) || !finite(instance.team_color))
            {
                throw std::invalid_argument("visual instance " + std::to_string(index) + " has non-finite values");
            }
        }
        dynamic_instances.emplace(instances.begin(), instances.end());
    }

    // Groups instances by mesh and layer into contiguous ranges and builds the draws, ordered like a
    // loaded scene: backgrounds first, then the world by blend mode. Pick ids are list index + 1.
    void batch_instances(const std::vector<VisualInstance>& instances, std::vector<GpuInstance>& gpu_instances, std::vector<GpuDraw>& draws) const
    {
        std::vector<std::uint32_t> order(instances.size());
        for (std::uint32_t index = 0; index < order.size(); ++index)
        {
            order[index] = index;
        }
        const auto range_key = [&](const std::uint32_t index)
        {
            return static_cast<std::size_t>(instances[index].mesh) * 2U + (instances[index].background ? 1U : 0U);
        };
        std::ranges::stable_sort(order, {}, range_key);
        gpu_instances.clear();
        draws.clear();
        std::size_t range_start = 0;
        for (std::size_t position = 0; position <= order.size(); ++position)
        {
            if (position < order.size())
            {
                const auto& instance = instances[order[position]];
                gpu_instances.push_back({instance.transform, instance.tint, instance.team_color, instance.bone_offset,
                                         (instance.background ? background_instance_flag : 0U) | (instance.highlighted ? highlighted_instance_flag : 0U), order[position] + 1U, 0U});
            }
            if (position == order.size() || (position > range_start && range_key(order[position]) != range_key(order[range_start])))
            {
                if (position > range_start)
                {
                    const auto& first = instances[order[range_start]];
                    for (auto draw : scene_mesh_draws[first.mesh])
                    {
                        draw.first_instance = static_cast<std::uint32_t>(range_start);
                        draw.instance_count = static_cast<std::uint32_t>(position - range_start);
                        draw.background = first.background;
                        draws.push_back(draw);
                    }
                }
                range_start = position;
            }
        }
        std::ranges::stable_sort(draws, {},
                                 [](const GpuDraw& draw)
                                 {
                                     return draw.background ? 0U : 1U + static_cast<unsigned>(draw.blend);
                                 });
        if (gpu_instances.empty())
        {
            gpu_instances.push_back({visual_identity_transform, {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0});
        }
    }

    // Writes the dynamic instances into this frame context's buffer, growing it when needed (the
    // outgrown buffer retires on the direct timeline).
    void write_dynamic_instances(FrameContext& frame)
    {
        batch_instances(*dynamic_instances, dynamic_gpu_instances, dynamic_draws);
        if (dynamic_gpu_instances.size() > frame.instance_capacity)
        {
            if (frame.instance_buffer)
            {
                frame.instance_buffer->Unmap(0, nullptr);
                frame.instance_buffer_mapped = nullptr;
                retired_resources.push({std::move(frame.instance_buffer)}, direct_queue.next_value());
            }
            const auto capacity = std::max<std::size_t>({dynamic_gpu_instances.size(), frame.instance_capacity * 2U, 256U});
            const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
            const auto description = buffer_description(static_cast<std::uint64_t>(capacity) * sizeof(GpuInstance));
            check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.instance_buffer)),
                          "CreateCommittedResource(frame instances)");
            set_name(frame.instance_buffer.Get(), L"EAWEngine frame instances");
            const D3D12_RANGE no_read{0, 0};
            check_hresult(frame.instance_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.instance_buffer_mapped)), "Map(frame instances)");
            frame.instance_capacity = capacity;
        }
        std::memcpy(frame.instance_buffer_mapped, dynamic_gpu_instances.data(), dynamic_gpu_instances.size() * sizeof(GpuInstance));
    }

    void set_overlay_text(const std::span<const std::string> lines)
    {
        overlay_lines.assign(lines.begin(), lines.end());
        if (overlay_lines.empty())
        {
            overlay_wants_mouse = false;
        }
    }

    void set_overlay_input(const VisualOverlayInput& input)
    {
        // Wheel steps accumulate until the overlay consumes them in the next rendered frame.
        const auto wheel = overlay_input.wheel + input.wheel;
        overlay_input = input;
        overlay_input.wheel = std::isfinite(wheel) ? wheel : 0.0F;
    }

    void set_highlight(const std::optional<std::uint32_t> instance)
    {
        highlighted_pick_id = instance && *instance < std::numeric_limits<std::uint32_t>::max() ? *instance + 1U : 0U;
    }

    // Writes this frame's logical-texture-to-slot table. Called once the frame context is reusable.
    void write_texture_residency(FrameContext& frame) const
    {
        auto* table = frame.residency_mapped;
        for (std::uint32_t slot = 0; slot < fallback_texture_count; ++slot)
        {
            table[slot] = slot;
        }
        for (std::size_t index = 0; index < scene_textures.size(); ++index)
        {
            const auto& entry = scene_textures[index];
            const auto* resident = textures.get(entry.handle);
            table[fallback_texture_count + index] = resident != nullptr ? resident->descriptor : entry.fallback_slot;
        }
    }

    // Returns completed texture slots to the allocator and points them at a null view so the shader
    // table never names a released resource.
    void reclaim_texture_slots()
    {
        const auto reclaimed = texture_slots.reclaim(direct_queue.completed_value());
        if (reclaimed.empty())
        {
            return;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};
        null_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_view.Texture2D.MipLevels = 1;
        const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (const auto slot : reclaimed)
        {
            auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(slot) * increment;
            device->CreateShaderResourceView(nullptr, &null_view, handle);
        }
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

    // Validates one scene material and converts it to the shader layout.
    static GpuMaterial gpu_material(const VisualScene& scene, const std::size_t index)
    {
        const auto& material = scene.materials[index];
        const auto slot = [&](const std::uint32_t texture, const std::uint32_t fallback, const char* role)
        {
            if (texture == invalid_visual_texture)
            {
                return fallback;
            }
            if (texture >= scene.textures.size())
            {
                throw std::invalid_argument("visual material " + std::to_string(index) + " (" + material.name + ") references a missing " + role + " texture");
            }
            return fallback_texture_count + texture;
        };
        const auto finite = [](const auto& values)
        {
            return std::ranges::all_of(values,
                                       [](const float value)
                                       {
                                           return std::isfinite(value);
                                       });
        };
        if (!finite(material.tint) || !finite(material.diffuse) || !finite(material.specular) || !finite(material.emissive) || !finite(material.uv_scroll) ||
            !std::isfinite(material.alpha_test_threshold) || !std::isfinite(material.uv_scale))
        {
            throw std::invalid_argument("visual material " + std::to_string(index) + " (" + material.name + ") has non-finite parameters");
        }
        GpuMaterial result;
        result.tint = material.tint;
        result.diffuse = material.diffuse;
        result.specular = {material.specular[0], material.specular[1], material.specular[2], material.uv_scale};
        result.emissive = {material.emissive[0], material.emissive[1], material.emissive[2], 0.0F};
        result.uv_scroll = material.uv_scroll;
        result.alpha_cutoff = material.alpha_test_threshold;
        result.base_texture = slot(material.base_color_texture, white_texture_slot, "base color");
        result.normal_texture = slot(material.normal_texture, flat_normal_texture_slot, "normal");
        result.gloss_texture = slot(material.gloss_texture, white_texture_slot, "gloss");
        result.lighting = static_cast<std::uint32_t>(material.lighting);
        result.flags = (material.blend == VisualBlendMode::alpha_test ? material_alpha_test : 0U) | (material.blend == VisualBlendMode::alpha_blend ? material_alpha_blend : 0U) |
                       (material.blend == VisualBlendMode::additive ? material_additive : 0U) | (material.team_color_mask ? material_team_color : 0U) |
                       (material.emissive_mask ? material_emissive_mask : 0U) | (static_cast<std::uint32_t>(material.gloss) << material_gloss_shift);
        return result;
    }

    void set_lighting(const VisualLightRig& next)
    {
        const auto finite = [](const std::array<float, 3>& value)
        {
            return std::ranges::all_of(value,
                                       [](const float component)
                                       {
                                           return std::isfinite(component);
                                       });
        };
        const auto valid_light = [&](const VisualDirectionalLight& light)
        {
            const auto& direction = light.direction;
            return finite(direction) && finite(light.color) && direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2] > 1.0e-12F;
        };
        if (!valid_light(next.key) || !valid_light(next.fills[0]) || !valid_light(next.fills[1]) || !finite(next.key_specular) || !finite(next.ambient))
        {
            throw std::invalid_argument("visual light rig contains non-finite values or a zero light direction");
        }
        lighting = next;
    }

    void set_scene_time(const float seconds)
    {
        if (!std::isfinite(seconds))
        {
            throw std::invalid_argument("scene time is not finite");
        }
        scene_time = seconds;
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
        retire_completed_resources();
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
        stream_scene_textures();
        write_texture_residency(frame);
        const bool dynamic = scene_loaded && dynamic_instances.has_value();
        if (dynamic)
        {
            write_dynamic_instances(frame);
        }
        collect_pick(frame);
        collect_timestamps(frame);
        if (frame_number % 30U == 0)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
            if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
            {
                video_memory_usage = memory.CurrentUsage;
                video_memory_budget = memory.Budget;
            }
        }
        std::uint32_t draw_calls = 0;
        std::uint64_t triangles = 0;
        std::uint64_t instances_drawn = 0;
        const auto debug_line_count = std::min(debug_lines.size(), maximum_debug_lines);
        auto* debug_vertices = reinterpret_cast<DebugVertex*>(frame.debug_vertices_mapped);
        for (std::size_t index = 0; index < debug_line_count; ++index)
        {
            const auto& line = debug_lines[index];
            debug_vertices[index * 2U] = {line.from, line.color};
            debug_vertices[index * 2U + 1U] = {line.to, line.color};
        }
        // This frame context's fence has completed above, so its constants can be rewritten.
        FrameConstants constants{};
        constants.view_projection = camera_matrix();
        constants.camera_position_time = {camera.eye[0], camera.eye[1], camera.eye[2], scene_time};
        const auto unit = [](const std::array<float, 3>& value)
        {
            const auto length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
            return std::array<float, 4>{value[0] / length, value[1] / length, value[2] / length, 0.0F};
        };
        const auto rgb = [](const std::array<float, 3>& value)
        {
            return std::array<float, 4>{value[0], value[1], value[2], 0.0F};
        };
        constants.key_direction = unit(lighting.key.direction);
        constants.key_diffuse = rgb(lighting.key.color);
        constants.key_specular = rgb(lighting.key_specular);
        constants.ambient = rgb(lighting.ambient);
        for (std::size_t index = 0; index < lighting.fills.size(); ++index)
        {
            constants.fill_direction[index] = unit(lighting.fills[index].direction);
            constants.fill_diffuse[index] = rgb(lighting.fills[index].color);
        }
        constants.selection = {highlighted_pick_id, 0, 0, 0};
        std::memcpy(frame.constant_buffer_mapped, &constants, sizeof(constants));

        check_hresult(frame.allocator->Reset(), "ID3D12CommandAllocator::Reset");
        check_hresult(command_list->Reset(frame.allocator.Get(), opaque_pipeline.Get()), "ID3D12GraphicsCommandList::Reset");
        command_list->EndQuery(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U);

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
        const std::array targets{render_target, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart()};
        command_list->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), FALSE, &depth_target);
        constexpr std::array clear_color{0.018F, 0.031F, 0.043F, 1.0F};
        constexpr std::array clear_pick{0.0F, 0.0F, 0.0F, 0.0F};
        command_list->ClearRenderTargetView(render_target, clear_color.data(), 0, nullptr);
        command_list->ClearRenderTargetView(targets[1], clear_pick.data(), 0, nullptr);
        command_list->ClearDepthStencilView(depth_target, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);

        ID3D12DescriptorHeap* descriptor_heaps[]{shader_heap.Get()};
        command_list->SetDescriptorHeaps(1, descriptor_heaps);
        command_list->SetGraphicsRootSignature(root_signature.Get());
        // Root parameters (legacy_common.hlsli): 0 frame CBV, 1 texture table, 2 bones, 3 instances,
        // 4 materials, 5 draw constants (first instance, material), 6 texture residency.
        command_list->SetGraphicsRootConstantBufferView(0, frame.constant_buffer->GetGPUVirtualAddress());
        command_list->SetGraphicsRootDescriptorTable(1, shader_heap->GetGPUDescriptorHandleForHeapStart());
        command_list->SetGraphicsRootShaderResourceView(2, frame.bone_buffer->GetGPUVirtualAddress());
        command_list->SetGraphicsRootShaderResourceView(3, (dynamic ? frame.instance_buffer : scene_loaded ? scene_instance_buffer : identity_instance_buffer)->GetGPUVirtualAddress());
        command_list->SetGraphicsRootShaderResourceView(4, (scene_loaded ? scene_material_buffer : identity_material_buffer)->GetGPUVirtualAddress());
        command_list->SetGraphicsRootShaderResourceView(6, frame.residency_buffer->GetGPUVirtualAddress());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (!scene_loaded)
        {
            command_list->SetPipelineState(opaque_pipeline.Get());
            const std::array<std::uint32_t, 2> draw_constants{0U, 0U};
            command_list->SetGraphicsRoot32BitConstants(5, 2, draw_constants.data(), 0);
            command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
            command_list->DrawInstanced(3, 1, 0, 0);
            ++draw_calls;
            ++triangles;
            ++instances_drawn;
        }
        else
        {
            bool terrain_drawn = terrain_vertex_count == 0;
            const auto draw_terrain = [&]
            {
                command_list->SetPipelineState(terrain_pipeline.Get());
                const std::array<std::uint32_t, 2> terrain_constants{0U, terrain_first_material};
                command_list->SetGraphicsRoot32BitConstants(5, 2, terrain_constants.data(), 0);
                command_list->IASetVertexBuffers(0, 1, &terrain_vertex_view);
                command_list->DrawInstanced(terrain_vertex_count, 1, 0, 0);
                ++draw_calls;
                triangles += terrain_vertex_count / 3U;
                command_list->IASetVertexBuffers(0, 1, &scene_vertex_view);
                terrain_drawn = true;
            };
            command_list->IASetVertexBuffers(0, 1, &scene_vertex_view);
            command_list->IASetIndexBuffer(&scene_index_view);
            for (const auto& draw : dynamic ? dynamic_draws : scene_draws)
            {
                if (!terrain_drawn && !draw.background)
                {
                    draw_terrain();
                }
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
                const std::array<std::uint32_t, 2> draw_constants{draw.first_instance, draw.material};
                command_list->SetGraphicsRoot32BitConstants(5, 2, draw_constants.data(), 0);
                command_list->DrawIndexedInstanced(draw.geometry.index_count, draw.instance_count, draw.geometry.first_index, draw.geometry.base_vertex, 0);
                ++draw_calls;
                triangles += static_cast<std::uint64_t>(draw.geometry.index_count / 3U) * draw.instance_count;
                instances_drawn += draw.instance_count;
            }
            if (!terrain_drawn)
            {
                draw_terrain();
            }
        }

        if (debug_line_count != 0)
        {
            command_list->SetPipelineState(debug_pipeline.Get());
            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            D3D12_VERTEX_BUFFER_VIEW debug_view{};
            debug_view.BufferLocation = frame.debug_vertex_buffer->GetGPUVirtualAddress();
            debug_view.SizeInBytes = static_cast<UINT>(debug_line_count * 2U * sizeof(DebugVertex));
            debug_view.StrideInBytes = sizeof(DebugVertex);
            command_list->IASetVertexBuffers(0, 1, &debug_view);
            command_list->DrawInstanced(static_cast<UINT>(debug_line_count * 2U), 1, 0, 0);
            ++draw_calls;
        }
        last_draw_calls = draw_calls;
        last_triangles = triangles;
        last_instances = instances_drawn;
        if (!overlay_lines.empty() && overlay_backend_initialized)
        {
            render_overlay(render_target);
        }

        // A requested pick copies one texel; it is read when this frame's fence completes.
        if (pending_pick)
        {
            const auto [x, y] = *pending_pick;
            pending_pick.reset();
            auto to_copy = transition_barrier(pick_target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            command_list->ResourceBarrier(1, &to_copy);
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = pick_target.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = frame.pick_readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Footprint = {pick_format, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
            const D3D12_BOX texel{std::min(x, width - 1), std::min(y, height - 1), 0, std::min(x, width - 1) + 1, std::min(y, height - 1) + 1, 1};
            command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, &texel);
            auto to_target = transition_barrier(pick_target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            command_list->ResourceBarrier(1, &to_target);
            frame.pick_request = next_pick_request++;
        }

        auto to_present = transition_barrier(frame.back_buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        command_list->ResourceBarrier(1, &to_present);
        command_list->EndQuery(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U + 1U);
        command_list->ResolveQueryData(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U, 2, frame.timestamp_readback.Get(), 0);
        frame.timestamps_pending = true;
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
        pick_target.Reset();
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
            retire_completed_resources();
            for (auto& frame : frames)
            {
                if (frame.pick_readback)
                {
                    collect_pick(frame);
                }
            }
        }
        if (error)
        {
            std::rethrow_exception(error);
        }
    }

    void retire_completed_resources()
    {
        retire_completed_uploads();
        retired_resources.retire(direct_queue.completed_value());
        if (shader_heap)
        {
            reclaim_texture_slots();
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

void D3d12Renderer::request_pick(const std::uint32_t x, const std::uint32_t y)
{
    impl_->request_pick(x, y);
}

std::optional<VisualPick> D3d12Renderer::take_pick()
{
    return std::exchange(impl_->completed_pick, std::nullopt);
}

void D3d12Renderer::set_highlight(const std::optional<std::uint32_t> instance)
{
    impl_->set_highlight(instance);
}

void D3d12Renderer::set_debug_lines(const std::span<const VisualDebugLine> lines)
{
    impl_->set_debug_lines(lines);
}

void D3d12Renderer::set_instances(const std::span<const VisualInstance> instances)
{
    impl_->set_instances(instances);
}

void D3d12Renderer::set_overlay_text(const std::span<const std::string> lines)
{
    impl_->set_overlay_text(lines);
}

void D3d12Renderer::set_overlay_input(const VisualOverlayInput& input)
{
    impl_->set_overlay_input(input);
}

bool D3d12Renderer::overlay_wants_mouse() const noexcept
{
    return impl_->overlay_wants_mouse;
}

std::size_t D3d12Renderer::max_debug_lines() noexcept
{
    return maximum_debug_lines;
}

void D3d12Renderer::load_scene(VisualScene scene)
{
    impl_->load_scene(std::move(scene));
}

void D3d12Renderer::set_bone_transforms(const std::span<const std::array<float, 16>> transforms)
{
    impl_->set_bone_transforms(transforms);
}

void D3d12Renderer::set_camera(const VisualCamera& camera)
{
    impl_->set_camera(camera);
}

void D3d12Renderer::set_lighting(const VisualLightRig& lighting)
{
    impl_->set_lighting(lighting);
}

void D3d12Renderer::set_scene_time(const float seconds)
{
    impl_->set_scene_time(seconds);
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
        .live_textures = impl_->textures.size(),
        .free_texture_slots = impl_->texture_slots.free_count(),
        .pending_texture_slots = impl_->texture_slots.pending_count(),
        .pending_retirements = impl_->retired_resources.size(),
        .pending_texture_uploads = impl_->pending_texture_count,
        .reused_textures = impl_->last_reused_textures,
        .dropped_debug_lines = impl_->debug_lines.size() - std::min(impl_->debug_lines.size(), maximum_debug_lines),
        .draw_calls = impl_->last_draw_calls,
        .triangles = impl_->last_triangles,
        .instances = impl_->last_instances,
        .gpu_frame_milliseconds = impl_->last_gpu_milliseconds,
        .video_memory_usage = impl_->video_memory_usage,
        .video_memory_budget = impl_->video_memory_budget,
        .texture_slot_capacity = impl_->texture_slots.capacity(),
        .width = impl_->width,
        .height = impl_->height,
        .tearing = !impl_->options.vsync && impl_->tearing_supported,
    };
}

} // namespace eaw
