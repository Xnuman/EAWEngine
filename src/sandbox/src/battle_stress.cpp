#include "battle_stress.hpp"

#include "core/log.hpp"
#include "simulation/ground_map.hpp"
#include "battle/battle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace eaw {
namespace {

// Engine choices for the benchmark: each group gets a new order once a second; a group is up to
// eight units of one owner (a few formations per side even at 10 units); points lie within 600
// units of the center (a few seconds' drive, so routes are long enough to cost something); the
// seed is fixed, so a stress battle repeats exactly.
constexpr double order_period_seconds = 1.0;
constexpr std::size_t group_size = 8;
constexpr float point_radius = 600.0F;
constexpr std::uint64_t stress_seed = 0x5EED5EEDULL;
// Orders start once the spawns have applied (as --auto-move's).
constexpr std::uint64_t first_order_tick = 2;
constexpr std::array order_cycle{BattleOrder::move, BattleOrder::attack_move, BattleOrder::attack, BattleOrder::guard};

} // namespace

StressOrders::StressOrders(const Vector3& center, const GroundMap* ground, std::vector<EntityId> placed, const double tick_seconds)
    : center_(center), ground_(ground), placed_(std::move(placed)), period_ticks_(std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(std::llround(order_period_seconds / tick_seconds)))),
      random_(stress_seed)
{
    std::ranges::sort(placed_);
}

Vector3 StressOrders::random_point()
{
    // Uniform over the disc.
    const auto angle = random_.unit() * 2.0F * std::numbers::pi_v<float>;
    const auto radius = point_radius * std::sqrt(random_.unit());
    Vector3 point{center_.x + std::cos(angle) * radius, center_.y + std::sin(angle) * radius, center_.z};
    if (ground_ != nullptr)
    {
        point = open_ground(*ground_, point, passability::clear);
        point.z = ground_->surface_height(point.x, point.y);
    }
    return point;
}

void StressOrders::issue(Battle& battle, const std::uint64_t tick)
{
    if (tick < first_order_tick)
    {
        return;
    }
    // The spawned mobile units by owner, in dense (spawn) order; hostiles are the other owners'.
    std::vector<std::pair<std::uint32_t, std::vector<EntityId>>> sides;
    std::vector<std::pair<std::uint32_t, EntityId>> targets;
    for (const auto& entity : battle.world().snapshot().entities)
    {
        if (std::ranges::binary_search(placed_, entity.id) || entity.health <= 0.0F)
        {
            continue;
        }
        targets.emplace_back(entity.owner, entity.id);
        // A squad takes its orders as its team.
        if (entity.max_speed <= 0.0F || entity.squad.valid())
        {
            continue;
        }
        auto side = std::ranges::find(sides, entity.owner, &std::pair<std::uint32_t, std::vector<EntityId>>::first);
        if (side == sides.end())
        {
            sides.emplace_back(entity.owner, std::vector<EntityId>{});
            side = sides.end() - 1;
        }
        side->second.push_back(entity.id);
    }
    // Nobody dies, so the load stays that of every unit for the whole run; damage still applies
    // down to the floor that cannot be killed keeps (the combat work is the same).
    if (tick == first_order_tick)
    {
        BattleCommand keep{.order = BattleOrder::set_cannot_be_killed, .flag = true};
        for (const auto& target : targets)
        {
            keep.entities.push_back(target.second);
        }
        if (!keep.entities.empty())
        {
            if (const auto submitted = battle.submit(std::move(keep)); !submitted)
            {
                log_warning("battle", submitted.error());
            }
        }
    }
    // Group g of the battle (counted over the sides in order) is ordered on the ticks where
    // (tick - first) % period == g % period, so the orders spread over the second.
    std::size_t group_index = 0;
    for (const auto& [owner, units] : sides)
    {
        for (std::size_t first = 0; first < units.size(); first += group_size, ++group_index)
        {
            if ((tick - first_order_tick) % period_ticks_ != group_index % period_ticks_)
            {
                continue;
            }
            BattleCommand command{.order = order_cycle[orders_++ % order_cycle.size()]};
            command.entities.assign(units.begin() + static_cast<std::ptrdiff_t>(first), units.begin() + static_cast<std::ptrdiff_t>(std::min(first + group_size, units.size())));
            if (command.order == BattleOrder::attack)
            {
                std::vector<EntityId> hostiles;
                for (const auto& [target_owner, id] : targets)
                {
                    if (target_owner != owner)
                    {
                        hostiles.push_back(id);
                    }
                }
                if (hostiles.empty())
                {
                    command.order = BattleOrder::attack_move;
                }
                else
                {
                    command.target = hostiles[random_.below(static_cast<std::uint32_t>(hostiles.size()))];
                }
            }
            if (command.order != BattleOrder::attack)
            {
                command.point = random_point();
            }
            if (const auto submitted = battle.submit(std::move(command)); !submitted)
            {
                log_warning("battle", submitted.error());
            }
        }
    }
}

} // namespace eaw
