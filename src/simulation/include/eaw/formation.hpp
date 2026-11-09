#pragma once

// Formation layout for group move orders: ranked rows of slots facing the direction of travel,
// centered on the ordered point. Deterministic: the same members and target give the same slots.

#include "eaw/simulation_types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

struct FormationMember
{
    EntityId id;
    Vector3 position;
    // Footprint radius (world units).
    float radius{};
    // Gap to the next slot as a fraction of the footprint diameter (XML FormationSpacing).
    float spacing{0.5F};
    // Rank: lower values stand in front (XML FormationOrder).
    std::uint32_t order{2};
};

struct FormationSlot
{
    EntityId id;
    Vector3 position;
};

// The facing of a group moving from its members' centroid to `target` (radians about +Z); +X when
// the target is at the centroid.
[[nodiscard]] float formation_facing(std::span<const FormationMember> members, Vector3 target);

// One slot per member, in member order. Members sort into ranks by order (then id); rows hold about
// twice as many columns as there are rows, front row first, each row as wide as its members need
// (pitch = diameter x (1 + spacing) of the row's largest member). Within a row, members take the
// slots in the order of where they stand across the facing, so their paths do not cross. The
// whole block is centered on `target`; slot heights are the target's.
[[nodiscard]] std::vector<FormationSlot> formation_slots(std::span<const FormationMember> members, Vector3 target, float facing);

} // namespace eaw
