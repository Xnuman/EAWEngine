#include "d3d12_support.hpp"

namespace eaw::d3d12 {

std::string hresult_text(const HRESULT result)
{
    std::ostringstream stream;
    stream << "HRESULT 0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(result);
    return stream.str();
}

std::string hresult_failure(const HRESULT result, const std::string_view operation)
{
    return std::string(operation) + " failed (" + hresult_text(result) + ")";
}

Status hresult_status(const HRESULT result, const std::string_view operation)
{
    if (FAILED(result))
    {
        return make_error(hresult_failure(result, operation));
    }
    return Status::success();
}

void check_hresult(const HRESULT result, const std::string_view operation, const std::source_location location)
{
    if (FAILED(result))
    {
        fatal_error(hresult_failure(result, operation), location);
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

Result<ComPtr<IDXGIAdapter4>> select_adapter(IDXGIFactory7* factory, const bool use_warp)
{
    ComPtr<IDXGIAdapter4> adapter;
    if (use_warp)
    {
        EAW_TRY(hresult_status(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "IDXGIFactory::EnumWarpAdapter"));
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
        EAW_TRY(hresult_status(result, "IDXGIFactory6::EnumAdapterByGpuPreference"));

        DXGI_ADAPTER_DESC3 description{};
        EAW_TRY(hresult_status(candidate->GetDesc3(&description), "IDXGIAdapter4::GetDesc3"));
        if ((description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            return candidate;
        }
    }
    return make_error("no Direct3D 12 feature-level 12_0 adapter was found (use --warp to test with WARP)");
}

Status configure_debugging(const bool gpu_validation)
{
#if !defined(_DEBUG)
    if (!gpu_validation)
    {
        return Status::success();
    }
#endif

    ComPtr<ID3D12Debug> debug;
    EAW_TRY(hresult_status(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12GetDebugInterface"));
    debug->EnableDebugLayer();
    if (gpu_validation)
    {
        ComPtr<ID3D12Debug1> validation;
        EAW_TRY(hresult_status(debug.As(&validation), "query ID3D12Debug1"));
        validation->SetEnableGPUBasedValidation(TRUE);
        validation->SetEnableSynchronizedCommandQueueValidation(TRUE);
    }

    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings))))
    {
        dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
    return Status::success();
}

#if defined(_DEBUG)
void CALLBACK debug_message_callback(const D3D12_MESSAGE_CATEGORY, const D3D12_MESSAGE_SEVERITY severity, const D3D12_MESSAGE_ID id, const LPCSTR description, void*)
{
    std::fprintf(stderr, "D3D12 validation [%u, severity %u]: %s\n", static_cast<unsigned>(id), static_cast<unsigned>(severity), description != nullptr ? description : "(no description)");
}
#endif

} // namespace eaw::d3d12
