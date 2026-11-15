#include "core/log.hpp"
#include "platform/platform_window.hpp"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace eaw {
namespace {

constexpr wchar_t window_class_name[] = L"EAWEngine.PlatformWindow";

Result<std::wstring> to_wide(const std::string_view value)
{
    if (value.empty())
    {
        return std::wstring{};
    }

    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        return make_error("window title is not valid UTF-8");
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
    // Last absolute raw position (tablets, remote desktop, virtual machines), in screen pixels.
    std::optional<std::array<LONG, 2>> last_absolute_mouse;
    // Reused buffer for batched raw input reads; 64-bit words keep the records 8-byte aligned.
    std::vector<std::uint64_t> raw_buffer;

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
        case WM_INPUT:
            // Only while this window is in the foreground (RIM_INPUT); DefWindowProc then releases
            // the system's raw input buffer, as the documentation requires.
            if (GET_RAWINPUT_CODE_WPARAM(w_param) == RIM_INPUT)
            {
                handle_raw_input(reinterpret_cast<HRAWINPUT>(l_param));
            }
            return DefWindowProcW(handle, message, w_param, l_param);
        case WM_CHAR:
        {
            // UTF-16 units; a surrogate pair arrives as two messages.
            const auto unit = static_cast<char32_t>(w_param);
            if (unit >= 0xD800U && unit <= 0xDBFFU)
            {
                pending_high_surrogate = unit;
            }
            else if (unit >= 0xDC00U && unit <= 0xDFFFU)
            {
                if (pending_high_surrogate != 0)
                {
                    input.text.push_back(0x10000U + ((pending_high_surrogate - 0xD800U) << 10U) + (unit - 0xDC00U));
                }
                pending_high_surrogate = 0;
            }
            else if (unit >= 0x20U && unit != 0x7FU)
            {
                input.text.push_back(unit);
            }
            return 0;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
            // With raw input the keyboard state comes from WM_INPUT; these messages remain for
            // future text entry.
            if (!input.raw_input)
            {
                update_key(w_param, message == WM_KEYDOWN);
            }
            return 0;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (!input.raw_input)
            {
                update_key(w_param, message == WM_SYSKEYDOWN);
            }
            // Alt is a modifier (Alt + right-click orders a formation move): releasing it alone must
            // not open the system menu, which would take the next keys. Alt+F4 keeps working.
            if (message == WM_SYSKEYUP && w_param == VK_MENU)
            {
                return 0;
            }
            return DefWindowProcW(handle, message, w_param, l_param);
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
                if (!input.raw_input)
                {
                    input.raw_mouse_delta_x += x - input.mouse_x;
                    input.raw_mouse_delta_y += y - input.mouse_y;
                }
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
        input.raw_mouse_delta_x = 0;
        input.raw_mouse_delta_y = 0;
        input.raw_events = 0;
        input.text.clear();
        input.wheel_delta = 0.0F;
    }

    // Registers the mouse and keyboard (HID usage page 1, usages 2 and 6) for WM_INPUT. Legacy
    // messages are still generated: buttons, wheel, and cursor position keep coming from them (so a
    // click on the title bar or another window never reaches the scene), and key messages remain
    // available for text entry.
    bool register_raw_input()
    {
        std::array<RAWINPUTDEVICE, 2> devices{};
        devices[0] = {0x01, 0x02, 0, handle};
        devices[1] = {0x01, 0x06, 0, handle};
        if (!RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof(RAWINPUTDEVICE)))
        {
            log_warning("input", "raw input is unavailable (Windows error " + std::to_string(GetLastError()) + "); using window messages");
            return false;
        }
        return true;
    }

