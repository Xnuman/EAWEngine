

template <class Data> bool SimulationWorld::ground_body(Data& data, const std::size_t index) noexcept
{
    return data.template get<PlacementColumns>().radii[index] > 0.0F && data.template get<GroundMoverColumns>().follows_ground[index] != 0 && !flies(data, index);
}

template <class Data> float SimulationWorld::unit_radius(Data& data, const std::size_t index) noexcept
{
    const auto* squad = data.template get<ComponentPool<SquadColumns>>().find(slot_of(data, index), &SquadColumns::states);
    return squad != nullptr ? squad->extent : data.template get<PlacementColumns>().radii[index];
}

template <class Data> float SimulationWorld::agent_room(Data& data, const std::size_t a, const Vector3& a_at, const std::size_t b, const Vector3& b_at) noexcept
{
    const auto& avoidance = data.template get<Avoidance>();
    return footprints_reach({a_at, avoidance.facings[a], avoidance.radii[a], avoidance.bodies[a]}, {b_at, avoidance.facings[b], avoidance.radii[b], avoidance.bodies[b]});
}

template <class Data> float SimulationWorld::agent_extent(Data& data, const std::size_t a, const Vector2 direction) noexcept
{
    const auto& avoidance = data.template get<Avoidance>();
    return footprint_extent(avoidance.bodies[a], avoidance.radii[a], avoidance.facings[a], direction);
}

template <class Data> Footing SimulationWorld::footing_of(Data& data, const std::size_t index) noexcept
{
    auto& ground = data.template get<GroundState>();
    auto& ground_mover = data.template get<GroundMoverColumns>();
    const auto mask = ground_mover.passable_types[index];
    const ClearanceField* field = nullptr;
    for (const auto& candidate : ground.clearance)
    {
        if (candidate.passable_types == mask)
        {
            field = &candidate;
        }
    }
    return {mask, field, ground_mover.footing_radii[index]};
}

template <class Data> bool SimulationWorld::destination_fits(Data& data, const float x, const float y, const std::size_t index, const Vector2 from)
{
    const auto& placement = data.template get<PlacementColumns>();
    const auto& ground_mover = data.template get<GroundMoverColumns>();
    const auto& ground = data.template get<GroundState>();
    const auto radius = unit_radius(data, index);
    const auto mask = ground_mover.passable_types[index];
    if (mask == 0)
    {
        return true;
    }
    // A unit with a footprint rectangle needs room for it, turned the way it arrives, besides its
    // footing; a squad the rectangle bounding its soldiers (without one, its circle).
    const auto* squad = data.template get<ComponentPool<SquadColumns>>().find(slot_of(data, index), &SquadColumns::states);
    const auto& body = squad != nullptr ? squad->body : placement.bodies[index];
    const bool shaped = body.half_width > 0.0F && (squad != nullptr || radius == placement.radii[index]);
    if (!ground_stands(ground.map, {x, y}, Footing(mask, footing_of(data, index).clearance, shaped ? ground_mover.footing_radii[index] : radius)))
    {
        return false;
    }
    if (!shaped)
    {
        return true;
    }
    const auto arriving = std::hypot(x - from.x, y - from.y) > 1.0e-3F ? std::atan2(y - from.y, x - from.x) : placement.headings[index];
    return ground_rectangle_fits(ground.map, footprint_center_at(body, {x, y, 0.0F}, arriving), arriving, body.half_length, body.half_width, mask);
}

template <class Data> Vector3 SimulationWorld::place_destination(Data& data, const Vector3& at, const std::size_t index, const Vector2 from)
{
    const auto radius = unit_radius(data, index);
    if (!data.template get<GroundState>().map.valid() || data.template get<GroundMoverColumns>().follows_ground[index] == 0 || !(radius > 0.0F))
    {
        return at;
    }
    const auto found = simulation_detail::nearest_free_point(at, 2.0F * radius, simulation_detail::free_ground_rings,
                                                             [&](const float x, const float y)
                                                             {
                                                                 return destination_fits(data, x, y, index, from);
                                                             });
    return found ? Vector3{found->x, found->y, at.z} : at;
}

template <class Data> bool SimulationWorld::stands_at(Data& data, const std::size_t index, const float x, const float y) noexcept
{
    auto& ground = data.template get<GroundState>();
    return data.template get<GroundMoverColumns>().passable_types[index] == 0 || ground_stands(ground.map, {x, y}, footing_of(data, index));
}

template <class Data> bool SimulationWorld::waits(Data& data, const std::size_t index) noexcept
{
    const auto* waiting = data.template get<ComponentPool<GroupColumns>>().find(slot_of(data, index), &GroupColumns::waiting);
    if (waiting != nullptr && *waiting != 0)
    {
        return true;
    }
    const auto* squad = data.template get<ComponentPool<SquadColumns>>().find(slot_of(data, index), &SquadColumns::states);
    return squad != nullptr && squad->waiting;
}

template <class Data> void SimulationWorld::leave_group(Data& data, const std::size_t index)
{
    data.template get<ComponentPool<GroupColumns>>().remove(slot_of(data, index));
}

template <class Data> void SimulationWorld::follow_slot(Data& data, const std::size_t member, const Vector3& slot, std::optional<Vector3>& sent)
{
    auto& route = data.template get<RouteColumns>();
    const auto& at = data.template get<PlacementColumns>().positions[member];
    const auto away = std::hypot(slot.x - at.x, slot.y - at.y);
    const auto reslot = route.paths[member].empty() ? simulation_detail::slot_reslot_distance : simulation_detail::slot_detour_distance;
    const bool moved = !sent || std::hypot(sent->x - slot.x, sent->y - slot.y) > reslot;
    if (away > simulation_detail::slot_follow_tolerance && (moved || (!route.move_targets[member] && route.pending[member] == 0)))
    {
        route.move_targets[member] = slot;
        route.paths[member].clear();
        route.pending[member] = 0;
        sent = slot;
    }
}