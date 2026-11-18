#include "simulation/formation.hpp"

#include "math/scalar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace eaw {
namespace {

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

FootprintRect team_footprint(const TeamShape& shape, const std::span<const float> radii)
{
    if (shape.offsets.empty() || shape.offsets.size() != radii.size())
    {
        return {};
    }
    // Offsets are (forward, right); the rectangle's center is (forward, left).
    auto front = -std::numeric_limits<float>::max();
    auto back = std::numeric_limits<float>::max();
    auto right = -std::numeric_limits<float>::max();
    auto left = std::numeric_limits<float>::max();
    for (std::size_t member = 0; member < radii.size(); ++member)
    {
        const auto [forward, rightward] = shape.offsets[member];
        front = std::max(front, forward + radii[member]);
        back = std::min(back, forward - radii[member]);
        right = std::max(right, rightward + radii[member]);
        left = std::min(left, rightward - radii[member]);
    }
    return {0.5F * (front - back), 0.5F * (right - left), {0.5F * (front + back), -0.5F * (right + left), 0.0F}};
}

TeamShape team_shape(const std::span<const FormationMember> members, const TeamArrangement arrangement)
{
    TeamShape shape;
    float member_pitch = 0.0F;
    float member_radius = 0.0F;
    for (const auto& member : members)
    {
        member_pitch = std::max(member_pitch, pitch(member));
        member_radius = std::max(member_radius, std::isfinite(member.radius) ? member.radius : 0.0F);
    }
    shape.offsets = team_offsets(arrangement, members.size(), member_pitch);
    float extent = 0.0F;
    for (std::size_t slot = 0; slot < shape.offsets.size(); ++slot)
    {
        const auto& member = members[slot];
        const auto ragged = std::isfinite(member.team.raggedness) ? std::clamp(member.team.raggedness, 0.0F, 10.0F) * member_pitch : 0.0F;
        auto& [ahead, aside] = shape.offsets[slot];
        ahead += jitter(member.id, 1U) * ragged;
        aside += jitter(member.id, 2U) * ragged;
        extent = std::max(extent, std::hypot(ahead, aside));
    }
    shape.extent = extent + member_radius;
    return shape;
}

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
                              return members[left].id < members[right].id;
                          });
        double x = 0.0;
        double y = 0.0;
        std::vector<FormationMember> team;
        for (const auto index : unit.members)
        {
            unit.order = std::min(unit.order, members[index].order);
            x += members[index].position.x;
            y += members[index].position.y;
            team.push_back(members[index]);
        }
        unit.id = members[unit.members.front()].id;
        const auto count = static_cast<double>(unit.members.size());
        unit.position = {static_cast<float>(x / count), static_cast<float>(y / count), first.position.z};
        auto shape = team_shape(team, first.team.arrangement);
        unit.offsets = std::move(shape.offsets);
        unit.pitch = pitch_of(shape.extent, first.team.spacing);
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
                          return units[left].id < units[right].id;
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
                              return units[left].id < units[right].id;
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

