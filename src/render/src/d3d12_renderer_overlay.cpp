// The Dear ImGui overlay: the caller's panels and the selection rectangle.

#include "d3d12_renderer_impl.hpp"

#include "imgui.h"
#include "imgui_impl_dx12.h"

namespace eaw {

Status D3d12Renderer::Impl::create_overlay()
{
    IMGUI_CHECKVERSION();
    overlay_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(overlay_context);
    auto& io = ImGui::GetIO();
    overlay_settings_file = options.overlay_settings_file.empty() ? std::string{} : options.overlay_settings_file.string();
    io.IniFilename = overlay_settings_file.empty() ? nullptr : overlay_settings_file.c_str();
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
        return make_error("the debug overlay (Dear ImGui DX12 backend) could not be initialized");
    }
    overlay_backend_initialized = true;
    return Status::success();
}

// The backend's allocation callbacks stay wired up (its initialization requires them), although
// its texture path, their only user, never runs: update_overlay_textures takes every request.
void D3d12Renderer::Impl::allocate_overlay_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
{
    reclaim_texture_slots();
    const auto slot = texture_slots.allocate();
    EAW_ASSERT_MSG(slot.has_value(), "the D3D12 texture table has no slot for a debug overlay texture");
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu = shader_heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(*slot) * increment;
    gpu = texture_slot_gpu_handle(*slot);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3d12Renderer::Impl::texture_slot_gpu_handle(const std::uint32_t slot) const
{
    auto gpu = shader_heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(slot) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return gpu;
}

void D3d12Renderer::Impl::retire_overlay_texture(OverlayTexture retired)
{
    const auto retire_value = direct_queue.next_value();
    texture_slots.release(retired.descriptor, retire_value);
    std::vector<ComPtr<ID3D12Resource>> resources;
    resources.push_back(std::move(retired.resource));
    retire_uploaded(std::move(resources), retire_value);
}

void D3d12Renderer::Impl::update_overlay_textures()
{
    auto* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr || draw_data->Textures == nullptr)
    {
        return;
    }
    for (ImTextureData* requested : *draw_data->Textures)
    {
        if (requested->Status == ImTextureStatus_WantDestroy)
        {
            // Retired by fence value, so frames still drawing with it keep it until they finish.
            if (const auto found = overlay_textures.find(requested); found != overlay_textures.end())
            {
                retire_overlay_texture(std::move(found->second));
                overlay_textures.erase(found);
            }
            requested->SetTexID(ImTextureID_Invalid);
            requested->SetStatus(ImTextureStatus_Destroyed);
            continue;
        }
        if (requested->Status != ImTextureStatus_WantCreate && requested->Status != ImTextureStatus_WantUpdates)
        {
            continue;
        }
        // An update uploads the whole texture into a new resource and retires the old one: atlas
        // changes are rare (startup, first use of new glyphs) and the atlas is small.
        const auto texture_width = static_cast<std::uint32_t>(requested->Width);
        const auto texture_height = static_cast<std::uint32_t>(requested->Height);
        VisualTexture source;
        source.name = "Dear ImGui texture";
        source.format = VisualTextureFormat::rgba8_unorm;
        VisualTextureMip mip;
        mip.width = texture_width;
        mip.height = texture_height;
        mip.row_pitch = static_cast<std::size_t>(texture_width) * 4U;
        mip.slice_pitch = mip.row_pitch * texture_height;
        mip.data.resize(mip.slice_pitch);
        const auto* pixels = static_cast<const std::byte*>(requested->GetPixels());
        if (requested->Format == ImTextureFormat_RGBA32)
        {
            std::memcpy(mip.data.data(), pixels, mip.data.size());
        }
        else
        {
            // Alpha8: white with the texel as coverage, as the backend expands it.
            for (std::size_t index = 0; index < static_cast<std::size_t>(texture_width) * texture_height; ++index)
            {
                mip.data[index * 4U] = mip.data[index * 4U + 1U] = mip.data[index * 4U + 2U] = std::byte{0xFF};
                mip.data[index * 4U + 3U] = pixels[index];
            }
        }
        source.mips.push_back(std::move(mip));
        reclaim_texture_slots();
        const auto slot = texture_slots.allocate();
        EAW_ASSERT_MSG(slot.has_value(), "the D3D12 texture table has no slot for a debug overlay texture");
        auto upload = begin_upload(L"EAWEngine overlay texture upload");
        auto resource = upload_texture(upload, source, *slot);
        submit_upload(std::move(upload));
        if (const auto found = overlay_textures.find(requested); found != overlay_textures.end())
        {
            retire_overlay_texture(std::move(found->second));
        }
        overlay_textures[requested] = OverlayTexture{std::move(resource), *slot};
        requested->SetTexID(static_cast<ImTextureID>(texture_slot_gpu_handle(*slot).ptr));
        requested->SetStatus(ImTextureStatus_OK);
    }
    // Handled here: the backend's own texture path (a fence of its own and a CPU wait) never runs.
    draw_data->Textures = nullptr;
}

