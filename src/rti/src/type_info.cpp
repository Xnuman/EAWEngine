#include "rti/type_info.hpp"

#include <algorithm>
#include <map>
#include <mutex>

namespace eaw::rti {

namespace {

// The process-wide type registry (AGENTS.md, process-wide services), by type number. A map: entries
// never move, so the pointers get() hands out stay valid while more types register.
struct Registry
{
    std::mutex lock;
    std::map<std::uint16_t, TypeInfo> types;
};

Registry& registry()
{
    static Registry instance;
    return instance;
}

} // namespace

const TypeInfo* TypeInfo::get(const Type type)
{
    auto& types = registry();
    const std::scoped_lock guard(types.lock);
    const auto found = types.types.find(type.id());
    return found != types.types.end() ? &found->second : nullptr;
}

const TypeInfo* TypeInfo::get(const std::string_view name)
{
    return get(Type::of(name));
}

std::vector<const TypeInfo*> TypeInfo::all()
{
    auto& types = registry();
    const std::scoped_lock guard(types.lock);
    std::vector<const TypeInfo*> result;
    result.reserve(types.types.size());
    for (const auto& [id, info] : types.types)
    {
        result.push_back(&info);
    }
    return result;
}

TypeInfo& TypeInfo::entry(const Type type, bool& made)
{
    auto& types = registry();
    const std::scoped_lock guard(types.lock);
    const auto [found, inserted] = types.types.try_emplace(type.id());
    made = inserted;
    return found->second;
}

TypeInfo& TypeInfo::entry(const Type type)
{
    bool made = false;
    auto& info = entry(type, made);
    EAW_ASSERT_MSG(!made, std::string("a parent type is registered before its children: ") + std::string(type.name()));
    return info;
}

const Property* TypeInfo::property(const std::string_view name) const
{
    const auto found = std::ranges::find(properties_, name, &Property::name);
    return found != properties_.end() ? &*found : nullptr;
}

bool TypeInfo::is_base_of(const TypeInfo& type) const
{
    if (&type == this)
    {
        return true;
    }
    return std::ranges::any_of(type.parents_,
                               [&](const Type parent)
                               {
                                   const auto* info = get(parent);
                                   return info != nullptr && is_base_of(*info);
                               });
}

const Variant* TypeInfo::meta(const std::string_view name) const
{
    const auto found = meta_.find(name);
    return found != meta_.end() ? &found->second : nullptr;
}

Variant TypeInfo::construct_from(const std::initializer_list<Type> types, const std::span<Variant> arguments) const
{
    for (const auto& constructor : constructors_)
    {
        if (std::ranges::equal(constructor.arguments, types))
        {
            return constructor.construct(arguments);
        }
    }
    return {};
}

} // namespace eaw::rti
