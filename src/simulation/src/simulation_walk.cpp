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
        if (!walk.turn_back && ahead > stop_distance(speed, movement.walk_stop_seconds) + 0.5F * stride / parts)
        {
            return;
        }
        walk.clip = WalkClip::stopping;
        walk.part = boundary % movement.walk_stop_parts;
        walk.phase = static_cast<float>(walk.part) / parts;
        walk.elapsed = over;
        walk.from_speed = speed;
        walk.ends_move = !walk.turn_back && distance > 0.0F && move_targets_[index].has_value();
        walk.turn_back = false;
        walk.destination = move_targets_[index].value_or(Vector3{});
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
        if (travel <= 0.0F || wanted_speed(index, facing, distance) <= 0.0F)
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

bool SimulationWorld::wants_turn_in_place(const std::size_t index) const noexcept
{
    const auto& point = intents_[index].point;
    if (!point || move_targets_[index])
    {
        return false;
    }
    const auto dx = point->x - positions_[index].x;
    const auto dy = point->y - positions_[index].y;
    return (dx != 0.0F || dy != 0.0F) && std::fabs(wrap_angle(std::atan2(dy, dx) - headings_[index])) > movements_[index].turn_in_place_angle;
}

bool SimulationWorld::walk_straight(const std::size_t index, const float dt)
{
    const auto& movement = movements_[index];
    auto& walk = walk_plans_[index];
    // A raised leg goes up or down while nothing else moves.
    if (walk.clip == WalkClip::raising || walk.clip == WalkClip::lowering)
    {
        const bool raising = walk.clip == WalkClip::raising;
        walk.elapsed += dt;
        drive_speeds_[index] = 0.0F;
        if (walk.elapsed >= (raising ? movement.walk_raise_seconds : movement.walk_lower_seconds))
        {
            walk = {};
            walk.clip = raising ? WalkClip::raised : WalkClip::standing;
        }
        return true;
    }
    // Standing with it raised and nothing to do (no destination, no turn about to begin), it lowers it.
    if (walk.clip == WalkClip::raised && !move_targets_[index] && !turn_plans_[index].active && !wants_turn_in_place(index))
    {
        walk = {};
        walk.clip = WalkClip::lowering;
        drive_speeds_[index] = 0.0F;
        return true;
    }
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
            // MOVE_END ends with a raised leg still up.
            walk.clip = movement.walk_raise_seconds > 0.0F ? WalkClip::raised : WalkClip::standing;
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

bool SimulationWorld::turn_as_clips(const std::size_t index, const float difference, const float dt)
{
    const auto& movement = movements_[index];
    auto& plan = turn_plans_[index];
    if (!(movement.turn_begin_seconds[0] > 0.0F) || !(movement.stopped_turn_rate > 0.0F))
    {
        return false;
    }
    const auto total = [&]
    {
        const auto side = plan.way > 0.0F ? 0U : 1U;
        return movement.turn_begin_seconds[side] + static_cast<float>(plan.loops) * movement.turn_loop_seconds[side] + static_cast<float>(plan.halves) * movement.turn_half_seconds[side] +
               static_cast<float>(plan.quarters) * movement.turn_quarter_seconds[side] + movement.turn_end_seconds[side];
    };
    if (!plan.active)
    {
        // A turn in place begins from rest, past the angle that needs one (Close_Enough_Angle_For_Move_Start).
        if (!(movement.turn_in_place_angle > 0.0F) || drive_speeds_[index] > 0.0F || std::fabs(difference) <= movement.turn_in_place_angle)
        {
            return false;
        }
        // A raised leg goes up before the turn: the walker raises it, then turns.
        if (auto& walk = walk_plans_[index]; movement.walk_raise_seconds > 0.0F && walk.clip != WalkClip::raised)
        {
            if (walk.clip == WalkClip::standing)
            {
                walk = {};
                walk.clip = WalkClip::raising;
            }
            return true;
        }
        plan = {};
        plan.active = true;
        plan.way = difference > 0.0F ? 1.0F : -1.0F;
        plan.start = headings_[index];
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
        plan.scale = plan.angle / std::max(turn_point(index, total()).turned, 1.0e-6F);
    }
    const auto length = total();
    plan.elapsed = std::min(plan.elapsed + dt, length);
    auto& heading = headings_[index];
    heading = wrap_angle(plan.start + plan.way * std::min(turn_point(index, plan.elapsed).turned, plan.angle));
    if (plan.elapsed >= length)
    {
        heading = wrap_angle(plan.start + plan.way * plan.angle);
        plan.active = false;
    }
    return true;
}

SimulationWorld::TurnPoint SimulationWorld::turn_point(const std::size_t index, const float time) const noexcept
{
    const auto& movement = movements_[index];
    const auto& plan = turn_plans_[index];
    const auto side = plan.way > 0.0F ? 0U : 1U;
    const auto begin = std::max(movement.turn_begin_seconds[side], 1.0e-6F);
    const auto end = std::max(movement.turn_end_seconds[side], 1.0e-6F);
    const auto loop = movement.turn_loop_seconds[side];
    const auto half = movement.turn_half_seconds[side];
    const auto quarter = movement.turn_quarter_seconds[side];
    // Each cycle turns at its nominal rate: the loop at the stopped rate, HALF and QUARTER through a
    // half and a quarter of the loop's angle in their own time; all scaled to end on the way.
    const auto rate = movement.stopped_turn_rate * plan.scale;
    struct Cycle
    {
        TurnClip clip;
        float seconds;
        float rate;
        std::uint32_t count;
    };
    const std::array<Cycle, 3> cycles{{
        {TurnClip::loop, loop, rate, loop > 0.0F ? plan.loops : 0U},
        {TurnClip::half, half, half > 0.0F ? rate * loop / (2.0F * half) : 0.0F, half > 0.0F ? plan.halves : 0U},
        {TurnClip::quarter, quarter, quarter > 0.0F ? rate * loop / (4.0F * quarter) : 0.0F, quarter > 0.0F ? plan.quarters : 0U},
    }};
    // BEGIN winds up to the first cycle's rate and END down from the last one's (the loop's rate
    // without cycles), linearly.
    auto first = rate;
    auto last = rate;
    bool any = false;
    for (const auto& cycle : cycles)
    {
        if (cycle.count > 0U)
        {
            first = any ? first : cycle.rate;
            last = cycle.rate;
            any = true;
        }
    }
    if (time < begin)
    {
        return {TurnClip::begin, time / begin, first * time * time / (2.0F * begin)};
    }
    auto turned = 0.5F * first * begin;
    auto at = begin;
    for (const auto& cycle : cycles)
    {
        const auto span = static_cast<float>(cycle.count) * cycle.seconds;
        if (cycle.count > 0U && time < at + span)
        {
            const auto into = time - at;
            const auto whole = std::floor(into / cycle.seconds);
            return {cycle.clip, (into - whole * cycle.seconds) / cycle.seconds, turned + cycle.rate * into};
        }
        turned += cycle.rate * span;
        at += span;
    }
    const auto into = std::clamp(time - at, 0.0F, end);
    return {TurnClip::end, into / end, turned + last * (into - into * into / (2.0F * end))};
}

} // namespace eaw
