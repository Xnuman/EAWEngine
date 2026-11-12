#pragma once

// Shared shell for the Dear ImGui desktop tools (EAWTools, EAWDevLauncher): a Win32 window, a
// Direct3D 11 device and swap chain, the ImGui platform and renderer backends, crash handlers, log
// sinks, and the message/render loop. A client draws its user interface once per frame.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>
#include <memory>
#include <string>

struct ID3D11Device;

namespace eaw::gui {

class ImGuiAppClient
{
public:
    virtual ~ImGuiAppClient() = default;
    // Called between ImGui::NewFrame and ImGui::Render.
    virtual void render(HWND window) = 0;
};

struct ImGuiAppDescription
{
    std::wstring window_class;
    std::wstring title;
    int width{1280};
    int height{800};
    // Program name for crash reports and the log file prefix.
    std::string program_name;
    // ImGui's layout file; empty keeps ImGui's default (imgui.ini in the working directory).
    std::string ini_path;
    bool docking{true};
    bool viewports{true};
};

// Creates the client once the device exists, then runs until the window closes. Returns the
// process exit code (1 when the window or device cannot be created).
using ImGuiAppFactory = std::function<std::unique_ptr<ImGuiAppClient>(ID3D11Device* device)>;
int run_imgui_app(HINSTANCE instance, const ImGuiAppDescription& description, const ImGuiAppFactory& create_client);

} // namespace eaw::gui
