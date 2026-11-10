// Engine events and callbacks. The engine queues calls; the framework's PumpEvents drains them in a
// script thread after each yield: GetEvent() returns the next queued function (nil when none are
// left), GetEvent.Params() the argument table of the one it returned last (nil without arguments),
// and GetEvent.Reset() drops the queue (Common_Base_Definitions does so when a script is created).
// Functions scripts pass to engine functions (Event_Object_In_Range's handler) are kept as
// callbacks the engine can queue later.

#include "script_host_internal.hpp"

namespace eaw {
namespace {

// Registry key of the table mapping a callback function to its index.
const char callback_index_key = 0;

int lua_get_event(lua_State* state)
{
    auto& host = host_of(state);
    if (host.events.empty())
    {
        lua_pushnil(state);
        return 1;
    }
    const auto event = host.events.front();
    host.events.pop_front();
    luaL_unref(state, LUA_REGISTRYINDEX, host.current_arguments);
    host.current_arguments = event.arguments;
    lua_rawgeti(state, LUA_REGISTRYINDEX, event.function);
    luaL_unref(state, LUA_REGISTRYINDEX, event.function);
    return 1;
}

int lua_get_event_params(lua_State* state)
{
    const auto& host = host_of(state);
    if (host.current_arguments == LUA_NOREF)
    {
        lua_pushnil(state);
        return 1;
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, host.current_arguments);
    return 1;
}

int lua_get_event_reset(lua_State* state)
{
    host_of(state).clear_events();
    return 0;
}

} // namespace

void install_events(lua_State* state)
{
    set_callable_table(state, "GetEvent", lua_get_event, {{"Params", lua_get_event_params}, {"Reset", lua_get_event_reset}});
    lua_pushlightuserdata(state, const_cast<char*>(&callback_index_key));
    lua_newtable(state);
    lua_rawset(state, LUA_REGISTRYINDEX);
}

void ScriptHost::Impl::queue_call(const std::vector<ScriptValue>& arguments)
{
    QueuedEvent event;
    event.function = luaL_ref(state, LUA_REGISTRYINDEX);
    event.arguments = LUA_NOREF;
    if (!arguments.empty())
    {
        lua_newtable(state);
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            push_value(state, arguments[index]);
            lua_rawseti(state, -2, static_cast<int>(index + 1));
        }
        // unpack reads the length the table's n field gives (Lua 5.0), so nil arguments survive.
        lua_pushstring(state, "n");
        lua_pushnumber(state, static_cast<lua_Number>(arguments.size()));
        lua_rawset(state, -3);
        event.arguments = luaL_ref(state, LUA_REGISTRYINDEX);
    }
    events.push_back(event);
}

void ScriptHost::Impl::clear_events()
{
    for (const auto& event : events)
    {
        luaL_unref(state, LUA_REGISTRYINDEX, event.function);
        luaL_unref(state, LUA_REGISTRYINDEX, event.arguments);
    }
    events.clear();
    luaL_unref(state, LUA_REGISTRYINDEX, current_arguments);
    current_arguments = LUA_NOREF;
}

std::size_t ScriptHost::Impl::register_callback(lua_State* caller, const int index, const std::string& registered_by)
{
    const auto function = index < 0 ? lua_gettop(caller) + index + 1 : index;
    lua_pushlightuserdata(caller, const_cast<char*>(&callback_index_key));
    lua_rawget(caller, LUA_REGISTRYINDEX);
    lua_pushvalue(caller, function);
    lua_rawget(caller, -2);
    if (lua_isnumber(caller, -1) != 0)
    {
        const auto known = static_cast<std::size_t>(lua_tonumber(caller, -1));
        lua_pop(caller, 2);
        return known;
    }
    lua_pop(caller, 1);
    const auto added = callbacks.size();
    lua_pushvalue(caller, function);
    lua_pushnumber(caller, static_cast<lua_Number>(added));
    lua_rawset(caller, -3);
    lua_pop(caller, 1);
    lua_pushvalue(caller, function);
    callback_functions.push_back(luaL_ref(caller, LUA_REGISTRYINDEX));
    callbacks.push_back({registered_by});
    return added;
}

Status ScriptHost::queue_event(const std::string_view function, const std::vector<ScriptValue>& arguments)
{
    if (impl_->exited)
    {
        return {};
    }
    auto* state = impl_->state;
    lua_pushlstring(state, function.data(), function.size());
    lua_rawget(state, LUA_GLOBALSINDEX);
    if (!lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        return make_error("event handler '" + std::string(function) + "' is not a script function");
    }
    impl_->queue_call(arguments);
    return {};
}

Status ScriptHost::queue_callback(const std::size_t callback, const std::vector<ScriptValue>& arguments)
{
    if (callback >= impl_->callback_functions.size())
    {
        return make_error("script callback " + std::to_string(callback) + " does not exist (" + std::to_string(impl_->callback_functions.size()) + " registered)");
    }
    if (impl_->exited)
    {
        return {};
    }
    lua_rawgeti(impl_->state, LUA_REGISTRYINDEX, impl_->callback_functions[callback]);
    impl_->queue_call(arguments);
    return {};
}

std::size_t ScriptHost::queued_events() const noexcept
{
    return impl_->events.size();
}

const std::vector<ScriptCallback>& ScriptHost::callbacks() const noexcept
{
    return impl_->callbacks;
}

} // namespace eaw
