// The tick's schedule: its systems in order, the tick layer each is timed under, and the data each
// reads and writes (docs/ecs-design.md, 6.3 and migration step 4).

#include "simulation_internal.hpp"

namespace eaw {

// A system on a typed view declares its access as its view's type (system_view.hpp), so the masks
// here come from it and the compiler holds the code to them. The barriers that spawn or remove
// entities (commands at the start of the tick, removal at its end) and the resolution of the
// tick's damage (which ends spins and orders) are member functions with full access and write the
// whole entity table.
const std::array<SimulationWorld::SystemEntry, 9> SimulationWorld::schedule_{{
    {"commands", TickPhase::commands, &SimulationWorld::run_commands, all_data, all_data},
    {"behavior", TickPhase::behavior, &SimulationWorld::run_behavior, reads_of<BehaviorSystem::View>(), writes_of<BehaviorSystem::View>()},
    {"navigation", TickPhase::navigation, &SimulationWorld::run_navigation, reads_of<NavigationSystem::View>(), writes_of<NavigationSystem::View>()},
    {"movement", TickPhase::movement, &SimulationWorld::run_movement, reads_of<MovementSystem::View>(), writes_of<MovementSystem::View>()},
    {"contact", TickPhase::contact, &SimulationWorld::run_contact, reads_of<ContactSystem::View>(), writes_of<ContactSystem::View>()},
    {"combat", TickPhase::combat, &SimulationWorld::run_combat, reads_of<CombatSystem::View>(), writes_of<CombatSystem::View>()},
    {"resolution", TickPhase::resolution, &SimulationWorld::run_resolution, all_data, all_data},
    {"removal", TickPhase::resolution, &SimulationWorld::run_removal, all_data, all_data},
    {"publish", TickPhase::publish, &SimulationWorld::run_publish, reads_of<PublishSystem::View>(), writes_of<PublishSystem::View>()},
}};

} // namespace eaw
