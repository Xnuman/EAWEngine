// The script host's Lua state: sandbox, loading and require, protected calls under an instruction
// budget, script threads on simulation ticks, and the functions the host implements itself.

#include "script_host_internal.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <utility>
#include <string_view>

namespace eaw {
namespace {

// Registry key of the host pointer (its address is the key).
const char host_registry_key = 0;
// Instructions between budget checks.
constexpr int hook_interval = 1000;

void budget_hook(lua_State* state, lua_Debug*)
{
    auto& host = host_of(state);
    if (host.remaining_instructions <= static_cast<std::uint64_t>(hook_interval))
    {
        host.remaining_instructions = 0;
        host.budget_exhausted = true;
        luaL_error(state, "instruction budget of %d exceeded (a loop without Sleep?)", static_cast<int>(host.options.instruction_budget));
    }
    host.remaining_instructions -= static_cast<std::uint64_t>(hook_interval);
}

// After a protected call made by a script: a spent budget or a memory error is not the script's
// to catch; it goes on to the host. The error object is on top of the stack.
void rethrow_if_unrecoverable(lua_State* state, const int status)
{
    if (status != 0 && (status == LUA_ERRMEM || host_of(state).budget_exhausted))
    {
        lua_error(state);
    }
}

// pcall, xpcall, and coroutine.resume as Lua 5.0's base library defines them, but re-raising what
// rethrow_if_unrecoverable names.
int lua_guarded_pcall(lua_State* state)
{
    luaL_checkany(state, 1);
    const auto status = lua_pcall(state, lua_gettop(state) - 1, LUA_MULTRET, 0);
    rethrow_if_unrecoverable(state, status);
    lua_pushboolean(state, status == 0 ? 1 : 0);
    lua_insert(state, 1);
    return lua_gettop(state);
}

int lua_guarded_xpcall(lua_State* state)
{
    luaL_checkany(state, 2);
    lua_settop(state, 2);
    // The error handler below the function.
    lua_insert(state, 1);
    const auto status = lua_pcall(state, 0, LUA_MULTRET, 1);
    rethrow_if_unrecoverable(state, status);
    lua_pushboolean(state, status == 0 ? 1 : 0);
    lua_replace(state, 1);
    return lua_gettop(state);
}

int lua_guarded_resume(lua_State* state)
{
    auto* coroutine = lua_tothread(state, 1);
    luaL_argcheck(state, coroutine != nullptr, 1, "coroutine expected");
    const auto arguments = lua_gettop(state) - 1;
    if (lua_checkstack(coroutine, arguments) == 0)
    {
        return luaL_error(state, "too many arguments to resume");
    }
    lua_xmove(state, coroutine, arguments);
    const auto status = lua_resume(coroutine, arguments);
    if (status != 0)
    {
        lua_xmove(coroutine, state, 1);
        rethrow_if_unrecoverable(state, status);
        lua_pushboolean(state, 0);
        lua_insert(state, -2);
        return 2;
    }
    const auto results = lua_gettop(coroutine);
    if (lua_checkstack(state, results + 1) == 0)
    {
        return luaL_error(state, "too many results to resume");
    }
    lua_xmove(coroutine, state, results);
    lua_pushboolean(state, 1);
    lua_insert(state, -(results + 1));
    return results + 1;
}

// collectgarbage([limit]) without the limit: a script may ask for a collection, never switch the
// collector off by raising its threshold.
int lua_full_collect(lua_State* state)
{
    lua_setgcthreshold(state, 0);
    return 0;
}

// string.rep (upvalue 1) with a cap on the result, and on the count (an empty string repeated
// billions of times is a loop the instruction budget cannot see).
int lua_bounded_rep(lua_State* state)
{
    std::size_t length = 0;
    (void)luaL_checklstring(state, 1, &length);
    const auto count = static_cast<double>(luaL_checknumber(state, 2));
    const auto limit = static_cast<double>(max_script_string_bytes);
    if (!std::isfinite(count) || count > limit || static_cast<double>(length) * count > limit)
    {
        return luaL_error(state, "string.rep: a result of more than %d bytes is refused", static_cast<int>(max_script_string_bytes));
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, 1);
    return 1;
}

// string.find, string.gfind, and string.gsub (upvalue 1) with a cap on the pattern's length.
int lua_bounded_pattern(lua_State* state)
{
    std::size_t length = 0;
    (void)luaL_checklstring(state, 2, &length);
    if (length > max_script_pattern_length)
    {
        return luaL_error(state, "a pattern of more than %d characters is refused", static_cast<int>(max_script_pattern_length));
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    return lua_gettop(state);
}

// Replaces table.field with `wrapper`, which gets the original as its upvalue 1.
void wrap_library_function(lua_State* state, const char* table, const char* field, const lua_CFunction wrapper)
{
    lua_pushstring(state, table);
    lua_rawget(state, LUA_GLOBALSINDEX);
    lua_pushstring(state, field);
    lua_pushstring(state, field);
    lua_rawget(state, -3);
    lua_pushcclosure(state, wrapper, 1);
    lua_rawset(state, -3);
    lua_pop(state, 1);
}

// Error handler of protected calls: the message plus the script positions of the stack.
int traceback(lua_State* state)
{
    std::string message = lua_isstring(state, 1) ? lua_tostring(state, 1) : "(error object is not a string)";
    lua_Debug frame{};
    for (int level = 1; level < 12 && lua_getstack(state, level, &frame) != 0; ++level)
    {
        if (lua_getinfo(state, "Sl", &frame) != 0 && frame.currentline > 0)
        {
            message += "\n  at ";
            message += frame.short_src;
            message += ":" + std::to_string(frame.currentline);
        }
    }
    lua_pushstring(state, message.c_str());
    return 1;
}

int lua_print(lua_State* state)
{
    std::string line;
    const auto count = lua_gettop(state);
    for (int index = 1; index <= count; ++index)
    {
        if (index > 1)
        {
            line += '\t';
        }
        switch (lua_type(state, index))
        {
        case LUA_TSTRING:
        case LUA_TNUMBER:
            line += lua_tostring(state, index);
            break;
        case LUA_TBOOLEAN:
            line += lua_toboolean(state, index) != 0 ? "true" : "false";
            break;
        default:
            line += lua_typename(state, lua_type(state, index));
            break;
        }
    }
    host_of(state).write_output(std::move(line));
    return 0;
}

// The framework's message sinks (PGDebug.lua formats, these write): _ScriptMessage (ScriptMessage,
// DebugMessage), _MessagePopup (MessageBox), and _OuputDebug (OutputDebug; the original's spelling).
// All three go to the script's output; the original shows popups in a window, an engine choice not
// to here.
int lua_debug_message(lua_State* state)
{
    auto& host = host_of(state);
    host.record(state, lua_tostring(state, lua_upvalueindex(1)), 1, true);
    host.write_output(lua_isstring(state, 1) ? lua_tostring(state, 1) : "");
    return 0;
}

// The Script object: tostring(Script) names the script (the framework prefixes its messages with
// it), and Script.Debug_Should_Issue_Event_Alert() asks whether every event should raise a popup:
// never here.
int lua_script_text(lua_State* state)
{
    const auto& name = host_of(state).script_name;
    lua_pushstring(state, name.substr(name.find_last_of('/') + 1U).c_str());
    return 1;
}

int lua_false(lua_State* state)
{
    lua_pushboolean(state, 0);
    return 1;
}

int lua_require(lua_State* state)
{
    const auto* name = luaL_checkstring(state, 1);
    auto& host = host_of(state);
    if (const auto loaded = host.load_module(name); !loaded)
    {
        return luaL_error(state, "%s", loaded.error().message.c_str());
    }
    lua_pushboolean(state, 1);
    return 1;
}

// loadstring that refuses compiled chunks (Lua 5.0 does not verify bytecode).
int lua_safe_loadstring(lua_State* state)
{
    std::size_t length = 0;
    const auto* text = luaL_checklstring(state, 1, &length);
    const auto* chunk = luaL_optstring(state, 2, text);
    if (length > 0 && text[0] == '\033')
    {
        lua_pushnil(state);
        lua_pushstring(state, "compiled Lua chunks are not loaded");
        return 2;
    }
    if (luaL_loadbuffer(state, text, length, chunk) != 0)
    {
        lua_pushnil(state);
        lua_insert(state, -2);
        return 2;
    }
    return 1;
}

int lua_random(lua_State* state)
{
    const auto value = host_of(state).next_random();
    switch (lua_gettop(state))
    {
    case 0:
        lua_pushnumber(state, value);
        return 1;
    case 1:
    {
        const auto upper = luaL_checkint(state, 1);
        luaL_argcheck(state, upper >= 1, 1, "interval is empty");
        lua_pushnumber(state, std::floor(value * upper) + 1);
        return 1;
    }
    default:
    {
        const auto lower = luaL_checkint(state, 1);
        const auto upper = luaL_checkint(state, 2);
        luaL_argcheck(state, lower <= upper, 2, "interval is empty");
        lua_pushnumber(state, std::floor(value * (upper - lower + 1)) + lower);
        return 1;
    }
    }
}

int lua_randomseed(lua_State* state)
{
    // The host's stream is seeded by the engine; scripts cannot reseed it.
    host_of(state).record(state, "math.randomseed", 1, true);
    return 0;
}

// Hooked replacements for coroutine.create and coroutine.wrap (5.0 threads do not inherit hooks).
int lua_coroutine_create(lua_State* state)
{
    luaL_argcheck(state, lua_isfunction(state, 1) && !lua_iscfunction(state, 1), 1, "Lua function expected");
    auto* thread = lua_newthread(state);
    host_of(state).hook_thread(thread);
    lua_pushvalue(state, 1);
    lua_xmove(state, thread, 1);
    return 1;
}

int lua_create_thread(lua_State* state)
{
    // Called as Create_Thread(...): argument 1 is the Create_Thread table itself.
    lua_remove(state, 1);
    auto& host = host_of(state);
    host.record(state, "Create_Thread", 1, true);
    std::string name = "(function)";
    if (lua_type(state, 1) == LUA_TSTRING)
    {
        name = lua_tostring(state, 1);
        lua_pushvalue(state, 1);
        lua_rawget(state, LUA_GLOBALSINDEX);
        lua_replace(state, 1);
    }
    if (!lua_isfunction(state, 1) || lua_iscfunction(state, 1))
    {
        return luaL_error(state, "Create_Thread: '%s' is not a script function", name.c_str());
    }
    if (host.live_thread_count() >= max_script_threads)
    {
        return luaL_error(state, "Create_Thread: more than %d script threads", static_cast<int>(max_script_threads));
    }
    // Arguments after the function become the thread's first arguments.
    const auto arguments = lua_gettop(state) - 1;
    lua_pushvalue(state, 1);
    lua_remove(state, 1);
    const auto id = static_cast<lua_Number>(host.next_thread_id);
    host.spawn_thread(state, name, arguments);
    lua_pushnumber(state, id);
    return 1;
}

// Create_Thread.Kill(id) and Create_Thread.Is_Thread_Active(id), on the ids Create_Thread returns.
int lua_kill_thread(lua_State* state)
{
    auto& host = host_of(state);
    host.record(state, "Create_Thread.Kill", 1, true);
    if (lua_isnumber(state, 1) != 0)
    {
        host.kill_thread(static_cast<std::int64_t>(lua_tonumber(state, 1)));
    }
    return 0;
}

int lua_is_thread_active(lua_State* state)
{
    auto& host = host_of(state);
    host.record(state, "Create_Thread.Is_Thread_Active", 1, true);
    const auto id = lua_isnumber(state, 1) != 0 ? static_cast<std::int64_t>(lua_tonumber(state, 1)) : -1;
    const bool active = std::ranges::any_of(host.threads,
                                            [&](const ScriptHost::Impl::Thread& thread)
                                            {
                                                return thread.id == id && thread.coroutine != nullptr && !thread.kill_requested;
                                            });
    lua_pushboolean(state, active ? 1 : 0);
    return 1;
}

// GameRandom(lower, upper): an integer in [lower, upper], as the stock scripts use it (indices
// into lists, GameRandom(1, 1)); GameRandom.Get_Float(): a number in [0, 1); GameRandom.Free_Random
// (lower, upper) like GameRandom. All draw from the host's seeded stream; that Free_Random is the
// original's unsynchronized variant is an inference from its name.
int random_range(lua_State* state, const int first, const char* name)
{
    auto& host = host_of(state);
    host.record(state, name, first, true);
    const auto lower = luaL_checkint(state, first);
    const auto upper = luaL_checkint(state, first + 1);
    luaL_argcheck(state, lower <= upper, first + 1, "interval is empty");
    lua_pushnumber(state, static_cast<lua_Number>(host.random_integer(lower, upper)));
    return 1;
}

int lua_game_random(lua_State* state)
{
    // Called as GameRandom(...): argument 1 is the GameRandom table itself.
    return random_range(state, 2, "GameRandom");
}

int lua_game_random_free(lua_State* state)
{
    return random_range(state, 1, "GameRandom.Free_Random");
}

int lua_game_random_float(lua_State* state)
{
    auto& host = host_of(state);
    host.record(state, "GameRandom.Get_Float", 1, true);
    lua_pushnumber(state, static_cast<lua_Number>(host.next_random()));
    return 1;
}

int lua_get_current_time(lua_State* state)
{
    auto& host = host_of(state);
    host.record(state, "GetCurrentTime", 1, true);
    lua_pushnumber(state, static_cast<lua_Number>(static_cast<double>(host.tick_count) * host.options.tick_seconds));
    return 1;
}

int lua_get_thread_id(lua_State* state)
{
    lua_pushnumber(state, static_cast<lua_Number>(host_of(state).current_thread));
    return 1;
}

// _ScriptExit: "set a flag in 'C' to terminate the whole script on next yield" (PGBase.lua).
int lua_request_exit(lua_State* state)
{
    auto& host = host_of(state);
    host.record(state, "_ScriptExit", 1, true);
    host.exit_requested = true;
    return 0;
}

// Per-thread values: ThreadValue(key) and ThreadValue.Set(key, value), kept in a registry table by
// thread id (-1 for code outside threads).
const char thread_values_key = 0;

void push_thread_values(lua_State* state)
{
    lua_pushlightuserdata(state, const_cast<char*>(&thread_values_key));
    lua_rawget(state, LUA_REGISTRYINDEX);
    lua_pushnumber(state, static_cast<lua_Number>(host_of(state).current_thread));
    lua_rawget(state, -2);
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushnumber(state, static_cast<lua_Number>(host_of(state).current_thread));
        lua_pushvalue(state, -2);
        lua_rawset(state, -4);
    }
    lua_remove(state, -2);
}

int lua_thread_value(lua_State* state)
{
    // Called as ThreadValue(key): argument 1 is the ThreadValue table itself.
    push_thread_values(state);
    lua_pushvalue(state, 2);
    lua_rawget(state, -2);
    return 1;
}

int lua_thread_value_set(lua_State* state)
{
    push_thread_values(state);
    lua_pushvalue(state, 1);
    lua_pushvalue(state, 2);
    lua_rawset(state, -3);
    return 0;
}

// ThreadValue.Reset(): "clear out the thread specific values" (PGBaseDefinitions.lua), every thread's.
int lua_thread_value_reset(lua_State* state)
{
    lua_pushlightuserdata(state, const_cast<char*>(&thread_values_key));
    lua_newtable(state);
    lua_rawset(state, LUA_REGISTRYINDEX);
    return 0;
}

void set_global_function(lua_State* state, const char* name, const lua_CFunction function)
{
    lua_pushstring(state, name);
    lua_pushcfunction(state, function);
    lua_rawset(state, LUA_GLOBALSINDEX);
}

void clear_global(lua_State* state, const char* name)
{
    lua_pushstring(state, name);
    lua_pushnil(state);
    lua_rawset(state, LUA_GLOBALSINDEX);
}

// Sets `table.field` (the table a global names) to a C function.
void set_library_function(lua_State* state, const char* table, const char* field, const lua_CFunction function)
{
    lua_pushstring(state, table);
    lua_rawget(state, LUA_GLOBALSINDEX);
    lua_pushstring(state, field);
    lua_pushcfunction(state, function);
    lua_rawset(state, -3);
    lua_pop(state, 1);
}

} // namespace

void set_callable_table(lua_State* state, const char* name, const lua_CFunction call, std::initializer_list<std::pair<const char*, lua_CFunction>> methods)
{
    lua_pushstring(state, name);
    lua_newtable(state);
    for (const auto& [field, method] : methods)
    {
        lua_pushstring(state, field);
        lua_pushcfunction(state, method);
        lua_rawset(state, -3);
    }
    lua_newtable(state);
    lua_pushstring(state, "__call");
    lua_pushcfunction(state, call);
    lua_rawset(state, -3);
    lua_setmetatable(state, -2);
    lua_rawset(state, LUA_GLOBALSINDEX);
}

ScriptHost::Impl& host_of(lua_State* state)
{
    lua_pushlightuserdata(state, const_cast<char*>(&host_registry_key));
    lua_rawget(state, LUA_REGISTRYINDEX);
    auto* host = static_cast<ScriptHost::Impl*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    EAW_ASSERT_MSG(host != nullptr, "a Lua state without its script host");
    return *host;
}

ScriptHost::Impl::Impl(const AssetReader& asset_reader, const ScriptHostOptions& host_options) : reader(asset_reader), options(host_options), random_state(host_options.random_seed)
{}

ScriptHost::Impl::~Impl()
{
    if (state != nullptr)
    {
        lua_close(state);
    }
}

Status ScriptHost::Impl::setup()
{
    state = lua_open();
    if (state == nullptr)
    {
        return make_error("could not create a Lua state");
    }
    lua_pushlightuserdata(state, const_cast<char*>(&host_registry_key));
    lua_pushlightuserdata(state, this);
    lua_rawset(state, LUA_REGISTRYINDEX);
    hook_thread(state);

    // Only the libraries the sandbox allows; no io, os, debug, or library loading.
    luaopen_base(state);
    lua_settop(state, 0);
    luaopen_table(state);
    lua_settop(state, 0);
    luaopen_string(state);
    lua_settop(state, 0);
    luaopen_math(state);
    lua_settop(state, 0);
    // newproxy makes userdata with a script-chosen metatable: forged engine objects and __gc
    // metamethods, which run outside the instruction budget.
    for (const auto* name : {"dofile", "loadfile", "load", "gcinfo", "newproxy"})
    {
        clear_global(state, name);
    }
    set_global_function(state, "pcall", lua_guarded_pcall);
    set_global_function(state, "xpcall", lua_guarded_xpcall);
    set_global_function(state, "collectgarbage", lua_full_collect);
    set_library_function(state, "coroutine", "resume", lua_guarded_resume);
    wrap_library_function(state, "string", "rep", lua_bounded_rep);
    for (const auto* name : {"find", "gfind", "gsub"})
    {
        wrap_library_function(state, "string", name, lua_bounded_pattern);
    }
    set_global_function(state, "loadstring", lua_safe_loadstring);
    set_global_function(state, "print", lua_print);
    set_global_function(state, "require", lua_require);
    set_library_function(state, "math", "random", lua_random);
    set_library_function(state, "math", "randomseed", lua_randomseed);
    set_library_function(state, "coroutine", "create", lua_coroutine_create);

    // The thread and event primitives the host implements itself (the scripts' own library builds
    // Sleep, ScriptExit, and PumpEvents on them); every other engine function is a stub.
    set_callable_table(state, "Create_Thread", lua_create_thread, {{"Kill", lua_kill_thread}, {"Is_Thread_Active", lua_is_thread_active}});
    set_callable_table(state, "GameRandom", lua_game_random, {{"Get_Float", lua_game_random_float}, {"Free_Random", lua_game_random_free}});
    set_global_function(state, "GetCurrentTime", lua_get_current_time);
    set_global_function(state, "GetThreadID", lua_get_thread_id);
    set_global_function(state, "_ScriptExit", lua_request_exit);
    lua_pushlightuserdata(state, const_cast<char*>(&thread_values_key));
    lua_newtable(state);
    lua_rawset(state, LUA_REGISTRYINDEX);
    set_callable_table(state, "ThreadValue", lua_thread_value, {{"Set", lua_thread_value_set}, {"Reset", lua_thread_value_reset}});
    install_events(state);
    for (const auto* sink : {"_ScriptMessage", "_MessagePopup", "_OuputDebug"})
    {
        lua_pushstring(state, sink);
        lua_pushstring(state, sink);
        lua_pushcclosure(state, lua_debug_message, 1);
        lua_rawset(state, LUA_GLOBALSINDEX);
    }
    lua_pushstring(state, "Script");
    lua_newtable(state);
    lua_pushstring(state, "Debug_Should_Issue_Event_Alert");
    lua_pushcfunction(state, lua_false);
    lua_rawset(state, -3);
    lua_newtable(state);
    lua_pushstring(state, "__tostring");
    lua_pushcfunction(state, lua_script_text);
    lua_rawset(state, -3);
    lua_setmetatable(state, -2);
    lua_rawset(state, LUA_GLOBALSINDEX);

    // coroutine.wrap over the hooked coroutine.create.
    constexpr std::string_view wrap = "local create, resume, unpack, error = coroutine.create, coroutine.resume, unpack, error\n"
                                      "coroutine.wrap = function(f)\n"
                                      "  local co = create(f)\n"
                                      "  return function(...)\n"
                                      "    local results = {resume(co, unpack(arg))}\n"
                                      "    if not results[1] then error(results[2], 2) end\n"
                                      "    return unpack(results, 2)\n"
                                      "  end\n"
                                      "end\n";
    if (luaL_loadbuffer(state, wrap.data(), wrap.size(), "=host") != 0)
    {
        return make_error(std::string("could not set up the script host: ") + lua_tostring(state, -1));
    }
    EAW_TRY(protected_call(0, "host setup"));

    install_stubs(*this);
    install_objects(*this);

    // Every script under Data/Scripts, by module name, for require.
    constexpr std::string_view prefix = "DATA/SCRIPTS/";
    for (const auto& asset : reader.list("DATA/SCRIPTS"))
    {
        const std::filesystem::path path(asset.virtual_path);
        if (ascii_lower(path.extension().string()) != ".lua")
        {
            continue;
        }
        auto relative = ascii_lower(asset.virtual_path);
        if (relative.starts_with(ascii_lower(prefix)))
        {
            relative.erase(0, prefix.size());
        }
        relative.erase(relative.size() - 4);
        module_paths.try_emplace(ascii_lower(path.stem().string()), asset.virtual_path);
        module_paths.try_emplace(relative, asset.virtual_path);
        std::ranges::replace(relative, '/', '.');
        module_paths.try_emplace(relative, asset.virtual_path);
    }
    return {};
}

void ScriptHost::Impl::hook_thread(lua_State* thread)
{
    lua_sethook(thread, budget_hook, LUA_MASKCOUNT, hook_interval);
}

std::uint64_t ScriptHost::Impl::service_interval() const
{
    lua_pushstring(state, "ServiceRate");
    lua_rawget(state, LUA_GLOBALSINDEX);
    const auto rate = lua_isnumber(state, -1) != 0 ? static_cast<double>(lua_tonumber(state, -1)) : 0.0;
    lua_pop(state, 1);
    if (!std::isfinite(rate) || rate <= 0.0)
    {
        return 1;
    }
    return std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(std::llround(rate / options.tick_seconds)));
}

