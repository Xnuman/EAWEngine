#pragma once

// The sandbox's rebindable actions (platform/input_bindings.hpp; the owner, 2026-11-16): the
// cameras, the order modifiers of a right-click, spawning, and the view toggles. Their defaults are
// the sandbox's keys until the original's hotkeys are transcribed from the owner's screenshots of its
// controls menu. Escape (quit), F1 (the panel bar), and the keys the panels' text fields use are not
// rebindable.

#include "platform/input_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace eaw::engine_app {

enum class SandboxAction : std::uint8_t
{
    camera_forward,
    camera_back,
    camera_left,
    camera_right,
    camera_fast,
    camera_rise,
    camera_sink,
    camera_reset,
    camera_mode,
    order_attack_move,
    order_guard,
    order_formation,
    spawn,
    spawn_owner,
    spawn_next_type,
    spawn_type_back,
    spawn_type_ten,
    pause,
    bounds,
    navigation,
    animation_next,
    animation_previous,
    count,
};

// Every action, in SandboxAction order.
[[nodiscard]] std::span<const eaw::InputAction> sandbox_actions() noexcept;

[[nodiscard]] constexpr std::size_t action_index(const SandboxAction action) noexcept
{
    return static_cast<std::size_t>(action);
}

} // namespace eaw::engine_app
