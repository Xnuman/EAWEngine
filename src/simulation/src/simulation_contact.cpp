// Contact (docs/ecs-design.md, 6.3): after movement, what units do by touching each other: crushers
// walk over what they can crush, overlapping units push each other apart, and the tick's speeds are
// measured from where movement and contact left them. The first system written over a typed view
// (system_view.hpp): it reaches only the data its View declares.

#include "simulation_internal.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

using namespace simulation_detail;

void SimulationWorld::ContactSystem::run(const View& view, const float dt)
{
    // One grid of footprints after movement for crushing and separation (crushing moves nothing).
    const auto& placement = view.get<PlacementColumns>();
    grid_.build(placement.positions, placement.radii, contact_cell_size);
    crush(view);
    separate(view, dt);
    measure_speeds(view, dt);
}

const CrushProfile& SimulationWorld::ContactSystem::crush_profile_of(const View& view, const std::size_t index) noexcept
{
    return crush_profile_in(view.get<ComponentPool<CrushColumns>>(), view.get<IdentityColumns>().ids[index].index);
}

bool SimulationWorld::ContactSystem::can_crush(const View& view, const std::size_t crusher, const std::size_t victim)
{
    const auto& identity = view.get<IdentityColumns>();
    const auto& rules = view.get<Rules>();
    const auto kind = crush_profile_of(view, crusher).crusher;
    const auto& squash = crush_profile_of(view, victim);
    const bool may = squash.squashable || (kind == Crusher::supercrusher && squash.squashable_by_supercrusher);
    const auto& state = combat_state(view, victim);
    return kind != Crusher::none && victim != crusher && may && state.profile->health > 0.0F && state.health > 0.0F && rules.hostility.hostile(identity.owners[crusher], identity.owners[victim]);
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
        // What lies under the crusher, in index order (as a scan of every entity found it).
        query_.clear();
        grid_.query(at, placement.radii[crusher], query_);
        std::ranges::sort(query_);
        for (const auto victim : query_)
        {
            if (!can_crush(view, crusher, victim))
            {
                continue;
            }
            if (std::hypot(placement.positions[victim].x - at.x, placement.positions[victim].y - at.y) < placement.radii[crusher])
            {
                damage.events.push_back({.kind = DamageEvent::Kind::crush, .victim = identity.ids[victim]});
            }
        }
    }
}

