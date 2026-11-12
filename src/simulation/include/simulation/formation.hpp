#pragma once

// Formation layout for group move orders: ranked rows of slots facing the direction of travel,
// centered on the ordered point. Deterministic: the same members and target give the same slots.

#include "simulation/simulation_types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace eaw {

// How a team's members stand within the team (the team container's XML ContainerArrangement):
// rows of a near-square grid (stormtroopers), or a center and rings around it (rebel troopers).
enum class TeamArrangement : std::uint8_t
{
    rows,
    dartboard,
};

// The team an entity belongs to: entities with the same nonzero `id` stand together as one unit of
// a formation (a company's teams: XML Create_Team_Type, Max_Squad_Size). `spacing` is the team's
// gap to the next unit (the team container's FormationSpacing); `raggedness` moves the member's own
// slot up to that fraction of its slot pitch (XML FormationRaggedness; the scale is an engine choice).
struct FormationTeam
{
    std::uint32_t id{};
    TeamArrangement arrangement{TeamArrangement::rows};
    float spacing{0.5F};
    float raggedness{};
};

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
    FormationTeam team;
};

struct FormationSlot
{
    EntityId id;
    Vector3 position;
};

// The facing of a group moving from its members' centroid to `target` (radians about +Z); +X when
// the target is at the centroid.
[[nodiscard]] float formation_facing(std::span<const FormationMember> members, Vector3 target);

// One slot per member, in member order. Units (a member, or a team's members together) sort into
// ranks by order (then id); rows hold about twice as many columns as there are rows, front row
// first, each row as wide as its units need (pitch = diameter x (1 + spacing) of the row's largest
// unit; a team's diameter is that of its members' layout). Within a row, units take the slots in
// the order of where they stand across the facing, so their paths do not cross. A team's members
// stand by its arrangement around the team's slot (in id order), each moved by its raggedness
// (a deterministic offset from its id). The whole block is centered on `target`; slot heights are
// the target's.
[[nodiscard]] std::vector<FormationSlot> formation_slots(std::span<const FormationMember> members, Vector3 target, float facing);

} // namespace eaw
