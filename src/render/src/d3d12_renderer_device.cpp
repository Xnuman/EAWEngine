// Device, swap chain, frame contexts, buffers, waits, and fence-based retirement.

#include "d3d12_renderer_impl.hpp"

namespace eaw {

D3d12Renderer::Impl::Impl(void* native_window, const std::uint32_t initial_width, const std::uint32_t initial_height, const D3d12RendererOptions& renderer_options)
    : window(static_cast<HWND>(native_window)), options(renderer_options), width(std::max(initial_width, 1U)), height(std::max(initial_height, 1U))
{
    options.maximum_frame_latency = std::clamp(options.maximum_frame_latency, 1U, frame_count - 1);
    lod_settings = options.lod;
}

D3d12Renderer::Impl::~Impl()
{
    // Best effort: a removed device must not turn shutdown (or a failed startup) into a fatal
    // error, so these waits report failure instead of ending the process.
    wait_idle_for_shutdown();
    shutdown_overlay();
    wait_idle_for_shutdown();
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

Status D3d12Renderer::Impl::initialize()
{
    if (window == nullptr)
    {
        return make_error("D3d12Renderer requires a valid HWND");
    }
    EAW_TRY(configure_debugging(options.gpu_validation));

#if defined(_DEBUG)
    constexpr UINT factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#else
    const UINT factory_flags = options.gpu_validation ? DXGI_CREATE_FACTORY_DEBUG : 0;
#endif
    EAW_TRY(hresult_status(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2"));
    EAW_TRY_ASSIGN(adapter, select_adapter(factory.Get(), options.use_warp));
    EAW_TRY(hresult_status(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice"));
    set_name(device.Get(), L"EAWEngine D3D12 device");

    DXGI_ADAPTER_DESC3 adapter_description{};
    EAW_TRY(hresult_status(adapter->GetDesc3(&adapter_description), "IDXGIAdapter4::GetDesc3"));
    adapter_name = to_utf8(adapter_description.Description);

#if defined(_DEBUG)
    if (SUCCEEDED(device.As(&debug_info_queue)))
    {
        debug_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        EAW_TRY(hresult_status(debug_info_queue->RegisterMessageCallback(debug_message_callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &debug_callback_cookie),
                               "ID3D12InfoQueue1::RegisterMessageCallback"));
        debug_callback_registered = true;
    }
#endif

    EAW_TRY(check_required_features());
    EAW_TRY(direct_queue.initialize(device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"EAWEngine direct queue"));
    EAW_TRY(copy_queue.initialize(device.Get(), D3D12_COMMAND_LIST_TYPE_COPY, L"EAWEngine copy queue"));
    EAW_TRY(create_swap_chain());
    create_frame_resources();
    open_pipeline_cache();
    create_pipeline();
    save_pipeline_cache();
    create_post_targets();
    create_shadow_maps();
    create_initial_assets();
    create_statistics_queries();
    return create_overlay();
}

void D3d12Renderer::Impl::create_statistics_queries()
{
    D3D12_QUERY_HEAP_DESC description{};
    description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    description.Count = frame_count * 2U;
    check_device_hresult(device->CreateQueryHeap(&description, IID_PPV_ARGS(&timestamp_heap)), device, "CreateQueryHeap(timestamps)");
    set_name(timestamp_heap.Get(), L"EAWEngine frame timestamps");
    check_hresult(direct_queue.get()->GetTimestampFrequency(&timestamp_frequency), "ID3D12CommandQueue::GetTimestampFrequency");
}

void D3d12Renderer::Impl::collect_timestamps(FrameContext& frame)
{
    if (!frame.timestamps_pending || timestamp_frequency == 0)
    {
        return;
    }
    frame.timestamps_pending = false;
    std::array<std::uint64_t, 2> ticks{};
    const D3D12_RANGE read_range{0, sizeof(ticks)};
    void* mapped{};
    check_device_hresult(frame.timestamp_readback->Map(0, &read_range, &mapped), frame.timestamp_readback, "Map(frame timestamps)");
    std::memcpy(ticks.data(), mapped, sizeof(ticks));
    const D3D12_RANGE no_write{0, 0};
    frame.timestamp_readback->Unmap(0, &no_write);
    if (ticks[1] >= ticks[0])
    {
        last_gpu_milliseconds = static_cast<double>(ticks[1] - ticks[0]) * 1000.0 / static_cast<double>(timestamp_frequency);
    }
}

Status D3d12Renderer::Impl::create_swap_chain()
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
    EAW_TRY(hresult_status(factory->CreateSwapChainForHwnd(direct_queue.get(), window, &description, nullptr, nullptr, &created), "IDXGIFactory2::CreateSwapChainForHwnd"));
    EAW_TRY(hresult_status(created.As(&swap_chain), "query IDXGISwapChain3"));
    EAW_TRY(hresult_status(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory::MakeWindowAssociation"));
    EAW_TRY(hresult_status(swap_chain->SetMaximumFrameLatency(options.maximum_frame_latency), "IDXGISwapChain2::SetMaximumFrameLatency"));

    frame_latency_event.reset(swap_chain->GetFrameLatencyWaitableObject());
    if (frame_latency_event.get() == nullptr)
    {
        return make_error("DXGI did not create the frame-latency waitable object");
    }
    return Status::success();
}

void D3d12Renderer::Impl::create_frame_resources()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = frame_count;
    check_device_hresult(device->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&rtv_heap)), device, "CreateDescriptorHeap(RTV)");
    set_name(rtv_heap.Get(), L"EAWEngine back-buffer RTV heap");
    rtv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (std::uint32_t index = 0; index < frame_count; ++index)
    {
        check_device_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[index].allocator)), device, "CreateCommandAllocator(direct)");
        set_name(frames[index].allocator.Get(), L"EAWEngine frame allocator " + std::to_wstring(index));
    }
    check_device_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&command_list)), device, "CreateCommandList(direct)");
    set_name(command_list.Get(), L"EAWEngine primary graphics command list");
    check_device_hresult(command_list->Close(), command_list, "close initial direct command list");
    acquire_back_buffers();
    create_depth_buffer();
    create_bone_buffers(1);
    create_constant_buffers();
}