std::vector<FormationSlot> cluster_slots(const std::span<const FormationMember> members, const Vector3 leader_slot, const float facing, const bool two_in_column)
{
    // A further cluster moves out along its direction in steps of half its pitch, at most this many
    // (an engine choice; clusters are a few pitches across, so this is never reached in practice).
    constexpr int cluster_place_steps = 64;
    const auto count = members.size();
    std::vector<FormationSlot> slots(count);
    if (count == 0)
    {
        return slots;
    }
    const Vector2 forward{std::cos(facing), std::sin(facing)};
    const Vector2 right{forward.y, -forward.x};
    const auto side_of = [&](const std::size_t member)
    {
        return members[member].position.x * right.x + members[member].position.y * right.y;
    };
    const auto depth_of = [&](const std::size_t member)
    {
        return members[member].position.x * forward.x + members[member].position.y * forward.y;
    };
    const auto across = [&](const std::size_t left, const std::size_t right_member)
    {
        const auto left_side = side_of(left);
        const auto right_side = side_of(right_member);
        return left_side != right_side ? left_side < right_side : members[left].id < members[right_member].id;
    };
    // Each member's offset (forward, right) in the formation, before the leader's moves to its slot.
    std::vector<Vector2> at(count);
    if (count == 2)
    {
        const auto step = std::max(pitch(members[0]), pitch(members[1]));
        at[1] = two_in_column ? Vector2{-step, 0.0F} : Vector2{0.0F, across(1, 0) ? -step : step};
    }
    else if (count > 2)
    {
        struct Cluster
        {
            Vector2 center;
            Vector2 centroid;
            float pitch{};
            float reach{};
        };
        const auto cluster_count = (count + max_cluster_size - 1) / max_cluster_size;
        std::vector<Cluster> clusters(cluster_count);
        std::size_t first = 0;
        for (std::size_t cluster = 0; cluster < cluster_count; ++cluster)
        {
            const auto size = count / cluster_count + (cluster < count % cluster_count ? 1U : 0U);
            auto& placed = clusters[cluster];
            auto largest = 0.0F;
            for (auto member = first; member < first + size; ++member)
            {
                placed.pitch = std::max(placed.pitch, pitch(members[member]));
                largest = std::max(largest, members[member].radius);
                placed.centroid += Vector2{members[member].position.x, members[member].position.y} * (1.0F / static_cast<float>(size));
            }
            const auto front = (size + 1) / 2;
            const auto back = size / 2;
            // Rows by where members stand along the facing (the owner, 2026-11-18: so their ways do not
            // cross): the furthest forward take the front row; the leader always stands in it.
            std::vector<std::size_t> by_depth(size);
            std::iota(by_depth.begin(), by_depth.end(), first);
            std::stable_sort(by_depth.begin() + (cluster == 0 ? 1 : 0), by_depth.end(),
                             [&](const std::size_t left, const std::size_t right_member)
                             {
                                 return depth_of(left) > depth_of(right_member);
                             });
            for (const auto& [row_first, row_size, ahead] : {std::tuple{std::size_t{0}, front, back > 0 ? 0.5F : 0.0F}, std::tuple{front, back, -0.5F}})
            {
                std::vector<std::size_t> row(by_depth.begin() + static_cast<std::ptrdiff_t>(row_first), by_depth.begin() + static_cast<std::ptrdiff_t>(row_first + row_size));
                std::ranges::sort(row, across);
                for (std::size_t column = 0; column < row.size(); ++column)
                {
                    at[row[column]] = {ahead * placed.pitch, (static_cast<float>(column) - 0.5F * static_cast<float>(row.size() - 1U)) * placed.pitch};
                }
            }
            placed.reach = std::hypot(0.5F * static_cast<float>(front - 1U) * placed.pitch + largest, (back > 0 ? 0.5F * placed.pitch : 0.0F) + largest);
            // The cluster's place: the first at the origin; a further one along the direction its
            // members stand in from the first cluster's (in the formation's frame), clear of the
            // clusters before it.
            if (cluster > 0)
            {
                const auto from_first = placed.centroid - clusters.front().centroid;
                const Vector2 way{dot(from_first, forward), dot(from_first, right)};
                const auto direction = length(way) > 1.0e-3F ? way * (1.0F / length(way)) : Vector2{-1.0F, 0.0F};
                auto distance = placed.reach + clusters.front().reach;
                for (int step = 0; step < cluster_place_steps; ++step)
                {
                    placed.center = direction * distance;
                    const bool clear = std::ranges::all_of(clusters.begin(), clusters.begin() + static_cast<std::ptrdiff_t>(cluster),
                                                           [&](const Cluster& other)
                                                           {
                                                               return length(placed.center - other.center) >= placed.reach + other.reach;
                                                           });
                    if (clear)
                    {
                        break;
                    }
                    distance += 0.5F * placed.pitch;
                }
            }
            for (auto member = first; member < first + size; ++member)
            {
                at[member] += placed.center;
            }
            first += size;
        }
    }
    for (std::size_t member = 0; member < count; ++member)
    {
        const auto offset = at[member] - at.front();
        slots[member] = {members[member].id, {leader_slot.x + forward.x * offset.x + right.x * offset.y, leader_slot.y + forward.y * offset.x + right.y * offset.y, leader_slot.z}};
    }
    return slots;
}

} // namespace eaw