    void handle_raw_input(const HRAWINPUT raw_handle)
    {
        UINT size = 0;
        if (GetRawInputData(raw_handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
        {
            return;
        }
        // RAWINPUT is variable-sized for HID devices; keyboard and mouse records fit in the struct.
        alignas(RAWINPUT) std::array<std::byte, sizeof(RAWINPUT) + 64> buffer{};
        if (size > buffer.size() || GetRawInputData(raw_handle, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        {
            return;
        }
        handle_raw_record(*reinterpret_cast<const RAWINPUT*>(buffer.data()));
    }

    void handle_raw_record(const RAWINPUT& raw)
    {
        ++input.raw_events;
        if (raw.header.dwType == RIM_TYPEMOUSE)
        {
            handle_raw_mouse(raw.data.mouse);
        }
        else if (raw.header.dwType == RIM_TYPEKEYBOARD)
        {
            handle_raw_keyboard(raw.data.keyboard);
        }
    }

    // Reads all queued raw input in batches. High polling rate mice (4-8 kHz) queue many WM_INPUT
    // messages per frame; dispatching each one through the message loop is costly, so they are read
    // here in bulk first. GetRawInputBuffer removes the WM_INPUT messages it reads from the queue,
    // so nothing is counted twice, and input that arrives later is still handled per message.
    // (This build is 64-bit only, so the WOW64 buffer-size adjustment does not apply.)
    void drain_raw_input()
    {
        if (!input.raw_input)
        {
            return;
        }
        for (int batch = 0; batch < 64; ++batch)
        {
            UINT record_size = 0;
            if (GetRawInputBuffer(nullptr, &record_size, sizeof(RAWINPUTHEADER)) != 0 || record_size == 0)
            {
                return;
            }
            // Room for many records per call; each is at most one RAWINPUT for mice and keyboards.
            const auto capacity_bytes = std::max<std::size_t>(record_size, sizeof(RAWINPUT)) * 256U;
            raw_buffer.resize((capacity_bytes + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t));
            auto size = static_cast<UINT>(raw_buffer.size() * sizeof(std::uint64_t));
            auto* records = reinterpret_cast<RAWINPUT*>(raw_buffer.data());
            const auto count = GetRawInputBuffer(records, &size, sizeof(RAWINPUTHEADER));
            if (count == 0 || count == static_cast<UINT>(-1))
            {
                return;
            }
            const auto* record = records;
            for (UINT index = 0; index < count; ++index)
            {
                handle_raw_record(*record);
                // What NEXTRAWINPUTBLOCK does: records are dwSize bytes, each starting 8-byte aligned.
                const auto next = (reinterpret_cast<std::uintptr_t>(record) + record->header.dwSize + 7U) & ~static_cast<std::uintptr_t>(7U);
                record = reinterpret_cast<const RAWINPUT*>(next);
            }
        }
    }

    void handle_raw_mouse(const RAWMOUSE& mouse)
    {
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
        {
            // A conventional mouse: unaccelerated relative counts.
            input.raw_mouse_delta_x += mouse.lLastX;
            input.raw_mouse_delta_y += mouse.lLastY;
            return;
        }
        // Absolute devices (pen tablets, remote desktop, virtual machines) report 0..65535 across the
        // primary or virtual screen; movement is the change between reports, in screen pixels.
        const bool virtual_desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        const auto width = GetSystemMetrics(virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        const auto height = GetSystemMetrics(virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
        const std::array<LONG, 2> position{static_cast<LONG>(static_cast<long long>(mouse.lLastX) * width / 65535), static_cast<LONG>(static_cast<long long>(mouse.lLastY) * height / 65535)};
        if (last_absolute_mouse)
        {
            input.raw_mouse_delta_x += position[0] - (*last_absolute_mouse)[0];
            input.raw_mouse_delta_y += position[1] - (*last_absolute_mouse)[1];
        }
        last_absolute_mouse = position;
    }

    void handle_raw_keyboard(const RAWKEYBOARD& keyboard)
    {
        // 255 marks the fake keys of escaped scan code sequences.
        if (keyboard.VKey == 0xFF)
        {
            return;
        }
        WPARAM virtual_key = keyboard.VKey;
        if (virtual_key == VK_SHIFT)
        {
            // Raw input reports both shifts as VK_SHIFT; the scan code tells them apart.
            virtual_key = MapVirtualKeyW(keyboard.MakeCode, MAPVK_VSC_TO_VK_EX);
        }
        update_key(virtual_key, (keyboard.Flags & RI_KEY_BREAK) == 0);
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

        // Edges latch until the next frame: a press and release inside one frame both register.
        auto& state = input.keys[index_of(*key)];
        state.pressed = state.pressed || (down && !state.down);
        state.released = state.released || (!down && state.down);
        state.down = down;
    }

    void update_mouse_button(const MouseButton button, const bool down)
    {
        // Edges latch until the next frame: a quick click inside one frame reports both edges.
        auto& state = input.mouse_buttons[index_of(button)];
        state.pressed = state.pressed || (down && !state.down);
        state.released = state.released || (!down && state.down);
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

    char32_t pending_high_surrogate{};

    static std::optional<Key> translate_key(const WPARAM virtual_key)
    {
        switch (virtual_key)
        {
        case VK_BACK:
            return Key::backspace;
        case VK_DELETE:
            return Key::del;
        case VK_HOME:
            return Key::home;
        case VK_END:
            return Key::end;
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
        case 'G':
            return Key::g;
        case 'N':
            return Key::n;
        case 'O':
            return Key::o;
        case 'Q':
            return Key::q;
        case 'E':
            return Key::e;
        case VK_TAB:
            return Key::tab;
        case VK_F1:
            return Key::f1;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return Key::shift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return Key::control;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return Key::alt;
        default:
            return std::nullopt;
        }
    }
};

PlatformWindow::PlatformWindow() : impl_(std::make_unique<Impl>())
{}

Result<PlatformWindow> PlatformWindow::create(const WindowDescription& description)
{
    PlatformWindow window;
    auto& impl = *window.impl_;
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
        return make_error("could not register the EAWEngine window class (Windows error " + std::to_string(GetLastError()) + ")");
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT bounds{0, 0, static_cast<LONG>(description.width), static_cast<LONG>(description.height)};
    if (!AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, GetDpiForSystem()))
    {
        return make_error("could not calculate the EAWEngine window size (Windows error " + std::to_string(GetLastError()) + ")");
    }

    EAW_TRY_ASSIGN(const auto title, to_wide(description.title));
    // The window procedure keeps a pointer to the Impl, which stays put when the window moves.
    impl.handle = CreateWindowExW(0, window_class_name, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, &impl);
    if (impl.handle == nullptr)
    {
        return make_error("could not create the EAWEngine window (Windows error " + std::to_string(GetLastError()) + ")");
    }

    RECT client{};
    GetClientRect(impl.handle, &client);
    impl.client_width = static_cast<std::uint32_t>(client.right - client.left);
    impl.client_height = static_cast<std::uint32_t>(client.bottom - client.top);
    impl.input.focused = true;
    impl.input.raw_input = description.raw_input && impl.register_raw_input();
    ShowWindow(impl.handle, SW_SHOWDEFAULT);
    UpdateWindow(impl.handle);
    return window;
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
    impl_->drain_raw_input();
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
