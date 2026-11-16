// The FFT ocean on the GPU (ocean.hlsl; docs/water-research.md, "The ocean plan", stage 2): the
// cascades' buffers and textures, the starting spectra uploaded when the sea state changes, and the
// passes that evolve them and copy the results into the textures the water shaders read.

#include "d3d12_renderer_impl.hpp"

#include <algorithm>
#include <cstring>
#include <span>

namespace eaw {

namespace {

constexpr std::uint32_t ocean_cells = ocean_cascade_size * ocean_cascade_size;
// Each cell's two values of two complex numbers (ocean.hlsl, `fields`), and a half4 texel.
constexpr std::uint64_t ocean_field_bytes = 2U * 4U * sizeof(float);
constexpr std::uint64_t ocean_texel_bytes = 4U * 2U;
constexpr DXGI_FORMAT ocean_texture_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
// The seed of the first cascade's sea (each further cascade takes the next one); an engine choice.
constexpr std::uint64_t ocean_seed = 0x0CEA4ULL;

} // namespace

void D3d12Renderer::Impl::prepare_ocean(const OceanSeaState& state)
{
    const auto cascades = static_cast<std::uint64_t>(ocean_cascades.size());
    if (!ocean_fields)
    {
        const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const auto make_buffer = [&](ComPtr<ID3D12Resource>& buffer, const std::uint64_t bytes, const wchar_t* name)
        {
            auto description = buffer_description(bytes);
            description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer)), device,
                                 "CreateCommittedResource(ocean buffer)");
            set_name(buffer.Get(), name);
        };
        make_buffer(ocean_fields, cascades * ocean_cells * ocean_field_bytes, L"EAWEngine ocean fields");
        make_buffer(ocean_packed, cascades * 2U * ocean_cells * ocean_texel_bytes, L"EAWEngine ocean packed");
        const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto make_texture = [&](ComPtr<ID3D12Resource>& texture, std::uint32_t& slot, const wchar_t* name)
        {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = ocean_cascade_size;
            description.Height = ocean_cascade_size;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = ocean_texture_format;
            description.SampleDesc.Count = 1;
            check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description,
                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texture)),
                                 device, "CreateCommittedResource(ocean texture)");
            set_name(texture.Get(), name);
            const auto allocated = texture_slots.allocate();
            EAW_ASSERT_MSG(allocated.has_value(), "the D3D12 texture table has no slot for an ocean texture");
            slot = *allocated;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = ocean_texture_format;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1;
            auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(slot) * increment;
            device->CreateShaderResourceView(texture.Get(), &view, handle);
        };
        for (std::size_t cascade = 0; cascade < ocean_cascades.size(); ++cascade)
        {
            make_texture(ocean_displacement[cascade], ocean_displacement_slots[cascade], L"EAWEngine ocean displacement");
            make_texture(ocean_derivatives[cascade], ocean_derivative_slots[cascade], L"EAWEngine ocean derivatives");
        }
    }
    if (ocean_sea_state && *ocean_sea_state == state)
    {
        return;
    }
    std::vector<OceanSpectrumCell> cells;
    cells.reserve(static_cast<std::size_t>(cascades) * ocean_cells);
    for (std::size_t cascade = 0; cascade < ocean_cascades.size(); ++cascade)
    {
        const auto spectrum = ocean_initial_spectrum(state, ocean_cascades[cascade], ocean_cascade_size, ocean_seed + cascade);
        cells.insert(cells.end(), spectrum.begin(), spectrum.end());
    }
    auto upload = begin_upload(L"EAWEngine ocean spectrum upload");
    auto spectrum = upload_buffer(upload, std::as_bytes(std::span(cells)), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"EAWEngine ocean spectrum");
    submit_upload(std::move(upload));
    std::vector<ComPtr<ID3D12Resource>> replaced{std::move(ocean_spectrum)};
    retire_uploaded(std::move(replaced), direct_queue.next_value());
    ocean_spectrum = std::move(spectrum);
    ocean_sea_state = state;
}

void D3d12Renderer::Impl::record_ocean(const float seconds, const float gravity, const float choppiness, const float foam_threshold)
{
    // The longest step foam fades by at once (a paused or stalled scene); an engine choice.
    constexpr float maximum_foam_step = 0.25F;
    struct OceanConstants
    {
        float time{};
        float gravity{};
        float choppiness{};
        std::uint32_t cascade_count{};
        std::array<float, 4> patch_sizes{};
        float delta{};
        float foam_threshold{};
        std::array<float, 2> unused{};
    };
    const auto delta = ocean_last_seconds ? std::clamp(seconds - *ocean_last_seconds, 0.0F, maximum_foam_step) : 0.0F;
    ocean_last_seconds = seconds;
    OceanConstants constants{seconds, gravity, choppiness, static_cast<std::uint32_t>(ocean_cascades.size()), {}, delta, foam_threshold, {}};
    for (std::size_t cascade = 0; cascade < ocean_cascades.size(); ++cascade)
    {
        constants.patch_sizes[cascade] = ocean_cascades[cascade].patch_size;
    }
    command_list->SetComputeRootSignature(ocean_root_signature.Get());
    command_list->SetComputeRoot32BitConstants(0, sizeof(OceanConstants) / sizeof(std::uint32_t), &constants, 0);
    command_list->SetComputeRootShaderResourceView(1, ocean_spectrum->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(2, ocean_fields->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(3, ocean_packed->GetGPUVirtualAddress());
    constexpr std::uint32_t cell_group = 16;
    const auto cascades = static_cast<UINT>(ocean_cascades.size());
    const auto between = uav_barrier(ocean_fields.Get());
    command_list->SetPipelineState(ocean_evolve_pipeline.Get());
    command_list->Dispatch(ocean_cascade_size / cell_group, ocean_cascade_size / cell_group, cascades);
    command_list->ResourceBarrier(1, &between);
    command_list->SetPipelineState(ocean_rows_pipeline.Get());
    command_list->Dispatch(ocean_cascade_size, cascades, 1);
    command_list->ResourceBarrier(1, &between);
    command_list->SetPipelineState(ocean_columns_pipeline.Get());
    command_list->Dispatch(ocean_cascade_size, cascades, 1);
    command_list->ResourceBarrier(1, &between);
    command_list->SetPipelineState(ocean_pack_pipeline.Get());
    command_list->Dispatch(ocean_cascade_size / cell_group, ocean_cascade_size / cell_group, cascades);
}

void D3d12Renderer::Impl::copy_ocean()
{
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = ocean_packed.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Footprint.Format = ocean_texture_format;
    source.PlacedFootprint.Footprint.Width = ocean_cascade_size;
    source.PlacedFootprint.Footprint.Height = ocean_cascade_size;
    source.PlacedFootprint.Footprint.Depth = 1;
    source.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(ocean_cascade_size * ocean_texel_bytes);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    for (std::size_t cascade = 0; cascade < ocean_cascades.size(); ++cascade)
    {
        for (std::uint64_t kind = 0; kind < 2U; ++kind)
        {
            source.PlacedFootprint.Offset = (cascade * 2U + kind) * ocean_cells * ocean_texel_bytes;
            destination.pResource = kind == 0U ? ocean_displacement[cascade].Get() : ocean_derivatives[cascade].Get();
            command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
    }
}

} // namespace eaw