void ScriptHost::Impl::halt(const std::string_view context, const std::string_view message)
{
    record_error(context, std::string(message) + " (the script is stopped)");
    exited = true;
    // The Lua state stays as it is until the host is destroyed; nothing calls into it again.
    threads.clear();
    events.clear();
}

std::size_t ScriptHost::Impl::live_thread_count() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(threads,
                                                          [](const Thread& thread)
                                                          {
                                                              return thread.coroutine != nullptr;
                                                          }));
}

void ScriptHost::Impl::finish_exit()
{
    exited = true;
    clear_events();
    for (auto& thread : threads)
    {
        end_thread(thread);
    }
}

void ScriptHost::Impl::end_thread(Thread& thread)
{
    if (thread.coroutine == nullptr)
    {
        return;
    }
    luaL_unref(state, LUA_REGISTRYINDEX, thread.reference);
    thread.coroutine = nullptr;
    lua_pushlightuserdata(state, const_cast<char*>(&thread_values_key));
    lua_rawget(state, LUA_REGISTRYINDEX);
    lua_pushnumber(state, static_cast<lua_Number>(thread.id));
    lua_pushnil(state);
    lua_rawset(state, -3);
    lua_pop(state, 1);
}

void ScriptHost::Impl::kill_thread(const std::int64_t id)
{
    for (auto& thread : threads)
    {
        if (thread.id != id)
        {
            continue;
        }
        if (id == current_thread)
        {
            thread.kill_requested = true;
        }
        else
        {
            end_thread(thread);
        }
    }
}

