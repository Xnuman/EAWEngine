// Walks and turns played as clips (MovementProfile::walk_stop_parts, turn_begin_seconds):
// animation-led walkers (XML Locomotor_Has_Animation_Priority) whose start, stride, stops, and turns
// in place follow their clips. The stock clips share two poses (docs/compatibility-research.md,
// "Walker clip joins" and "The turn kit"): MOVESTART ends where MOVE starts, MOVE_END clip n starts
// at n/parts of the stride, and every MOVE_END and turn END ends in IDLE's first frame, where
// MOVESTART and the turns begin; a turn's BEGIN ends, and its END starts, where the loop, HALF, and
// QUARTER cycles start and end. So a walk starts over MOVESTART's time, stops only at a part of its
// stride, and brakes over its whole MOVE_END clip before anything else begins, and a turn is
// BEGIN, whole cycles, and END.

#include "simulation_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace eaw {

using namespace simulation_detail;

namespace {

// The distance braking linearly from `speed` to rest over `seconds` covers.
[[nodiscard]] float stop_distance(const float speed, const float seconds) noexcept
{
    return 0.5F * speed * seconds;
}

} // namespace

float SimulationWorld::MovementSystem::move_speed(const View& view, const std::size_t index, const float facing, const float distance, const float dt) noexcept
{
    auto& locomotor = view.get<LocomotorColumns>();
    return locomotor.movements[index]->walk_stop_parts > 0 ? walk_speed(view, index, facing, distance, dt) : drive(view, index, wanted_speed(view, index, facing, distance), dt);
}

