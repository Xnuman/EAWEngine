#include "rti/type.hpp"

#include "core/fatal_error.hpp"

#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <string>

namespace eaw::rti {

namespace {

// The process-wide table of type names (AGENTS.md, process-wide services: the type registry).
struct Names
{
    std::mutex lock;
    std::map<std::string, std::uint16_t, std::less<>> ids;
    // By number; the 0-th is no type. A deque: names() hands out views of its strings, which a
    // vector would move (short strings live inside the string) when it grows.
    std::deque<std::string> names{std::string{}};
};

Names& names()
{
    static Names table;
    return table;
}

} // namespace

Type Type::of(const std::string_view name)
{
    auto& table = names();
    const std::scoped_lock guard(table.lock);
    if (const auto found = table.ids.find(name); found != table.ids.end())
    {
        return Type(found->second);
    }
    EAW_ASSERT_MSG(table.names.size() <= std::numeric_limits<std::uint16_t>::max(), "too many reflected types");
    const auto id = static_cast<std::uint16_t>(table.names.size());
    table.names.emplace_back(name);
    table.ids.emplace(std::string(name), id);
    return Type(id);
}

std::string_view Type::name() const
{
    auto& table = names();
    const std::scoped_lock guard(table.lock);
    return id_ < table.names.size() ? std::string_view(table.names[id_]) : std::string_view{};
}

} // namespace eaw::rti