std::int64_t ScriptHost::Impl::random_integer(const std::int64_t lower, const std::int64_t upper)
{
    const auto span = static_cast<double>(upper - lower + 1);
    return lower + std::min(static_cast<std::int64_t>(next_random() * span), upper - lower);
}

double ScriptHost::Impl::next_random()
{
    // SplitMix64, seeded by the engine.
    random_state += 0x9E3779B97F4A7C15ULL;
    auto z = random_state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    z ^= z >> 31U;
    return static_cast<double>(z >> 11U) * (1.0 / 9007199254740992.0);
}

void ScriptHost::Impl::record_error(const std::string_view context, const std::string_view message)
{
    errors.push_back({std::string(context), std::string(message)});
}

void ScriptHost::Impl::record(lua_State* caller, const std::string_view name, const int first_argument, const bool implemented) const
{
    if (options.recorder != nullptr)
    {
        options.recorder->record(name, script_name, argument_signature(caller, first_argument), implemented);
    }
}

void ScriptHost::Impl::write_output(std::string line)
{
    if (output.size() < max_script_output)
    {
        output.push_back(std::move(line));
    }
    ++output_count;
}

void ScriptHost::Impl::warn(const std::string_view message)
{
    if (warnings.size() < max_script_warnings)
    {
        warnings.emplace_back(message);
    }
    ++warning_count;
}

