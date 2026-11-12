#include "imgui_app/imgui_app.hpp"

#include "platform/crash_handler.hpp"
#include "core/log.hpp"
#include "platform/log_sinks.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <filesystem>
#include <iterator>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace eaw::gui {
namespace {

// Device objects and the resize the window procedure requested; owned by run_imgui_app and reached
// from the window procedure through GWLP_USERDATA.
struct AppWindow
{
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    IDXGISwapChain* swap_chain{};
    ID3D11RenderTargetView* render_target{};
    UINT pending_width{};
    UINT pending_height{};

    void create_render_target()
    {
        ID3D11Texture2D* back_buffer = nullptr;
        if (SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
        {
            device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
            back_buffer->Release();
        }
    }

    void destroy_render_target()
    {
        if (render_target != nullptr)
        {
            render_target->Release();
            render_target = nullptr;
        }
    }

    bool create_device(HWND window)
    {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        constexpr D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL selected{};
        const auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, static_cast<UINT>(std::size(requested)), D3D11_SDK_VERSION, &description,
                                                          &swap_chain, &device, &selected, &context);
        if (FAILED(result))
        {
            return false;
        }
        create_render_target();
        return render_target != nullptr;
    }

    void destroy_device()
    {
        destroy_render_target();
        if (swap_chain != nullptr)
        {
            swap_chain->Release();
            swap_chain = nullptr;
        }
        if (context != nullptr)
        {
            context->Release();
            context = nullptr;
        }
        if (device != nullptr)
        {
            device->Release();
            device = nullptr;
        }
    }
};

LRESULT WINAPI window_proc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
    {
        return true;
    }
    auto* state = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_SIZE:
        if (state != nullptr && wparam != SIZE_MINIMIZED)
        {
            state->pending_width = static_cast<UINT>(LOWORD(lparam));
            state->pending_height = static_cast<UINT>(HIWORD(lparam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0U) == SC_KEYMENU)
        {
            return 0;
        }
        break;
    case WM_DPICHANGED:
    {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int run_imgui_app(HINSTANCE instance, const ImGuiAppDescription& description, const ImGuiAppFactory& create_client)
{
    // Unexpected failures end the program with an error window describing them.
    install_crash_handlers({description.program_name, true});
    // A GUI program has no console: the debugger output window and the log file receive warnings.
    (void)install_log_sinks({description.program_name, {}, 20, false});
    ImGui_ImplWin32_EnableDpiAwareness();
    const auto com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const WNDCLASSEXW window_class{sizeof(WNDCLASSEXW), CS_CLASSDC, window_proc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, description.window_class.c_str(), nullptr};
    RegisterClassExW(&window_class);
    AppWindow state;
    const auto window = CreateWindowW(window_class.lpszClassName, description.title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, description.width, description.height, nullptr, nullptr,
                                      instance, nullptr);
    const auto cleanup_window = [&]
    {
        if (window != nullptr)
        {
            DestroyWindow(window);
        }
        UnregisterClassW(window_class.lpszClassName, instance);
        if (SUCCEEDED(com_result))
        {
            CoUninitialize();
        }
    };
    if (window == nullptr || !state.create_device(window))
    {
        log_warning("gui", make_error("could not create the window or its Direct3D 11 device"));
        state.destroy_device();
        cleanup_window();
        return 1;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (description.docking)
    {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    if (description.viewports)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    // ImGui keeps the pointer, so the string must outlive the context (it lives in `description`).
    if (!description.ini_path.empty())
    {
        io.IniFilename = description.ini_path.c_str();
    }
    const std::filesystem::path ui_font = L"C:\\Windows\\Fonts\\segoeui.ttf";
    std::error_code font_error;
    if (std::filesystem::is_regular_file(ui_font, font_error))
    {
        io.Fonts->AddFontFromFileTTF(ui_font.string().c_str(), 17.0F, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    }
    ImGui::StyleColorsDark();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
        ImGui::GetStyle().WindowRounding = 0.0F;
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0F;
    }
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(state.device, state.context);

    {
        const auto client = create_client(state.device);
        bool done = client == nullptr;
        while (!done)
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (message.message == WM_QUIT)
                {
                    done = true;
                }
            }
            if (done)
            {
                break;
            }
            if (state.pending_width != 0 && state.pending_height != 0)
            {
                state.destroy_render_target();
                if (FAILED(state.swap_chain->ResizeBuffers(0, state.pending_width, state.pending_height, DXGI_FORMAT_UNKNOWN, 0)))
                {
                    log_warning("gui", make_error("could not resize the swap chain"));
                }
                state.pending_width = state.pending_height = 0;
                state.create_render_target();
            }
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            client->render(window);
            ImGui::Render();
            constexpr float clear_color[4]{0.025F, 0.035F, 0.050F, 1.0F};
            state.context->OMSetRenderTargets(1, &state.render_target, nullptr);
            state.context->ClearRenderTargetView(state.render_target, clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
            state.swap_chain->Present(1, 0);
        }
        // The client (and any GPU objects it owns) goes before the device it was created on.
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    state.destroy_device();
    cleanup_window();
    return 0;
}

} // namespace eaw::gui
