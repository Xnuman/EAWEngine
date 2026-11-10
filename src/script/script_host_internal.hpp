#pragma once

// Shared by the script host's files (script_host.cpp: the state, sandbox, loading, threads;
// script_stubs.cpp: recording stubs and stub objects). Private to eaw_script.

#include "eaw/script_host.hpp"
#include "vfs/virtual_file_system.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

class ScriptHost::Impl
{
public:
    Impl(const AssetReader& reader, HostApiRecorder& recorder, const ScriptHostOptions& options);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    const AssetReader& reader;
    HostApiRecorder& recorder;
    ScriptHostOptions options;
    lua_State* state{};
    // The script being surveyed (the name the recorder files calls under).
    std::string script_name;
    // Globals the loaded chunks assign or define; reading one before it is set gives nil.
    std::set<std::string> owned_globals;
    // Loaded modules by folded name (require runs each once).
    std::set<std::string> loaded_modules;
    // Lowercase module stem or dotted path -> virtual path of every Lua file under DATA/SCRIPTS.
    std::unordered_map<std::string, std::string> module_paths;

    struct Thread
    {
        int reference{};
        lua_State* coroutine{};
        std::string function;
        std::int64_t id{};
        std::uint64_t wake_tick{};
        // Arguments still to pass on the first resume (stack top of the coroutine).
        int first_arguments{};
        bool started{};
    };
    std::vector<Thread> threads;
    std::uint64_t tick_count{};
    std::uint64_t next_thread_id{1};
    bool exited{};
    // _ScriptExit was called: the script ends when the running code next yields or returns.
    bool exit_requested{};
    // The thread being resumed (GetThreadID, ThreadValue), -1 outside threads.
    std::int64_t current_thread{-1};
    std::uint64_t random_state{};
    std::uint64_t remaining_instructions{};
    // Protected calls in progress (a require inside a script nests one).
    int call_depth{};
    std::vector<ScriptError> errors;
    std::vector<std::string> output;

    Status setup();
    // Runs the function on the top of the stack with `arguments` below it, under the budget.
    Status protected_call(int arguments, std::string_view context);
    Status load_module(std::string_view name);
    std::optional<std::string> resolve_module(std::string_view name) const;
    void record_error(std::string_view context, std::string_view message);
    // A new script thread on the function on top of `caller`'s stack (popped), with `arguments`
    // values below it moved along as its first resume's arguments. `caller` is the state running
    // (the main state, or a script thread calling Create_Thread).
    void spawn_thread(lua_State* caller, std::string function, int arguments);
    void hook_thread(lua_State* thread);
    double next_random();
    // Ticks between services of a thread: the script's ServiceRate (seconds), at least one tick.
    std::uint64_t service_interval() const;
    // Ends the script after _ScriptExit (its threads and their thread values).
    void finish_exit();
};

// The host of a Lua state (stored in its registry at setup).
ScriptHost::Impl& host_of(lua_State* state);

// Recording stubs (script_stubs.cpp).
void install_stubs(ScriptHost::Impl& host);
// Pushes the stub a global of that name stands for.
void push_global_stub(lua_State* state, const std::string& name);
// "string, number, object(Find_Player)": the argument types of the call on the stack (from `first`).
std::string argument_signature(lua_State* state, int first);

} // namespace eaw
