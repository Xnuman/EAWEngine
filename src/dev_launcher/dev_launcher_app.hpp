#pragma once

// EAWDevLauncher's user interface: edit, save, and start sandbox scenarios (installation, mods,
// scene, debug spawns, engine options) and watch the engine processes it started.

#include "eaw/imgui_app.hpp"

#include <memory>

namespace eaw::dev_launcher {

class DevLauncherApp final : public gui::ImGuiAppClient
{
public:
    DevLauncherApp();
    ~DevLauncherApp() override;
    DevLauncherApp(const DevLauncherApp&) = delete;
    DevLauncherApp& operator=(const DevLauncherApp&) = delete;

    void render(HWND window) override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace eaw::dev_launcher
