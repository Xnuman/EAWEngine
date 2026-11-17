// Route tracing (SimulationWorld::RouteTrace): debug records of every change to a ground unit's route
// and to formation frames' routes, with the stages of the search behind each. Recorded only while
// tracing is on, in the serial code that applies a search's result; never hashed, never read back by
// the simulation.

#include "simulation_internal.hpp"

#include <algorithm>

namespace eaw {

using namespace simulation_detail;

namespace {

void keep(std::deque<SimulationWorld::RouteTrace>& records, SimulationWorld::RouteTrace record)
{
    records.push_back(std::move(record));
    while (records.size() > route_trace_depth)
    {
        records.pop_front();
    }
}

} // namespace

void SimulationWorld::NavigationSystem::set_tracing(const bool tracing)
{
    navigation_data_.set_tracing(tracing);
    if (!tracing)
    {
        unit_traces_.clear();
        frame_traces_.clear();
    }
}

const std::deque<SimulationWorld::RouteTrace>* SimulationWorld::NavigationSystem::unit_traces(const EntityId id) const
{
    const auto found = unit_traces_.find({id.index, id.generation});
    return found != unit_traces_.end() ? &found->second : nullptr;
}

const std::deque<SimulationWorld::RouteTrace>* SimulationWorld::NavigationSystem::frame_traces(const std::uint32_t group) const
{
    const auto found = frame_traces_.find(group);
    return found != frame_traces_.end() ? &found->second : nullptr;
}

void SimulationWorld::NavigationSystem::record_route(const View& view, const std::size_t index, const RouteTraceKind kind, const PathTrace& stages)
{
    if (!navigation_data_.tracing())
    {
        return;
    }
    const auto& route = view.get<RouteColumns>();
    const auto& here = view.get<PlacementColumns>().positions[index];
    RouteTrace record{.tick = view.get<Clock>().tick, .kind = kind, .stages = stages};
    record.route.push_back({here.x, here.y});
    for (auto waypoint = route.paths[index].rbegin(); waypoint != route.paths[index].rend(); ++waypoint)
    {
        record.route.push_back({waypoint->x, waypoint->y});
    }
    if (route.move_targets[index])
    {
        record.route.push_back({route.move_targets[index]->x, route.move_targets[index]->y});
    }
    const auto id = view.get<IdentityColumns>().ids[index];
    keep(unit_traces_[{id.index, id.generation}], std::move(record));
}

void SimulationWorld::NavigationSystem::record_frame(const View& view, const FormationFrame& frame, const PathTrace& stages)
{
    if (!navigation_data_.tracing())
    {
        return;
    }
    RouteTrace record{.tick = view.get<Clock>().tick, .kind = RouteTraceKind::frame, .stages = stages};
    record.route.push_back({frame.position.x, frame.position.y});
    for (auto waypoint = frame.route.rbegin(); waypoint != frame.route.rend(); ++waypoint)
    {
        record.route.push_back({waypoint->x, waypoint->y});
    }
    keep(frame_traces_[frame.group], std::move(record));
}

void SimulationWorld::set_route_tracing(const bool tracing)
{
    navigation_.set_tracing(tracing);
}

std::vector<SimulationWorld::RouteTrace> SimulationWorld::route_traces(const EntityId id) const
{
    std::vector<RouteTrace> records;
    if (const auto* own = navigation_.unit_traces(id))
    {
        records.assign(own->begin(), own->end());
    }
    if (const auto dense = dense_index(*this, id))
    {
        if (const auto row = group_.find(slot_of(*this, *dense)))
        {
            if (const auto* frame = navigation_.frame_traces(group_.columns.ids[*row]))
            {
                records.insert(records.end(), frame->begin(), frame->end());
            }
        }
    }
    std::ranges::stable_sort(records, {}, &RouteTrace::tick);
    return records;
}

} // namespace eaw
