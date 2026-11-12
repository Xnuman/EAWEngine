#include "simulation/formation.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eaw {
namespace {

bool lower_id(const EntityId left, const EntityId right) noexcept
{
    return left.index != right.index ? left.index < right.index : left.generation < right.generation;
}

// Slot pitch of a footprint: its diameter plus the spacing fraction of it, at least one world unit.
float pitch_of(const float radius, const float spacing) noexcept
{
    const auto gap = std::isfinite(spacing) ? std::max(spacing, 0.0F) : 0.5F;
    const auto size = std::isfinite(radius) ? std::max(radius, 0.0F) : 0.0F;
    return std::max(1.0F, 2.0F * size * (1.0F + gap));
}

float pitch(const FormationMember& member) noexcept
{
    return pitch_of(member.radius, member.spacing);
}

// A deterministic value in [-1, 1] from an id and a salt (a 32-bit integer hash).
float jitter(const EntityId id, const std::uint32_t salt) noexcept
{
    auto value = id.index * 0x9E3779B1U ^ (id.generation + salt) * 0x85EBCA77U;
    value ^= value >> 15U;
    value *= 0x2C1B3C6DU;
    value ^= value >> 12U;
    value *= 0x297A2D39U;
    value ^= value >> 15U;
    return static_cast<float>(value & 0xFFFFU) / 32767.5F - 1.0F;
}

// Offsets (forward, right) of `count` members around their team's center, `pitch` apart.
std::vector<std::pair<float, float>> team_offsets(const TeamArrangement arrangement, const std::size_t count, const float pitch)
{
    std::vector<std::pair<float, float>> offsets;
    if (arrangement == TeamArrangement::dartboard)
    {
        // The center, then rings of 6, 12, 18, ... one pitch apart.
        offsets.emplace_back(0.0F, 0.0F);
        for (std::size_t ring = 1; offsets.size() < count; ++ring)
        {
            const auto slots = 6U * ring;
            for (std::size_t slot = 0; slot < slots && offsets.size() < count; ++slot)
            {
                const auto angle = two_pi * static_cast<float>(slot) / static_cast<float>(slots);
                offsets.emplace_back(std::cos(angle) * pitch * static_cast<float>(ring), std::sin(angle) * pitch * static_cast<float>(ring));
            }
        }
        return offsets;
    }
    // Rows of a near-square grid, front row first, centered.
    const auto columns = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
    const auto rows = (count + columns - 1U) / columns;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto row = index / columns;
        const auto in_row = std::min(columns, count - row * columns);
        const auto column = index % columns;
        offsets.emplace_back((static_cast<float>(rows - 1U) * 0.5F - static_cast<float>(row)) * pitch, (static_cast<float>(column) - static_cast<float>(in_row - 1U) * 0.5F) * pitch);
    }
    return offsets;
}

} // namespace

float formation_facing(const std::span<const FormationMember> members, const Vector3 target)
{
    if (members.empty())
    {
        return 0.0F;
    }
    double x = 0.0;
    double y = 0.0;
    for (const auto& member : members)
    {
        x += member.position.x;
        y += member.position.y;
    }
    const auto count = static_cast<double>(members.size());
    const auto dx = static_cast<float>(static_cast<double>(target.x) - x / count);
    const auto dy = static_cast<float>(static_cast<double>(target.y) - y / count);
    return dx * dx + dy * dy > 1.0e-6F ? std::atan2(dy, dx) : 0.0F;
}