Status ScriptHost::Impl::protected_call(const int arguments, const std::string_view context)
{
    // A nested call (require inside a script) shares the outer call's budget.
    if (call_depth == 0)
    {
        remaining_instructions = options.instruction_budget;
        budget_exhausted = false;
    }
    const auto base = lua_gettop(state) - arguments;
    lua_pushcfunction(state, traceback);
    lua_insert(state, base);
    ++call_depth;
    const auto status = lua_pcall(state, arguments, 0, base);
    --call_depth;
    lua_remove(state, base);
    if (call_depth == 0 && exit_requested)
    {
        finish_exit();
    }
    if (status == 0)
    {
        return {};
    }
    std::string message = lua_isstring(state, -1) ? lua_tostring(state, -1) : "(error object is not a string)";
    lua_pop(state, 1);
    if (status == LUA_ERRMEM)
    {
        halt(context, message);
        return make_error(std::string(context) + ": " + message + " (the script is stopped)");
    }
    record_error(context, message);
    return make_error(std::string(context) + ": " + message);
}

std::optional<std::string> ScriptHost::Impl::resolve_module(const std::string_view name) const
{
    auto key = ascii_lower(name);
    std::ranges::replace(key, '\\', '/');
    if (key.starts_with("data/scripts/"))
    {
        key.erase(0, std::string_view("data/scripts/").size());
    }
    if (key.ends_with(".lua"))
    {
        key.erase(key.size() - 4);
    }
    if (const auto found = module_paths.find(key); found != module_paths.end())
    {
        return found->second;
    }
    return std::nullopt;
}

