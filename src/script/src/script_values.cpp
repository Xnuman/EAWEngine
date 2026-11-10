// Values between the engine and Lua: ScriptValue's helpers, pushing a value onto a Lua stack, and
// reading one back with limits (a script's table may be nested arbitrarily deep, or refer to
// itself).

#include "script_host_internal.hpp"

#include <algorithm>

namespace eaw {
namespace {

// Read limits (engine choices): definition tables in the stock scripts nest at most a few levels
// and hold a few hundred entries, so these only stop runaway or cyclic tables.
constexpr int max_read_depth = 16;
constexpr std::size_t max_read_entries = 65'536;

// Sort order of table keys: numbers, then strings, then everything else.
int key_rank(const ScriptValue& key)
{
    switch (key.kind)
    {
    case ScriptValue::Kind::number:
        return 0;
    case ScriptValue::Kind::string:
        return 1;
    default:
        return 2;
    }
}

Result<ScriptValue> read_at(lua_State* state, const int index, const int depth, std::size_t& entries)
{
    ScriptValue value;
    switch (lua_type(state, index))
    {
    case LUA_TBOOLEAN:
        value.kind = ScriptValue::Kind::boolean;
        value.boolean = lua_toboolean(state, index) != 0;
        return value;
    case LUA_TNUMBER:
        value.kind = ScriptValue::Kind::number;
        value.number = static_cast<double>(lua_tonumber(state, index));
        return value;
    case LUA_TSTRING:
        value.kind = ScriptValue::Kind::string;
        value.text.assign(lua_tostring(state, index), lua_strlen(state, index));
        return value;
    case LUA_TFUNCTION:
        value.kind = ScriptValue::Kind::function;
        return value;
    case LUA_TUSERDATA:
        if (const auto object = object_at(state, index))
        {
            value = ScriptValue::real_object(*object, object_kind_name(state, *object));
        }
        return value;
    case LUA_TTABLE:
        break;
    default:
        // nil, and userdata or threads the engine has no use for.
        return value;
    }
    if (auto origin = stub_origin(state, index))
    {
        value.kind = ScriptValue::Kind::object;
        value.text = std::move(*origin);
        return value;
    }
    if (depth >= max_read_depth)
    {
        return make_error("a table is nested more than " + std::to_string(max_read_depth) + " levels deep (or refers to itself)");
    }
    value.kind = ScriptValue::Kind::table;
    lua_pushnil(state);
    while (lua_next(state, index) != 0)
    {
        if (++entries > max_read_entries)
        {
            lua_pop(state, 2);
            return make_error("tables hold more than " + std::to_string(max_read_entries) + " entries");
        }
        const auto top = lua_gettop(state);
        auto key = read_at(state, top - 1, depth + 1, entries);
        if (!key)
        {
            lua_pop(state, 2);
            return key.error();
        }
        auto field = read_at(state, top, depth + 1, entries);
        if (!field)
        {
            lua_pop(state, 2);
            return field.error();
        }
        value.fields.push_back({std::move(*key), std::move(*field)});
        lua_pop(state, 1);
    }
    std::ranges::stable_sort(value.fields,
                             [](const ScriptField& left, const ScriptField& right)
                             {
                                 const auto left_rank = key_rank(left.key);
                                 const auto right_rank = key_rank(right.key);
                                 if (left_rank != right_rank)
                                 {
                                     return left_rank < right_rank;
                                 }
                                 if (left_rank == 0)
                                 {
                                     return left.key.number < right.key.number;
                                 }
                                 return left_rank == 1 && left.key.text < right.key.text;
                             });
    return value;
}

} // namespace

ScriptValue ScriptValue::from_boolean(const bool value)
{
    ScriptValue result;
    result.kind = Kind::boolean;
    result.boolean = value;
    return result;
}

ScriptValue ScriptValue::from_number(const double value)
{
    ScriptValue result;
    result.kind = Kind::number;
    result.number = value;
    return result;
}

ScriptValue ScriptValue::from_string(const std::string_view value)
{
    ScriptValue result;
    result.kind = Kind::string;
    result.text = value;
    return result;
}

ScriptValue ScriptValue::object(const std::string_view name)
{
    ScriptValue result;
    result.kind = Kind::object;
    result.text = name;
    return result;
}

ScriptValue ScriptValue::real_object(const ScriptObject& object, const std::string_view kind_name)
{
    ScriptValue result;
    result.kind = Kind::object;
    result.text = kind_name;
    result.handle = object;
    return result;
}

const ScriptValue* ScriptValue::field(const std::string_view key) const
{
    const auto found = std::ranges::find_if(fields,
                                            [&](const ScriptField& entry)
                                            {
                                                return entry.key.kind == Kind::string && entry.key.text == key;
                                            });
    return found == fields.end() ? nullptr : &found->value;
}

const ScriptValue* ScriptValue::element(const double index) const
{
    const auto found = std::ranges::find_if(fields,
                                            [&](const ScriptField& entry)
                                            {
                                                return entry.key.kind == Kind::number && entry.key.number == index;
                                            });
    return found == fields.end() ? nullptr : &found->value;
}

void push_value(lua_State* state, const ScriptValue& value)
{
    switch (value.kind)
    {
    case ScriptValue::Kind::boolean:
        lua_pushboolean(state, value.boolean ? 1 : 0);
        return;
    case ScriptValue::Kind::number:
        lua_pushnumber(state, static_cast<lua_Number>(value.number));
        return;
    case ScriptValue::Kind::string:
        lua_pushlstring(state, value.text.data(), value.text.size());
        return;
    case ScriptValue::Kind::object:
        if (value.handle)
        {
            push_object(state, *value.handle);
            return;
        }
        push_global_stub(state, value.text);
        return;
    case ScriptValue::Kind::table:
        lua_newtable(state);
        for (const auto& [key, field] : value.fields)
        {
            push_value(state, key);
            push_value(state, field);
            if (lua_isnil(state, -2))
            {
                lua_pop(state, 2);
                continue;
            }
            lua_rawset(state, -3);
        }
        return;
    case ScriptValue::Kind::nil:
    case ScriptValue::Kind::function:
        lua_pushnil(state);
        return;
    }
}

Result<ScriptValue> read_value(lua_State* state, const int index)
{
    std::size_t entries = 0;
    const auto absolute = index < 0 ? lua_gettop(state) + index + 1 : index;
    return read_at(state, absolute, 0, entries);
}

} // namespace eaw
