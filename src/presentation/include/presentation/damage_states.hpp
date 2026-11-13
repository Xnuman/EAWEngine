#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

class GameObjectCatalog;
struct XmlDefinition;

// One damage state of a land model: shown while the health fraction is at or below `threshold`
// and above the next step's (XML Land_Damage_Thresholds, descending), as the model's _ALT<n>
// meshes and effects numbered `alternate` (XML Land_Damage_Alternates, the parallel list).
struct DamageStep
{
    float threshold{};
    std::uint32_t alternate{};
};

// A type's damage states, in file order; empty without both lists. The lists pair up to the
// shorter one; a bad number ends them there (reported to `warnings` when given).
[[nodiscard]] std::vector<DamageStep> type_damage_states(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>* warnings = nullptr);

// The step a health fraction (health / max health) shows: the last whose threshold it is at or
// below; none above the first threshold or without steps.
[[nodiscard]] std::optional<std::size_t> damage_step(std::span<const DamageStep> steps, float health_fraction) noexcept;

// The _ALT<n> state to draw at a health fraction: its step's alternate, 0 (intact) without one.
[[nodiscard]] std::uint32_t damage_alternate(std::span<const DamageStep> steps, float health_fraction) noexcept;

} // namespace eaw
