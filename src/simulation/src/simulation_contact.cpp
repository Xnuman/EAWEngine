// Contact (docs/ecs-design.md, 6.3): after movement, what units do by touching each other: crushers
// walk over what they can crush, overlapping flyers of one owner push each other apart, and the
// tick's speeds are measured from where movement and contact left them. Ground units and space ships
// never push: they avoid contact before they move (simulation_avoidance.cpp). The first system
// written over a typed view (system_view.hpp): it reaches only the data its View declares.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::ContactSystem::run(const View& view, const float dt)
{
    // One grid of footprints after movement for crushing and separation (crushing moves nothing): a
    // ground unit as far as its rectangle reaches, anything else by its radius (flyers separate by it).
    const auto& placement = view.get<PlacementColumns>();
    const auto& ground_mover = view.get<GroundMoverColumns>();
    reaches_.resize(placement.radii.size());
    for (std::size_t index = 0; index < reaches_.size(); ++index)
    {
        reaches_[index] = ground_mover.follows_ground[index] != 0 ? footprint_reach(placement.bodies[index], placement.radii[index]) : placement.radii[index];
    }
    grid_.build(placement.positions, reaches_, contact_cell_size);
    crush(view);
    separate(view, dt);
    measure_speeds(view, dt);
}

const CrushProfile& SimulationWorld::ContactSystem::crush_profile_of(const View& view, const std::size_t index) noexcept
{
    return crush_profile_in(view.get<ComponentPool<CrushColumns>>(), view.get<IdentityColumns>().ids[index].index);
}

void SimulationWorld::ContactSystem::crush(const View& view)
{
    const auto& ground = view.get<GroundState>().map;
    const auto& identity = view.get<IdentityColumns>();
    const auto& placement = view.get<PlacementColumns>();
    const auto& motion = view.get<MotionColumns>();
    const auto& ground_mover = view.get<GroundMoverColumns>();
    auto& damage = view.get<DamageQueue>();
    if (!ground.valid())
    {
        return;
    }
    for (std::size_t crusher = 0; crusher < identity.ids.size(); ++crusher)
    {
        const auto kind = crush_profile_of(view, crusher).crusher;
        const auto& from = motion.starts[crusher];
        const auto& at = placement.positions[crusher];
        if (kind == Crusher::none || ground_mover.follows_ground[crusher] == 0 || placement.radii[crusher] <= 0.0F || (from.x == at.x && from.y == at.y))
        {
            continue;
        }
        // What lies under the crusher (its rectangle, or its circle without one), in index order (as a
        // scan of every entity found it).
        query_.clear();
        grid_.query(at, reaches_[crusher], query_);
        std::ranges::sort(query_);
        for (const auto victim : query_)
        {
            if (!can_crush(view, crusher, victim))
            {
                continue;
            }
            if (footprint_contains(placement.bodies[crusher], placement.radii[crusher], at, placement.headings[crusher], {placement.positions[victim].x, placement.positions[victim].y}))
            {
                damage.events.push_back({.kind = DamageEvent::Kind::crush, .victim = identity.ids[victim]});
            }
        }
    }
}

// Overlapping flyers of one owner are pushed apart sideways, half each, so they keep their plane
// (observed in the original: squadron mates push each other apart); overlap counts in 3D, so flyers
// passing above or below each other do not push. Displacements are summed for every pair first and
// applied afterwards, so the result does not depend on which flyer of a pair is visited first; a push
// is limited per tick, so a packed squadron spreads out over a few ticks instead of jumping.
void SimulationWorld::ContactSystem::separate(const View& view, const float dt)
{
    const auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    const auto& locomotor = view.get<LocomotorColumns>();
    separated_pairs = 0;
    const auto flyer = [&](const std::size_t index)
    {
        return placement.radii[index] > 0.0F && flies(view, index);
    };
    pushes_.assign(identity.ids.size(), Vector3{});
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (!flyer(index))
        {
            continue;
        }
        query_.clear();
        grid_.query_overlapping(placement.positions[index], placement.radii[index], query_);
        // Each pair once, from its lower dense index, and only flyers that touch, in index order, so
        // the pushes add up the same whatever the grid's cells.
        const auto& here = placement.positions[index];
        const auto own_radius = placement.radii[index];
        std::erase_if(query_,
                      [&](const std::uint32_t other)
                      {
                          const auto reach = own_radius + placement.radii[other];
                          const auto gap_x = placement.positions[other].x - here.x;
                          const auto gap_y = placement.positions[other].y - here.y;
                          return other <= index || gap_x * gap_x + gap_y * gap_y >= reach * reach;
                      });
        std::ranges::sort(query_);
        for (const auto other : query_)
        {
            if (!flyer(other) || identity.owners[index] != identity.owners[other])
            {
                continue;
            }
            const auto dx = placement.positions[other].x - placement.positions[index].x;
            const auto dy = placement.positions[other].y - placement.positions[index].y;
            const auto dz = placement.positions[other].z - placement.positions[index].z;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            const auto overlap = placement.radii[index] + placement.radii[other] - std::sqrt(distance * distance + dz * dz);
            // A hundredth of a world unit of overlap is tolerated, so flyers at rest stop pushing.
            if (overlap <= 0.01F)
            {
                continue;
            }
            // Coincident flyers part along a direction fixed by their ids.
            float nx = 0.0F;
            float ny = 0.0F;
            if (distance > 1.0e-4F)
            {
                nx = dx / distance;
                ny = dy / distance;
            }
            else
            {
                const auto angle = static_cast<float>((identity.ids[index].index * 2654435761U + identity.ids[other].index) % 6283U) / 1000.0F;
                nx = std::cos(angle);
                ny = std::sin(angle);
            }
            const auto share = overlap / 2.0F;
            pushes_[index].x -= nx * share;
            pushes_[index].y -= ny * share;
            pushes_[other].x += nx * share;
            pushes_[other].y += ny * share;
            ++separated_pairs;
        }
    }
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        auto push = pushes_[index];
        const auto length = std::sqrt(push.x * push.x + push.y * push.y);
        if (length <= 0.0F)
        {
            continue;
        }
        // At most the separation rate in radii, or the flyer's own speed.
        const auto limit = std::max(placement.radii[index] * separation_radii_per_second, locomotor.max_speeds[index]) * dt;
        if (length > limit)
        {
            push.x *= limit / length;
            push.y *= limit / length;
        }
        auto& position = placement.positions[index];
        const auto next_x = position.x + push.x;
        const auto next_y = position.y + push.y;
        if (!stands_at(view, index, next_x, next_y))
        {
            continue;
        }
        position = {next_x, next_y, position.z};
    }
}

void SimulationWorld::ContactSystem::measure_speeds(const View& view, const float dt)
{
    const auto& placement = view.get<PlacementColumns>();
    auto& motion = view.get<MotionColumns>();
    // Movement and separation are the only systems that move entities, and entities are only added
    // (commands) before and removed (the end of the tick) after them, so dense indices line up here.
    for (std::size_t index = 0; index < placement.positions.size(); ++index)
    {
        const auto& from = motion.starts[index];
        const auto& to = placement.positions[index];
        const auto moved = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y) + (to.z - from.z) * (to.z - from.z));
        motion.speeds[index] = dt > 0.0F && std::isfinite(moved) ? moved / dt : 0.0F;
        const Vector2 velocity{(to.x - from.x) / dt, (to.y - from.y) / dt};
        motion.velocities[index] = dt > 0.0F && is_finite(velocity) ? velocity : Vector2{};
    }
}

} // namespace eaw
