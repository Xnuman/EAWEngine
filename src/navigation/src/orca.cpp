#include "navigation/orca.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace eaw {

namespace {

// Lines closer to parallel than this (by the cross product of their unit directions) are treated
// as parallel. A numerical tolerance.
constexpr float parallel_tolerance = 1.0e-5F;

// Whether `velocity` lies in the forbidden half-plane of `line`.
[[nodiscard]] float violation(const OrcaLine& line, const Vector2& velocity) noexcept
{
    return cross(line.direction, line.point - velocity);
}

// The velocity on lines[index] within `max_speed` that every earlier line allows, closest to
// `preferred` (or farthest along `preferred` when `along` is set, `preferred` then being a unit
// direction); false when no point of the line qualifies.
bool solve_on_line(std::span<const OrcaLine> lines, const std::size_t index, const float max_speed, const Vector2& preferred, const bool along, Vector2& result) noexcept
{
    const auto& line = lines[index];
    const auto towards = dot(line.point, line.direction);
    const auto discriminant = towards * towards + max_speed * max_speed - dot(line.point, line.point);
    if (discriminant < 0.0F)
    {
        // The speed circle misses the line.
        return false;
    }
    const auto root = std::sqrt(discriminant);
    auto first = -towards - root;
    auto last = -towards + root;
    for (std::size_t earlier = 0; earlier < index; ++earlier)
    {
        const auto denominator = cross(line.direction, lines[earlier].direction);
        const auto numerator = cross(lines[earlier].direction, line.point - lines[earlier].point);
        if (std::fabs(denominator) <= parallel_tolerance)
        {
            if (numerator < 0.0F)
            {
                return false;
            }
            continue;
        }
        const auto at = numerator / denominator;
        if (denominator >= 0.0F)
        {
            last = std::min(last, at);
        }
        else
        {
            first = std::max(first, at);
        }
        if (first > last)
        {
            return false;
        }
    }
    if (along)
    {
        result = line.point + line.direction * (dot(preferred, line.direction) > 0.0F ? last : first);
    }
    else
    {
        result = line.point + line.direction * std::clamp(dot(line.direction, preferred - line.point), first, last);
    }
    return true;
}

// Adds the lines one at a time, moving the result onto a line whenever it falls outside it; returns
// the index of the first line that could not be met (lines.size() when every line is).
std::size_t solve_in_plane(std::span<const OrcaLine> lines, const float max_speed, const Vector2& preferred, const bool along, Vector2& result) noexcept
{
    if (along)
    {
        result = preferred * max_speed;
    }
    else if (dot(preferred, preferred) > max_speed * max_speed)
    {
        result = normalize_or(preferred, {}) * max_speed;
    }
    else
    {
        result = preferred;
    }
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        if (violation(lines[index], result) > 0.0F)
        {
            const auto before = result;
            if (!solve_on_line(lines, index, max_speed, preferred, along, result))
            {
                result = before;
                return index;
            }
        }
    }
    return lines.size();
}

// No velocity meets every line: from `first_failed` on, the velocity whose largest violation is
// smallest, each worse line in turn solved against the earlier ones projected onto it.
void solve_least_violation(std::span<const OrcaLine> lines, const std::size_t first_failed, const float max_speed, Vector2& result)
{
    auto worst = 0.0F;
    std::vector<OrcaLine> projected;
    for (auto index = first_failed; index < lines.size(); ++index)
    {
        const auto& line = lines[index];
        if (violation(line, result) <= worst)
        {
            continue;
        }
        projected.clear();
        for (std::size_t earlier = 0; earlier < index; ++earlier)
        {
            const auto& other = lines[earlier];
            OrcaLine bisector;
            const auto determinant = cross(line.direction, other.direction);
            if (std::fabs(determinant) <= parallel_tolerance)
            {
                if (dot(line.direction, other.direction) > 0.0F)
                {
                    // Same direction: the earlier line adds nothing.
                    continue;
                }
                bisector.point = (line.point + other.point) * 0.5F;
            }
            else
            {
                bisector.point = line.point + line.direction * (cross(other.direction, line.point - other.point) / determinant);
            }
            bisector.direction = normalize_or(other.direction - line.direction, {});
            projected.push_back(bisector);
        }
        const auto before = result;
        if (solve_in_plane(projected, max_speed, {-line.direction.y, line.direction.x}, true, result) < projected.size())
        {
            // Can only fail by rounding; the last result is still the best.
            result = before;
        }
        worst = violation(line, result);
    }
}

} // namespace

OrcaLine orca_line(const Vector2 offset, const Vector2 relative_velocity, const float reach, const float horizon, const float step, const float share, const Vector2 velocity) noexcept
{
    OrcaLine line;
    Vector2 correction;
    const auto distance_squared = dot(offset, offset);
    const auto reach_squared = reach * reach;
    if (distance_squared > reach_squared)
    {
        // Apart: the velocity obstacle is a cone cut off by the circle of contact at the horizon.
        const auto from_cutoff = relative_velocity - offset / horizon;
        const auto cutoff_squared = dot(from_cutoff, from_cutoff);
        const auto along = dot(from_cutoff, offset);
        if (along < 0.0F && along * along > reach_squared * cutoff_squared)
        {
            // Nearest the cut-off circle.
            const auto cutoff_length = std::sqrt(cutoff_squared);
            const auto unit = from_cutoff / cutoff_length;
            line.direction = {unit.y, -unit.x};
            correction = unit * (reach / horizon - cutoff_length);
        }
        else
        {
            // Nearest one of the cone's legs.
            const auto leg = std::sqrt(distance_squared - reach_squared);
            if (cross(offset, from_cutoff) > 0.0F)
            {
                line.direction = Vector2{offset.x * leg - offset.y * reach, offset.x * reach + offset.y * leg} / distance_squared;
            }
            else
            {
                line.direction = -Vector2{offset.x * leg + offset.y * reach, -offset.x * reach + offset.y * leg} / distance_squared;
            }
            correction = line.direction * dot(relative_velocity, line.direction) - relative_velocity;
        }
    }
    else
    {
        // Overlapping: part within one step.
        const auto from_cutoff = relative_velocity - offset / step;
        const auto cutoff_length = length(from_cutoff);
        const auto unit = normalize_or(from_cutoff, normalize_or(-offset, {1.0F, 0.0F}));
        line.direction = {unit.y, -unit.x};
        correction = unit * (reach / step - cutoff_length);
    }
    line.point = velocity + correction * share;
    return line;
}

Vector2 orca_velocity(const std::span<const OrcaLine> lines, const float max_speed, const Vector2 preferred)
{
    Vector2 result;
    const auto failed = solve_in_plane(lines, max_speed, preferred, false, result);
    if (failed < lines.size())
    {
        solve_least_violation(lines, failed, max_speed, result);
    }
    return result;
}

} // namespace eaw