void D3d12Renderer::Impl::free_overlay_descriptor(const D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto slot = static_cast<std::uint32_t>((cpu.ptr - shader_heap->GetCPUDescriptorHandleForHeapStart().ptr) / increment);
    // Also called from shutdown; a slot that cannot be released is only bookkeeping discarded
    // with the renderer, so it is skipped there rather than asserted.
    if (texture_slots.can_release(slot, direct_queue.next_value()))
    {
        texture_slots.release(slot, direct_queue.next_value());
    }
}

void D3d12Renderer::Impl::shutdown_overlay() noexcept
{
    if (overlay_context == nullptr)
    {
        return;
    }
    ImGui::SetCurrentContext(overlay_context);
    // The GPU is idle at shutdown (~Impl waits first), so the textures are released directly.
    overlay_textures.clear();
    if (overlay_backend_initialized)
    {
        ImGui_ImplDX12_Shutdown();
        overlay_backend_initialized = false;
    }
    ImGui::DestroyContext(overlay_context);
    overlay_context = nullptr;
}

void D3d12Renderer::Impl::prepare_overlay()
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
    for (const auto character : overlay_input.text)
    {
        io.AddInputCharacter(static_cast<unsigned int>(character));
    }
    overlay_input.text.clear();
    io.AddKeyEvent(ImGuiKey_Backspace, overlay_input.backspace);
    io.AddKeyEvent(ImGuiKey_Delete, overlay_input.del);
    io.AddKeyEvent(ImGuiKey_Enter, overlay_input.enter);
    io.AddKeyEvent(ImGuiKey_Escape, overlay_input.escape);
    io.AddKeyEvent(ImGuiKey_Tab, overlay_input.tab);
    io.AddKeyEvent(ImGuiKey_LeftArrow, overlay_input.left);
    io.AddKeyEvent(ImGuiKey_RightArrow, overlay_input.right);
    io.AddKeyEvent(ImGuiKey_UpArrow, overlay_input.up);
    io.AddKeyEvent(ImGuiKey_DownArrow, overlay_input.down);
    io.AddKeyEvent(ImGuiKey_Home, overlay_input.home);
    io.AddKeyEvent(ImGuiKey_End, overlay_input.end);
    io.AddKeyEvent(ImGuiMod_Ctrl, overlay_input.control);
    io.AddKeyEvent(ImGuiMod_Shift, overlay_input.shift);
    if (overlay_input.wheel != 0.0F)
    {
        io.AddMouseWheelEvent(0.0F, overlay_input.wheel);
    }
    overlay_input.wheel = 0.0F;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();

    if (selection_rectangle)
    {
        const auto& [x0, y0, x1, y1] = *selection_rectangle;
        auto* draw_list = ImGui::GetForegroundDrawList();
        const ImVec2 minimum(std::min(x0, x1), std::min(y0, y1));
        const ImVec2 maximum(std::max(x0, x1), std::max(y0, y1));
        draw_list->AddRectFilled(minimum, maximum, IM_COL32(120, 200, 255, 40));
        draw_list->AddRect(minimum, maximum, IM_COL32(150, 215, 255, 220));
    }
    if (overlay_ui)
    {
        overlay_ui();
    }
    overlay_wants_mouse = io.WantCaptureMouse;
    overlay_wants_keyboard = io.WantTextInput;
    ImGui::Render();
    update_overlay_textures();
}

void D3d12Renderer::Impl::render_overlay(const D3D12_CPU_DESCRIPTOR_HANDLE render_target)
{
    ImGui::SetCurrentContext(overlay_context);
    // The overlay pipeline has one color target and no depth.
    command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());
}

void D3d12Renderer::Impl::set_overlay_input(const VisualOverlayInput& input)
{
    // Wheel steps and typed characters accumulate until the overlay consumes them in the next
    // rendered frame.
    const auto wheel = overlay_input.wheel + input.wheel;
    auto text = std::move(overlay_input.text);
    text += input.text;
    overlay_input = input;
    overlay_input.wheel = std::isfinite(wheel) ? wheel : 0.0F;
    overlay_input.text = std::move(text);
}

} // namespace eaw
