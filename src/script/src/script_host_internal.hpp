#pragma once

// Shared by the script host's files (script_host.cpp: the state, sandbox, loading, threads;
// script_stubs.cpp: recording stubs and stub objects; script_events.cpp: the event queue and
// callbacks; script_values.cpp: values between the engine and Lua). Private to eaw_script.

#include "script/script_host.hpp"
#include "vfs/virtual_file_system.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdint>
#include <deque>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eaw {

class ScriptHost::Impl
{
public:
    Impl(const AssetReader& reader, const ScriptHostOptions& options);
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    const AssetReader& reader;
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
        // Create_Thread.Kill was called on the thread while it ran: it ends when it next yields.
        bool kill_requested{};
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
    std::uint64_t output_count{};
    std::vector<std::string> warnings;
    std::uint64_t warning_count{};

    // Engine events waiting for GetEvent: registry references to the function and to its argument
    // table (LUA_NOREF without arguments).
    struct QueuedEvent
    {
        int function{};
        int arguments{};
    };
    std::deque<QueuedEvent> events;
    // The arguments of the event GetEvent returned last (GetEvent.Params).
    int current_arguments{LUA_NOREF};
    std::vector<ScriptCallback> callbacks;
    // Registry references to the callbacks' functions, by index.
    std::vector<int> callback_functions;

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
    // Ends one thread: releases its coroutine and its thread values.
    void end_thread(Thread& thread);
    // Create_Thread.Kill: ends the thread with that id now, or at its next yield if it is running.
    void kill_thread(std::int64_t id);
    // A uniform integer in [lower, upper] from the host's stream.
    std::int64_t random_integer(std::int64_t lower, std::int64_t upper);
    // Queues the function on top of the main state's stack (popped) with `arguments`.
    void queue_call(const std::vector<ScriptValue>& arguments);
    // Drops every queued event and the current event's arguments.
    void clear_events();
    // The index of the Lua function at `index` among the callbacks, registering it when new.
    std::size_t register_callback(lua_State* caller, int index, const std::string& registered_by);
    // Records a host API call (with the argument types from `first_argument` on) when a recorder is
    // set; without one nothing is built.
    void record(lua_State* caller, std::string_view name, int first_argument, bool implemented) const;
    void warn(std::string_view message);
    // Keeps an output line (up to max_script_output) and counts it.
    void write_output(std::string line);
};

// The host of a Lua state (stored in its registry at setup).
ScriptHost::Impl& host_of(lua_State* state);

// A global table callable through its metatable (ThreadValue, GetEvent), with fields set to C
// functions (script_host.cpp).
void set_callable_table(lua_State* state, const char* name, lua_CFunction call, std::initializer_list<std::pair<const char*, lua_CFunction>> methods);

// GetEvent and its Params and Reset (script_events.cpp).
void install_events(lua_State* state);

// Engine objects (script_objects.cpp): the provider's kinds and functions, set up once.
void install_objects(ScriptHost::Impl& host);
// Pushes a real object (interned kinds: the existing userdata of that object, if any).
void push_object(lua_State* state, const ScriptObject& object);
// The real object at `index`, if it is one.
std::optional<ScriptObject> object_at(lua_State* state, int index);
// The name of a real object's kind.
std::string_view object_kind_name(lua_State* state, const ScriptObject& object);

// Values between the engine and Lua (script_values.cpp). Pushes `value`; engine objects become
// stubs, functions nil (the engine cannot pass one in).
void push_value(lua_State* state, const ScriptValue& value);
// The value at `index`, with the read limits of ScriptHost::global.
Result<ScriptValue> read_value(lua_State* state, int index);

// Recording stubs (script_stubs.cpp).
void install_stubs(ScriptHost::Impl& host);
// Pushes the stub a global of that name stands for.
void push_global_stub(lua_State* state, const std::string& name);
// "string, number, object(Find_Player)": the argument types of the call on the stack (from `first`).
std::string argument_signature(lua_State* state, int first);
// The name of the engine object a stub at `index` stands for, or nothing if it is not a stub.
std::optional<std::string> stub_origin(lua_State* state, int index);

} // namespace eaw
