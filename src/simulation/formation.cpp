#include "eaw/formation.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eaw {
namespace {

bool lower_id(const EntityId left, const EntityId right) noexcept
{
    return left.index != right.index ? left.index < right.index : left.generation < right.generation;
}

// Slot pitch of a member: its diameter plus the spacing fraction of it, at least one world unit.
float pitch(const FormationMember& member) noexcept
{
    const auto spacing = std::isfinite(member.spacing) ? std::max(member.spacing, 0.0F) : 0.5F;
    const auto radius = std::isfinite(member.radius) ? std::max(member.radius, 0.0F) : 0.0F;
    return std::max(1.0F, 2.0F * radius * (1.0F + spacing));
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

    std::vector<std::size_t> ranked(members.size());
    std::iota(ranked.begin(), ranked.end(), std::size_t{0});
    std::ranges::sort(ranked,
                      [&](const std::size_t left, const std::size_t right)
                      {
                          if (members[left].order != members[right].order)
                          {
                              return members[left].order < members[right].order;
                          }
                          return lower_id(members[left].id, members[right].id);
                      });
    const auto count = members.size();
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
            row.pitch = std::max(row.pitch, pitch(members[ranked[index]]));
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
                              const auto left_side = members[left].position.x * right_x + members[left].position.y * right_y;
                              const auto right_side = members[right].position.x * right_x + members[right].position.y * right_y;
                              if (left_side != right_side)
                              {
                                  return left_side < right_side;
                              }
                              return lower_id(members[left].id, members[right].id);
                          });
        const auto width = row.pitch * static_cast<float>(row.members.size() - 1U);
        for (std::size_t column = 0; column < row.members.size(); ++column)
        {
            const auto side = -width * 0.5F + row.pitch * static_cast<float>(column);
            const auto member = row.members[column];
            slots[member] = {members[member].id, {target.x + forward_x * forward_offset + right_x * side, target.y + forward_y * forward_offset + right_y * side, target.z}};
        }
        forward_offset -= row.pitch * 0.5F;
    }
    return slots;
}

} // namespace eaw
