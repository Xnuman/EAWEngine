#pragma once

// Engine objects in scripts. The engine gives a script real objects (a game object, its type, its
// owner, a position, the block an order returns) through a ScriptObjectProvider; the host shows each
// as a userdata whose methods call back into the provider, as the original's objects are userdata
// (the framework services Object only when type(Object) == "userdata"). Objects of an interned kind
// are one Lua value per object, so `==` and table keys (DeathTable[obj]) behave as scripts expect.
//
// Calls are cheap: each kind's method names are resolved to numeric ids once, when the host is
// created, and a call reads its arguments from and pushes its results onto the Lua stack through a
// ScriptCall, without converting values or allocating. Names the provider does not implement stay
// recording stubs, so the survey keeps measuring them.

#include "core/status.hpp"
#include "math/vector.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct lua_State;

namespace eaw {

struct ScriptObject
{
    // Index of the kind in the provider's kinds().
    std::uint16_t kind{};
    // The engine's id of the object (an entity's slot, a type's index, an owner slot, a block).
    std::uint32_t index{};
    // Generation of the slot the index names: a userdata of an older generation is never reused for
    // the object now in that slot.
    std::uint32_t generation{};
    // Positions: the point.
    Vector3 point;
};

struct ScriptObjectKind
{
    // The name scripts see in tostring and diagnostics ("GameObject").
    std::string_view name;
    // The methods the provider implements; a method's id is its index here.
    std::span<const std::string_view> methods;
    // One Lua value per object (by index and generation). Kinds whose values are not compared or
    // used as keys (positions, blocks) are created fresh.
    bool interned{};
};

// One call of a provider function or method, on the Lua stack. Arguments are numbered from 1 as in
// Lua (a method's object is not among them: scripts call methods with a dot, obj.Move_To(x)).
class ScriptCall final
{
public:
    // Made by the host for each call; `arguments` values are on top of the stack.
    ScriptCall(lua_State* state, int arguments) noexcept;

    [[nodiscard]] int argument_count() const noexcept;
    [[nodiscard]] bool is_nil(int argument) const;
    // Lua truthiness: false for nil (or a missing argument) and false, true for everything else.
    [[nodiscard]] bool truthy(int argument) const;
    [[nodiscard]] std::optional<double> number(int argument) const;
    [[nodiscard]] std::optional<std::string_view> string(int argument) const;
    // A real object of any kind (stubs and other values are nothing).
    [[nodiscard]] std::optional<ScriptObject> object(int argument) const;
    // A script function, kept as one of the host's callbacks (ScriptHost::callbacks) the engine can
    // queue later; the same function always gives the same index. Nothing for other values.
    [[nodiscard]] std::optional<std::size_t> callback(int argument, std::string_view registered_by);

    void return_nil();
    void return_boolean(bool value);
    void return_number(double value);
    void return_string(std::string_view value);
    void return_object(const ScriptObject& object);
    // A list (a table indexed from 1) of objects.
    void return_objects(std::span<const ScriptObject> objects);
    [[nodiscard]] int results() const noexcept;

    // A diagnostic that does not stop the script (an order to an object that no longer exists).
    void warn(std::string_view message);

private:
    lua_State* state_;
    int arguments_;
    int results_{};
};

// What a script can ask of the engine, implemented above the host (the engine's battle bridge).
// Kinds and functions are fixed for the provider's life: the host resolves them once when created.
// A call's error stops the script with that message (wrong arguments); conditions a script may meet
// in play (a dead object) are answered with nil and ScriptCall::warn instead.
class ScriptObjectProvider
{
public:
    virtual ~ScriptObjectProvider() = default;

    [[nodiscard]] virtual std::span<const ScriptObjectKind> kinds() const noexcept = 0;
    // Global functions the provider implements (Find_First_Object); a function's id is its index.
    [[nodiscard]] virtual std::span<const std::string_view> functions() const noexcept = 0;
    virtual Status call_function(std::size_t function, ScriptCall& call) = 0;
    virtual Status call_method(const ScriptObject& object, std::size_t method, ScriptCall& call) = 0;
};

} // namespace eaw