// Overlapping ground units are pushed apart, half each by default. Displacements are summed for
// every pair first and applied afterwards, so the result does not depend on which unit of a pair
// is visited first. Entities that cannot move are never pushed (they push the other unit the whole
// way); idle units yield more than moving ones, so a moving unit shoulders through instead of
// stopping. A push never moves a unit onto a cell its class may not enter, and it is limited per
// tick so packed groups spread out over a few ticks instead of jumping.
void SimulationWorld::ContactSystem::separate(const View& view, const float dt)
{
    const auto& ground = view.get<GroundState>().map;
    const auto& identity = view.get<IdentityColumns>();
    auto& placement = view.get<PlacementColumns>();
    const auto& locomotor = view.get<LocomotorColumns>();
    const auto& ground_mover = view.get<GroundMoverColumns>();
    auto& route = view.get<RouteColumns>();
    separated_pairs = 0;
    // On land, ground units separate on the surface. Space ships do not push each other (they avoid
    // contact instead: plan_space_avoidance); only flyers separate there.
    const bool land = ground.valid();
    // Flyers separate only from flyers of their owner (observed in the original: squadron mates push
    // each other apart), sideways so they keep their plane; overlap counts in 3D, so flyers passing
    // above or below each other do not push.
    const auto flyer = [&](const std::size_t index)
    {
        return placement.radii[index] > 0.0F && flies(view, index);
    };
    const auto takes_part = [&](const std::size_t index)
    {
        return flyer(index) || (placement.radii[index] > 0.0F && !flies(view, index) && land && ground_mover.follows_ground[index] != 0);
    };
    pushes_.assign(identity.ids.size(), Vector3{});
    // Remaining route length of every moving unit: of two moving units, the one further along
    // (less route left) has priority and yields much less, so units queue through a narrow
    // passage instead of shoving each other back and forth (Age of Empires II's priority rule).
    measure_remaining_routes(route, placement);
    const auto weight = [&](const std::size_t index, const std::size_t other)
    {
        if (locomotor.max_speeds[index] <= 0.0F)
        {
            return 0.0F;
        }
        // Only units of one owner make way for each other. An idle unit of another owner (an enemy
        // or a neutral) stands its ground against a moving one, which is held back instead; two
        // foreign units that both move or both stand split the push evenly.
        if (identity.owners[index] != identity.owners[other])
        {
            return !route.move_targets[index] && route.move_targets[other] ? 0.0F : 1.0F;
        }
        if (!route.move_targets[index])
        {
            return 2.0F;
        }
        if (route.move_targets[other] && locomotor.max_speeds[other] > 0.0F && route.remaining[index] != route.remaining[other])
        {
            return route.remaining[index] < route.remaining[other] ? 0.25F : 1.75F;
        }
        return 1.0F;
    };
    for (std::size_t index = 0; index < identity.ids.size(); ++index)
    {
        if (!takes_part(index))
        {
            continue;
        }
        query_.clear();
        grid_.query_overlapping(placement.positions[index], placement.radii[index], query_);
        // Each pair once, from its lower dense index, and only units that touch: whether they do comes
        // from positions and footprints alone (most neighbours a query finds do not), before anything
        // else about the other unit is read (separation is memory-bound: a crowd of 2,500 makes about
        // 125,000 candidate pairs a tick). The few left go in index order, so the pushes add up the
        // same whatever the grid's cells.
        const bool flying = flyer(index);
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
            if (!takes_part(other))
            {
                continue;
            }
            if (flying != flyer(other) || (flying && identity.owners[index] != identity.owners[other]))
            {
                continue;
            }
            // A crusher and what it can crush do not push each other: a walker walks over standing
            // infantry (and crushes it) instead of being held back by it, the owner's report; pushing
            // the infantry aside instead would keep a slow walker from ever reaching it. Infantry may
            // stand at a still walker's feet, out of its guns' reach, as in the original.
            if (!flying && (can_crush(view, index, other) || can_crush(view, other, index)))
            {
                continue;
            }
            const auto dx = placement.positions[other].x - placement.positions[index].x;
            const auto dy = placement.positions[other].y - placement.positions[index].y;
            const auto dz = flying ? placement.positions[other].z - placement.positions[index].z : 0.0F;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            const auto overlap = placement.radii[index] + placement.radii[other] - std::sqrt(distance * distance + dz * dz);
            const auto first_weight = flying ? 1.0F : weight(index, other);
            const auto second_weight = flying ? 1.0F : weight(other, index);
            // A hundredth of a world unit of overlap is tolerated, so units at rest stop pushing.
            if (overlap <= 0.01F || first_weight + second_weight <= 0.0F)
            {
                continue;
            }
            // Coincident units part along a direction fixed by their ids.
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
            const auto total = first_weight + second_weight;
            const auto first_share = overlap * first_weight / total;
            const auto second_share = overlap * second_weight / total;
            pushes_[index].x -= nx * first_share;
            pushes_[index].y -= ny * first_share;
            pushes_[other].x += nx * second_share;
            pushes_[other].y += ny * second_share;
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
        // At most the separation rate in radii, or the unit's own speed.
        const auto limit = std::max(placement.radii[index] * separation_radii_per_second, locomotor.max_speeds[index]) * dt;
        if (length > limit)
        {
            push.x *= limit / length;
            push.y *= limit / length;
        }
        auto& position = placement.positions[index];
        const auto next_x = position.x + push.x;
        const auto next_y = position.y + push.y;
        // Never onto ground without room for its footprint.
        if (!stands_at(view, index, next_x, next_y))
        {
            continue;
        }
        position = {next_x, next_y, land && !flyer(index) ? ground.surface_height(next_x, next_y) : position.z};
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
    }
}

} // namespace eaw
