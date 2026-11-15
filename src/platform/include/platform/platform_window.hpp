#pragma once

#include "core/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace eaw {

enum class Key : std::uint8_t
{
    escape,
    enter,
    space,
    left,
    right,
    up,
    down,
    w,
    a,
    s,
    d,
    p,
    b,
    c,
    g,
    n,
    o,
    q,
    e,
    tab,
    f1,
    shift,
    control,
    alt,
    backspace,
    del,
    home,
    end,
    count,
};

enum class MouseButton : std::uint8_t
{
    left,
    right,
    middle,
    count,
};

struct ButtonState
{
    bool down{};
    bool pressed{};
    bool released{};
};

struct InputState
{
    std::array<ButtonState, static_cast<std::size_t>(Key::count)> keys;
    std::array<ButtonState, static_cast<std::size_t>(MouseButton::count)> mouse_buttons;
    std::int32_t mouse_x{};
    std::int32_t mouse_y{};
    // Cursor movement in client pixels this frame (pointer speed and acceleration applied); use it
    // for anything tied to the cursor, such as dragging a selection box.
    std::int32_t mouse_delta_x{};
    std::int32_t mouse_delta_y{};
    // Device movement this frame from raw input: unaccelerated mouse counts, independent of the
    // cursor (keeps moving at screen edges). Use it for camera rotation and other relative control.
    // Equals the cursor movement when raw input is unavailable.
    std::int32_t raw_mouse_delta_x{};
    std::int32_t raw_mouse_delta_y{};
    float wheel_delta{};
    // Characters typed this frame (WM_CHAR, as Unicode code points), for text fields.
    std::u32string text;
    bool focused{};
    // Mouse movement and keys come from raw input (WM_INPUT) rather than window messages.
    bool raw_input{};
    // Raw input records processed this frame (high polling rate mice produce many per frame).
    std::uint32_t raw_events{};
};

struct WindowDescription
{
    std::string title{"EAWEngine Visual Sandbox"};
    std::uint32_t width{1600};
    std::uint32_t height{900};
    // Read mouse movement and keys through raw input (falls back to window messages when the
    // devices cannot be registered).
    bool raw_input{true};
};

class PlatformWindow final
{
public:
    // Creates and shows the window; fails when Windows cannot register or create it, or the title is
    // not valid UTF-8.
    [[nodiscard]] static Result<PlatformWindow> create(const WindowDescription& description);
    ~PlatformWindow();

    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;
    PlatformWindow(PlatformWindow&&) noexcept;
    PlatformWindow& operator=(PlatformWindow&&) noexcept;

    void poll_events();
    void request_close();

    [[nodiscard]] bool close_requested() const;
    [[nodiscard]] bool minimized() const;
    [[nodiscard]] std::uint32_t client_width() const;
    [[nodiscard]] std::uint32_t client_height() const;
    [[nodiscard]] void* native_handle() const;
    [[nodiscard]] const InputState& input() const;

private:
    struct Impl;
    PlatformWindow();
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
