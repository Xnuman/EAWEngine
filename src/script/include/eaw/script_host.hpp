#pragma once

// The Lua host for Empire at War scripts (Lua 5.0, as the original). A ScriptHost is one script's
// sandboxed Lua state: no file, OS, or debug access, `print` to the log, `math.random` from a seeded
// stream of its own, `require` resolved through the layered VFS, and compiled bytecode refused (Lua
// 5.0 does not verify bytecode). Script threads are coroutines woken on simulation ticks
// (Create_Thread, Sleep). Every other engine function a script calls is a recording stub: it
// records the call with the HostApiRecorder and returns a neutral stub object whose methods record
// too, so the scripts' real use of the host API can be measured before the engine implements it.
// Every call and resume runs under an instruction budget, so a script that never yields cannot hang
// the engine.

#include "eaw/host_api_recorder.hpp"
#include "eaw/status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class AssetReader;

struct ScriptHostOptions
{
    std::uint64_t random_seed{1};
    double tick_seconds{1.0 / 30.0};
    // Lua instructions one call or one thread resume may run before it is stopped as runaway.
    std::uint64_t instruction_budget{5'000'000};
    // Names any script defines (script_defined_globals): reading one that is not set gives nil, never
    // a stub, even when the file defining it is not loaded (`if PG_Story_Mode_Init then`).
    std::shared_ptr<const std::set<std::string>> script_globals;
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
    // `reader` resolves scripts and require (it and `recorder` must outlive the host).
    [[nodiscard]] static Result<std::unique_ptr<ScriptHost>> create(const AssetReader& reader, HostApiRecorder& recorder, const ScriptHostOptions& options = {});
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Loads and runs a script's top level (`virtual_path`, or a module name as require takes it).
    // Errors are also recorded in errors().
    Status load(std::string_view script);
    [[nodiscard]] bool has_function(std::string_view name) const;
    // Calls a global function without arguments (Definitions); errors are recorded.
    Status call(std::string_view name);
    // Starts a script thread on a global function (as Create_Thread does), first resumed next tick.
    Status start_thread(std::string_view function);
    // Sets a global to a recording stub named after it: an engine object the host will provide
    // (a game-object script's Object), measured until it exists.
    void bind_stub(std::string_view global);
    // Advances one simulation tick: resumes every thread whose sleep has ended, in start order.
    void tick();

    [[nodiscard]] std::size_t live_threads() const noexcept;
    [[nodiscard]] std::uint64_t ticks() const noexcept;
    // ScriptExit was called: the script and its threads have ended.
    [[nodiscard]] bool exited() const noexcept;
    [[nodiscard]] const std::vector<ScriptError>& errors() const noexcept;
    // Lines the script printed (print, DebugMessage, _OuputDebug), for diagnostics.
    [[nodiscard]] const std::vector<std::string>& output() const noexcept;

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