Status ScriptHost::Impl::load_module(const std::string_view name)
{
    const auto path = resolve_module(name);
    if (!path)
    {
        return make_error("script '" + std::string(name) + "' was not found under Data/Scripts");
    }
    if (!loaded_modules.insert(ascii_lower(*path)).second)
    {
        return {};
    }
    EAW_TRY_ASSIGN(const auto bytes, reader.read(*path));
    if (!bytes.empty() && bytes.front() == std::byte{0x1B})
    {
        return make_error(*path + " is compiled Lua; compiled chunks are not run (Lua 5.0 does not verify bytecode)");
    }
    const std::string_view source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    for (auto& owned : script_owned_globals(source))
    {
        owned_globals.insert(std::move(owned));
    }
    const auto chunk_name = "@" + *path;
    if (luaL_loadbuffer(state, source.data(), source.size(), chunk_name.c_str()) != 0)
    {
        std::string message = lua_tostring(state, -1);
        lua_pop(state, 1);
        record_error("load " + *path, message);
        return make_error(*path + ": " + message);
    }
    return protected_call(0, "load " + *path);
}

void ScriptHost::Impl::spawn_thread(lua_State* caller, std::string function, const int arguments)
{
    // Stack: [arguments..., function]; the coroutine needs [function, arguments...].
    lua_insert(caller, -(arguments + 1));
    auto* coroutine = lua_newthread(caller);
    hook_thread(coroutine);
    const auto reference = luaL_ref(caller, LUA_REGISTRYINDEX);
    lua_xmove(caller, coroutine, arguments + 1);
    threads.push_back({reference, coroutine, std::move(function), static_cast<std::int64_t>(next_thread_id++), tick_count + 1, arguments, false});
}

