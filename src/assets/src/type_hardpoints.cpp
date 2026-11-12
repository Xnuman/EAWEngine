// A type's hardpoint list (assets/type_hardpoints.hpp).

#include "assets/type_hardpoints.hpp"

#include "core/string_utils.hpp"
#include "assets/map_object_layout.hpp"

namespace eaw {

const XmlDefinition* find_hardpoint(const GameObjectCatalog& catalog, const std::string_view name)
{
    const auto candidates = catalog.find(name);
    for (const auto* candidate : candidates)
    {
        if (ascii_equal_ignoring_case(candidate->type, "HardPoint"))
        {
            return candidate;
        }
    }
    return candidates.empty() ? nullptr : candidates.front();
}

std::vector<TypeHardpoint> type_hardpoints(const GameObjectCatalog& catalog, const XmlDefinition& type)
{
    std::vector<TypeHardpoint> hardpoints;
    const auto* list = catalog.field(type, "HardPoints").field;
    if (list == nullptr)
    {
        return hardpoints;
    }
    for (const auto name : split_list(list->text))
    {
        hardpoints.push_back({std::string(name), find_hardpoint(catalog, name)});
    }
    return hardpoints;
}

} // namespace eaw
