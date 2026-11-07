#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "eaw/crash_handler.hpp"
#include <windows.h>

#include "gui_app.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

ID3D11Device* device{};
ID3D11DeviceContext* device_context{};
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
    const auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, static_cast<UINT>(std::size(requested)), D3D11_SDK_VERSION, &description, &swap_chain,
                                                      &device, &selected, &device_context);
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
    if (device_context != nullptr)
    {
        device_context->Release();
        device_context = nullptr;
    }
    if (device != nullptr)
    {
        device->Release();
        device = nullptr;
    }
}

LRESULT WINAPI window_proc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
    {
        return true;
    }
    switch (message)
    {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
        {
            pending_width = static_cast<UINT>(LOWORD(lparam));
            pending_height = static_cast<UINT>(HIWORD(lparam));
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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Unexpected failures end the workbench with an error window describing them.
    eaw::install_crash_handlers({"EAW Tools", true});
    ImGui_ImplWin32_EnableDpiAwareness();
    const auto com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const WNDCLASSEXW window_class{sizeof(WNDCLASSEXW), CS_CLASSDC, window_proc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"EAWToolsWorkbench", nullptr};
    RegisterClassExW(&window_class);

    const auto window = CreateWindowW(window_class.lpszClassName, L"EAW Tools — Alamo Asset & Localization Workbench", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 920, nullptr, nullptr,
                                      instance, nullptr);
    if (window == nullptr || !create_device(window))
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
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    const std::filesystem::path ui_font = L"C:\\Windows\\Fonts\\segoeui.ttf";
    if (std::filesystem::is_regular_file(ui_font))
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
    ImGui_ImplDX11_Init(device, device_context);

    eaw::tools::GuiApp application(device);
    bool done = false;
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

        if (pending_width != 0 && pending_height != 0)
        {
            destroy_render_target();
            swap_chain->ResizeBuffers(0, pending_width, pending_height, DXGI_FORMAT_UNKNOWN, 0);
            pending_width = pending_height = 0;
            create_render_target();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        application.render(window);
        ImGui::Render();

        constexpr float clear_color[4]{0.025F, 0.035F, 0.050F, 1.0F};
        device_context->OMSetRenderTargets(1, &render_target, nullptr);
        device_context->ClearRenderTargetView(render_target, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        swap_chain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroy_device();
    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, instance);
    if (SUCCEEDED(com_result))
    {
        CoUninitialize();
    }
    return 0;
}
