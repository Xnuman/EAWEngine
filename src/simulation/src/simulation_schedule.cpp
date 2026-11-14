// The tick's schedule: its systems in order, the tick layer each is timed under, and the data each
// reads and writes (docs/ecs-design.md, 6.3 and migration step 4), with the check of the writes
// that DebugValidation builds run (EAW_SIMULATION_ACCESS_CHECK).

#include "simulation_internal.hpp"

#include "core/fatal_error.hpp"
#include "simulation/simulation.hpp"

#include <functional>
#include <string>
#include <vector>

namespace eaw {

// Reads come from what each system's functions mention (a static over-approximation, reviewed, not
// checked); writes are what its checked runs and its code show, and DebugValidation builds check
// them. Until migration
// step 6 queues structural changes for the barrier, the systems that spawn or remove entities
// (commands, and combat's removal of the dead) write the whole entity table.
const std::array<SimulationWorld::SystemEntry, 7> SimulationWorld::schedule_{{
    {"commands", TickPhase::commands, &SimulationWorld::run_commands, all_data, all_data},
    {"behavior", TickPhase::behavior, &SimulationWorld::run_behavior,
     access_to({SimData::clock, SimData::combat, SimData::flight, SimData::flight_maneuvers, SimData::ground, SimData::ground_mover, SimData::group, SimData::identity, SimData::locomotor,
                SimData::orders, SimData::path_queue, SimData::placement, SimData::route, SimData::slots, SimData::steering, SimData::walk_clips, SimData::rules}),
     // plan_move: the route, a walker's new way, and the queued route search.
     access_to({SimData::steering, SimData::orders, SimData::combat, SimData::route, SimData::walk_clips, SimData::path_queue})},
    {"navigation", TickPhase::navigation, &SimulationWorld::run_navigation,
     access_to({SimData::combat, SimData::flight, SimData::ground, SimData::ground_mover, SimData::group, SimData::identity, SimData::locomotor, SimData::path_queue, SimData::placement,
                SimData::route, SimData::slots}),
     access_to({SimData::route, SimData::path_queue, SimData::group, SimData::locomotor, SimData::avoidance})},
    {"movement", TickPhase::movement, &SimulationWorld::run_movement, reads_of<MovementSystem::View>(), writes_of<MovementSystem::View>(), true},
    // Contact's access is its view's type (ContactSystem::View).
    {"contact", TickPhase::contact, &SimulationWorld::run_contact, reads_of<ContactSystem::View>(), writes_of<ContactSystem::View>(), true},
    {"combat", TickPhase::combat, &SimulationWorld::run_combat, all_data, all_data},
    {"publish", TickPhase::publish, &SimulationWorld::run_publish,
     access_to({SimData::clock, SimData::combat, SimData::events, SimData::flight_maneuvers, SimData::ground_mover, SimData::identity, SimData::locomotor, SimData::motion, SimData::orders,
                SimData::placement, SimData::projectiles, SimData::route, SimData::snapshot, SimData::turn_clips, SimData::walk_clips}),
     access_to({SimData::clock, SimData::snapshot})},
}};

std::string_view SimulationWorld::sim_data_name(const SimData data) noexcept
{
    static constexpr std::array<std::string_view, sim_data_count> names{
        "identity",         "placement",  "motion", "locomotor", "ground_mover", "route",       "flight", "combat",        "orders",   "steering", "formation", "group",    "turn_clips", "walk_clips",
        "flight_maneuvers", "footprints", "crush",  "ground",    "path_queue",   "projectiles", "events", "combat_random", "snapshot", "clock",    "slots",     "commands", "rules",      "avoidance"};
    static_assert(!names.back().empty(), "every kind of simulation data needs its name");
    return names[static_cast<std::size_t>(data)];
}

void SimulationWorld::run_system(const SystemEntry& system)
{
#ifdef EAW_SIMULATION_ACCESS_CHECK
    // Copies of the data the system does not write, compared after it ran.
    std::vector<std::function<void()>> checks;
    for_each_data(
        [&](const SimData data, const auto& object)
        {
            if (system.typed || (system.writes & access_to({data})) != 0 || data == SimData::ground)
            {
                return;
            }
            checks.push_back(
                [&system, data, &object, copy = object]
                {
                    EAW_ASSERT_MSG(copy == object, "simulation system '" + std::string(system.name) + "' changed " + std::string(sim_data_name(data)) +
                                                       ", which it does not declare as written (simulation_schedule.cpp)");
                });
        });
#endif
    (this->*system.run)();
#ifdef EAW_SIMULATION_ACCESS_CHECK
    for (const auto& check : checks)
    {
        check();
    }
#endif
}

} // namespace eaw
