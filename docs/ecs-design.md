# Entities, components, and jobs: research and design

The design of the simulation's storage, its tick, and the engine's parallel work (roadmap work
order 2, items c to e), written 2026-11-14/15 and agreed by the owner on 2026-11-15 (the decisions
are in section 9). It says how the simulation stores its entities, how its tick is organized, how
the engine runs work in parallel, and in which steps to get there without changing a battle's
outcome. The research behind section 4, with its sources, is in
[ecs-research.md](ecs-research.md).

## Contents

1. Why change anything
2. Requirements
3. What we have, sorted into candidate components
4. Research: what others do
5. Options for storage
6. Proposed design: entities, components, systems
7. Proposed design: the job system
8. Migration, step by step
9. Decisions
10. What this design deliberately leaves out
11. Sources

## 1. Why change anything

Measured on 2026-11-14 (Release, Utapau, headless `battle`):

| Battle | Entities at the end | Average tick | Slowest tick |
| --- | --- | --- | --- |
| 20 + 20 infantry squads, 20 T2-B, 10 AT-ST | 1,189 | 0.70 ms | 8.6 ms (tick 2) |
| 80 + 80 squads, 60 T2-B, 40 AT-ST | 3,296 | 4.66 ms | 51.5 ms (tick 2: combat 38 ms, route searches 8 ms) |

A 30 Hz tick has 33 ms (the tick is 60 Hz, 16.6 ms, since 2026-11-15). Speed is not the main reason to change now, but two numbers matter: at 2.7
times the entities the average tick took 6.6 times as long (something grows faster than linearly;
nothing in the engine could say what, because it had no per-system timings), and the opening tick
already misses a frame at 3,300 entities. The per-layer timings added since (2026-11-15) answered
both: separation took 3.4 of the 4.5 ms of an average tick (now 1.9 of 3.05, its candidates
filtered before they are sorted), and the opening tick is mostly combat, not the route searches. The structural reasons:

1. **One table for everything.** `SimulationWorld` keeps 67 per-entity arrays (plus world-level and
   per-tick buffers). Every entity has every field: a building has a walk plan, a turn plan, a
   flight roll, formation slots, and a crush profile. Optional behavior is switched on by values
   ("zero `turn_begin_seconds` plays no turn clips"), which is how `MovementProfile` became a bag of
   modes (the 2026-11-14 review, problem 5).
2. **Bookkeeping by hand.** A new field touches four places (spawn, swap-remove, pop, hash);
   forgetting one is a silent desynchronization (the review, problem 6).
3. **Systems are member functions over all arrays.** Nothing states which data a step reads or
   writes, so nothing can check that two steps may run side by side, and any step can touch
   anything.
4. **Two ways to build an entity.** `Battle::spawn_of` (runtime spawns) and the map builder (placed
   objects) both copy a `SpawnableType` into the 22 fields of `EntitySpawn`.
5. **Squads need composition.** Real squads (work order 2f) are an entity made of entities: a
   relationship the flat table cannot express.

## 2. Requirements

From AGENTS.md, the roadmap, and what the simulation does today:

- **Determinism.** Same commands, same result, bit for bit, on one build; the state hash, replays,
  and `--check-determinism` stay valid; and now also: the same result whatever the number of
  worker threads. Iteration order, floating-point order of operations, random streams, and merge
  order are compatibility state.
- **Hash-neutral migration.** Each migration step keeps `check-battle.ps1`'s hash. A deliberate
  change of the hash's layout comes once, at the end, said as such.
- **Layers.** The simulation knows nothing of XML, files, rendering, or Lua. The battle layer turns
  XML types into what the simulation needs; presentation reads immutable snapshots and never feeds
  back; scripts read snapshots and send validated commands.
- **No exceptions, no hidden globals.** The job system is created and passed, or it is added to
  AGENTS.md's short list of process-wide services by an explicit decision.
- **Data-driven types.** An XML type becomes a recipe of components once; spawning instantiates it.
  Modder-defined components get room, not an implementation (the owner, 2026-11-13).
- **Scale without caps.** Thousands of entities in a battle, more in space with fighters and debris;
  no fixed limit on entities or component types (AGENTS.md: no hard-coded stock limits).
- **Small, testable systems.** A system can be tested with a handful of entities and only the
  components it reads.
- **Debuggable.** Data stays inspectable in a debugger; no reflection layer or template maze on the
  hot path.

## 3. What we have, sorted into candidate components

The per-entity arrays before migration step 2, by subject (every entity has all of them; step 2
grouped them into column groups, section 6.2, under the same field names):

| Candidate component | Arrays today | Who needs it |
| --- | --- | --- |
| Identity | `ids_`, `type_names_`, `owners_`, `visuals_`, `selectable_` | every entity |
| Placement | `positions_`, `headings_`, `radii_`, `speeds_` (derived), `tick_start_positions_` (per tick) | every entity |
| Locomotor | `max_speeds_`, `turn_rates_`, `movements_` (profile), `drive_speeds_`, `speed_limits_`, `locomotor_suspended_`, `arriving_` (derived) | movers |
| Ground mover | `follows_ground_`, `passable_types_`, `separation_push_` | land units |
| Destination and route | `move_targets_`, `paths_`, `path_pending_`, `remaining_route_`, `detours_`, `loiter_points_` | movers with an order |
| Steering intent | `intents_` (rebuilt each tick by behavior) | movers |
| Flight | `min_speeds_`, `pitches_`, `cruise_heights_`, `yaw_rates_`, `looping_`, `rolls_` | fighters and bombers |
| Walk played as clips | `walk_plans_`, the walk fields of `MovementProfile` | walkers with transition clips |
| Turn played as clips | `turn_plans_`, the turn fields of `MovementProfile` | walkers with turn clips |
| Combat | `combat_` (profile, health, shield, target, weapons, hardpoints), `last_shot_ticks_`, `aims_with_body_`, `stances_` | armed or damageable |
| Orders | `command_serials_`, `standing_orders_` | commandable |
| Formation member | `formation_orders_`, `formation_spacings_`, `formation_solos_`, `formation_teams_` | land units in groups |
| Group cohesion | `group_ids_`, `group_best_remaining_`, `group_stall_ticks_`, `group_waiting_` | members of a moving group |
| Obstacle footprint | `footprints_`, `footprint_stamped_`, `footprint_cover_` | immobile obstacles |
| Crushing | `crushes_` | crushers and crushables |

