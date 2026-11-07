#include "eaw/platform_window.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <windows.h>
#include <windowsx.h>

namespace eaw {
namespace {

constexpr wchar_t window_class_name[] = L"EAWEngine.PlatformWindow";

std::wstring to_wide(const std::string_view value)
{
    if (value.empty())
    {
        return {};
    }

    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        throw std::runtime_error("window title is not valid UTF-8");
    }

    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

template <typename Enum> constexpr std::size_t index_of(const Enum value)
{
    return static_cast<std::size_t>(value);
}

} // namespace

struct PlatformWindow::Impl
{
    HWND handle{};
    InputState input;
    std::uint32_t client_width{};
    std::uint32_t client_height{};
    bool close_requested{};
    bool minimized{};
    bool mouse_position_initialized{};

    static LRESULT CALLBACK window_proc(const HWND handle, const UINT message, const WPARAM w_param, const LPARAM l_param)
    {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<Impl*>(create->lpCreateParams);
            self->handle = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        if (self != nullptr)
        {
            return self->handle_message(message, w_param, l_param);
        }
        return DefWindowProcW(handle, message, w_param, l_param);
    }

    LRESULT handle_message(const UINT message, const WPARAM w_param, const LPARAM l_param)
    {
        switch (message)
        {
        case WM_CLOSE:
            close_requested = true;
            DestroyWindow(handle);
            return 0;
        case WM_DESTROY:
            close_requested = true;
            handle = nullptr;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            client_width = static_cast<std::uint32_t>(LOWORD(l_param));
            client_height = static_cast<std::uint32_t>(HIWORD(l_param));
            minimized = w_param == SIZE_MINIMIZED || client_width == 0 || client_height == 0;
            return 0;
        case WM_SETFOCUS:
            input.focused = true;
            return 0;
        case WM_KILLFOCUS:
            input.focused = false;
            release_all_input();
            return 0;
        case WM_KEYDOWN:
            update_key(w_param, true);
            return 0;
        case WM_KEYUP:
            update_key(w_param, false);
            return 0;
        case WM_SYSKEYDOWN:
            if (w_param == VK_F4)
            {
                return DefWindowProcW(handle, message, w_param, l_param);
            }
            update_key(w_param, true);
            return 0;
        case WM_SYSKEYUP:
            update_key(w_param, false);
            return 0;
        case WM_LBUTTONDOWN:
            update_mouse_button(MouseButton::left, true);
            return 0;
        case WM_LBUTTONUP:
            update_mouse_button(MouseButton::left, false);
            return 0;
        case WM_RBUTTONDOWN:
            update_mouse_button(MouseButton::right, true);
            return 0;
        case WM_RBUTTONUP:
            update_mouse_button(MouseButton::right, false);
            return 0;
        case WM_MBUTTONDOWN:
            update_mouse_button(MouseButton::middle, true);
            return 0;
        case WM_MBUTTONUP:
            update_mouse_button(MouseButton::middle, false);
            return 0;
        case WM_MOUSEMOVE:
        {
            const auto x = GET_X_LPARAM(l_param);
            const auto y = GET_Y_LPARAM(l_param);
            if (mouse_position_initialized)
            {
                input.mouse_delta_x += x - input.mouse_x;
                input.mouse_delta_y += y - input.mouse_y;
            }
            mouse_position_initialized = true;
            input.mouse_x = x;
            input.mouse_y = y;
            return 0;
        }
        case WM_MOUSEWHEEL:
            input.wheel_delta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(w_param)) / static_cast<float>(WHEEL_DELTA);
            return 0;
        case WM_DPICHANGED:
        {
            const auto* suggested = reinterpret_cast<const RECT*>(l_param);
            SetWindowPos(handle, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        default:
            return DefWindowProcW(handle, message, w_param, l_param);
        }
    }

    void begin_input_frame()
    {
        for (auto& key : input.keys)
        {
            key.pressed = false;
            key.released = false;
        }
        for (auto& button : input.mouse_buttons)
        {
            button.pressed = false;
            button.released = false;
        }
        input.mouse_delta_x = 0;
        input.mouse_delta_y = 0;
        input.wheel_delta = 0.0F;
    }

    void release_all_input()
    {
        for (auto& key : input.keys)
        {
            if (key.down)
            {
                key.down = false;
                key.released = true;
            }
        }
        for (auto& button : input.mouse_buttons)
        {
            if (button.down)
            {
                button.down = false;
                button.released = true;
            }
        }
    }

    void update_key(const WPARAM virtual_key, const bool down)
    {
        const auto key = translate_key(virtual_key);
        if (!key)
        {
            return;
        }

        auto& state = input.keys[index_of(*key)];
        state.pressed = down && !state.down;
        state.released = !down && state.down;
        state.down = down;
    }

    void update_mouse_button(const MouseButton button, const bool down)
    {
        auto& state = input.mouse_buttons[index_of(button)];
        state.pressed = down && !state.down;
        state.released = !down && state.down;
        state.down = down;
        if (down)
        {
            SetCapture(handle);
        }
        else if (std::none_of(input.mouse_buttons.begin(), input.mouse_buttons.end(),
                              [](const ButtonState& value)
                              {
                                  return value.down;
                              }))
        {
            ReleaseCapture();
        }
    }

    static std::optional<Key> translate_key(const WPARAM virtual_key)
    {
        switch (virtual_key)
        {
        case VK_ESCAPE:
            return Key::escape;
        case VK_RETURN:
            return Key::enter;
        case VK_SPACE:
            return Key::space;
        case VK_LEFT:
            return Key::left;
        case VK_RIGHT:
            return Key::right;
        case VK_UP:
            return Key::up;
        case VK_DOWN:
            return Key::down;
        case 'W':
            return Key::w;
        case 'A':
            return Key::a;
        case 'S':
            return Key::s;
        case 'D':
            return Key::d;
        case 'P':
            return Key::p;
        case 'B':
            return Key::b;
        case 'C':
            return Key::c;
        case 'N':
            return Key::n;
        case VK_TAB:
            return Key::tab;
        case VK_F1:
            return Key::f1;
        case VK_SHIFT:
            return Key::shift;
        case VK_CONTROL:
            return Key::control;
        default:
            return std::nullopt;
        }
    }
};

PlatformWindow::PlatformWindow(const WindowDescription& description) : impl_(std::make_unique<Impl>())
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    window_class.lpfnWndProc = Impl::window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.lpszClassName = window_class_name;
    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        throw std::runtime_error("could not register the EAWEngine window class");
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT bounds{0, 0, static_cast<LONG>(description.width), static_cast<LONG>(description.height)};
    if (!AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, GetDpiForSystem()))
    {
        throw std::runtime_error("could not calculate the EAWEngine window size");
    }