void D3d12Renderer::Impl::acquire_back_buffers()
{
    auto handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t index = 0; index < frame_count; ++index)
    {
        check_device_hresult(swap_chain->GetBuffer(index, IID_PPV_ARGS(&frames[index].back_buffer)), device, "IDXGISwapChain::GetBuffer");
        set_name(frames[index].back_buffer.Get(), L"EAWEngine back buffer " + std::to_wstring(index));
        device->CreateRenderTargetView(frames[index].back_buffer.Get(), nullptr, handle);
        handle.ptr += rtv_increment;
    }
}

void D3d12Renderer::Impl::create_depth_buffer()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_description.NumDescriptors = 1;
    if (!dsv_heap)
    {
        check_device_hresult(device->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&dsv_heap)), device, "CreateDescriptorHeap(DSV)");
        set_name(dsv_heap.Get(), L"EAWEngine depth DSV heap");
    }

    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    // Typeless, so temporal anti-aliasing can read it through a float view.
    description.Format = DXGI_FORMAT_R32_TYPELESS;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0F;
    check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth_buffer)), device,
                         "CreateCommittedResource(depth buffer)");
    set_name(depth_buffer.Get(), L"EAWEngine depth buffer");
    D3D12_DEPTH_STENCIL_VIEW_DESC depth_view{};
    depth_view.Format = DXGI_FORMAT_D32_FLOAT;
    depth_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depth_buffer.Get(), &depth_view, dsv_heap->GetCPUDescriptorHandleForHeapStart());

    // Selection target: the pick id of the surface in front at every pixel.
    if (!pick_rtv_heap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC pick_heap{};
        pick_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        pick_heap.NumDescriptors = 1;
        check_device_hresult(device->CreateDescriptorHeap(&pick_heap, IID_PPV_ARGS(&pick_rtv_heap)), device, "CreateDescriptorHeap(pick RTV)");
        set_name(pick_rtv_heap.Get(), L"EAWEngine pick RTV heap");
    }
    description.Format = pick_format;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE pick_clear{};
    pick_clear.Format = pick_format;
    check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_RENDER_TARGET, &pick_clear, IID_PPV_ARGS(&pick_target)), device,
                         "CreateCommittedResource(pick target)");
    set_name(pick_target.Get(), L"EAWEngine pick target");
    device->CreateRenderTargetView(pick_target.Get(), nullptr, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart());
}

