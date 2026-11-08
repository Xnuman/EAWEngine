#pragma once

// Direct3D 12 helpers of the renderer: HRESULT diagnostics, object names, the command queue with its
// fence timeline, handles, resource descriptions, adapter selection, and debug-layer setup. Internal
// to src/render.

#include "eaw/d3d12_renderer.hpp"
#include "eaw/fatal_error.hpp"
#include "eaw/gpu_resource_lifetime.hpp"
#include "eaw/status.hpp"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <source_location>
#include <sstream>
#include <span>
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

namespace eaw::d3d12 {

using Microsoft::WRL::ComPtr;

inline constexpr std::uint32_t frame_count = 3;

// Bone transforms per frame palette (16 MiB of matrices per frame buffer). A temporary, explicit
// implementation limit: scenes and set_bone_transforms beyond it are rejected or asserted.
inline constexpr std::uint32_t maximum_bone_transforms = 1U << 18U;

// Size of the shader-visible texture table (matches numDescriptors in sandbox.hlsl). The first
// fallback_texture_count slots hold the validation checker, white, and a flat normal map.
inline constexpr std::uint32_t maximum_scene_textures = 4096;

inline constexpr std::uint32_t checker_texture_slot = 0;

inline constexpr std::uint32_t white_texture_slot = 1;

inline constexpr std::uint32_t flat_normal_texture_slot = 2;

inline constexpr std::uint32_t fallback_texture_count = 3;

// Per-frame constant buffers are 256-byte aligned, as D3D12 requires for CBVs.
inline constexpr std::uint64_t frame_constant_bytes = 256;

inline constexpr DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

// Pick target texel: (pick id, depth-buffer value bits) so a click also yields a world position.
inline constexpr DXGI_FORMAT pick_format = DXGI_FORMAT_R32G32_UINT;

// Temporary per-frame debug line capacity (an implementation limit, diagnosed through stats).
inline constexpr std::size_t maximum_debug_lines = 32768;

inline constexpr std::uint32_t pick_terrain = 0xFFFFFFFFU;

// Picks copied per frame; further requests wait for the next frame (none are dropped).
inline constexpr std::uint32_t maximum_picks_per_frame = 8;

std::string hresult_text(const HRESULT result);

std::string hresult_failure(const HRESULT result, const std::string_view operation);

// Startup and creation failures: reported to the caller of D3d12Renderer::create.
Status hresult_status(const HRESULT result, const std::string_view operation);

// Failures after startup (frame recording, uploads, synchronization) are unrecoverable.
void check_hresult(const HRESULT result, const std::string_view operation, const std::source_location location = std::source_location::current());

void set_name(ID3D12Object* object, const std::wstring_view name);

std::string to_utf8(const std::wstring_view value);

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
    [[nodiscard]] Status initialize(ID3D12Device* device, const D3D12_COMMAND_LIST_TYPE type, const wchar_t* name)
    {
        D3D12_COMMAND_QUEUE_DESC description{};
        description.Type = type;
        description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        EAW_TRY(hresult_status(device->CreateCommandQueue(&description, IID_PPV_ARGS(&queue_)), "CreateCommandQueue"));
        EAW_TRY(hresult_status(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence"));

        set_name(queue_.Get(), name);
        const std::wstring fence_name = std::wstring(name) + L" fence";
        set_name(fence_.Get(), fence_name);

        event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (event_.get() == nullptr)
        {
            return make_error("CreateEventW failed for a D3D12 fence");
        }
        return Status::success();
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
            fatal_error("waiting for a D3D12 fence failed");
        }
    }

    void flush()
    {
        wait_cpu(signal());
    }

    // Best-effort flush for shutdown and failed startup: reports failure instead of ending the
    // process, so a removed device cannot turn cleanup into a fatal error.
    bool try_flush() noexcept
    {
        const auto value = next_fence_value_++;
        if (FAILED(queue_->Signal(fence_.Get(), value)))
        {
            return false;
        }
        if (fence_->GetCompletedValue() >= value)
        {
            return true;
        }
        return SUCCEEDED(fence_->SetEventOnCompletion(value, event_.get())) && WaitForSingleObjectEx(event_.get(), INFINITE, FALSE) == WAIT_OBJECT_0;
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

D3D12_HEAP_PROPERTIES heap_properties(const D3D12_HEAP_TYPE type);

D3D12_RESOURCE_DESC buffer_description(const std::uint64_t size);

D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource* resource, const D3D12_RESOURCE_STATES before, const D3D12_RESOURCE_STATES after);

std::array<float, 16> identity_matrix();

bool query_tearing_support(IDXGIFactory7* factory);

DXGI_FORMAT dxgi_format(const VisualTextureFormat format);

Result<ComPtr<IDXGIAdapter4>> select_adapter(IDXGIFactory7* factory, const bool use_warp);

Status configure_debugging(const bool gpu_validation);

#if defined(_DEBUG)
void CALLBACK debug_message_callback(const D3D12_MESSAGE_CATEGORY, const D3D12_MESSAGE_SEVERITY severity, const D3D12_MESSAGE_ID id, const LPCSTR description, void*);
#endif

} // namespace eaw::d3d12
