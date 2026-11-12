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

void D3d12Renderer::Impl::allocate_overlay_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
{
    reclaim_texture_slots();
    const auto slot = texture_slots.allocate();
    // The backend allocates its font texture during startup, while the table is nearly empty.
    EAW_ASSERT_MSG(slot.has_value(), "the D3D12 texture table has no slot for a debug overlay texture");
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu = shader_heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(*slot) * increment;
    gpu = shader_heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(*slot) * increment;
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
    if (overlay_backend_initialized)
    {
        ImGui_ImplDX12_Shutdown();
        overlay_backend_initialized = false;
    }
    ImGui::DestroyContext(overlay_context);
    overlay_context = nullptr;
}

void D3d12Renderer::Impl::render_overlay(const D3D12_CPU_DESCRIPTOR_HANDLE render_target)
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
