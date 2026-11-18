#include "sandbox/movement_metrics.hpp"

#include "math/scalar.hpp"
#include "sandbox/text_format.hpp"

#include <cmath>

namespace eaw {

void MovementMeter::observe(const SimSnapshot& snapshot)
{
    ++tick_;
    for (const auto& entity : snapshot.entities)
    {
        if (!entity.follows_ground || entity.squad.valid() || entity.max_speed <= 0.0F)
        {
            continue;
        }
        if (entity.moving)
        {
            ++metrics_.moving_ticks;
            if (entity.speed < entity.max_speed * stall_speed_fraction)
            {
                ++metrics_.stalled_ticks;
            }
        }
        if (entity.id.index >= tracks_.size())
        {
            tracks_.resize(entity.id.index + 1U);
        }
        auto& track = tracks_[entity.id.index];
        if (track.generation != entity.id.generation)
        {
            track = {.generation = entity.id.generation, .heading = entity.heading, .still_at = entity.position};
            continue;
        }
        if (!entity.moving || std::hypot(entity.position.x - track.still_at.x, entity.position.y - track.still_at.y) > freeze_tolerance)
        {
            track.still_at = entity.position;
            track.still_ticks = 0;
        }
        else if (++track.still_ticks == freeze_ticks)
        {
            ++metrics_.freezes;
        }
        // The heading change, wrapped to (-pi, pi].
        auto change = entity.heading - track.heading;
        change -= two_pi * std::round(change / two_pi);
        track.heading = entity.heading;
        if (std::fabs(change) > turn_noise_radians)
        {
            const std::int8_t turn = change > 0.0F ? 1 : -1;
            if (turn == track.turn)
            {
                track.swing += std::fabs(change);
            }
            else
            {
                if (track.turn != 0 && track.swing >= reversal_swing_radians && tick_ - track.swing_start <= reversal_window_ticks)
                {
                    ++metrics_.reversals;
                }
                track.turn = turn;
                track.swing = std::fabs(change);
                track.swing_start = tick_;
            }
        }
    }
}

std::string format_movement(const MovementMetrics& metrics, const SimulationWorld::Stats& stats, const double tick_seconds)
{
    const auto seconds = [&](const std::uint64_t ticks)
    {
        return format_fixed(static_cast<double>(ticks) * tick_seconds, 1);
    };
    const auto changes = [&](const SimulationWorld::RouteTraceKind kind)
    {
        return std::to_string(stats.route_changes[static_cast<std::size_t>(kind)]);
    };
    using Kind = SimulationWorld::RouteTraceKind;
    return std::to_string(metrics.reversals) + " reversals, stalled " + seconds(metrics.stalled_ticks) + " of " + seconds(metrics.moving_ticks) + " unit-s moving, " + std::to_string(metrics.freezes) +
           " frozen | route changes: plan " + changes(Kind::plan) + ", shared " + changes(Kind::shared) + ", around " + changes(Kind::around) + " | stopped at units " +
           std::to_string(stats.blocked_stops);
}

} // namespace eaw
