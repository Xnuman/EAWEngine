#pragma once

// A game object type's hardpoints: the HardPoint definitions its XML HardPoints list names.

#include "assets/game_object_catalog.hpp"

#include <string>
#include <vector>

namespace eaw {

struct TypeHardpoint
{
    // The name as the list writes it, and its definition (a HardPoint is preferred when several
    // kinds share the name; null when nothing defines it).
    std::string name;
    const XmlDefinition* definition{};
};

// The entries of `type`'s HardPoints list (inherited through Variant_Of_Existing_Type), in order.
[[nodiscard]] std::vector<TypeHardpoint> type_hardpoints(const GameObjectCatalog& catalog, const XmlDefinition& type);

// The HardPoint definition of `name`: a HardPoint when several kinds share it, else the first.
[[nodiscard]] const XmlDefinition* find_hardpoint(const GameObjectCatalog& catalog, std::string_view name);

} // namespace eaw