D3D12_CPU_DESCRIPTOR_HANDLE D3d12Renderer::Impl::post_rtv(const std::size_t index) const
{
    auto handle = post_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * rtv_increment;
    return handle;
}

void D3d12Renderer::Impl::create_post_targets()
{
    if (!post_rtv_heap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = static_cast<UINT>(post_slots.size());
        check_device_hresult(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&post_rtv_heap)), device, "CreateDescriptorHeap(post RTV)");
        set_name(post_rtv_heap.Get(), L"EAWEngine post-processing RTV heap");
        // Startup: the texture table is nearly empty, and these slots are never released.
        for (auto& slot : post_slots)
        {
            const auto allocated = texture_slots.allocate();
            EAW_ASSERT_MSG(allocated.has_value(), "the D3D12 texture table has no slot for a post-processing target");
            slot = *allocated;
        }
        const auto allocated = texture_slots.allocate();
        EAW_ASSERT_MSG(allocated.has_value(), "the D3D12 texture table has no slot for the depth view");
        depth_slot = *allocated;
    }
    bloom_width = std::max(1U, (width + bloom_downscale - 1U) / bloom_downscale);
    bloom_height = std::max(1U, (height + bloom_downscale - 1U) / bloom_downscale);
    const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto make_target = [&](ComPtr<ID3D12Resource>& target, const std::uint32_t target_width, const std::uint32_t target_height, const DXGI_FORMAT format, const std::size_t index,
                                 const wchar_t* name, const std::array<float, 4>& clear_color = {0.018F, 0.031F, 0.043F, 1.0F})
    {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = target_width;
        description.Height = target_height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
        std::ranges::copy(clear_color, std::begin(clear.Color));
        check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&target)), device,
                             "CreateCommittedResource(post-processing target)");
        set_name(target.Get(), name);
        device->CreateRenderTargetView(target.Get(), nullptr, post_rtv(index));
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(post_slots[index]) * increment;
        device->CreateShaderResourceView(target.Get(), &view, handle);
    };
    make_target(scene_color, width, height, back_buffer_format, 0, L"EAWEngine scene color");
    make_target(bloom_targets[0], bloom_width, bloom_height, bloom_format, 1, L"EAWEngine bloom A");
    make_target(bloom_targets[1], bloom_width, bloom_height, bloom_format, 2, L"EAWEngine bloom B");
    make_target(post_color, width, height, back_buffer_format, 3, L"EAWEngine post color");
    make_target(taa_history[0], width, height, back_buffer_format, 4, L"EAWEngine TAA history A");
    make_target(taa_history[1], width, height, back_buffer_format, 5, L"EAWEngine TAA history B");
    make_target(motion_target, width, height, motion_format, 6, L"EAWEngine motion", {motion_unknown, motion_unknown, 0.0F, 0.0F});
    taa_history_valid = false;
    D3D12_SHADER_RESOURCE_VIEW_DESC depth_view{};
    depth_view.Format = DXGI_FORMAT_R32_FLOAT;
    depth_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depth_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depth_view.Texture2D.MipLevels = 1;
    auto depth_handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
    depth_handle.ptr += static_cast<SIZE_T>(depth_slot) * increment;
    device->CreateShaderResourceView(depth_buffer.Get(), &depth_view, depth_handle);
}

