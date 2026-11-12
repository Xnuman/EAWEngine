#pragma once

// The Lua host for Empire at War scripts (Lua 5.0, as the original). A ScriptHost is one script's
// sandboxed Lua state: no file, OS, or debug access, `print` to the log, `math.random` from a seeded
// stream of its own, `require` resolved through the layered VFS, and compiled bytecode refused (Lua
// 5.0 does not verify bytecode). Script threads are coroutines woken on simulation ticks
// (Create_Thread, Sleep), and engine events reach them through the framework's PumpEvents, which
// drains GetEvent. Every other engine function a script calls is a recording stub: it
// records the call with the HostApiRecorder and returns a neutral stub object whose methods record
// too, so the scripts' real use of the host API can be measured before the engine implements it.
// Every call and resume runs under an instruction budget, so a script that never yields cannot hang
// the engine.

#include "core/status.hpp"
#include "script/host_api_recorder.hpp"
#include "script/script_object.hpp"
#include "script/script_value.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class AssetReader;

// Warnings a host keeps (a script that keeps ordering a dead unit would otherwise grow them without
// bound); later ones are only counted. An engine choice.
inline constexpr std::size_t max_script_warnings = 256;
// Output lines a host keeps (the stock scripts write debug messages on every service); later ones
// are only counted. An engine choice.
inline constexpr std::size_t max_script_output = 4096;
// Sandbox limits on what one script may ask for (engine choices, far above what stock scripts use):
// live script threads, the length of a string.find/gfind/gsub pattern (pattern matching recurses
// per pattern item, so this bounds its C stack), and the size of a string.rep result. Memory as a
// whole is capped per Lua state in the Lua build (third_party/lua/eaw_lua_config.h), and pattern
// matching steps there too.
inline constexpr std::size_t max_script_threads = 256;
inline constexpr std::size_t max_script_pattern_length = 1024;
inline constexpr std::size_t max_script_string_bytes = 16U * 1024U * 1024U;

struct ScriptHostOptions
{
    std::uint64_t random_seed{1};
    double tick_seconds{1.0 / 30.0};
    // Lua instructions one call or one thread resume may run before it is stopped as runaway.
    std::uint64_t instruction_budget{5'000'000};
    // Names any script defines (script_defined_globals): reading one that is not set gives nil, never
    // a stub, even when the file defining it is not loaded (`if PG_Story_Mode_Init then`).
    std::shared_ptr<const std::set<std::string>> script_globals;
    // Where host API calls are recorded (surveys); nothing is recorded without one, which keeps
    // calls cheap in play.
    HostApiRecorder* recorder{};
    // The engine objects and functions scripts get (null: every engine name is a stub).
    ScriptObjectProvider* objects{};
};

// A function a script handed to an engine function, to be called back later (the handler
// Event_Object_In_Range takes).
struct ScriptCallback
{
    // The engine function it was passed to ("Object.Event_Object_In_Range").
    std::string registered_by;
};

struct ScriptError
{
    // What was running: "load", a function name, or "thread <function>".
    std::string context;
    std::string message;
};

class ScriptHost
{
public:
    // `reader` resolves scripts and require; it and the options' recorder and provider must outlive
    // the host.
    [[nodiscard]] static Result<std::unique_ptr<ScriptHost>> create(const AssetReader& reader, const ScriptHostOptions& options = {});
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Loads and runs a script's top level (`virtual_path`, or a module name as require takes it).
    // Errors are also recorded in errors().
    Status load(std::string_view script);
    [[nodiscard]] bool has_function(std::string_view name) const;
    // Calls a global function without arguments (Definitions); errors are recorded.
    Status call(std::string_view name);
    // The engine's call when a script is created: Base_Definitions (the framework's, which calls the
    // script's Definitions), else Definitions, else nothing.
    Status run_definitions();
    // Starts a script thread on a global function (as Create_Thread does), first resumed next tick.
    Status start_thread(std::string_view function);
    // Sets a global (a game-object script's Object, an AI plan's task forces); engine objects
    // become recording stubs of their name until the engine provides them.
    void set_global(std::string_view name, const ScriptValue& value);
    // A global's value, read back with nesting and size limits (an error past them).
    [[nodiscard]] Result<ScriptValue> global(std::string_view name) const;

    // Engine events. Each queues a call that the next script thread to pump events (PumpEvents
    // through GetEvent and GetEvent.Params) makes, in queue order, with `arguments`. Ignored once
    // the script has exited.
    // A call of a global script function; fails when the global is not one.
    Status queue_event(std::string_view function, const std::vector<ScriptValue>& arguments = {});
    // A call of a function a script handed to the engine (an index into callbacks()).
    Status queue_callback(std::size_t callback, const std::vector<ScriptValue>& arguments = {});
    [[nodiscard]] std::size_t queued_events() const noexcept;
    // Every function scripts passed to engine functions, in first-seen order; passing the same
    // function again keeps its index.
    [[nodiscard]] const std::vector<ScriptCallback>& callbacks() const noexcept;
    // Advances one simulation tick: resumes every thread whose sleep has ended, in start order.
    void tick();

    [[nodiscard]] std::size_t live_threads() const noexcept;
    [[nodiscard]] std::uint64_t ticks() const noexcept;
    // ScriptExit was called: the script and its threads have ended.
    [[nodiscard]] bool exited() const noexcept;
    [[nodiscard]] const std::vector<ScriptError>& errors() const noexcept;
    // Lines the script wrote (print, and the framework's _ScriptMessage, _MessagePopup, and
    // _OuputDebug), the first max_script_output of them; output_count() counts all.
    [[nodiscard]] const std::vector<std::string>& output() const noexcept;
    [[nodiscard]] std::uint64_t output_count() const noexcept;
    // Diagnostics of provider calls that did not stop the script (ScriptCall::warn), the first
    // max_script_warnings of them; warning_count() counts all.
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept;
    [[nodiscard]] std::uint64_t warning_count() const noexcept;

    class Impl;

private:
    explicit ScriptHost(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Global names a Lua source assigns or defines as functions (comments and strings skipped). The host
// treats them as the script's own: reading one before it is set gives nil, never a stub.
[[nodiscard]] std::vector<std::string> script_owned_globals(std::string_view source);
// script_owned_globals of every Lua source under Data/Scripts (compiled chunks skipped).
[[nodiscard]] std::set<std::string> script_defined_globals(const AssetReader& reader);

} // namespace eaw
