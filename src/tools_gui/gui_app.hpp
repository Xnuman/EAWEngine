#pragma once

#include <memory>

struct HWND__;
struct ID3D11Device;
using NativeWindowHandle = HWND__*;

namespace eaw::tools {

class GuiApp final {
public:
    struct State;

    explicit GuiApp(ID3D11Device* device);
    ~GuiApp();
    GuiApp(const GuiApp&) = delete;
    GuiApp& operator=(const GuiApp&) = delete;

    void render(NativeWindowHandle window);

private:
    std::unique_ptr<State> state_;
};

} // namespace eaw::tools