void D3d12Renderer::Impl::create_shadow_maps()
{
    if (!options.shadows)
    {
        return;
    }
    shadow_resolution = std::clamp(options.shadow_resolution, minimum_shadow_resolution, maximum_shadow_resolution);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = static_cast<UINT>(shadow_maps.size());
    check_device_hresult(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&shadow_dsv_heap)), device, "CreateDescriptorHeap(shadow DSV)");
    set_name(shadow_dsv_heap.Get(), L"EAWEngine shadow DSV heap");
    shadow_dsv_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (std::size_t index = 0; index < shadow_maps.size(); ++index)
    {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = shadow_resolution;
        description.Height = shadow_resolution;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R32_TYPELESS;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0F;
        check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&shadow_maps[index])), device,
                             "CreateCommittedResource(shadow map)");
        set_name(shadow_maps[index].Get(), L"EAWEngine shadow cascade");
        D3D12_DEPTH_STENCIL_VIEW_DESC depth_view{};
        depth_view.Format = DXGI_FORMAT_D32_FLOAT;
        depth_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        auto dsv = shadow_dsv_heap->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += index * shadow_dsv_increment;
        device->CreateDepthStencilView(shadow_maps[index].Get(), &depth_view, dsv);
        // Startup: these slots are never released.
        const auto allocated = texture_slots.allocate();
        EAW_ASSERT_MSG(allocated.has_value(), "the D3D12 texture table has no slot for a shadow map");
        shadow_slots[index] = *allocated;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(shadow_slots[index]) * increment;
        device->CreateShaderResourceView(shadow_maps[index].Get(), &view, handle);
    }
}

void D3d12Renderer::Impl::create_constant_buffers()
{
    const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = buffer_description(frame_constant_bytes);
    for (std::uint32_t index = 0; index < frame_count; ++index)
    {
        auto& frame = frames[index];
        check_device_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.constant_buffer)),
                             device, "CreateCommittedResource(frame constants)");
        set_name(frame.constant_buffer.Get(), L"EAWEngine frame constants " + std::to_wstring(index));
        const D3D12_RANGE no_read{0, 0};
        check_device_hresult(frame.constant_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.constant_buffer_mapped)), frame.constant_buffer, "Map(frame constants)");

        const auto residency_description = buffer_description(static_cast<std::uint64_t>(maximum_scene_textures) * sizeof(std::uint32_t));
        check_device_hresult(
            device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &residency_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.residency_buffer)), device,
            "CreateCommittedResource(frame texture residency)");
        set_name(frame.residency_buffer.Get(), L"EAWEngine frame texture residency " + std::to_wstring(index));
        check_device_hresult(frame.residency_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.residency_mapped)), frame.residency_buffer, "Map(frame texture residency)");

        const auto readback_heap = heap_properties(D3D12_HEAP_TYPE_READBACK);
        // One placement-aligned slot per pick copied in a frame.
        const auto readback_description = buffer_description(static_cast<std::uint64_t>(maximum_picks_per_frame) * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        check_device_hresult(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&frame.pick_readback)),
                             device, "CreateCommittedResource(frame pick readback)");
        set_name(frame.pick_readback.Get(), L"EAWEngine frame pick readback " + std::to_wstring(index));

        const auto debug_description = buffer_description(static_cast<std::uint64_t>(maximum_debug_lines) * 2U * sizeof(DebugVertex));
        check_device_hresult(
            device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &debug_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.debug_vertex_buffer)), device,
            "CreateCommittedResource(frame debug lines)");
        set_name(frame.debug_vertex_buffer.Get(), L"EAWEngine frame debug lines " + std::to_wstring(index));
        check_device_hresult(frame.debug_vertex_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.debug_vertices_mapped)), frame.debug_vertex_buffer, "Map(frame debug lines)");

        const auto timestamp_description = buffer_description(2U * sizeof(std::uint64_t));
        check_device_hresult(
            device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &timestamp_description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&frame.timestamp_readback)), device,
            "CreateCommittedResource(frame timestamps)");
        set_name(frame.timestamp_readback.Get(), L"EAWEngine frame timestamps " + std::to_wstring(index));
    }
}

void D3d12Renderer::Impl::create_bone_buffers(const std::uint32_t requested_count)
{
    const auto count = std::max(requested_count, 1U);
    // load_scene validates the scene's bone count before it gets here.
    EAW_ASSERT_MSG(count <= maximum_bone_transforms, "visual scene exceeds the D3D12 bone palette capacity");
    bone_transforms.assign(count, Matrix4::identity());
}