float SimulationWorld::MovementSystem::walk_speed(const View& view, const std::size_t index, const float facing, const float distance, const float dt) noexcept
{
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& walk_clips = view.get<ComponentPool<WalkClipColumns>>();
    const auto& movement = *locomotor.movements[index];
    auto& walk = walk_clips.get(slot_of(view, index), &WalkClipColumns::plans);
    auto& speed = locomotor.drive_speeds[index];
    const auto travel = travel_speed(view, index);
    const auto stride = locomotor.max_speeds[index] * movement.walk_stride_seconds;
    const auto parts = static_cast<float>(movement.walk_stop_parts);
    // Begins the stop at the stride's part `boundary` (1..parts; 0 as MOVESTART ends), `over`
    // seconds before this tick ends, when stopping there lands closer to the destination than a part
    // later; `ahead` is the distance to the destination from there.
    const auto stop_if_closest = [&](const std::uint32_t boundary, const float over, const float ahead)
    {
        if (!walk.turn_back && ahead > stop_distance(speed, movement.walk_stop_seconds) + 0.5F * stride / parts)
        {
            return;
        }
        walk.clip = WalkClip::stopping;
        walk.part = boundary % movement.walk_stop_parts;
        walk.phase = static_cast<float>(walk.part) / parts;
        walk.elapsed = over;
        walk.from_speed = speed;
        walk.ends_move = !walk.turn_back && distance > 0.0F && route.move_targets[index].has_value();
        walk.turn_back = false;
        walk.destination = route.move_targets[index].value_or(Vector3{});
    };
    switch (walk.clip)
    {
    case WalkClip::stopping:
    case WalkClip::unplanned:
    case WalkClip::raising:
    case WalkClip::lowering:
        return speed;
    case WalkClip::standing:
    case WalkClip::raised:
    {
        if (travel <= 0.0F || wanted_speed(view, index, facing, distance) <= 0.0F)
        {
            speed = 0.0F;
            return 0.0F;
        }
        // A raised leg goes up before the walk (walk_straight raises it).
        if (walk.clip == WalkClip::standing && movement.walk_raise_seconds > 0.0F)
        {
            walk = {};
            walk.clip = WalkClip::raising;
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
    speed = drive(view, index, wanted_speed(view, index, facing, distance) * walk.scale, dt);
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

bool SimulationWorld::MovementSystem::wants_turn_in_place(const View& view, const std::size_t index) const noexcept
{
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& route = view.get<RouteColumns>();
    auto& steering = view.get<SteeringColumns>();
    const auto& point = steering.intents[index].point;
    if (!point || route.move_targets[index])
    {
        return false;
    }
    const auto dx = point->x - placement.positions[index].x;
    const auto dy = point->y - placement.positions[index].y;
    return (dx != 0.0F || dy != 0.0F) && std::fabs(wrap_angle(std::atan2(dy, dx) - placement.headings[index])) > locomotor.movements[index]->turn_in_place_angle;
}

bool SimulationWorld::MovementSystem::walk_straight(const View& view, const std::size_t index, const float dt)
{
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& ground_mover = view.get<GroundMoverColumns>();
    auto& route = view.get<RouteColumns>();
    auto& walk_clips = view.get<ComponentPool<WalkClipColumns>>();
    auto& ground = view.get<GroundState>();
    const auto& movement = *locomotor.movements[index];
    auto& walk = walk_clips.get(slot_of(view, index), &WalkClipColumns::plans);
    // A raised leg goes up or down while nothing else moves.
    if (walk.clip == WalkClip::raising || walk.clip == WalkClip::lowering)
    {
        const bool raising = walk.clip == WalkClip::raising;
        walk.elapsed += dt;
        locomotor.drive_speeds[index] = 0.0F;
        if (walk.elapsed >= (raising ? movement.walk_raise_seconds : movement.walk_lower_seconds))
        {
            walk = {};
            walk.clip = raising ? WalkClip::raised : WalkClip::standing;
        }
        return true;
    }
    // Standing with it raised and nothing to do (no destination, no turn about to begin), it lowers it.
    if (walk.clip == WalkClip::raised && !route.move_targets[index] && !turning_as_clips(view, index) && !wants_turn_in_place(view, index))
    {
        walk = {};
        walk.clip = WalkClip::lowering;
        locomotor.drive_speeds[index] = 0.0F;
        return true;
    }
    const bool walking = walk.clip == WalkClip::starting || walk.clip == WalkClip::moving;
    const bool destination = route.move_targets[index].has_value() && !waits(view, index);
    if (walk.clip != WalkClip::stopping && (!walking || (destination && route.pending[index] == 0)))
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
        locomotor.drive_speeds[index] = walk.from_speed * (1.0F - after);
        locomotor.arriving[index] = 1;
        if (walk.elapsed >= seconds)
        {
            // The stop that ends its move ends the order, unless a newer one came meanwhile (it
            // goes on from rest, its MOVESTART starting where MOVE_END ended).
            if (const auto& target = route.move_targets[index]; walk.ends_move && target && target->x == walk.destination.x && target->y == walk.destination.y && target->z == walk.destination.z)
            {
                route.move_targets[index].reset();
                route.paths[index].clear();
            }
            walk = {};
            // MOVE_END ends with a raised leg still up.
            walk.clip = movement.walk_raise_seconds > 0.0F ? WalkClip::raised : WalkClip::standing;
            locomotor.drive_speeds[index] = 0.0F;
        }
    }
    else
    {
        // Waiting for its route, it walks on; without a destination (a stop order, or its group
        // waits for it to fall back) it walks on to its stride's next part and stops there.
        const auto ahead = destination ? std::numeric_limits<float>::max() : 0.0F;
        step = walk_speed(view, index, 1.0F, ahead, dt) * dt;
    }
    const auto heading = placement.headings[index];
    const auto moved = keep_apart(view, index, {std::cos(heading) * step, std::sin(heading) * step, 0.0F});
    auto& position = placement.positions[index];
    Vector3 next{position.x + moved.x, position.y + moved.y, position.z};
    if (ground_mover.follows_ground[index] != 0 && ground.map.valid())
    {
        // Ground it may not enter holds it where it is while the clip plays on.
        if (!stands_at(view, index, next.x, next.y))
        {
            return true;
        }
        next.z = ground.map.surface_height(next.x, next.y);
    }
    position = next;
    return true;
}

bool SimulationWorld::MovementSystem::turn_as_clips(const View& view, const std::size_t index, const float difference, const float dt)
{
    auto& placement = view.get<PlacementColumns>();
    auto& locomotor = view.get<LocomotorColumns>();
    auto& turn_clips = view.get<ComponentPool<TurnClipColumns>>();
    auto& walk_clips = view.get<ComponentPool<WalkClipColumns>>();
    const auto& movement = *locomotor.movements[index];
    if (!(movement.turn_begin_seconds[0] > 0.0F) || !(movement.stopped_turn_rate > 0.0F))
    {
        return false;
    }
    auto& plan = turn_clips.get(slot_of(view, index), &TurnClipColumns::plans);
    const auto total = [&]
    {
        const auto side = plan.way > 0.0F ? 0U : 1U;
        return movement.turn_begin_seconds[side] + static_cast<float>(plan.loops) * movement.turn_loop_seconds[side] + static_cast<float>(plan.halves) * movement.turn_half_seconds[side] +
               static_cast<float>(plan.quarters) * movement.turn_quarter_seconds[side] + movement.turn_end_seconds[side];
    };
    if (!plan.active)
    {
        // A turn in place begins from rest, past the angle that needs one (Close_Enough_Angle_For_Move_Start).
        if (!(movement.turn_in_place_angle > 0.0F) || locomotor.drive_speeds[index] > 0.0F || std::fabs(difference) <= movement.turn_in_place_angle)
        {
            return false;
        }
        // A raised leg goes up before the turn: the walker raises it, then turns.
        // (Raising a leg makes the entity a walk-pool member at spawn.)
        if (movement.walk_raise_seconds > 0.0F && walk_clips.get(slot_of(view, index), &WalkClipColumns::plans).clip != WalkClip::raised)
        {
            if (auto& walk = walk_clips.get(slot_of(view, index), &WalkClipColumns::plans); walk.clip == WalkClip::standing)
            {
                walk = {};
                walk.clip = WalkClip::raising;
            }
            return true;
        }
        plan = {};
        plan.active = true;
        plan.way = difference > 0.0F ? 1.0F : -1.0F;
        plan.start = placement.headings[index];
        plan.angle = std::fabs(difference);
        const auto side = plan.way > 0.0F ? 0U : 1U;
        const auto loop = movement.turn_loop_seconds[side];
        const auto half = movement.turn_half_seconds[side];
        const auto quarter = movement.turn_quarter_seconds[side];
        // The cycles that come closest to the turn at the stopped rate, in loops (BEGIN and END turn
        // half their time's worth): quarters with a QUARTER clip, else halves with a HALF clip, else
        // whole loops, as few clips as that takes.
        const auto loops = loop > 0.0F ? std::max((plan.angle / movement.stopped_turn_rate - 0.5F * (movement.turn_begin_seconds[side] + movement.turn_end_seconds[side])) / loop, 0.0F) : 0.0F;
        if (quarter > 0.0F)
        {
            const auto quarters = static_cast<std::uint32_t>(std::lround(loops * 4.0F));
            plan.loops = quarters / 4U;
            plan.halves = half > 0.0F ? (quarters % 4U) / 2U : 0U;
            plan.quarters = half > 0.0F ? quarters % 2U : quarters % 4U;
        }
        else if (half > 0.0F)
        {
            const auto halves = static_cast<std::uint32_t>(std::lround(loops * 2.0F));
            plan.loops = halves / 2U;
            plan.halves = halves % 2U;
        }
        else
        {
            plan.loops = static_cast<std::uint32_t>(std::lround(loops));
        }
        // The scale of the clips' nominal rates that ends the turn exactly on the way.
        plan.scale = 1.0F;
        plan.scale = plan.angle / std::max(turnPoint(view, index, total()).turned, 1.0e-6F);
    }
    const auto length = total();
    plan.elapsed = std::min(plan.elapsed + dt, length);
    auto& heading = placement.headings[index];
    heading = wrap_angle(plan.start + plan.way * std::min(turnPoint(view, index, plan.elapsed).turned, plan.angle));
    if (plan.elapsed >= length)
    {
        heading = wrap_angle(plan.start + plan.way * plan.angle);
        plan.active = false;
    }
    return true;
}

} // namespace eaw
