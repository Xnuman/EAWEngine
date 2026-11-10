// Recording stubs: every engine name a script uses that the host does not implement is a stub
// object. Calling one records the call (name, script, argument types), keeps the script functions
// passed to it as callbacks the engine can call later, and returns a stub object for its result; indexing one gives a member stub ("GameRandom.Get_Float", "Find_Player().Get_Name"),
// so engine tables and the objects engine functions return both keep working while their use is
// measured. Stub objects are tables with a private metatable per name: pairs over one sees nothing,
// arithmetic gives 0, concatenation and tostring give "<name>", and comparing one with a number
// fails as a nil would (recorded as the script's error).

#include "script_host_internal.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace eaw {
namespace {

constexpr const char* origin_field = "__eaw_origin";

// `origin` names what produced the stub (argument signatures show it as object(origin)).
void push_stub(lua_State* state, const std::string& call_name, const std::string& member_prefix, const std::string& origin);

// The label a result is named by: the part after the last "()." (Find_Player().Get_Owner ->
// Get_Owner), so member names stay short however long the chain.
std::string result_label(const std::string& call_name)
{
    const auto last = call_name.rfind(").");
    return last == std::string::npos ? call_name : call_name.substr(last + 2);
}

// Metamethods; upvalue 1 is the stub's call name, upvalue 2 its member prefix.
int stub_call(lua_State* state)
{
    auto& host = host_of(state);
    const std::string name = lua_tostring(state, lua_upvalueindex(1));
    // Argument 1 is the stub itself.
    host.recorder.record(name, host.script_name, argument_signature(state, 2), false);
    for (int index = 2; index <= lua_gettop(state); ++index)
    {
        if (lua_isfunction(state, index) && !lua_iscfunction(state, index))
        {
            (void)host.register_callback(state, index, name);
        }
    }
    const auto label = result_label(name);
    push_stub(state, label + "()()", label + "().", label);
    return 1;
}

int stub_index(lua_State* state)
{
    if (lua_type(state, 2) != LUA_TSTRING)
    {
        lua_pushnil(state);
        return 1;
    }
    const std::string member = std::string(lua_tostring(state, lua_upvalueindex(2))) + lua_tostring(state, 2);
    push_stub(state, member, member + ".", member);
    return 1;
}

int stub_arithmetic(lua_State* state)
{
    lua_pushnumber(state, 0);
    return 1;
}

int stub_text(lua_State* state)
{
    lua_pushstring(state, ("<" + std::string(lua_tostring(state, lua_upvalueindex(1))) + ">").c_str());
    return 1;
}

int stub_concat(lua_State* state)
{
    // One side is the stub; the other keeps its text.
    std::string text;
    for (int index = 1; index <= 2; ++index)
    {
        if (lua_type(state, index) == LUA_TSTRING || lua_type(state, index) == LUA_TNUMBER)
        {
            text += lua_tostring(state, index);
        }
        else
        {
            text += "<" + std::string(lua_tostring(state, lua_upvalueindex(1))) + ">";
        }
    }
    lua_pushstring(state, text.c_str());
    return 1;
}

void push_stub(lua_State* state, const std::string& call_name, const std::string& member_prefix, const std::string& origin)
{
    lua_newtable(state);
    // One metatable per name, cached in the registry.
    const auto key = "eaw.stub." + call_name;
    lua_pushstring(state, key.c_str());
    lua_rawget(state, LUA_REGISTRYINDEX);
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        lua_newtable(state);
        const auto set = [&](const char* field, const lua_CFunction function)
        {
            lua_pushstring(state, field);
            lua_pushstring(state, call_name.c_str());
            lua_pushstring(state, member_prefix.c_str());
            lua_pushcclosure(state, function, 2);
            lua_rawset(state, -3);
        };
        set("__call", stub_call);
        set("__index", stub_index);
        set("__concat", stub_concat);
        set("__tostring", stub_text);
        for (const auto* name : {"__add", "__sub", "__mul", "__div", "__pow", "__unm"})
        {
            set(name, stub_arithmetic);
        }
        lua_pushstring(state, origin_field);
        lua_pushstring(state, origin.c_str());
        lua_rawset(state, -3);
        lua_pushstring(state, key.c_str());
        lua_pushvalue(state, -2);
        lua_rawset(state, LUA_REGISTRYINDEX);
    }
    lua_setmetatable(state, -2);
}

// __index of the globals table: names the scripts never define are engine names, answered with
// stubs; the scripts' own names read before they are set, and the standard names the sandbox
// leaves out, stay nil.
int globals_index(lua_State* state)
{
    if (lua_type(state, 2) != LUA_TSTRING)
    {
        lua_pushnil(state);
        return 1;
    }
    auto& host = host_of(state);
    const std::string name = lua_tostring(state, 2);
    static constexpr std::array<std::string_view, 11> standard_absent{"io", "os", "debug", "dofile", "loadfile", "load", "loadlib", "gcinfo", "LUA_PATH", "_LOADED", "_ALERT"};
    if (host.owned_globals.contains(name) || (host.options.script_globals && host.options.script_globals->contains(name)) || std::ranges::find(standard_absent, name) != standard_absent.end())
    {
        lua_pushnil(state);
        return 1;
    }
    push_stub(state, name, name + ".", name);
    return 1;
}

} // namespace

void push_global_stub(lua_State* state, const std::string& name)
{
    push_stub(state, name, name + ".", name);
}

void install_stubs(ScriptHost::Impl& host)
{
    auto* state = host.state;
    lua_newtable(state);
    lua_pushstring(state, "__index");
    lua_pushcfunction(state, globals_index);
    lua_rawset(state, -3);
    lua_setmetatable(state, LUA_GLOBALSINDEX);
}

std::string argument_signature(lua_State* state, const int first)
{
    std::string signature;
    const auto count = lua_gettop(state);
    for (int index = first; index <= count; ++index)
    {
        if (!signature.empty())
        {
            signature += ", ";
        }
        if (const auto origin = stub_origin(state, index))
        {
            signature += "object(" + *origin + ")";
            continue;
        }
        signature += lua_typename(state, lua_type(state, index));
    }
    return signature.empty() ? "()" : signature;
}

std::optional<std::string> stub_origin(lua_State* state, const int index)
{
    if (lua_type(state, index) != LUA_TTABLE || lua_getmetatable(state, index) == 0)
    {
        return std::nullopt;
    }
    lua_pushstring(state, origin_field);
    lua_rawget(state, -2);
    std::optional<std::string> origin;
    if (lua_type(state, -1) == LUA_TSTRING)
    {
        origin = lua_tostring(state, -1);
    }
    lua_pop(state, 2);
    return origin;
}

} // namespace eaw