void D3d12Renderer::Impl::ensure_bone_capacity(FrameContext& frame, const std::size_t count)
{
    if (frame.bone_buffer && frame.bone_capacity >= count)
    {
        return;
    }
    // Called once this context is reusable; the GPU may still hold a reference through retirement
    // bookkeeping, so the old buffer retires on the direct timeline like other replaced resources.
    if (frame.bone_buffer)
    {
        frame.bone_buffer->Unmap(0, nullptr);
        frame.bone_buffer_mapped = nullptr;
        retired_resources.push({std::move(frame.bone_buffer)}, direct_queue.next_value());
    }
    const auto capacity = static_cast<std::uint32_t>(std::min<std::size_t>(std::max<std::size_t>({count, static_cast<std::size_t>(frame.bone_capacity) * 2U, 256U}), maximum_bone_transforms));
    const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const auto description = buffer_description(static_cast<std::uint64_t>(capacity) * sizeof(Matrix4));
    check_device_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.bone_buffer)), device,
                         "CreateCommittedResource(frame bone palette)");
    set_name(frame.bone_buffer.Get(), L"EAWEngine frame bone palette");
    const D3D12_RANGE no_read{0, 0};
    check_device_hresult(frame.bone_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.bone_buffer_mapped)), frame.bone_buffer, "Map(frame bone palette)");
    frame.bone_capacity = capacity;
}

Status D3d12Renderer::Impl::check_required_features() const
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        return make_error("the D3D12 device does not support shader model 6.0, which the offline-compiled sandbox shaders require");
    }
    // The material shader indexes a 4096-entry texture table, which needs resource binding
    // tier 2 (tier 1 limits a stage to 128 shader resource views).
    D3D12_FEATURE_DATA_D3D12_OPTIONS binding_options{};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &binding_options, sizeof(binding_options))) || binding_options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2)
    {
        return make_error("the D3D12 device does not support resource binding tier 2, which the sandbox texture table requires");
    }
    return Status::success();
}

bool D3d12Renderer::Impl::wait_for_frame_latency()
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
                check_device_hresult(removed, device, "waiting for the DXGI frame-latency object");
            }
            return false;
        }
        fatal_error("waiting for the DXGI frame-latency object failed (Windows error " + std::to_string(GetLastError()) + ")");
    }
}

void D3d12Renderer::Impl::resize(const std::uint32_t new_width, const std::uint32_t new_height)
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
    scene_color.Reset();
    bloom_targets = {};
    for (auto& frame : frames)
    {
        // The flush finished every frame; a pending capture is read before its fence is forgotten.
        collect_capture(frame);
        frame.back_buffer.Reset();
        frame.fence_value = 0;
    }

    width = new_width;
    height = new_height;
    check_device_hresult(swap_chain->ResizeBuffers(frame_count, width, height, back_buffer_format, swap_chain_flags), device, "IDXGISwapChain::ResizeBuffers");
    acquire_back_buffers();
    create_depth_buffer();
    create_post_targets();
    occluded = false;
}

void D3d12Renderer::Impl::wait_idle()
{
    if (direct_queue.initialized())
    {
        direct_queue.flush();
    }
    if (copy_queue.initialized())
    {
        copy_queue.flush();
    }
    if (direct_queue.initialized() && copy_queue.initialized())
    {
        retire_completed_after_idle();
    }
}

void D3d12Renderer::Impl::wait_idle_for_shutdown() noexcept
{
    bool idle = true;
    if (direct_queue.initialized())
    {
        idle = direct_queue.try_flush() && idle;
    }
    if (copy_queue.initialized())
    {
        idle = copy_queue.try_flush() && idle;
    }
    if (idle && direct_queue.initialized() && copy_queue.initialized())
    {
        retire_completed_after_idle();
    }
}

void D3d12Renderer::Impl::retire_completed_after_idle()
{
    retire_completed_resources();
    for (auto& frame : frames)
    {
        if (frame.pick_readback)
        {
            collect_pick(frame);
        }
        collect_capture(frame);
    }
}

void D3d12Renderer::Impl::retire_completed_resources()
{
    retire_completed_uploads();
    retired_resources.retire(direct_queue.completed_value());
    if (shader_heap)
    {
        reclaim_texture_slots();
    }
}

void D3d12Renderer::Impl::retire_completed_uploads()
{
    const auto completed = copy_queue.completed_value();
    std::erase_if(pending_uploads,
                  [completed](const UploadBatch& upload)
                  {
                      return upload.fence_value <= completed;
                  });
}

} // namespace eaw
