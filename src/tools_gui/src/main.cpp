#include "imgui_app/imgui_app.hpp"
#include "tools_gui/gui_app.hpp"

#include <memory>

namespace {

class WorkbenchClient final : public eaw::gui::ImGuiAppClient
{
public:
    explicit WorkbenchClient(ID3D11Device* device) : application_(device)
    {}

    void render(HWND window) override
    {
        application_.render(window);
    }

private:
    eaw::tools_gui::GuiApp application_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    eaw::gui::ImGuiAppDescription description;
    description.window_class = L"EAWToolsWorkbench";
    description.title = L"EAW Tools — Alamo Asset & Localization Workbench";
    description.width = 1500;
    description.height = 920;
    description.program_name = "EAWTools";
    return eaw::gui::run_imgui_app(instance, description,
                                   [](ID3D11Device* device)
                                   {
                                       return std::make_unique<WorkbenchClient>(device);
                                   });
}
