// Walks played as clips (MovementProfile::walk_stop_parts): animation-led walkers (XML
// Locomotor_Has_Animation_Priority) whose start, stride, and stops follow their clips. The stock
// clips share two poses (docs/compatibility-research.md, "Walker clip joins" and "The turn kit"):
// MOVESTART ends where MOVE starts, MOVE_END clip n starts at n/parts of the stride, and every
// MOVE_END ends in IDLE's first frame, where MOVESTART and the turns begin. So a walk starts over
// MOVESTART's time, stops only at a part of its stride, and brakes over its whole MOVE_END clip
// before anything else begins.

#include "simulation/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

namespace {

// The distance braking linearly from `speed` to rest over `seconds` covers.
[[nodiscard]] float stop_distance(const float speed, const float seconds) noexcept
{
    return 0.5F * speed * seconds;
}

} // namespace

float SimulationWorld::move_speed(const std::size_t index, const float facing, const float distance, const float dt) noexcept
{
    return movements_[index].walk_stop_parts > 0 ? walk_speed(index, facing, distance, dt) : drive(index, wanted_speed(index, facing, distance), dt);
}

float SimulationWorld::walk_speed(const std::size_t index, const float facing, const float distance, const float dt) noexcept
{
    const auto& movement = movements_[index];
    auto& walk = walk_plans_[index];
    auto& speed = drive_speeds_[index];
    const auto travel = travel_speed(index);
    const auto stride = max_speeds_[index] * movement.walk_stride_seconds;
    const auto parts = static_cast<float>(movement.walk_stop_parts);
    // Begins the stop at the stride's part `boundary` (1..parts; 0 as MOVESTART ends), `over`
    // seconds before this tick ends, when stopping there lands closer to the destination than a part
    // later; `ahead` is the distance to the destination from there.
    const auto stop_if_closest = [&](const std::uint32_t boundary, const float over, const float ahead)
    {
        if (ahead > stop_distance(speed, movement.walk_stop_seconds) + 0.5F * stride / parts)
        {
            return;
        }
        walk.clip = WalkClip::stopping;
        walk.part = boundary % movement.walk_stop_parts;
        walk.phase = static_cast<float>(walk.part) / parts;
        walk.elapsed = over;
        walk.from_speed = speed;
        walk.ends_move = distance > 0.0F && move_targets_[index].has_value();
        walk.destination = move_targets_[index].value_or(Vector3{});
    };
    switch (walk.clip)
    {
    case WalkClip::stopping:
    case WalkClip::unplanned:
        return speed;
    case WalkClip::standing:
    {
        if (travel <= 0.0F || wanted_speed(index, facing, distance) <= 0.0F)
        {
            speed = 0.0F;
            return 0.0F;
        }
        walk = {};
        walk.clip = WalkClip::starting;
        // A walk too short for its start and its stop at the travel speed goes slower (an engine
        // choice: the clips keep their rate, so the feet slide a little on such a step).
        const auto shortest = stop_distance(travel, movement.walk_start_seconds + movement.walk_stop_seconds);
        walk.scale = distance < shortest ? std::max(distance / shortest, 0.0F) : 1.0F;
        [[fallthrough]];
    }
    case WalkClip::starting:
    {
        walk.elapsed += dt;
        speed = walk.scale * travel * std::min(walk.elapsed / movement.walk_start_seconds, 1.0F);
        if (walk.elapsed >= movement.walk_start_seconds)
        {
            // MOVESTART ends where MOVE starts, which is where MOVE_ENDONE starts.
            const auto over = walk.elapsed - movement.walk_start_seconds;
            walk.clip = WalkClip::moving;
            walk.elapsed = 0.0F;
            walk.phase = stride > 0.0F ? std::fmod(speed * over / stride, 1.0F) : 0.0F;
            stop_if_closest(0, over, distance - speed * (dt - over));
        }
        return speed;
    }
    case WalkClip::moving:
        break;
    }
    speed = drive(index, wanted_speed(index, facing, distance) * walk.scale, dt);
    if (!(stride > 0.0F) || !(speed > 0.0F))
    {
        return speed;
    }
    const auto before = walk.phase * parts;
    const auto after = before + speed * dt / stride * parts;
    walk.phase = std::fmod(after / parts, 1.0F);
    if (const auto boundary = std::floor(after); boundary > std::floor(before))
    {
        const auto over = (after - boundary) / parts * stride / speed;
        stop_if_closest(static_cast<std::uint32_t>(boundary), over, distance - speed * (dt - over));
    }
    return speed;
}

bool SimulationWorld::walk_straight(const std::size_t index, const float dt)
{
    const auto& movement = movements_[index];
    auto& walk = walk_plans_[index];
    const bool walking = walk.clip == WalkClip::starting || walk.clip == WalkClip::moving;
    const bool destination = move_targets_[index].has_value() && group_waiting_[index] == 0;
    if (walk.clip != WalkClip::stopping && (!walking || (destination && path_pending_[index] == 0)))
    {
        return false;
    }
    float step = 0.0F;
    if (walk.clip == WalkClip::stopping)
    {
        const auto seconds = movement.walk_stop_seconds;
        // The share of the stop's distance braking linearly covers by `share` of its time.
        const auto covered = [&](const float share)
        {
            return walk.from_speed * seconds * (share - 0.5F * share * share);
        };
        const auto before = std::min(walk.elapsed / seconds, 1.0F);
        walk.elapsed += dt;
        const auto after = std::min(walk.elapsed / seconds, 1.0F);
        step = covered(after) - covered(before);
        drive_speeds_[index] = walk.from_speed * (1.0F - after);
        arriving_[index] = 1;
        if (walk.elapsed >= seconds)
        {
            // The stop that ends its move ends the order, unless a newer one came meanwhile (it
            // goes on from rest, its MOVESTART starting where MOVE_END ended).
            if (const auto& target = move_targets_[index]; walk.ends_move && target && target->x == walk.destination.x && target->y == walk.destination.y && target->z == walk.destination.z)
            {
                move_targets_[index].reset();
                paths_[index].clear();
            }
            walk = {};
            drive_speeds_[index] = 0.0F;
        }
    }
    else
    {
        // Waiting for its route, it walks on; without a destination (a stop order, or its group
        // waits for it to fall back) it walks on to its stride's next part and stops there.
        const auto ahead = destination ? std::numeric_limits<float>::max() : 0.0F;
        step = walk_speed(index, 1.0F, ahead, dt) * dt;
    }
    const auto heading = headings_[index];
    const auto moved = keep_apart(index, {std::cos(heading) * step, std::sin(heading) * step, 0.0F});
    auto& position = positions_[index];
    Vector3 next{position.x + moved.x, position.y + moved.y, position.z};
    if (follows_ground_[index] != 0 && ground_.valid())
    {
        // Ground it may not enter holds it where it is while the clip plays on.
        if (const auto allowed = passable_types_[index]; allowed != 0)
        {
            if (const auto there = ground_.passability_at(next.x, next.y); there && (*there & allowed) == 0)
            {
                return true;
            }
        }
        next.z = ground_.surface_height(next.x, next.y);
    }
    position = next;
    return true;
}

} // namespace eaw
