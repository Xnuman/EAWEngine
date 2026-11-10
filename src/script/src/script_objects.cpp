// Engine objects as userdata (script_object.hpp). Each kind has one metatable in the registry: its
// __index resolves a method name through the kind's name-to-id table and returns a closure bound to
// the object (scripts call methods with a dot, so the object travels as an upvalue), or a recording
// stub for names the provider does not implement. Objects of interned kinds are kept in a weak table
// per kind, keyed by their index: a hit of the same generation is reused, an older one replaced.
// Indices stay below 2^32, so Lua 5.0's double keys hold them exactly.

#include "script_host_internal.hpp"

#include <new>
#include <string>

namespace eaw {
namespace {

// Registry keys (their addresses are the keys): the table of kind metatables by kind id, the table
// of method-name tables by kind id, and the table of interning tables by kind id.
const char kind_metatables_key = 0;
const char kind_methods_key = 0;
const char kind_objects_key = 0;
// Field of a kind's metatable holding its id.
constexpr const char* kind_field = "__eaw_kind";

void push_registry_table(lua_State* state, const char* key)
{
    lua_pushlightuserdata(state, const_cast<char*>(key));
    lua_rawget(state, LUA_REGISTRYINDEX);
}

// Pushes entry `id` of the registry table at `key`.
void push_kind_entry(lua_State* state, const char* key, const std::uint16_t kind)
{
    push_registry_table(state, key);
    lua_rawgeti(state, -1, static_cast<int>(kind) + 1);
    lua_remove(state, -2);
}

ScriptObjectProvider& provider_of(lua_State* state)
{
    auto* provider = host_of(state).options.objects;
    EAW_ASSERT_MSG(provider != nullptr, "an engine object without a provider");
    return *provider;
}

// Runs a provider call over the arguments on the stack and returns its results (a provider error
// becomes the script's error).
int finish_call(lua_State* state, const Status& status, const ScriptCall& call)
{
    if (!status)
    {
        return luaL_error(state, "%s", status.error().message.c_str());
    }
    return call.results();
}

// A bound method: upvalue 1 is the object, upvalue 2 the method id.
int object_method(lua_State* state)
{
    const auto object = object_at(state, lua_upvalueindex(1));
    const auto method = static_cast<std::size_t>(lua_tonumber(state, lua_upvalueindex(2)));
    auto& provider = provider_of(state);
    const auto& kind = provider.kinds()[object->kind];
    if (host_of(state).options.recorder != nullptr)
    {
        host_of(state).record(state, std::string(kind.name) + "." + std::string(kind.methods[method]), 1, true);
    }
    ScriptCall call(state, lua_gettop(state));
    const auto status = provider.call_method(*object, method, call);
    return finish_call(state, status, call);
}

// __index of a kind's objects: upvalue 1 is the kind id.
int object_index(lua_State* state)
{
    const auto kind = static_cast<std::uint16_t>(lua_tonumber(state, lua_upvalueindex(1)));
    if (lua_type(state, 2) != LUA_TSTRING)
    {
        lua_pushnil(state);
        return 1;
    }
    push_kind_entry(state, &kind_methods_key, kind);
    lua_pushvalue(state, 2);
    lua_rawget(state, -2);
    if (lua_isnumber(state, -1) != 0)
    {
        lua_pushvalue(state, 1);
        lua_insert(state, -2);
        lua_pushcclosure(state, object_method, 2);
        return 1;
    }
    // Not implemented: a recording stub named after the kind ("GameObject.Play_SFX_Event").
    const auto& kinds = provider_of(state).kinds();
    push_global_stub(state, std::string(kinds[kind].name) + "." + lua_tostring(state, 2));
    return 1;
}

int object_text(lua_State* state)
{
    const auto object = object_at(state, 1);
    const auto name = object ? object_kind_name(state, *object) : std::string_view("object");
    lua_pushstring(state, ("<" + std::string(name) + " " + std::to_string(object ? object->index : 0U) + ">").c_str());
    return 1;
}

// A provider function: upvalue 1 is the function id.
int provider_function(lua_State* state)
{
    const auto function = static_cast<std::size_t>(lua_tonumber(state, lua_upvalueindex(1)));
    auto& provider = provider_of(state);
    host_of(state).record(state, provider.functions()[function], 1, true);
    ScriptCall call(state, lua_gettop(state));
    const auto status = provider.call_function(function, call);
    return finish_call(state, status, call);
}

} // namespace

void install_objects(ScriptHost::Impl& host)
{
    if (host.options.objects == nullptr)
    {
        return;
    }
    auto* state = host.state;
    const auto kinds = host.options.objects->kinds();
    EAW_ASSERT_MSG(kinds.size() < 0xFFFFU, "too many script object kinds");
    for (const auto* key : {&kind_metatables_key, &kind_methods_key, &kind_objects_key})
    {
        lua_pushlightuserdata(state, const_cast<char*>(key));
        lua_newtable(state);
        lua_rawset(state, LUA_REGISTRYINDEX);
    }
    for (std::size_t kind = 0; kind < kinds.size(); ++kind)
    {
        const auto slot = static_cast<int>(kind) + 1;
        push_registry_table(state, &kind_methods_key);
        lua_newtable(state);
        for (std::size_t method = 0; method < kinds[kind].methods.size(); ++method)
        {
            lua_pushlstring(state, kinds[kind].methods[method].data(), kinds[kind].methods[method].size());
            lua_pushnumber(state, static_cast<lua_Number>(method));
            lua_rawset(state, -3);
        }
        lua_rawseti(state, -2, slot);
        lua_pop(state, 1);

        push_registry_table(state, &kind_metatables_key);
        lua_newtable(state);
        lua_pushstring(state, "__index");
        lua_pushnumber(state, static_cast<lua_Number>(kind));
        lua_pushcclosure(state, object_index, 1);
        lua_rawset(state, -3);
        lua_pushstring(state, "__tostring");
        lua_pushcfunction(state, object_text);
        lua_rawset(state, -3);
        lua_pushstring(state, kind_field);
        lua_pushnumber(state, static_cast<lua_Number>(kind));
        lua_rawset(state, -3);
        lua_rawseti(state, -2, slot);
        lua_pop(state, 1);

        // Interned objects: values weak, so unused objects are collected.
        push_registry_table(state, &kind_objects_key);
        lua_newtable(state);
        lua_newtable(state);
        lua_pushstring(state, "__mode");
        lua_pushstring(state, "v");
        lua_rawset(state, -3);
        lua_setmetatable(state, -2);
        lua_rawseti(state, -2, slot);
        lua_pop(state, 1);
    }
    const auto functions = host.options.objects->functions();
    for (std::size_t function = 0; function < functions.size(); ++function)
    {
        lua_pushlstring(state, functions[function].data(), functions[function].size());
        lua_pushnumber(state, static_cast<lua_Number>(function));
        lua_pushcclosure(state, provider_function, 1);
        lua_rawset(state, LUA_GLOBALSINDEX);
    }
}

void push_object(lua_State* state, const ScriptObject& object)
{
    const auto* provider = host_of(state).options.objects;
    EAW_ASSERT_MSG(provider != nullptr && object.kind < provider->kinds().size(), "an engine object of an unknown kind");
    const bool interned = provider->kinds()[object.kind].interned;
    if (interned)
    {
        push_kind_entry(state, &kind_objects_key, object.kind);
        lua_pushnumber(state, static_cast<lua_Number>(object.index));
        lua_rawget(state, -2);
        if (const auto existing = object_at(state, -1); existing && existing->generation == object.generation)
        {
            lua_remove(state, -2);
            return;
        }
        lua_pop(state, 1);
    }
    auto* storage = lua_newuserdata(state, sizeof(ScriptObject));
    new (storage) ScriptObject(object);
    push_kind_entry(state, &kind_metatables_key, object.kind);
    lua_setmetatable(state, -2);
    if (interned)
    {
        // Stack: interning table, object.
        lua_pushnumber(state, static_cast<lua_Number>(object.index));
        lua_pushvalue(state, -2);
        lua_rawset(state, -4);
        lua_remove(state, -2);
    }
}

std::optional<ScriptObject> object_at(lua_State* state, const int index)
{
    if (lua_type(state, index) != LUA_TUSERDATA || lua_getmetatable(state, index) == 0)
    {
        return std::nullopt;
    }
    lua_pushstring(state, kind_field);
    lua_rawget(state, -2);
    const bool ours = lua_isnumber(state, -1) != 0;
    lua_pop(state, 2);
    if (!ours)
    {
        return std::nullopt;
    }
    return *static_cast<const ScriptObject*>(lua_touserdata(state, index));
}

std::string_view object_kind_name(lua_State* state, const ScriptObject& object)
{
    const auto* provider = host_of(state).options.objects;
    return provider != nullptr && object.kind < provider->kinds().size() ? provider->kinds()[object.kind].name : std::string_view("object");
}

// --- ScriptCall ---

ScriptCall::ScriptCall(lua_State* state, const int arguments) noexcept : state_(state), arguments_(arguments)
{}

int ScriptCall::argument_count() const noexcept
{
    return arguments_;
}

bool ScriptCall::is_nil(const int argument) const
{
    return argument > arguments_ || lua_isnil(state_, argument);
}

std::optional<double> ScriptCall::number(const int argument) const
{
    if (argument > arguments_ || lua_type(state_, argument) != LUA_TNUMBER)
    {
        return std::nullopt;
    }
    return static_cast<double>(lua_tonumber(state_, argument));
}

std::optional<std::string_view> ScriptCall::string(const int argument) const
{
    if (argument > arguments_ || lua_type(state_, argument) != LUA_TSTRING)
    {
        return std::nullopt;
    }
    return std::string_view(lua_tostring(state_, argument), lua_strlen(state_, argument));
}

std::optional<ScriptObject> ScriptCall::object(const int argument) const
{
    return argument > arguments_ ? std::nullopt : object_at(state_, argument);
}

std::optional<std::size_t> ScriptCall::callback(const int argument, const std::string_view registered_by)
{
    if (argument > arguments_ || !lua_isfunction(state_, argument) || lua_iscfunction(state_, argument))
    {
        return std::nullopt;
    }
    return host_of(state_).register_callback(state_, argument, std::string(registered_by));
}

void ScriptCall::return_nil()
{
    lua_pushnil(state_);
    ++results_;
}

void ScriptCall::return_boolean(const bool value)
{
    lua_pushboolean(state_, value ? 1 : 0);
    ++results_;
}

void ScriptCall::return_number(const double value)
{
    lua_pushnumber(state_, static_cast<lua_Number>(value));
    ++results_;
}

void ScriptCall::return_string(const std::string_view value)
{
    lua_pushlstring(state_, value.data(), value.size());
    ++results_;
}

void ScriptCall::return_object(const ScriptObject& object)
{
    push_object(state_, object);
    ++results_;
}

void ScriptCall::return_objects(const std::span<const ScriptObject> objects)
{
    lua_newtable(state_);
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        push_object(state_, objects[index]);
        lua_rawseti(state_, -2, static_cast<int>(index + 1));
    }
    ++results_;
}

int ScriptCall::results() const noexcept
{
    return results_;
}

void ScriptCall::warn(const std::string_view message)
{
    host_of(state_).warn(message);
}

} // namespace eaw