World-level state (one per world): the ground and its stamped passability, the path queue,
projectiles, the tick's events (deaths, impacts, shots, hardpoint deaths, spins), the broad-phase
grid, the hostility table, the command queue.

About half the arrays belong to a minority of entities (flight, clip locomotion, footprints,
formation). A building or a prop carries about 50 fields it never uses. Another split shows in the
table: much of each entity's data is its type's (profiles: combat, movement, clip times, crush,
formation order and spacing), copied into every instance and hashed for every instance, and the
rest is its state.

## 4. Research: what others do

### 4.1 Storage

- **Archetypes (tables of entities with the same component set).** flecs, Unity Entities (16 KiB
  chunks), Bevy (by default), Singe. Fastest multi-component iteration; adding or removing a
  component moves the entity's whole row to another table; removal fills the gap with the last
  entity, so row order follows the history of adds and removes. flecs and Bevy let single
  components opt out into sparse storage for cheap toggling, and flecs adds a toggle bit so
  frequent state changes do not move rows; Unity has "enableable" components for the same reason.
- **Sparse sets (one packed pool per component).** EnTT, Photon Quantum (a deterministic ECS used
  for rollback games). Adding or removing touches one pool; one pool or an "owning group" iterates
  as fast as a table; iterating several pools checks membership per entity. A pool can be sorted
  explicitly (by entity, or by another pool's order), which archetypes cannot do easily ("only one
  order can be imposed on an array", skypjack). EnTT's multi-pool views iterate the smallest pool,
  so their order depends on pool sizes unless one says otherwise.
- **Plain struct-of-arrays per entity kind.** What we have, and what critics recommend when runtime
  composition is not needed (Gustafsson 2025: "will probably take you all the way to the finish
  line"; the ECS FAQ: ECS is poor at problems that need specialized structures, such as spatial
  grids and pathfinding).
- **Prefabs.** flecs alone has native data-driven templates with inheritance (IsA, variants, and a
  per-component choice to copy into each instance or share from the prefab). EaW's XML types
  already behave so (`Variant_Of_Existing_Type`), which our `TypeFields` already resolves.
- **Change tracking, observers.** Coarse everywhere (per table or chunk, "did a writer run", not
  "did data change"); observers mostly fire at sync points. Our Machinery found add/remove
  callbacks problematic.

### 4.2 Determinism in shipped games

- **Lockstep with a hashed state is the RTS norm** (Age of Empires, Supreme Commander, Spring/Recoil,
  OpenRA, Factorio, Stormgate): commands are shared, never state; the simulation runs at a fixed
  low rate (SupCom 10 Hz, AoE turns of 200 ms) and presentation interpolates and never writes back
  (SupCom's User layer, Spring's unsynced code). Planetary Annihilation is the counterexample
  (server-authoritative replication).
- **Order.** OpenRA iterates actors in ascending, monotonic actor id and folds iteration position
  into its hash; structural changes go through end-of-tick tasks. Unity makes parallel command
  recording deterministic by sorting on a key that does not depend on scheduling.
- **Side effects as data.** Overwatch's systems queue impacts and damage into singleton components
  resolved by one later system ("deferment"); about 40% of its components are singletons holding
  shared state that would otherwise hide inside systems. Systems run in a fixed order; read/write
  tags let readers run in parallel.
- **Tooling decides.** AoE checksummed per system with labeled sync macros; SupCom found desyncs by
  binary search over a printed state hash; Spring's sync-debug build records every synced write
  with a backtrace. Forrest Smith: build prevention and tooling, not desync recovery.
- **Parallelism is hard and often not the win.** Factorio: running nearly independent systems in
  parallel was slower (cache invalidation from shared writes); multithreading electric networks
  cost 30 times the CPU for 0.11 ms; the big wins came from not updating (sleeping entities,
  stored intentions, update buckets); and a determinism bug that depended on the machine's core
  count lived from 2017 to 2024.
- **Floating point.** Same binary on the same architecture is achievable; across compilers or
  architectures it is very hard (Gaffer). Hazards: `/fp:fast`, FMA contraction, estimate
  instructions, per-thread FPU state, library transcendentals that differ between CPUs or
  dispatch by CPU features (Dawson; Battlezone 2 wrapped sin, cos, tan; Factorio's only
  cross-platform issues came from sin). Quantum uses fixed point; SupCom, Spring, and Factorio
  kept floats under control.

### 4.3 Job systems

- **Fibers** (Naughty Dog) let a waiting job give up its worker, at the cost of fiber-safe TLS
  (`/GT`), no OS mutexes across waits, and a lot of machinery. Bungie's "job fibers" are plain job
  chains. Our Machinery and FiberTaskingLib follow Naughty Dog; Our Machinery's lock-free queue
  "failed miserably" and it fell back to a locked one.
- **Thread pools with help-while-waiting** (Molecular Matters, enkiTS, oneTBB, Taskflow): N-1
  workers plus the calling thread, per-worker deques with work stealing (Chase-Lev; subtle fences
  and races), `parallel_for` by recursive halving, dependencies by counters or continuations, idle
  workers sleeping on a semaphore. The hazard of help-while-waiting: a waiter may run unrelated
  work while holding thread-local state or a lock (oneTBB's work isolation).
- **Declared access.** Unity's safety system rejects two independent jobs writing the same
  container at schedule time; Bevy schedules systems in parallel from their declared parameters
  and can report "ambiguities" (conflicting access without an order) and shuffle system order to
  expose hidden dependencies; Destiny validates every read and write at run time.
- **Deterministic parallel work.** oneTBB's `parallel_deterministic_reduce` makes the same splits and
  joins whatever the thread count; enkiTS: "the thread index should not be used for changing
  processing of data", only for per-thread output buckets; Rayon (contrast): reduction order
  unspecified, so float sums are not reproducible. The rule that follows: chunk boundaries depend
  on the problem size and a constant grain only, each chunk writes its own output slot, and chunk
  results merge in chunk order at a barrier.
- **Libraries.** enkiTS: small, C and C++, no exceptions, priorities, pinned tasks, profiler
  callbacks. Taskflow: C++20 task graphs but built on exceptions (conflicts with `/EHs-c-`). oneTBB:
  large external dependency. Marl: fibers.

### 4.4 Singe (the owner's engine)

- Archetypes of 16 KB chunks with one type pool per component; a per-archetype hash map from
  entity to (chunk, slot); moving between archetypes copies components.
- Entities are 64-bit UUIDs; component types get an 8-bit id from a global library (at most 64
  types, `std::bitset<64>` masks) and are reflected through Singe's `rti` library.
- Views by modifiers: `RWAccess<T>`, `ROAccess<T>`, `Without<T>`, and reactive views that see
  components being added; views cached per modifier list.
- Systems ordered by before/after metadata into "turns" by a topological sort, run one after
  another on one thread; snapshots of every entity through reflection.
- A job system beside it (counted dependencies, three priority queues under one lock, workers, a
  thread helping until a job completes), not used by the systems.

Worth taking: the read/write/without modifiers (exactly the access declarations a scheduler and a
Debug checker need), ordered phases, setup when a component is added. Not as is: the 64-type limit
(mods), global component and job registries, `dynamic_cast` and hash maps on the hot path, UUIDs in
place of the index and generation our commands and snapshots use, a reflection library we do not
have, and no determinism rules.

## 5. Options for storage

| | A. Status quo, tidied | B. Dense core + sparse pools (recommended) | C. Archetypes | D. EnTT |
| --- | --- | --- | --- | --- |
| What | one table, fields grouped into structs, bookkeeping generated | every entity in one dense core table; optional components in sparse pools | tables per component set (Singe, flecs, Unity) | the library's sparse sets and groups |
| Optional data | carried by all | only where present | only where present | only where present |
| Add/remove cost | n/a (flags) | O(1), one pool | row move of all components | O(1) |
| Iteration | fastest | core: as today; pools: packed | fastest per table | fast for one pool or a group |
| Order | as today | core order drives every query unless a system opts out | history of moves; needs explicit sorting for a canonical order | smallest pool unless told; sortable |
| Hash-neutral migration | yes | yes (absent components hash their defaults until the declared change) | harder: the row order changes as soon as tables split | as B, plus learning its order rules |
| New code | small | about 1,000 lines with tests | about 2,000 lines | none, but a dependency |
| Fits squads, abilities, deploy states | poorly | yes (cheap add/remove) | yes, with row moves | yes |

Recommendation: **B**. Reasons:

1. Our scale is thousands, not hundreds of thousands. The archetype advantage (contiguous rows for
   every query) matters little at this size; Factorio's experience says the memory traffic of
   shared writes, not iteration layout, decides at our scale.
2. Component churn is coming: squads joining and losing members, deploy states (AT-AA, SPMAT,
   AT-AT troop drop), abilities with durations, shields switched on and off, garrisons. Sparse
   pools take these in O(1) without moving other data.
3. One order for everything is easy to reason about and to keep deterministic: queries walk the
   dense core in its order and look up pools by slot in O(1). A system whose work is purely per
   entity can declare that it may walk a pool's packed order instead (an optimization, opted into).
4. The migration from today's arrays is mechanical, and each step can keep the hash.
5. It needs no dependency. EnTT would save the pool code but bring its own order rules (smallest
   pool first, swap-and-pop), which we would have to wrap anyway; that is the owner's call
   (question 1).

## 6. Proposed design: entities, components, systems

### 6.1 Entities

- Keep `EntityId{index, generation}` as the handle commands, snapshots, scripts, and presentation
  already use.
- Add a monotonic 64-bit **spawn serial** per entity: the canonical order (like OpenRA's actor id),
  the key for deterministic merges, and the identity that survives a future save and load.
- The dense core keeps every entity in one order. Removal compacts at the end of the tick
  preserving order, so the dense order is always the spawn order (done at migration steps 6 and 7,
  2026-11-15; removal used to swap the last entity into the gap). Not yet: the spawn serial, added
  with its first reader (a merge keyed by it, or save and load).

### 6.2 Components

- A component is a group of columns, one array per field (the owner's decision, 2026-11-15:
  hot loops stream single fields, and the broad-phase grid takes the positions as one span), not
  an array of structs: `PlacementColumns` holds `positions`, `headings`, `pitches`, and `radii`.
  Its elements are plain data: no virtual functions, no ownership of other entities' data,
  default-constructible, trivially copyable where possible (vectors allowed where a component holds
  a list: a route, a squad's members).
- **Core** (every simulated entity, in the dense table): Identity (type, owner, selectable, visual
  key), Placement (position, heading, pitch, radius), Motion (speed in the last tick).
- **Pooled** (only where present): Locomotor, GroundMover, Route, Flight, WalkClips, TurnClips,
  Combat, Shield, Weapons, Hardpoints, Orders, FormationMember, GroupMember, Footprint, Crusher,
  Crushable, and later SquadMember, Squad, Deployable, ability states.
- **Type data apart from state.** A recipe's immutable profiles (combat profile, movement profile,
  clip times, crush profile, formation order and spacing) are stored once per recipe; an entity's
  component holds its recipe index and its state only (flecs' "inherit" choice). Today each
  instance copies and hashes its type's profiles. This also removes the "bag of modes": a walker
  has a `WalkClips` component whose profile holds the clip times, and a building simply lacks it.
- **World resources** for what exists once: the ground and stamped passability, the path queue,
  projectiles, the hostility table, the broad-phase grid, the random streams, and **event queues**
  (impacts, damage, deaths, shots, crushes): side effects as data, written by the systems that
  cause them and resolved by one system in a defined order (Overwatch's deferment).
- **A registry, built when the world is built** (no globals): each component type gets a run-time
  id (no fixed maximum), its name, its storage (core or pool), its hash function (field by field,
  as today, because of padding), and its snapshot hook if it feeds the snapshot.

### 6.3 Queries and systems

- A **system** is a function over a context: `run_locomotion(SystemContext&)`, with a declaration of
  what it reads and writes (components and resources), modeled on Singe's modifiers:
  `Reads<Placement, Locomotor>`, `Writes<Motion>`, `Without<Flight>`, `ReadsResource<Ground>`.
- **Queries** iterate the dense core order and yield the components the system declared, skipping
  entities without them. In Debug, the context refuses access to anything undeclared (an assert
  naming the system and the component): Destiny's validation, cheaply.
- **The schedule** is one explicit list, in today's tick layers, so it can be read and reviewed in
  one place:

  1. Commands (apply the tick's commands in submission order)
  2. Behavior (orders, stances, targets into steering intents and route requests)
  3. Navigation (route requests, group cohesion)
  4. Locomotion (walks and turns played as clips, ground movement, flight)
  5. Contact (crushing, separation)
  6. Combat (targeting, weapons, projectiles, damage events)
  7. Resolution (apply damage events, deaths, hardpoint losses, in a fixed order)
  8. Structural changes (spawns, removals, component additions and removals queued during the tick)
  9. Publish (the snapshot, the hash)

- Within a phase, systems run in their listed order. A phase may mark some of its systems as
  independent (disjoint writes); only those may run in parallel, and the schedule checks the
  claim at start-up from the declarations (Bevy's ambiguity rule, as an error, not a log line).
- Per-system timings are recorded every tick (the profile section 1 lacks).

### 6.4 Structural changes

- During a tick, systems never add or remove entities or components directly; they queue commands
  in a command buffer with a sort key (system, chunk, sequence). The buffers are applied at the
  structural-change barrier in key order (Unity's sorted command buffers; OpenRA's end-of-tick
  tasks). New entities take their slots and spawn serials there, deterministically.
- Today's rule ("entities are only added by commands before movement and removed by combat after
  it") becomes the general rule.

### 6.5 Recipes from XML

- The battle layer owns a **reader registry**: for each component, a function from an XML type's
  fields (`TypeFields`, which already resolves `Variant_Of_Existing_Type`) and its resources
  (clips, model bounds) to the component's profile, or nothing when the type does not have it.
  `apply_animation_locomotion`, `type_movement`, `combat_profile`, and the crush and formation
  readers become such readers.
- A **recipe** is built once per XML type (cached per battle): the list of components it has and
  their profiles. `SpawnableType` and `EntitySpawn` shrink to "recipe plus per-instance values"
  (position, heading, owner, visual key, team), and the two construction paths (runtime spawns and
  placed objects) become one: instantiate a recipe.
- The simulation sees recipes as plain data (component ids and profile values); it never sees XML.

### 6.6 Snapshots, scripts, presentation

- The Publish phase builds `SimSnapshot` exactly as today, so presentation, scripts, and the
  sandbox are untouched by the migration. Later, snapshot fields can follow components (a snapshot
  entry carries a walk clip only if the entity has `WalkClips`).
- Scripts keep reading snapshots and sending commands (Factorio and Spring both name script
  ordering as a determinism hazard; ours is already serialized at defined points).

### 6.7 Squads (for work order 2f)

- A squad is an entity with a `Squad` component (its members, its formation, its order); each
  member has a `SquadMember` component pointing back. Orders go to the squad; a squad system turns
  them into member intents. A member's death queues its removal from the squad at the structural
  barrier. flecs' relationships do this generically; at our scale two components and one system
  are enough.

### 6.8 Room for modder-defined components (not designed further)

- Component ids are assigned at run time, so a mod could register more without a limit.
- A future data-only component would be a named schema (fields of number, flag, text, entity
  reference) stored as a byte pool, read and written by Lua through commands, with no native
  system; a mod's manifest would map its XML tags to it through the same reader registry.

### 6.9 Determinism rules (in AGENTS.md, "Simulation and concurrency principles")

1. One tick order: the schedule's list; systems in a phase run in order unless declared independent
   and checked.
2. One entity order: the dense core order (spawn order after the declared change); no system
   iterates a hash container.
3. Structural changes only at the barrier, in sort-key order.
4. Side effects as data in event queues, resolved by one system.
5. Parallel work: chunk boundaries from the item count and a constant grain, never the thread
   count; each chunk writes only its own output; outputs merge in chunk order; atomics only for
   scheduling (section 7).
6. Random numbers from per-purpose streams keyed by entity and tick, never a shared generator in
   parallel code; presentation's randomness never touches them; their state is hashed.
7. Floating point: keep `/fp:precise` (today's default; no flag is set), never `/fp:fast`, no FMA
   contraction (to verify for x64 builds); before multiplayer (M6), engine-owned `sin`, `cos`,
   `atan2` for the simulation, because the C runtime's may differ between CPUs (the simulation
   calls them about 85 times today).
8. Tooling: per-system sub-hashes behind a flag, so a replay mismatch can be narrowed to the
   system and tick that diverged (AoE, SupCom, Spring); `check-battle.ps1` also runs with 1 and N
   worker threads and requires the same hash.

## 7. Proposed design: the job system

The owner's decision (2026-11-15): fibers, and no OS mutexes in the job system. The rest of this
section is the design for that; the thread-pool alternative of the first draft is kept in 7.6 for
the record.

### 7.1 Shape

- A new library `eaw_jobs` (`src/jobs`), depending on `eaw_core` only, Windows fibers
  (`ConvertThreadToFiberEx`, `CreateFiberEx`, `SwitchToFiber`) behind a small platform seam.
- A `JobSystem` object, created by the executable and passed by reference (no global): N-1 worker
  threads (configurable; 0 runs everything on the calling thread), each turned into a fiber, plus
  the calling thread. A fixed pool of fibers created up front (count and stack size configurable,
  stacks reserved with guard pages so an overflow is a crash report, not corruption).
- Jobs are plain records (function, argument, counter, priority, chunk index) copied into bounded
  lock-free queues, one per priority (Vyukov's bounded multi-producer, multi-consumer ring: one
  atomic sequence per cell, no ABA). A full queue is a diagnosed failure, never an overwrite.
- **Counters** are the only synchronization (Naughty Dog, FiberTaskingLib, Our Machinery): a job
  decrements its counter when it finishes; `wait(counter, value)` returns at once if the counter
  is there, and otherwise parks the calling fiber on the counter and switches the thread to a
  fresh fiber that goes on running jobs. When the counter reaches the value, the fiber that
  finished the last job moves the parked fibers to the ready queue, and some worker resumes them.
- **No OS mutexes, condition variables, or events in the job system.** Atomics and lock-free queues
  only. Idle workers spin briefly (`_mm_pause`), then yield, then sleep on an atomic with
  `std::atomic::wait`, woken by `notify_one` when work arrives (on Windows this is `WaitOnAddress`:
  a wait on a memory address, not a mutex).
- No exceptions: a job writes its `Status` into its own output slot; the waiter checks them.
  Unrecoverable failures stay `fatal_error`.

### 7.2 The hard parts, and how they are handled

1. **Parking a fiber that is still running.** A fiber cannot put itself on a wait list and then
   switch away: another thread could resume it before the switch completes. Every switch therefore
   goes through one helper that hands the "after the switch" work to the fiber it switches to
   (park the previous fiber on its counter, or return the previous scheduling fiber to the pool),
   done first thing on the other side; the parked fiber's counter is checked again after it is
   registered, so a counter that finished in between resumes it at once. (FiberTaskingLib's design.)
2. **The main thread.** Window, Direct3D, and audio calls must stay on the thread that made the
   window. Fibers that must stay on a thread (the main thread's root fiber) are pinned: when ready
   they go to that thread's own ready queue, which only that thread takes from.
3. **Thread-local storage.** A fiber may resume on another thread, so a function that waits must
   not keep a thread-local address across the wait. The code base has no `thread_local` today
   (checked 2026-11-15); the rule goes into AGENTS.md, and the build adds MSVC's `/GT` (fiber-safe
   thread-local storage) so the compiler does not cache such addresses either.
4. **Locks outside the job system.** The log, the asset cache, the fatal-error path, and the
   frame watchdog use OS locks; a job may take them only for a short section that never waits on a
   counter (a fiber holding a lock and parked on another thread could deadlock). Rule for
   AGENTS.md: never wait while holding a lock.
5. **Debugging.** A parked fiber's call stack is not visible as a thread in the debugger. Mitigations:
   each fiber records the job it runs and where it waits (a name and counter), readable in a
   debugger watch and printed by the crash handler; a serial mode (0 workers) for stepping through.
6. **Fixed resources.** Fibers, queue capacity, and waiter slots per counter are preallocated and
   checked: running out is a contextual fatal error naming the limit and how to raise it, never a
   silent drop (AGENTS.md: temporary capacities explicit, checked, diagnosed).

### 7.3 Primitives

1. `run(jobs, counter)` and `wait(counter, value)`.
2. `parallel_for(count, grain, function(begin, end, chunk))`: chunks fixed by the count and a
   constant grain, never the thread count; the caller waits on the loop's counter (and runs jobs
   meanwhile). The workhorse for simulation systems.
3. `ChunkOutputs<T>`: one output slot per chunk and a merge in chunk order, the deterministic
   reduction.
4. Priorities: simulation tick, frame (presentation), background (asset decoding, content builds,
   hot-reload checks). Background work is split into short jobs, so a long decode does not hold a
   worker for a whole frame; the sandbox's scattered `std::async` calls move onto it.
5. Profiling hooks: job names and timestamps per worker and fiber, readable by the sandbox's
   statistics and a future trace view.

### 7.4 Determinism

The fiber machinery decides only when and where a job runs. Results stay independent of it by the
rules of 6.9: chunk boundaries from the count and grain, outputs per chunk, merges in chunk order,
atomics for scheduling only. A test runs the same `parallel_for` workloads with 0, 1, 2, and 8
workers and requires identical outputs; `check-battle.ps1` does the same with the battle hash once
systems run in parallel.

### 7.5 First uses, by measured need

1. **Route searches** (8 ms of the 51 ms opening tick at 3,300 entities; the rest is combat): up to K requests searched at once on the
   immutable ground (each with its own budget share), results applied in request order in the same
   tick.
2. **Separation and contact**: read-only neighbor queries; pushes per chunk, summed in chunk order.
3. **Combat target searches**: read-only over the broad-phase grid; results per chunk. Fewer
   searches come first (the roadmap's work order 2g: the opening tick's 38-47 ms is every entity
   searching at once).
4. **Presentation**: pose sampling on a cache miss, particles per system, frame building.
5. Measured before and after each (Factorio found shared writes make parallel updates slower).

### 7.6 The alternative not taken

A thread pool with locked queues, help-while-waiting, and no fibers (the first draft): simpler to
debug, no thread-local or pinning concerns, but a waiting job either blocks its worker or runs
unrelated work on top of its own stack (oneTBB's work-isolation hazard). Fibers avoid both at the
cost of 7.2.

## 8. Migration, step by step

Each step is its own commit and keeps `check-battle.ps1`'s hash, the test modules, and the
captures, except step 7, which changes the hash's layout once, on purpose.

0. **Tooling.** Per-system timings in the battle statistics; per-system sub-hashes behind a flag.
   (Needed anyway to explain the superlinear tick.) Done 2026-11-15: per tick layer, and
   `--hash-trace` by state subject.
1. **The job library** (work order d) with its tests (determinism of `parallel_for` and
   `ChunkOutputs` across 1, 2, and 8 threads; Status propagation; preallocation limits). First
   client: presentation's pose sampling (not hash-relevant), then the background lane replacing
   `std::async`.
2. **Components as column groups inside the one table.** Group the arrays into the components of
   section 6.2, still one dense table; spawn, remove, and hash generated per component.
   Mechanical; the hash function keeps its interleaved order. Done 2026-11-15: 48 columns in 16
   groups (`SimulationWorld::IdentityColumns` and the rest), with today's memory layout. Removal
   goes through `for_each_column`; spawn still sets each column's value itself, and a size check
   after it needs every column to have grown by one; the constructor checks that the lists name
   every column and group (`check_entity_table`). The hash stays field by field in its interleaved
   order until step 7 makes it per component. The per-tick scratch arrays (start positions,
   separation pushes, detours, remaining routes) stay outside the table.
3. **The registry and pools.** Move optional components into pools one at a time, smallest first:
   WalkClips and TurnClips, then Flight, Footprint, Crush, FormationMember and GroupMember, then
   Combat. Until step 7 the hash visits every entity in today's order and hashes an absent
   component's default values where they used to be, so the hash is unchanged. Under way
   (2026-11-15): `ComponentPool<Columns>` (`simulation/component_pool.hpp`: a column group packed
   for its members, a sparse row index by slot, swap-removal within the pool; `find` and `get` by
   slot and column) holds WalkClips and TurnClips. An entity gets them at spawn from its movement
   profile (turn clips with `turn_begin_seconds`; walk clips with `walk_stop_parts` or
   `walk_raise_seconds`); the hash already hashed these plans only for such entities, so it needed
   no defaults. The registry (run-time component ids, names, hash and snapshot hooks) is not built
   yet: nothing reads it before recipes (step 5).

   The hash rule decides what can be pooled before step 7 (the owner's choice, 2026-11-15): an
   absent component can stand in for its old values only where every non-member had the same
   constant ones. Pooled on that rule: flight manoeuvres (yaw rate, half-loop, roll: zero for
   non-flyers), footprints (not hashed), and crush profiles (the default profile for the others).
   Kept in the core table until step 7, because a non-member's hashed values are its own: the
   minimum speed and cruise height (a ground unit's spawn values; the minimum speed also marks a
   flyer in hot loops), formation membership, and combat. Group cohesion has constant values for
   non-members but changes membership during a tick (move orders form groups), so it waits for the
   command buffers of step 6.
4. **Systems with declared access.** Turn the `run_*` member functions into systems over queries,
   in the schedule of section 6.3, with Debug access checks. First slice done 2026-11-15: the tick
   is one table, `SimulationWorld::schedule_` (`simulation_schedule.cpp`): nine systems (commands,
   behavior, navigation, movement, crushing, separation, motion, combat, publish), each with its
   tick layer and the data it reads and writes (`SimData`: the 17 components and pools and the
   resources). Reads are a static over-approximation (what each system's functions mention),
   reviewed, not checked. Writes are checked in the `DebugValidation` build only (the owner's
   choice: the check copies every undeclared kind of data before each system and compares it
   after, about 17 times a Debug tick on a populated map): a system that changes data it did not
   declare stops the run, naming both. The ground is declared but not checked (megabytes on a real
   map). The first declarations, read from the code, missed 15 writes, all found by the check's
   first runs. Commands and combat wrote everything until step 6 moved removals to the barrier. Not yet: queries, and systems as functions over a context.

   Typed views (2026-11-15, Singe's access modifiers adapted to column groups,
   `simulation/system_view.hpp`): a system declares its access as the type of its view,
   `SystemView<Read<GroundState>, Write<CombatColumns>, ...>`, and reaches the data only through
   `view.get<T>()`: a const reference for a Read, a mutable one for a Write, a compile error for
   anything undeclared or for a write through a Read. The schedule entry takes its masks from the
   view's type, so the declaration cannot drift from the code, and the copy check skips such
   systems. Systems keep their dense-index loops over the columns; their own scratch and counters
   are members of the system object. The first one is contact (crushing, separation, and the
   measured speeds, one system for the design's contact phase, so the table has seven systems:
   commands, behavior, navigation, movement, contact, combat, publish). The other systems follow
   one at a time, each with unchanged hashes. Movement is the second (2026-11-15:
   `MovementSystem`, ground movement, walks and turns as clips, flight, keeping ships apart). A
   helper several systems use is a static template over its data, `template <class Data>
   travel_speed(Data& data, ...)`, reading through `data.get<T>()`: a system passes its view (the
   compiler holds the helper to that view's declaration), the systems still written as member
   functions pass the world (`SimulationWorld::get<T>()`, full access), and there is one copy.
   Navigation, behavior, combat, and publish followed (2026-11-15): six of the eight systems are on
   typed views. Commands and the removal of the dead stay member functions with full access, since
   they spawn and remove entities, until step 6 queues structural changes for the barrier; removal
   was split from combat for that reason. A typed view found writes the hand-written declarations
   missed and DebugValidation's runs never reached (behavior clears a unit's group speed limit and
   group when it starts an engagement or sends a flyer to loiter).
   The copy check was removed then (the owner, 2026-11-15): no system is left for it, since the typed
   ones are checked by the compiler and commands and removal declare that they write everything;
   `DebugValidation` stays for later time-consuming checks. Queries as a separate helper were tried and dropped:
   the loops already walk the dense order and look pools up by slot, and sorting a pool's members
   cost more than it saved (crushing, 0.006 to 0.025 ms a tick).
5. **Recipes.** Readers per component; one construction path for spawns and placed objects;
   `EntitySpawn` reduced to a recipe plus per-instance values. First part done 2026-11-15, in the
   battle layer: `TypeRecipe` (`battle/type_recipe.hpp`) is what an XML type gives every entity,
   read once from its XML and model (`build_type_recipe`), and `make_spawn(recipe, instance)` is
   the one construction path; `SpawnableType` is a recipe plus a company's members, and placed
   objects take a recipe per type and model from a cache of their own (so the runtime-spawn list,
   its looks, and the sandbox's spawn menu are as before). The owner's rule for the two paths'
   differences: a type behaves the same however it appears, so runtime-spawned obstacles block the
   ground and immobile space objects keep ships clear, and following the ground needs a valid
   ground on both paths. The readers stay the free functions they were (`type_movement`,
   `type_crush`, ...); a registry of them waits for modder-defined components.

   Second part done 2026-11-15 (the owner's decision, after weighing it against waiting for step 6):
   the simulation's side. `EntityRecipe` holds a type's data and `EntitySpawn` is a `RecipeId` plus
   the instance's values (owner, position, heading, look, selectability, formation team, footprint,
   stance). `SimulationWorld::add_recipe` validates a recipe once and keeps equal recipes as one,
   so entities of one type that must differ (a company member taking its team's speed, a placement
   scaled differently) get a recipe of their own: per-entity variation is a variant recipe, ids in
   registration order. The recipes live in an append-only deque, never changed during a battle; an
   entity points at its recipe's combat and movement profiles (`CombatState::profile`,
   `LocomotorColumns::movements`) instead of copying them, and its type name is its recipe's.
   Cheap scalars (speeds, radius, turn rate) stay per-entity columns copied at spawn. The hash reads
   the same values through the pointers, so it is unchanged. Temporary per-entity changes of type
   data (abilities, veterancy) will need override columns when they come.
6. **Command buffers and event queues.** Structural changes and damage through the barrier, in the
   order they happen today.
7. **The declared change.** Spawn order as the dense order (compacting removal), per-component
   hashing, absent components no longer hashed, recipe profiles hashed once per recipe; the
   battle hash is rebaselined once and said so.

   Steps 6 and 7 done together 2026-11-15 (the owner's choice: one declared hash change). Damage is
   an event queue (`DamageQueue`): combat queues each shot's hit or miss with its blast, contact its
   crushes, and the resolution system (after combat, its own tick layer) applies them in queue
   order. A shot whose victim died earlier in the tick strikes the body without damage (the
   owner's rule: two units firing at once both hit), so no system sees damage done within the tick
   before resolution. Deaths are then handled in dense order, each entity once per tick (spins
   begin or count down, the dead are reported and their death blasts queued), in rounds until a
   round kills nothing, so chain kills end in the tick they start. The dead leave at the end of the
   tick in one stable pass over every column (`remove_entities`), and a script's destroy command
   uses the same pass at once (commands are the start barrier; script damage stays immediate
   there too). The hash is per component, each in dense order, pooled components only for their
   members, recipe type names and movement profiles once per recipe. Every battle hash changed
   once; the battles' outcomes stay close (the capital ships fire the same 3,789 shots and land 50
   more, the shots that now strike a body) and their tick times unchanged. The pools for the rest
   followed as declared hash changes of their own (the owner, 2026-11-15), each with the battles'
   outcomes unchanged: flight first (2026-11-15), the minimum speed and cruise height joining the
   manoeuvres in one `FlightColumns` pool, flyers only, so `flies(data, index)` (pool membership)
   is the flyer test that a minimum speed above 0 was. Then formation: its order, spacing, and
   moving alone are recipe data, read from the recipe (and hashed once per recipe) instead of
   copied into columns, and a company member's team is a pool (`FormationColumns`, entities spawned
   with a team other than the default). Then group cohesion (`GroupColumns`): an entity joins the
   pool when a formation move of more than one unit takes it and leaves it when the move ends, is
   broken off (an engagement, a spin, a loiter, a new order), or stalls (`leave_group`), so the
   membership changes within a tick as it did; queries look rows up per entity, since a leaving
   member's row is refilled from the pool's last, and `waits(data, index)` is false for a
   non-member. Then combat (`CombatColumns`, one column of `CombatState`, which took in the
   stance, whether the body aims, and the last shot's tick): members are the entities whose
   recipe has health, a shield, weapons, or hardpoints (on Utapau 25 of 770 placed objects; the
   rest are props). `combat_state(data, index)` reads a member's state or `no_combat()` (no
   health, weapons, or target, the default profile), `combat_of` gives a member's to change (null
   for others), and `drop_target` clears a target; scripted conditions, damage, and stances reach
   members only. The pool's lookups cost about 9% of a 500-unit tick at first (the target search
   looked a candidate up three times); one lookup per candidate, after the cheaper hostility test,
   brought it level with the core table (A/B on one machine, 12.0 against 12.2 ms). Every component
   but the core's (identity, placement, motion, locomotor, ground mover, route, orders, steering)
   is now a pool.
8. **Parallel systems.** Route searches, then contact and targeting, each verified with 1 and N
   threads giving the same hash, and measured. Measured first (2026-11-16, the 500-unit
   benchmark): route searches 0.16 ms a tick, budget-limited, against ground avoidance's 4.3 ms
   (2.5 of it giving way, made cheaper serially first). So by 7.5's rule (by measured need) ground
   avoidance's ORCA went first: per steering unit, chunks of 4, scratch per chunk, 1.48 to 0.43 ms
   on 7 workers. The simulation takes the job system through `SimulationWorld::set_job_system`
   (null: serial); `--check-determinism` compares a run on it with a serial one. Next by
   measurement: movement (1.6 ms), combat (1.1 ms), contact (0.6 ms); route searches where the
   opening tick's spikes need them.
9. **Refactoring.** A review of what the migration left, its scope and plan to be agreed with the
   owner first (the owner, 2026-11-15).

Squads (work order f) follow, built on pools and the command buffer.

## 9. Decisions

Agreed by the owner on 2026-11-15:

1. **Storage:** a dense core table plus sparse component pools (section 5, option B), no archetypes,
   no third-party ECS.
2. **Job system:** our own, on fibers, without OS mutexes (section 7).
3. **Canonical order:** spawn order with compacting removal at migration step 7, the battle hash
   rebaselined once.
4. **Cross-machine determinism:** floats under controlled settings; the simulation's own
   trigonometry before multiplayer (M6); no fixed point.
5. **The job system's ownership:** created by the executable and passed explicitly, no global.
6. **Type data:** recipe profiles stored once and referred to, not copied into each entity.
7. **Singe:** its ideas (access modifiers, phases), not its code.
8. **Where it lives:** this document; the rules of 6.9 and 7.2 in AGENTS.md; the steps of section 8
   in the roadmap. `/GT` (fiber-safe thread-local storage) in the global compile options.
9. **Component layout** (2026-11-15, at migration step 2): a component is a group of columns, one
   array per field, not an array of structs (section 6.2). The owner, 2026-11-15: kept for now; the more usual
   struct-per-entity style may replace it later.

## 10. What this design deliberately leaves out

- A reflection library, runtime-defined native components, and observers or reactive systems:
  explicit systems and the reader registry do the same jobs at our scale.
- Automatic parallel scheduling from access sets: the schedule stays an explicit list; declared
  access is checked, not used to reorder.
- Change detection: no system needs it yet.
- Work stealing between per-worker queues: shared lock-free queues per priority first; stealing
  only if a profile asks.
- Fixed point: floats under controlled settings, as SupCom, Spring, and Factorio do.
- A generic relationship system: squads need two components.

## 11. Sources

[ecs-research.md](ecs-research.md) lists every source with what it supports; the main ones:

- Bettner, Terrano: "1500 Archers on a 28.8: Network Programming in Age of Empires and Beyond".
  https://www.gamedeveloper.com/programming/1500-archers-on-a-28-8-network-programming-in-age-of-empires-and-beyond
- Forrest Smith: "Synchronous RTS Engines and a Tale of Desyncs", "Synchronous RTS Engines 2: Sync
  Harder", "The Tech of Planetary Annihilation: ChronoCam".
  https://www.gamedeveloper.com/design/opinion-synchronous-rts-engines-and-a-tale-of-desyncs,
  https://www.forrestthewoods.com/blog/synchronous_rts_engines_2_sync_harder/,
  https://forrestthewoods.com/the-tech-of-planetary-annihilation-chronocam/
- Factorio Friday Facts 47, 215, 415, 421. https://factorio.com/blog/post/fff-215,
  https://factorio.com/blog/post/fff-421, https://direct.factorio.com/blog/post/fff-415
- OpenRA `World.cs`. https://raw.githubusercontent.com/OpenRA/OpenRA/bleed/OpenRA.Game/World.cs
- Spring/Recoil sync system and sync debugging (wiki, via search; pages blocked fetching).
  https://springrts.com/wiki/Debugging_sync_errors
- Timothy Ford: "Overwatch Gameplay Architecture and Netcode", GDC 2017 (details via a
  translation). https://gdcvault.com/play/1024001
- Unity Entities 1.3 manual: archetypes, structural changes, entity command buffer playback.
  https://docs.unity3d.com/Packages/com.unity.entities@1.3/manual/systems-entity-command-buffer-playback.html
- flecs manuals: queries, relationships, component traits, prefabs, systems, Flecs Script.
  https://www.flecs.dev/flecs/Queries.html, https://www.flecs.dev/flecs/PrefabsManual.html,
  https://www.flecs.dev/flecs/Systems.html
- Sander Mertens: ECS FAQ. https://github.com/SanderMertens/ecs-faq
- Bevy ECS documentation (storage, scheduling, ambiguity detection).
  https://docs.rs/bevy_ecs/latest/bevy_ecs/,
  https://docs.rs/bevy_ecs/latest/bevy_ecs/schedule/struct.ScheduleBuildSettings.html
- skypjack: "ECS back and forth" parts 2 and 3, EnTT wiki.
  https://skypjack.github.io/2019-03-07-ecs-baf-part-2/, https://github.com/skypjack/entt/wiki/Entity-Component-System
- Photon Quantum introduction. https://doc.photonengine.com/quantum/v2/quantum-intro
- Our Machinery: "Syncing a data-oriented ECS", "Fiber-based job system".
  https://ruby0x1.github.io/machinery_blog_archive/post/syncing-a-data-oriented-ecs/index.html,
  https://ruby0x1.github.io/machinery_blog_archive/post/fiber-based-job-system
- Adam Martin: "Entity Systems are the future of MMOG development", parts 1 and 2.
  https://t-machine.org/index.php/2007/09/03/entity-systems-are-the-future-of-mmog-development-part-1/
- Dennis Gustafsson: "Thoughts on ECS" (2025). https://blog.voxagon.se/2025/03/28/thoughts-on-ecs.html
- Christian Gyrling: "Parallelizing the Naughty Dog Engine Using Fibers", GDC 2015.
  https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine
- Barry Genova: "Multithreading the Entire Destiny Engine", GDC 2015.
  https://www.gdcvault.com/play/1022164/Multithreading
- Johan Andersson: "Parallel Futures of a Game Engine v2.0" (2010).
  https://www.slideshare.net/slideshow/parallel-futures-of-a-game-engine-v20/4345460
- Stefan Reinalter: "Job System 2.0: Lock-Free Work Stealing", parts 1-5 (2015-16).
  https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/
- enkiTS. https://github.com/dougbinks/enkiTS
- oneTBB: `parallel_deterministic_reduce`, work isolation.
  https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_deterministic_reduce_func
- Taskflow, Marl, FiberTaskingLib. https://taskflow.github.io/taskflow/index.html,
  https://github.com/google/marl, https://github.com/RichieSams/FiberTaskingLib
- Glenn Fiedler: "Floating Point Determinism", "Deterministic Lockstep".
  https://gafferongames.com/post/floating_point_determinism/
- Bruce Dawson: "Floating-Point Determinism" (2013).
  https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/
- Singe source, `E:\Projects\Singe\engine\core\src\ecs` and `\dsa` (read 2026-11-14).

Second-hand or unverified (marked so in the notes): Overwatch details (translation), Naughty Dog's
thread and fiber counts (slide transcription), Recoil's multithreaded pathing, SupCom's
single-threaded simulation, StarCraft II's arithmetic.