    const auto title = to_wide(description.title);
    impl_->handle =
        CreateWindowExW(0, window_class_name, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, impl_.get());
    if (impl_->handle == nullptr)
    {
        throw std::runtime_error("could not create the EAWEngine window");
    }

    RECT client{};
    GetClientRect(impl_->handle, &client);
    impl_->client_width = static_cast<std::uint32_t>(client.right - client.left);
    impl_->client_height = static_cast<std::uint32_t>(client.bottom - client.top);
    impl_->input.focused = true;
    ShowWindow(impl_->handle, SW_SHOWDEFAULT);
    UpdateWindow(impl_->handle);
}

PlatformWindow::~PlatformWindow()
{
    if (impl_ && impl_->handle != nullptr)
    {
        DestroyWindow(impl_->handle);
    }
}

PlatformWindow::PlatformWindow(PlatformWindow&&) noexcept = default;
PlatformWindow& PlatformWindow::operator=(PlatformWindow&&) noexcept = default;

void PlatformWindow::poll_events()
{
    impl_->begin_input_frame();
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            impl_->close_requested = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void PlatformWindow::request_close()
{
    impl_->close_requested = true;
}

bool PlatformWindow::close_requested() const
{
    return impl_->close_requested;
}

bool PlatformWindow::minimized() const
{
    return impl_->minimized;
}

std::uint32_t PlatformWindow::client_width() const
{
    return impl_->client_width;
}

std::uint32_t PlatformWindow::client_height() const
{
    return impl_->client_height;
}

void* PlatformWindow::native_handle() const
{
    return impl_->handle;
}

const InputState& PlatformWindow::input() const
{
    return impl_->input;
}

} // namespace eaw
