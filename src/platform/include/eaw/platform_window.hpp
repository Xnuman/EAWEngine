#pragma once

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
    shift,
    control,
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
    std::int32_t mouse_delta_x{};
    std::int32_t mouse_delta_y{};
    float wheel_delta{};
    bool focused{};
};

struct WindowDescription
{
    std::string title{"EAWEngine Visual Sandbox"};
    std::uint32_t width{1600};
    std::uint32_t height{900};
};

class PlatformWindow final
{
public:
    explicit PlatformWindow(const WindowDescription& description);
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
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