std::vector<FormationSlot> formation_slots(const std::span<const FormationMember> members, const Vector3 target, const float facing)
{
    std::vector<FormationSlot> slots(members.size());
    if (members.empty())
    {
        return slots;
    }
    // Forward along the facing, right is its clockwise perpendicular.
    const auto forward_x = std::cos(facing);
    const auto forward_y = std::sin(facing);
    const auto right_x = forward_y;
    const auto right_y = -forward_x;

    // Units: each member alone, or a team's members together, in first-seen order.
    struct Unit
    {
        std::vector<std::size_t> members;
        EntityId id;
        Vector3 position;
        std::uint32_t order{};
        float pitch{};
        // Team members' offsets (forward, right) from the unit's slot.
        std::vector<std::pair<float, float>> offsets;
    };
    std::vector<Unit> units;
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        const auto team = members[index].team.id;
        auto found = team == 0 ? units.end()
                               : std::ranges::find_if(units,
                                                      [&](const Unit& unit)
                                                      {
                                                          return members[unit.members.front()].team.id == team;
                                                      });
        if (found == units.end())
        {
            units.push_back({{index}, members[index].id, members[index].position, members[index].order});
            continue;
        }
        found->members.push_back(index);
    }
    for (auto& unit : units)
    {
        const auto& first = members[unit.members.front()];
        if (first.team.id == 0)
        {
            unit.pitch = pitch(first);
            continue;
        }
        std::ranges::sort(unit.members,
                          [&](const std::size_t left, const std::size_t right)
                          {
                              return lower_id(members[left].id, members[right].id);
                          });
        float member_pitch = 0.0F;
        float member_radius = 0.0F;
        double x = 0.0;
        double y = 0.0;
        for (const auto index : unit.members)
        {
            member_pitch = std::max(member_pitch, pitch(members[index]));
            member_radius = std::max(member_radius, std::isfinite(members[index].radius) ? members[index].radius : 0.0F);
            unit.order = std::min(unit.order, members[index].order);
            x += members[index].position.x;
            y += members[index].position.y;
        }
        unit.id = members[unit.members.front()].id;
        const auto count = static_cast<double>(unit.members.size());
        unit.position = {static_cast<float>(x / count), static_cast<float>(y / count), first.position.z};
        unit.offsets = team_offsets(first.team.arrangement, unit.members.size(), member_pitch);
        float extent = 0.0F;
        for (std::size_t slot = 0; slot < unit.offsets.size(); ++slot)
        {
            const auto& member = members[unit.members[slot]];
            const auto ragged = std::isfinite(member.team.raggedness) ? std::clamp(member.team.raggedness, 0.0F, 10.0F) * member_pitch : 0.0F;
            auto& [ahead, aside] = unit.offsets[slot];
            ahead += jitter(member.id, 1U) * ragged;
            aside += jitter(member.id, 2U) * ragged;
            extent = std::max(extent, std::hypot(ahead, aside));
        }
        unit.pitch = pitch_of(extent + member_radius, first.team.spacing);
    }

    std::vector<std::size_t> ranked(units.size());
    std::iota(ranked.begin(), ranked.end(), std::size_t{0});
    std::ranges::sort(ranked,
                      [&](const std::size_t left, const std::size_t right)
                      {
                          if (units[left].order != units[right].order)
                          {
                              return units[left].order < units[right].order;
                          }
                          return lower_id(units[left].id, units[right].id);
                      });
    const auto count = units.size();
    const auto columns = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::sqrt(2.0 * static_cast<double>(count)))));

    struct Row
    {
        std::vector<std::size_t> members;
        float pitch{};
    };
    std::vector<Row> rows;
    for (std::size_t start = 0; start < count; start += columns)
    {
        Row row;
        for (auto index = start; index < std::min(count, start + columns); ++index)
        {
            row.members.push_back(ranked[index]);
            row.pitch = std::max(row.pitch, units[ranked[index]].pitch);
        }
        rows.push_back(std::move(row));
    }
    float depth = 0.0F;
    for (const auto& row : rows)
    {
        depth += row.pitch;
    }
    // Rows from the front (half a pitch ahead of the center line for the first) to the back.
    auto forward_offset = depth * 0.5F;
    for (auto& row : rows)
    {
        forward_offset -= row.pitch * 0.5F;
        // Members take slots left to right by where they stand across the facing.
        std::ranges::sort(row.members,
                          [&](const std::size_t left, const std::size_t right)
                          {
                              const auto left_side = units[left].position.x * right_x + units[left].position.y * right_y;
                              const auto right_side = units[right].position.x * right_x + units[right].position.y * right_y;
                              if (left_side != right_side)
                              {
                                  return left_side < right_side;
                              }
                              return lower_id(units[left].id, units[right].id);
                          });
        const auto width = row.pitch * static_cast<float>(row.members.size() - 1U);
        for (std::size_t column = 0; column < row.members.size(); ++column)
        {
            const auto side = -width * 0.5F + row.pitch * static_cast<float>(column);
            const auto& unit = units[row.members[column]];
            const Vector3 center{target.x + forward_x * forward_offset + right_x * side, target.y + forward_y * forward_offset + right_y * side, target.z};
            if (unit.offsets.empty())
            {
                slots[unit.members.front()] = {members[unit.members.front()].id, center};
                continue;
            }
            for (std::size_t slot = 0; slot < unit.members.size(); ++slot)
            {
                const auto [ahead, aside] = unit.offsets[slot];
                slots[unit.members[slot]] = {members[unit.members[slot]].id, {center.x + forward_x * ahead + right_x * aside, center.y + forward_y * ahead + right_y * aside, target.z}};
            }
        }
        forward_offset -= row.pitch * 0.5F;
    }
    return slots;
}

} // namespace eaw
