#include "engine_app/sandbox_actions.hpp"

#include <array>

namespace eaw::engine_app {

namespace {

using eaw::Key;

constexpr std::array<eaw::InputAction, static_cast<std::size_t>(SandboxAction::count)> actions{{
    {"camera_forward", "Camera: pan (free camera: fly) forward", {Key::w, Key::up}},
    {"camera_back", "Camera: pan back", {Key::s, Key::down}},
    {"camera_left", "Camera: pan left", {Key::a, Key::left}},
    {"camera_right", "Camera: pan right", {Key::d, Key::right}},
    {"camera_fast", "Camera: faster while held", {Key::shift, std::nullopt}},
    {"camera_rise", "Free camera: up", {Key::e, std::nullopt}},
    {"camera_sink", "Free camera: down", {Key::q, std::nullopt}},
    {"camera_reset", "Camera: back to its start", {Key::space, std::nullopt}},
    {"camera_mode", "Camera: next mode (fixed, debug, free)", {Key::c, std::nullopt}},
    {"order_attack_move", "Right-click: attack-move while held", {Key::control, std::nullopt}},
    {"order_guard", "Right-click: guard while held", {Key::shift, std::nullopt}},
    {"order_formation", "Right-click: move as a formation while held", {Key::alt, std::nullopt}},
    {"spawn", "Spawn the chosen type at the cursor", {Key::n, std::nullopt}},
    {"spawn_owner", "Spawn: next owner", {Key::o, std::nullopt}},
    {"spawn_next_type", "Spawn: next type", {Key::tab, std::nullopt}},
    {"spawn_type_back", "Spawn: next type goes back while held", {Key::shift, std::nullopt}},
    {"spawn_type_ten", "Spawn: next type steps ten while held", {Key::control, std::nullopt}},
    {"pause", "Pause the world (models: the animation)", {Key::p, std::nullopt}},
    {"bounds", "Show bounds", {Key::b, std::nullopt}},
    {"navigation", "Show navigation: footprints, routes, passability", {Key::g, std::nullopt}},
    {"animation_next", "Model viewer: next animation", {Key::right, std::nullopt}},
    {"animation_previous", "Model viewer: previous animation", {Key::left, std::nullopt}},
}};
static_assert(!actions.back().name.empty(), "every action needs its entry");

} // namespace

std::span<const eaw::InputAction> sandbox_actions() noexcept
{
    return actions;
}

} // namespace eaw::engine_app