std::vector<std::string> script_owned_globals(const std::string_view source)
{
    std::vector<std::string> names;
    const auto is_start = [](const char character)
    {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || character == '_';
    };
    const auto is_part = [&](const char character)
    {
        return is_start(character) || (character >= '0' && character <= '9');
    };
    std::size_t position = 0;
    char previous = ' ';
    bool after_function = false;
    while (position < source.size())
    {
        const auto character = source[position];
        // Comments: "--" to the end of the line, or "--[[ ... ]]".
        if (source.substr(position, 2) == "--")
        {
            if (source.substr(position + 2, 2) == "[[")
            {
                const auto end = source.find("]]", position + 4);
                position = end == std::string_view::npos ? source.size() : end + 2;
            }
            else
            {
                const auto end = source.find('\n', position);
                position = end == std::string_view::npos ? source.size() : end;
            }
            continue;
        }
        // Strings: quoted (with escapes) or long brackets.
        if (character == '"' || character == '\'')
        {
            ++position;
            while (position < source.size() && source[position] != character && source[position] != '\n')
            {
                position += source[position] == '\\' ? 2 : 1;
            }
            ++position;
            previous = character;
            continue;
        }
        if (source.substr(position, 2) == "[[")
        {
            const auto end = source.find("]]", position + 2);
            position = end == std::string_view::npos ? source.size() : end + 2;
            previous = ']';
            continue;
        }
        if (is_start(character))
        {
            const auto start = position;
            while (position < source.size() && is_part(source[position]))
            {
                ++position;
            }
            const auto name = source.substr(start, position - start);
            if (name == "function")
            {
                after_function = true;
                previous = 'f';
                continue;
            }
            if (after_function)
            {
                // "function Name" or "function Name.field": the first name is the script's own.
                names.emplace_back(name);
                after_function = false;
                previous = 'n';
                continue;
            }
            if (previous != '.' && previous != ':')
            {
                auto next = position;
                while (next < source.size() && (source[next] == ' ' || source[next] == '\t'))
                {
                    ++next;
                }
                if (next < source.size() && source[next] == '=' && (next + 1 >= source.size() || source[next + 1] != '='))
                {
                    names.emplace_back(name);
                }
            }
            previous = 'n';
            continue;
        }
        if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
        {
            after_function = after_function && character == '(' ? false : after_function;
            previous = character;
        }
        ++position;
    }
    std::ranges::sort(names);
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

// --- ScriptHost ---

ScriptHost::ScriptHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{}

ScriptHost::~ScriptHost() = default;

Result<std::unique_ptr<ScriptHost>> ScriptHost::create(const AssetReader& reader, const ScriptHostOptions& options)
{
    auto impl = std::make_unique<Impl>(reader, options);
    EAW_TRY(impl->setup());
    return std::unique_ptr<ScriptHost>(new ScriptHost(std::move(impl)));
}

Status ScriptHost::load(const std::string_view script)
{
    if (impl_->script_name.empty())
    {
        const auto path = impl_->resolve_module(script);
        impl_->script_name = path ? *path : std::string(script);
    }
    return impl_->load_module(script);
}

bool ScriptHost::has_function(const std::string_view name) const
{
    lua_pushlstring(impl_->state, name.data(), name.size());
    lua_rawget(impl_->state, LUA_GLOBALSINDEX);
    const bool function = lua_isfunction(impl_->state, -1);
    lua_pop(impl_->state, 1);
    return function;
}

Status ScriptHost::call(const std::string_view name)
{
    if (impl_->exited)
    {
        return {};
    }
    lua_pushlstring(impl_->state, name.data(), name.size());
    lua_rawget(impl_->state, LUA_GLOBALSINDEX);
    if (!lua_isfunction(impl_->state, -1))
    {
        lua_pop(impl_->state, 1);
        return make_error("'" + std::string(name) + "' is not a script function");
    }
    return impl_->protected_call(0, name);
}

Status ScriptHost::run_definitions()
{
    for (const auto* name : {"Base_Definitions", "Definitions"})
    {
        if (has_function(name))
        {
            return call(name);
        }
    }
    return {};
}

void ScriptHost::set_global(const std::string_view name, const ScriptValue& value)
{
    lua_pushlstring(impl_->state, name.data(), name.size());
    push_value(impl_->state, value);
    lua_rawset(impl_->state, LUA_GLOBALSINDEX);
}

Result<ScriptValue> ScriptHost::global(const std::string_view name) const
{
    lua_pushlstring(impl_->state, name.data(), name.size());
    lua_rawget(impl_->state, LUA_GLOBALSINDEX);
    auto value = read_value(impl_->state, -1);
    lua_pop(impl_->state, 1);
    if (!value)
    {
        return make_error("global '" + std::string(name) + "': " + value.error().message);
    }
    return value;
}

std::set<std::string> script_defined_globals(const AssetReader& reader)
{
    std::set<std::string> names;
    for (const auto& asset : reader.list("DATA/SCRIPTS"))
    {
        if (ascii_lower(std::filesystem::path(asset.virtual_path).extension().string()) != ".lua")
        {
            continue;
        }
        const auto bytes = reader.read(asset.virtual_path);
        if (!bytes || (!bytes->empty() && bytes->front() == std::byte{0x1B}))
        {
            continue;
        }
        for (auto& name : script_owned_globals(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size())))
        {
            names.insert(std::move(name));
        }
    }
    return names;
}

