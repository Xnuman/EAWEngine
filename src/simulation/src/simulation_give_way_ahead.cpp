// Giving way ahead (docs/pathfinding-design.md, 4.4; the owner, 2026-11-17): when a ground unit's
// route is planned, the idle allies standing along the whole route are asked whether they could step
// out of its way to a free place (a dry run of make_way). Those that can are asked for real when the
// unit is give_way_ahead_seconds of travel from them, step aside, wait until it has passed, and
// return to their places (plan_giving_way). The rest are left to giving way as the unit arrives
// (a lane of allies stepping on, or a detour); allies fighting hold their place.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

namespace {

// The point of segment a-b nearest `point`.
Vector2 nearest_on_segment(const Vector2 a, const Vector2 b, const Vector2 point) noexcept
{
    const auto step = b - a;
    const auto span = dot(step, step);
    return a + step * (span > 0.0F ? std::clamp(dot(point - a, step) / span, 0.0F, 1.0F) : 0.0F);
}

} // namespace

bool SimulationWorld::NavigationSystem::idle_ally(const View& view, const std::size_t unit, const std::size_t passer) const noexcept
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& route = view.get<RouteColumns>();
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& avoidance = view.get<Avoidance>();
    return unit != passer && avoidance.radii[unit] > 0.0F && identity.owners[unit] == identity.owners[passer] && !route.move_targets[unit] && !route.give_ways[unit] &&
           locomotor.max_speeds[unit] > 0.0F && locomotor.suspended[unit] == 0;
}

bool SimulationWorld::NavigationSystem::fighting(const View& view, const std::size_t unit) const noexcept
{
    const auto* combat = view.get<ComponentPool<CombatColumns>>().find(slot_of(view, unit), &CombatColumns::states);
    return combat != nullptr && combat->target.valid();
}

void SimulationWorld::NavigationSystem::scan_ahead(const View& view, const std::size_t passer)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    auto& avoidance = view.get<Avoidance>();
    auto& ahead = route.ahead[passer];
    ahead.scan = false;
    ahead.yielders.clear();
    if (!route.move_targets[passer])
    {
        return;
    }
    // The route in travel order: here, the waypoints (the next one last in the list), the goal.
    const auto& here = placement.positions[passer];
    std::vector<Vector2> points{{here.x, here.y}};
    for (auto waypoint = route.paths[passer].rbegin(); waypoint != route.paths[passer].rend(); ++waypoint)
    {
        points.push_back({waypoint->x, waypoint->y});
    }
    points.push_back({route.move_targets[passer]->x, route.move_targets[passer]->y});
    for (std::size_t segment = 0; segment + 1 < points.size(); ++segment)
    {
        const auto from = points[segment];
        const auto to = points[segment + 1];
        const auto span = length(to - from);
        if (span <= 0.0F)
        {
            continue;
        }
        const auto heading = (to - from) * (1.0F / span);
        const auto middle = from + (to - from) * 0.5F;
        ahead_query_.clear();
        avoidance.grid.query_overlapping({middle.x, middle.y, 0.0F}, span * 0.5F + avoidance.reaches[passer] * (1.0F + give_way_margin), ahead_query_);
        std::ranges::sort(ahead_query_);
        for (const auto other : ahead_query_)
        {
            // Moving allies keep today's rules (plan_giving_way); fighting ones hold their place.
            if (!idle_ally(view, other, passer) || fighting(view, other) ||
                std::ranges::any_of(ahead.yielders,
                                    [&](const AheadYield& entry)
                                    {
                                        return entry.yielder == identity.ids[other];
                                    }))
            {
                continue;
            }
            const Vector2 there{placement.positions[other].x, placement.positions[other].y};
            const auto beside = nearest_on_segment(from, to, there);
            const auto off = there - beside;
            const auto apart = length(off);
            // In the corridor: nearer the way than the passer's extent across it and the ally's toward it.
            const Vector2 across = apart > 0.0F ? off * (1.0F / apart) : Vector2{-heading.y, heading.x};
            const auto room = (footprint_extent(avoidance.bodies[passer], avoidance.radii[passer], heading, across) + agent_extent(view, other, {-across.x, -across.y})) * (1.0F + give_way_margin);
            // Only one that steps to a free place is asked ahead; one that needs a lane of allies to step
            // on is left to the lane as the passer arrives (asked one at a time, the nearest first: a
            // block asked all at once jammed its own lanes).
            bool chained = false;
            if (apart < room && make_way(view, other, passer, heading, beside, heading, false, &chained) && !chained)
            {
                ahead.yielders.push_back({identity.ids[other], {beside.x, beside.y, here.z}, heading});
            }
        }
    }
}

void SimulationWorld::NavigationSystem::give_way_ahead(const View& view)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    auto& route = view.get<RouteColumns>();
    // Scans of new routes, a few a tick in dense order (a large order spreads over ticks).
    std::uint32_t scanned = 0;
    for (std::size_t passer = 0; passer < identity.ids.size() && scanned < ahead_scans_per_tick; ++passer)
    {
        if (route.ahead[passer].scan && ground_steers(view, passer))
        {
            scan_ahead(view, passer);
            ++scanned;
        }
    }
    // Asks: an ally on the list is asked once the passer is within give_way_ahead_seconds of travel.
    const auto grace = ticks_for(view, give_way_ahead_seconds);
    for (std::size_t passer = 0; passer < identity.ids.size(); ++passer)
    {
        auto& yielders = route.ahead[passer].yielders;
        if (yielders.empty())
        {
            continue;
        }
        if (!ground_steers(view, passer))
        {
            yielders.clear();
            continue;
        }
        const auto reach = travel_speed(view, passer) * give_way_ahead_seconds;
        std::erase_if(yielders,
                      [&](const AheadYield& entry)
                      {
                          const auto unit = dense_index(view, entry.yielder);
                          if (!unit || !idle_ally(view, *unit, passer) || fighting(view, *unit))
                          {
                              return true;
                          }
                          if (distance_xy(placement.positions[passer], entry.beside) > reach)
                          {
                              return false;
                          }
                          const auto before = requests_.size();
                          if (make_way(view, *unit, passer, entry.heading, {entry.beside.x, entry.beside.y}, entry.heading, true))
                          {
                              // Its limit counts from when the passer is due.
                              for (auto request = before; request < requests_.size(); ++request)
                              {
                                  requests_[request].grace = grace;
                              }
                          }
                          return true;
                      });
    }
}

} // namespace eaw
