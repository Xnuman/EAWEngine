#pragma once

// Rebindable input (the owner, 2026-11-16: an action table, a bindings text file, a panel to rebind;
// the in-game UI later uses the same). A caller lists its actions, each with up to two default keys;
// the bindings say whether an action's key is down or was pressed this frame, and read and write
// themselves as text:
//
//   # comment
//   camera_pan_forward = w, up
//   pause = p
//   spawn =                       (no key)
//
// One action a line, its keys by key_name (keys.hpp), ignoring ASCII case. Actions and keys the text
// does not name keep their defaults; lines that cannot be read are reported, not fatal, so an old or
// hand-edited file never stops the program.

#include "platform/platform_window.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

// Keys an action may have.
inline constexpr std::size_t keys_per_action = 2;

struct InputAction
{
    // The action's name in the bindings text (lower case, words joined by '_').
    std::string_view name;
    // What it does, for the panel that rebinds it.
    std::string_view description;
    std::array<std::optional<Key>, keys_per_action> defaults;
};

class InputBindings final
{
public:
    // Bindings of `actions` (indexed as they are listed; the caller keeps the list alive), at their
    // defaults.
    explicit InputBindings(std::span<const InputAction> actions);

    [[nodiscard]] std::span<const InputAction> actions() const noexcept
    {
        return actions_;
    }
    [[nodiscard]] const std::array<std::optional<Key>, keys_per_action>& keys(std::size_t action) const;
    void set(std::size_t action, std::size_t slot, std::optional<Key> key);
    // Every action back to its defaults.
    void reset();

    // Whether one of the action's keys is held, or went down this frame.
    [[nodiscard]] bool down(std::size_t action, const InputState& input) const;
    [[nodiscard]] bool pressed(std::size_t action, const InputState& input) const;

    // Reads bindings from text over the current ones; returns what it could not read, one message a
    // line ("line 3: unknown action 'foo'").
    [[nodiscard]] std::vector<std::string> read(std::string_view text);
    // Every action, in order, with its keys (empty after '=' for none).
    [[nodiscard]] std::string write() const;

private:
    std::span<const InputAction> actions_;
    std::vector<std::array<std::optional<Key>, keys_per_action>> keys_;
};

} // namespace eaw