Status ScriptHost::start_thread(const std::string_view function)
{
    if (impl_->exited)
    {
        return {};
    }
    if (impl_->live_thread_count() >= max_script_threads)
    {
        return make_error("cannot start '" + std::string(function) + "': more than " + std::to_string(max_script_threads) + " script threads");
    }
    lua_pushlstring(impl_->state, function.data(), function.size());
    lua_rawget(impl_->state, LUA_GLOBALSINDEX);
    if (!lua_isfunction(impl_->state, -1) || lua_iscfunction(impl_->state, -1))
    {
        lua_pop(impl_->state, 1);
        return make_error("'" + std::string(function) + "' is not a script function");
    }
    impl_->spawn_thread(impl_->state, std::string(function), 0);
    return {};
}

void ScriptHost::tick()
{
    auto& host = *impl_;
    if (host.exited)
    {
        return;
    }
    ++host.tick_count;
    const auto count = host.threads.size();
    for (std::size_t index = 0; index < count && index < host.threads.size() && !host.exited; ++index)
    {
        if (host.threads[index].coroutine == nullptr || host.threads[index].wake_tick > host.tick_count)
        {
            continue;
        }
        host.remaining_instructions = host.options.instruction_budget;
        host.budget_exhausted = false;
        host.current_thread = host.threads[index].id;
        const auto arguments = host.threads[index].started ? 0 : host.threads[index].first_arguments;
        host.threads[index].started = true;
        auto* coroutine = host.threads[index].coroutine;
        // A resume is a call level: a require inside the thread shares its budget and never ends
        // the script (and with it this coroutine) before the resume returns.
        ++host.call_depth;
        const auto status = lua_resume(coroutine, arguments);
        --host.call_depth;
        host.current_thread = -1;
        if (status == LUA_ERRMEM)
        {
            host.halt("thread " + host.threads[index].function, lua_isstring(coroutine, -1) ? lua_tostring(coroutine, -1) : "not enough memory");
            break;
        }
        // Spawning may have grown the vector; refer to the thread by index from here.
        auto& resumed = host.threads[index];
        bool finished = false;
        if (status != 0)
        {
            host.record_error("thread " + resumed.function, lua_isstring(coroutine, -1) ? lua_tostring(coroutine, -1) : "(error object is not a string)");
            finished = true;
        }
        else
        {
            // A finished coroutine has no active function; a suspended one does. Yielding false
            // ends the thread ("return false to exit this thread", PGBase.lua).
            lua_Debug frame{};
            const bool yielded_false = lua_gettop(coroutine) > 0 && lua_isboolean(coroutine, -1) && lua_toboolean(coroutine, -1) == 0;
            finished = lua_getstack(coroutine, 0, &frame) == 0 || yielded_false || resumed.kill_requested;
            lua_settop(coroutine, 0);
            resumed.wake_tick = host.tick_count + host.service_interval();
        }
        if (finished)
        {
            host.end_thread(resumed);
        }
        if (host.exit_requested)
        {
            host.finish_exit();
        }
    }
    std::erase_if(host.threads,
                  [](const Impl::Thread& thread)
                  {
                      return thread.coroutine == nullptr;
                  });
}

std::size_t ScriptHost::live_threads() const noexcept
{
    return impl_->threads.size();
}

std::uint64_t ScriptHost::ticks() const noexcept
{
    return impl_->tick_count;
}

bool ScriptHost::exited() const noexcept
{
    return impl_->exited;
}

const std::vector<ScriptError>& ScriptHost::errors() const noexcept
{
    return impl_->errors;
}

const std::vector<std::string>& ScriptHost::output() const noexcept
{
    return impl_->output;
}

std::uint64_t ScriptHost::output_count() const noexcept
{
    return impl_->output_count;
}

const std::vector<std::string>& ScriptHost::warnings() const noexcept
{
    return impl_->warnings;
}

std::uint64_t ScriptHost::warning_count() const noexcept
{
    return impl_->warning_count;
}

} // namespace eaw
