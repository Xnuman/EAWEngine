#include "imgui_app/imgui_app.hpp"
#include "dev_launcher/dev_launcher_app.hpp"

#include <filesystem>
#include <memory>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    eaw::gui::ImGuiAppDescription description;
    description.window_class = L"EAWDevLauncher";
    description.title = L"EAW Dev Launcher — sandbox scenarios";
    description.width = 1180;
    description.height = 900;
    description.program_name = "EAWDevLauncher";
    // Keep the window layout next to the executable (the build folder), not in the working directory.
    std::wstring module(32768, L'\0');
    module.resize(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())));
    description.ini_path = (std::filesystem::path(module).parent_path() / "EAWDevLauncher.imgui.ini").string();
    description.viewports = false;
    return eaw::gui::run_imgui_app(instance, description,
                                   [](ID3D11Device*)
                                   {
                                       return std::make_unique<eaw::dev_launcher::DevLauncherApp>();
                                   });
}
