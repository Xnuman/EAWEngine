# Entities, components, and jobs: research and design

The agreed design of the simulation's storage, its tick, and the engine's parallel work (roadmap
work order 2, items c to e), with the steps that lead there without changing a battle's outcome
by accident. The owner agreed it; the decisions are in section 9. The research behind section 4,
with its sources, is in [ecs-research.md](ecs-research.md). What is built is in
[status.md](status.md); section 8 says, step by step, what is done and what remains.

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

The simulation used to keep every entity in one flat table of 67 per-entity arrays. Measured on
2026-11-14 on that table (Release, Utapau, headless `battle`):

| Battle | Entities at the end | Average tick | Slowest tick |
| --- | --- | --- | --- |
| 20 + 20 infantry squads, 20 T2-B, 10 AT-ST | 1,189 | 0.70 ms | 8.6 ms (tick 2) |
| 80 + 80 squads, 60 T2-B, 40 AT-ST | 3,296 | 4.66 ms | 51.5 ms (tick 2: combat 38 ms, route searches 8 ms) |

The tick is 60 Hz (16.6 ms). At 2.7 times the entities the average tick took 6.6 times as long,
and the opening tick missed a frame at 3,300 entities. The per-layer timings (step 0) showed why:
separation took 3.4 of an average tick's 4.5 ms (1.9 of 3.05 ms once its candidates were filtered
before sorting), and the opening tick is mostly combat, not route searches.

Speed was not the main reason. The structural ones, and where the design answers them:

1. **One table for everything.** Every entity had every field: a building had a walk plan, a turn
   plan, a flight roll, formation slots, and a crush profile, and optional behavior was switched on
   by values ("zero `turn_begin_seconds` plays no turn clips"), which made `MovementProfile` a bag
   of modes. Answered by pooled components (6.2, step 3).
2. **Bookkeeping by hand.** A new field touched four places (spawn, swap-remove, pop, hash);
   forgetting one was a silent desynchronization. Answered by column groups (step 2).
3. **Systems as member functions over all arrays.** Nothing stated which data a step read or
   wrote, so nothing could check that two steps may run side by side. Answered by typed views
   (6.3, step 4).
4. **Two ways to build an entity.** Runtime spawns and placed objects each copied a type into the
   fields of `EntitySpawn`. Answered by recipes (6.5, step 5).
5. **Squads need composition**: an entity made of entities (6.7).

## 2. Requirements

From AGENTS.md, the roadmap, and what the simulation does:

- **Determinism.** Same commands, same result, bit for bit, on one build, whatever the number of
  worker threads; the state hash, replays, and `--check-determinism` stay valid. Iteration order,
  floating-point order of operations, random streams, and merge order are compatibility state.
- **Hash-neutral migration.** Each migration step keeps `check-battle.ps1`'s hash; a deliberate
  change of the hash's layout is declared as such.
- **Layers.** The simulation knows nothing of XML, files, rendering, or Lua. The battle layer turns
  XML types into what the simulation needs; presentation reads immutable snapshots and never feeds
  back; scripts read snapshots and send validated commands.
- **No exceptions, no hidden globals.** The job system is created and passed (decision 5).
- **Data-driven types.** An XML type becomes a recipe once; spawning instantiates it.
  Modder-defined components get room, not an implementation (the owner's requirement).
- **Scale without caps.** Thousands of entities in a battle, more in space with fighters and
  debris; no fixed limit on entities or component types.
- **Small, testable systems.** A system can be tested with a handful of entities and only the data
  it reads.
- **Debuggable.** Data stays inspectable in a debugger; no reflection layer or template maze on the
  hot path.

## 3. What we have, sorted into candidate components

The entity table's column groups (`SimulationWorld`, `simulation/simulation.hpp`) and where each
lives:

| Component | Column group | Storage | Who has it |
| --- | --- | --- | --- |
| Identity | `IdentityColumns` (id, recipe, owner, look, selectable) | core | every entity |
| Placement | `PlacementColumns` (position, heading, pitch, radius) | core | every entity |
| Motion | `MotionColumns` (speed in the last tick, the tick's start positions) | core | every entity |
| Locomotor | `LocomotorColumns` (speeds, turn rate, movement profile pointer, limits) | core | every entity |
| Ground mover | `GroundMoverColumns` | core | every entity |
| Route | `RouteColumns` (targets, paths, remaining route, detours) | core | every entity |
| Orders | `OrderColumns` (command serials, standing orders) | core | every entity |
| Steering | `SteeringColumns` (the tick's intents) | core | every entity |
| Turn clips | `TurnClipColumns` | pool | walkers with turn clips |
| Walk clips | `WalkClipColumns` | pool | walkers with walk transition clips |
| Flight | `FlightColumns` (minimum speed, cruise height, yaw rate, half-loop, roll) | pool | flyers |
| Footprint | `FootprintColumns` | pool | immobile obstacles |
| Crush | `CrushColumns` | pool | crushers and crushables |
| Formation team | `FormationColumns` | pool | entities spawned with a non-default team |
| Group cohesion | `GroupColumns` | pool | members of a moving formation group |
| Combat | `CombatColumns` (one `CombatState`: health, shield, target, weapons, hardpoints, stance) | pool | entities whose recipe has health, a shield, weapons, or hardpoints |
| Squad | `SquadColumns` | pool | team entities |
| Squad member | `SquadMemberColumns` | pool | members of a team |

World resources, one per world, each a struct with its `SimData` id: `Clock`, `GroundState`,
`PathQueue`, `FormationFrames`, `ProjectileState`, `TickEvents`, `SlotTable`, `Rules` (the damage
table and hostility), `Avoidance`, `CombatCounters`, `DamageQueue`, `CombatRandom`, and the
recipes.

Of the 3,296 entities above, most are props; on Utapau 25 of 770 placed objects are combat
members. Type data (combat and movement profiles, clip times, crush profile, formation order and
spacing) is the recipe's, stored once (6.2).

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
- **Plain struct-of-arrays per entity kind.** What critics recommend when runtime composition is
  not needed (Gustafsson 2025: "will probably take you all the way to the finish line"; the ECS
  FAQ: ECS is poor at problems that need specialized structures, such as spatial grids and
  pathfinding).
- **Prefabs.** flecs alone has native data-driven templates with inheritance (IsA, variants, and a
  per-component choice to copy into each instance or share from the prefab). EaW's XML types
  already behave so (`Variant_Of_Existing_Type`), which `TypeFields` resolves.
- **Change tracking, observers.** Coarse everywhere (per table or chunk, "did a writer run", not
  "did data change"); observers mostly fire at sync points. Our Machinery found add/remove
  callbacks problematic.

### 4.2 Determinism in shipped games

- **Lockstep with a hashed state is the RTS norm** (Age of Empires, Supreme Commander,
  Spring/Recoil, OpenRA, Factorio, Stormgate): commands are shared, never state; the simulation
  runs at a fixed low rate (SupCom 10 Hz, AoE turns of 200 ms) and presentation interpolates and
  never writes back (SupCom's User layer, Spring's unsynced code). Planetary Annihilation is the
  counterexample (server-authoritative replication).
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
- **Deterministic parallel work.** oneTBB's `parallel_deterministic_reduce` makes the same splits
  and joins whatever the thread count; enkiTS: "the thread index should not be used for changing
  processing of data", only for per-thread output buckets; Rayon (contrast): reduction order
  unspecified, so float sums are not reproducible. The rule that follows: chunk boundaries depend
  on the problem size and a constant grain only, each chunk writes its own output slot, and chunk
  results merge in chunk order at a barrier.
- **Libraries.** enkiTS: small, C and C++, no exceptions, priorities, pinned tasks, profiler
  callbacks. Taskflow: C++20 task graphs but built on exceptions (conflicts with `/EHs-c-`).
  oneTBB: large external dependency. Marl: fibers.

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
checker need), ordered phases, setup when a component is added. Not as is: the 64-type limit
(mods), global component and job registries, `dynamic_cast` and hash maps on the hot path, UUIDs
in place of the index and generation our commands and snapshots use, a reflection library we do
not have, and no determinism rules.

## 5. Options for storage

| | A. Status quo, tidied | B. Dense core + sparse pools (chosen) | C. Archetypes | D. EnTT |
| --- | --- | --- | --- | --- |
| What | one table, fields grouped into structs, bookkeeping generated | every entity in one dense core table; optional components in sparse pools | tables per component set (Singe, flecs, Unity) | the library's sparse sets and groups |
| Optional data | carried by all | only where present | only where present | only where present |
| Add/remove cost | n/a (flags) | O(1), one pool | row move of all components | O(1) |
| Iteration | fastest | core: dense; pools: packed | fastest per table | fast for one pool or a group |
| Order | as the table | core order drives every query unless a system opts out | history of moves; needs explicit sorting for a canonical order | smallest pool unless told; sortable |
| Hash-neutral migration | yes | yes (absent components hash their defaults until the declared change) | harder: the row order changes as soon as tables split | as B, plus learning its order rules |
| New code | small | about 1,000 lines with tests | about 2,000 lines | none, but a dependency |
| Fits squads, abilities, deploy states | poorly | yes (cheap add/remove) | yes, with row moves | yes |

Option **B** was chosen (decision 1). Reasons:

1. Our scale is thousands, not hundreds of thousands. The archetype advantage (contiguous rows for
   every query) matters little at this size; Factorio's experience says the memory traffic of
   shared writes, not iteration layout, decides at our scale.
2. Component churn is coming: squads joining and losing members, deploy states (AT-AA, SPMAT,
   AT-AT troop drop), abilities with durations, shields switched on and off, garrisons. Sparse
   pools take these in O(1) without moving other data.
3. One order for everything is easy to reason about and to keep deterministic: loops walk the
   dense core in its order and look up pools by slot in O(1). A system whose work is purely per
   entity may walk a pool's packed order instead (an optimization, opted into).
4. The migration from flat arrays is mechanical, and each step can keep the hash.
5. It needs no dependency. EnTT would save the pool code but bring its own order rules (smallest
   pool first, swap-and-pop), which we would have to wrap anyway.

## 6. Proposed design: entities, components, systems

### 6.1 Entities

- `EntityId{index, generation}` is the handle commands, snapshots, scripts, and presentation use.
- The dense core keeps every entity in one order. Removal compacts at the end of the tick and
  preserves order (`SimulationWorld::remove_entities`), so the dense order is the spawn order.
- Planned, not built: a monotonic 64-bit **spawn serial** per entity, the canonical order (like
  OpenRA's actor id), the key for deterministic merges, and the identity that survives a save and
  load. It comes with its first reader (a merge keyed by it, or save and load).

### 6.2 Components

- A component is a group of columns, one array per field, not an array of structs (the owner's
  decision 9: hot loops stream single fields, and the broad-phase grid takes the positions as one
  span): `PlacementColumns` holds `positions`, `headings`, `pitches`, and `radii`. Its elements are
  plain data: no virtual functions, no ownership of other entities' data, default-constructible,
  trivially copyable where possible (vectors allowed where a component holds a list: a route, a
  squad's members). Each group lists its columns in `for_each_column`, the table its groups in
  `for_each_group`; the constructor checks both lists against the sizes of what they list
  (`check_entity_table`).
- **Core** (every simulated entity, in the dense table): Identity, Placement, Motion, Locomotor,
  GroundMover, Route, Orders, Steering (section 3). The design listed Locomotor, GroundMover,
  Route, and Orders as pools; they are still core columns (unverified whether pooling them would
  pay; nothing has asked for it).
- **Pooled** (only where present; `ComponentPool<Columns>`, `simulation/component_pool.hpp`: the
  column group packed for its members, a sparse row index by slot, swap-removal within the pool):
  TurnClips, WalkClips, Flight, Footprint, Crush, Formation, Group, Combat, Squad, SquadMember.
  Shield, weapons, and hardpoints are part of `CombatState`, not pools of their own. Later:
  Deployable and ability states.
- **Type data apart from state.** A recipe's immutable profiles (combat profile, movement profile,
  clip times, crush profile, formation order and spacing) are stored once per recipe; an entity
  points at them (`CombatState::profile`, `LocomotorColumns::movements`) and holds its state only
  (flecs' "inherit" choice; decision 6). Cheap scalars (speeds, radius, turn rate) stay per-entity
  columns copied at spawn. A walker has a `WalkClips` component whose profile holds the clip
  times, and a building simply lacks it. Temporary per-entity changes of type data (abilities,
  veterancy) will need override columns when they come.
- **World resources** for what exists once (section 3), each a struct with a `SimData` id, so every
  kind of data a system uses has a type of its own. Side effects are data in **event queues**,
  written by the systems that cause them and resolved by one system in a defined order
  (Overwatch's deferment): `DamageQueue` holds the tick's hits, misses with their blasts, and
  crushes.
- **A registry** (planned, not built): each component type with a run-time id (no fixed maximum),
  its name, its storage (core or pool), its hash function, and its snapshot hook. Built when the
  world is built, no globals. Nothing reads it yet; it comes with modder-defined components (6.8).

### 6.3 Queries and systems

- A **system** is an object with a typed view of the world's data (`simulation/system_view.hpp`,
  Singe's access modifiers adapted to column groups): it declares its access as the type of its
  view, `SystemView<Read<GroundState>, Write<CombatColumns>, ...>`, and reaches data only through
  `view.get<T>()`: a const reference for a `Read`, a mutable one for a `Write`, a compile error for
  anything undeclared or for a write through a `Read`. The schedule entry takes its read and write
  masks from the view's type (`reads_of`, `writes_of`), so the declaration cannot drift from the
  code. The compile-time check also covers branches a test run never takes.
- A system's own scratch and counters (query scratch, target grid, pathfinders) are members of the
  system object. A helper several systems use is a static template over its data
  (`template <class Data> travel_speed(Data& data, ...)`, in `simulation_internal.hpp`), reading
  through `data.get<T>()`: a system passes its view (the compiler holds the helper to the view's
  declaration), a member-function system passes the world (`SimulationWorld::get<T>()`, full
  access), and there is one copy.
- **Loops** walk the dense core order and look pools up by slot. Separate query helpers were tried
  and dropped: the loops already do this, and sorting a pool's members cost more than it saved
  (crushing, 0.006 to 0.025 ms a tick).
- **The schedule** is one explicit table, `SimulationWorld::schedule_`
  (`simulation_schedule.cpp`), read and reviewed in one place. Its phases:

  1. Commands (apply the tick's commands in submission order; spawns and script destroys happen
     here, the start barrier)
  2. Behavior (orders, stances, targets into steering intents and route requests)
  3. Navigation (route requests, group cohesion, avoidance)
  4. Movement (walks and turns played as clips, ground movement, flight)
  5. Contact (crushing, separation, measured speeds)
  6. Combat (shields, targeting, turrets, weapons, projectiles; damage queued)
  7. Resolution (apply the queued damage, deaths, hardpoint losses, in a fixed order)
  8. Structural changes (removal of the dead in one stable pass)
  9. Publish (the snapshot, the tick counter)

  The table has nine systems in eight tick layers (`TickPhase`): removal (phase 8) is timed under
  the resolution layer. Six systems are on typed views (behavior, navigation, movement, contact,
  combat, publish); commands, resolution, and removal are member functions with full access, since
  they spawn, remove, or end spins and orders, and declare that they write everything.
- Within a phase, systems run in their listed order. A phase may mark systems independent
  (disjoint writes) so they run in parallel, with the schedule checking the claim at start-up from
  the declarations (Bevy's ambiguity rule, as an error). Not built: no phase has two systems that
  could run side by side; parallel work runs inside systems (7.5).
- Timings are recorded every tick per tick layer (the battle statistics).

### 6.4 Structural changes

- During a tick, systems never add or remove entities or components directly; they queue them in
  a command buffer with a sort key (system, chunk, sequence), applied at the structural-change
  barrier in key order (Unity's sorted command buffers; OpenRA's end-of-tick tasks). New entities
  take their slots there, deterministically.
- Built so far: spawns are applied by the commands phase at the start of the tick; the dead leave
  at the end in one stable pass (`remove_entities`), and a script's destroy command uses the same
  pass at once. Not yet: a sorted command buffer for changes made by systems during the tick.
  Group cohesion joins and leaves its pool within the tick (`leave_group`), so its loops look rows
  up per entity, since a leaving member's row is refilled from the pool's last.

### 6.5 Recipes from XML

- The battle layer reads each XML type once into a `TypeRecipe` (`battle/type_recipe.hpp`,
  `build_type_recipe`): what the type gives every entity of it, from its fields (`TypeFields`,
  which resolves `Variant_Of_Existing_Type`) and its resources (clips, model bounds). The readers
  are free functions (`type_movement`, `type_crush`, `apply_animation_locomotion`,
  `combat_profile`, ...).
- `make_spawn(recipe, instance)` is the one construction path for runtime spawns and placed objects
  (`Battle::spawn_of` and the map builder): it returns the simulation's `EntityRecipe` and the
  instance's values (`PreparedSpawn`). A type behaves the same however it appears (the owner's
  rule): a building spawned at runtime blocks the ground like a placed one, an immobile object
  spawned in space keeps ships clear by its radius, and following the ground needs a valid ground
  on both paths. Placed objects take a recipe per type and model from a cache, so a type's
  warnings appear once.
- The simulation sees recipes as plain data; it never sees XML. `EntitySpawn` is a `RecipeId` plus
  the instance's values (owner, position, heading, look, selectability, formation team, footprint,
  stance). `SimulationWorld::add_recipe` validates a recipe once and keeps equal recipes as one;
  entities of one type that must differ (a company member taking its team's speed, a placement
  scaled differently) get a variant recipe of their own, ids in registration order. Recipes live
  in an append-only deque, never changed during a battle.
- Planned: a **reader registry**, a reader per component from an XML type to the component's
  profile or nothing, for modder-defined components (6.8).

### 6.6 Snapshots, scripts, presentation

- The publish system builds `SimSnapshot` as its own output (`SimulationWorld::snapshot` reads it),
  so presentation, scripts, and the sandbox did not change with the migration. Later, snapshot
  fields can follow components (a snapshot entry carries a walk clip only if the entity has
  `WalkClips`).
- Scripts read snapshots and send commands (Factorio and Spring both name script ordering as a
  determinism hazard; ours is serialized at defined points).

### 6.7 Squads (for work order 2f)

- A squad is a team entity with a `Squad` component (its members, its arrangement, its order);
  each member has a `SquadMember` component pointing back. Orders go to the team; a squad system
  turns them into member intents. flecs' relationships do this generically; at our scale two
  components and one system are enough.
- Built (`simulation_squads.cpp`; the behavior is specified in
  [pathfinding-design.md](pathfinding-design.md), 4.3): the team takes its squad's orders and
  plans its route, has no body, and leaves with its last member.

### 6.8 Room for modder-defined components (not designed further)

- Component ids are assigned at run time, so a mod could register more without a limit.
- A future data-only component would be a named schema (fields of number, flag, text, entity
  reference) stored as a byte pool, read and written by Lua through commands, with no native
  system; a mod's manifest would map its XML tags to it through the reader registry.

### 6.9 Determinism rules (in AGENTS.md, "Simulation and concurrency principles")

1. One tick order: the schedule's list; systems in a phase run in order unless declared independent
   and checked.
2. One entity order: the dense core order (the spawn order); no system iterates a hash container.
3. Structural changes only at the barrier, in sort-key order.
4. Side effects as data in event queues, resolved by one system. Built for damage (`DamageQueue`):
   a shot whose victim died earlier in the tick strikes the body without damage (the owner's rule:
   two units firing at once both hit); deaths are handled in dense order, each entity once per
   tick, in rounds until a round kills nothing, so a death blast kills in the tick it goes off and
   so do the blasts it sets off.
5. Parallel work: chunk boundaries from the item count and a constant grain, never the thread
   count; each chunk writes only its own output; outputs merge in chunk order; atomics only for
   scheduling (section 7).
6. Random numbers from per-purpose streams keyed by entity and tick, never a shared generator in
   parallel code; presentation's randomness never touches them; their state is hashed. Open: combat
   draws from one stream (`CombatRandom`), so weapons fire serially in entity order.
7. Floating point: `/fp:precise` (the compiler's default; no flag is set), never `/fp:fast`, no FMA
   contraction (to verify for x64 builds). Before multiplayer (M6), engine-owned `sin`, `cos`,
   `atan2` for the simulation, because the C runtime's may differ between CPUs (`src/simulation`
   has 94 calls of `std::sin`, `std::cos`, and `std::atan2`).
8. Tooling: sub-hashes by state subject (`--hash-trace`, with `--check-determinism`) narrow a
   mismatch to the subject and tick that diverged (AoE, SupCom, Spring did so per system);
   `check-battle.ps1` runs each battle on the job system and serially and requires the same hash.

## 7. Proposed design: the job system

The owner's decision: fibers, and no OS mutexes in the job system. The thread-pool alternative is
in 7.6. The library is `eaw_jobs` (`src/jobs`: `jobs/job_system.hpp`, `jobs/parallel.hpp`).

### 7.1 Shape

- `eaw_jobs` depends on `eaw_core` only and uses Windows fibers (`ConvertThreadToFiberEx`,
  `CreateFiberEx`, `SwitchToFiber`).
- A `JobSystem` object is created by the executable and passed by reference (no global): N-1
  worker threads (`JobSystemOptions::workers`, default the hardware threads less one; 0 runs
  everything on the calling thread), each turned into a fiber, plus the calling thread. A fixed
  pool of fibers is created up front (`fibers`, default 128; `fiber_stack_bytes`, default 512 KiB
  reserved, 64 KiB committed, with a guard page so an overflow is a crash report, not corruption).
- A job is a plain record (function, argument, index) copied into bounded lock-free queues, one
  per priority (Vyukov's bounded multi-producer, multi-consumer ring: one atomic sequence per cell,
  no ABA; `queue_capacity`, default 4,096). A full queue runs the remaining jobs on the calling
  fiber, so nothing is dropped or overwritten.
- **Counters** are the only synchronization (Naughty Dog, FiberTaskingLib, Our Machinery):
  `run(jobs, priority, counter)` adds the jobs to the counter and each finished job takes one off;
  `wait(counter)` returns at once if the counter is zero, and otherwise parks the calling fiber on
  the counter and switches the thread to a pool fiber that goes on running jobs. The job that
  brings the counter to zero moves the parked fibers to the ready queue, and some worker resumes
  them. A counter tracks jobs still touching it after their decrement, so it may be destroyed as
  soon as `wait` returns.
- **No OS mutexes, condition variables, or events in the job system.** Atomics and lock-free queues
  only. Idle workers spin briefly (`_mm_pause`), then sleep on an atomic epoch with
  `std::atomic::wait`, woken when work arrives (on Windows `WaitOnAddress`: a wait on a memory
  address, not a mutex).
- No exceptions: a job writes its results, a `Status` included, into its own output; the waiter
  checks them. Unrecoverable failures stay `fatal_error`.

### 7.2 The hard parts, and how they are handled

1. **Parking a fiber that is still running.** A fiber cannot put itself on a wait list and then
   switch away: another thread could resume it before the switch completes. Every switch therefore
   goes through one helper that hands the "after the switch" work to the fiber it switches to
   (park the previous fiber on its counter, or return the previous scheduling fiber to the pool),
   done first thing on the other side; the parked fiber's counter is checked again after it is
   registered, so a counter that finished in between resumes it at once. (FiberTaskingLib's
   design.)
2. **The main thread.** Window, Direct3D, and audio calls must stay on the thread that made the
   window. Fibers that must stay on a thread (the creating thread's own fiber) are pinned: when
   ready they go to that thread's own ready queue, which only that thread takes from.
3. **Thread-local storage.** A fiber may resume on another thread, so a function that waits must
   not keep a thread-local address across the wait. The rule is in AGENTS.md, and the build adds
   MSVC's `/GT` (fiber-safe thread-local storage) so the compiler does not cache such addresses.
4. **Locks outside the job system.** The log, the asset cache, the fatal-error path, and the frame
   watchdog use OS locks; a job may take them only for a short section that never waits on a
   counter (a fiber holding a lock and parked on another thread could deadlock). AGENTS.md: never
   wait while holding a lock.
5. **Debugging.** A parked fiber's call stack is not visible as a thread in the debugger. The
   serial mode (0 workers) is for stepping through; pool fibers and workers carry names in the
   profiler (`EAW_PROFILE_FIBER_ENTER`). Planned, not built: each fiber recording the job it runs
   and where it waits, readable in a debugger watch and printed by the crash handler.
6. **Fixed resources.** Fibers and waiter slots per counter (`max_counter_waiters`, 4) are
   preallocated and checked: running out is a fatal error naming the limit, never a silent drop
   (AGENTS.md: temporary capacities explicit, checked, diagnosed).

### 7.3 Primitives

1. `run(jobs, priority, counter)` and `wait(counter)`.
2. `parallel_for(jobs, count, grain, function(begin, end, chunk), priority)`: chunk k covers
   `[k * grain, min((k + 1) * grain, count))`, fixed by the count and a constant grain, never the
   thread count. One chunk runs on the caller; otherwise a helper job per worker (at most one per
   chunk beyond the caller's first, and at most `max_parallel_helpers`, 64) and the caller take
   chunks in turn from a shared index, and the caller waits for the helpers still finishing. (A job
   per chunk cost a queue operation and a wake-up per chunk and left the caller parked: the
   `jobs-loops` benchmark.) The workhorse for simulation systems.
3. Per-chunk outputs and a merge in chunk order, the deterministic reduction. The callers keep
   their own per-chunk outputs (scratch per chunk, an output per item); a shared `ChunkOutputs<T>`
   helper is not built.
4. Priorities (`JobPriority`): simulation tick, frame (presentation), background (asset decoding,
   content builds, hot-reload checks). Background work is to be split into short jobs, so a long
   decode does not hold a worker for a whole frame; the sandbox's `std::async` calls are to move
   onto it (not yet: step 1).
5. Profiling: Tracy zones per job and fiber in profiling builds (`core/profile.hpp`), and the
   battle statistics' per-layer timings.

### 7.4 Determinism

The fiber machinery decides only when and where a job runs. Results stay independent of it by the
rules of 6.9: chunk boundaries from the count and grain, outputs per chunk, merges in chunk order,
atomics for scheduling only. The `jobs` test module runs `parallel_for` workloads with 0, 1, 2,
and 7 workers and requires identical outputs (a float sum merged in chunk order, bit for bit);
`check-battle.ps1` and `--check-determinism` compare a battle on the job system with a serial run.

### 7.5 First uses, by measured need

Each use is measured before and after (Factorio found shared writes make parallel updates slower).
The simulation takes the job system through `SimulationWorld::set_job_system` (null: serial).

1. **Ground avoidance** (ORCA, `simulation_avoidance.cpp`): per steering unit, from the state at
   the start of the tick, chunks of 4 with scratch per chunk. Built; measured on the 500-unit
   benchmark: 0.43 ms on 7 workers against 1.48 ms serially.
2. **Route searches** (`simulation_navigation.cpp`): the pathfinders' regions and chunk graphs are
   shared `NavigationData`, frozen while searches run; requests are planned eight at a time and
   given in request order under the unchanged budget. Built; a large order's worst search tick
   10-12 ms against 18 ms serially. Route searches cost 0.16 ms an average tick (budget-limited),
   so they matter for the spikes only.
3. **Combat target choice and turrets** (`simulation_combat.cpp`): read-only over the others'
   state (damage waits in its queue), each chunk with its own query scratch. Built; combat 0.82 ms
   a tick against 1.2 ms, its opening spike 2.3 ms against 6.2 ms. Weapons stay serial on the one
   combat random stream (6.9 rule 6).
4. **Presentation**: pose sampling on a cache miss (`UnitAnimator`, `JobPriority::frame`). Built;
   256 new 40-bone poses 2.3 ms on 7 workers against 4.7 ms serially (`EAWBenchmarks poses`).
   Particles per system and frame building: open.
5. Next by measurement (500-unit benchmark): movement (1.6 ms), contact (0.6 ms); separation and
   contact are read-only neighbor queries with pushes per chunk, summed in chunk order. Fewer
   target searches come first (the roadmap's work order 2g).

### 7.6 The alternative not taken

A thread pool with locked queues, help-while-waiting, and no fibers: simpler to debug, no
thread-local or pinning concerns, but a waiting job either blocks its worker or runs unrelated
work on top of its own stack (oneTBB's work-isolation hazard). Fibers avoid both at the cost of
7.2.

## 8. Migration, step by step

Each step keeps `check-battle.ps1`'s hash, the test modules, and the captures, except where a hash
change is declared (steps 6 and 7, and the later pools of step 3). State per step, checked against
`src/simulation` and `src/jobs`:

0. **Tooling.** Per-system timings in the battle statistics; per-system sub-hashes behind a flag.
   **Done**, in this form: timings per tick layer, and `--hash-trace` hashing by state subject.
1. **The job library** (work order d) with its tests (determinism of `parallel_for` across worker
   counts; preallocation limits) and its first clients. **Partly done.** Built: `eaw_jobs`, the
   `jobs` tests, `EAWBenchmarks`, presentation's pose sampling. Remains: the background lane in
   place of `std::async` (still used in `engine_app/sandbox_app.cpp`, `sandbox_session_content.cpp`,
   `sandbox_session_spawning.cpp`, and `dev_launcher_app.cpp`; content builds must first be split
   into short jobs, 7.3).
2. **Components as column groups inside the one table** (section 6.2). **Done**: the core table's
   column groups, removal through `for_each_column`, spawn's size check (every column grew by one;
   spawn still sets each column's value itself), and `check_entity_table`.
3. **The registry and pools.** Optional components into pools (`ComponentPool`). **Partly done.**
   Every component but the core's is a pool (section 3). Remains: the registry (6.2), which waits
   for modder-defined components. The rule for pooling while the hash was still field by field (the
   owner's choice): an absent component could stand in for its old values only where every
   non-member had the same constant ones; the pools that broke that rule (flight, formation, group
   cohesion, combat) came after step 7 as declared hash changes of their own, with the battles'
   outcomes unchanged. `flies(data, index)` (pool membership) is the flyer test;
   `combat_state(data, index)` reads a member's state or `no_combat()`, `combat_of` gives a
   member's to change (null for others), and scripted conditions, damage, and stances reach members
   only. Combat's pool lookups were brought level with the core table by one lookup per candidate,
   after the cheaper hostility test (A/B on one machine, 12.0 against 12.2 ms for a 500-unit
   tick).
4. **Systems with declared access** (section 6.3). **Done** in the typed-view form: six of the nine
   systems on typed views, the schedule's masks from their types; commands, resolution, and removal
   with full access. Dropped: queries as a separate helper. Not built: the start-up check of
   independent systems (nothing to check until a phase has two). The `DebugValidation`
   configuration (`build.ps1 -Configuration DebugValidation`, files with `_dv`) stays, empty, for
   later checks too slow for Debug.
5. **Recipes** (section 6.5). **Done**: `TypeRecipe` and `make_spawn` in the battle layer,
   `EntityRecipe` and `add_recipe` in the simulation, profiles referred to, not copied. Remains:
   the reader registry, with modder-defined components. `test_type_recipe` covers the footprint,
   the space radius, and the radius through a placement's scale.
6. **Command buffers and event queues** (6.4, 6.9 rule 4). **Partly done.** Built: the damage
   queue and its resolution system; removal at the end of the tick in one stable pass. Remains: a
   sorted command buffer for structural changes made by systems during the tick (spawns are still
   applied by the commands phase, and group cohesion changes its pool within the tick).
   `test_damage_resolution` covers the shot at a body, a chain of death blasts, and the survivors'
   order.
7. **The declared change.** **Done**: spawn order as the dense order (compacting removal), the
   hash per component in dense order, pooled components only for their members, recipe type names
   and profiles once per recipe. The battle hashes were rebaselined once (decision 3).
8. **Parallel systems** (7.5). **Partly done.** Built: ground avoidance, route searches, combat
   target choice and turrets, each verified against a serial run. Remains: movement, contact,
   weapons (after keyed random streams, 6.9 rule 6).
9. **Refactoring.** A review of what the migration left, its scope and plan to be agreed with the
   owner first. **Open.**

Squads (work order f) build on pools; see 6.7.

## 9. Decisions

Agreed by the owner:

1. **Storage:** a dense core table plus sparse component pools (section 5, option B), no
   archetypes, no third-party ECS.
2. **Job system:** our own, on fibers, without OS mutexes (section 7).
3. **Canonical order:** spawn order with compacting removal at migration step 7, the battle hash
   rebaselined once.
4. **Cross-machine determinism:** floats under controlled settings; the simulation's own
   trigonometry before multiplayer (M6); no fixed point.
5. **The job system's ownership:** created by the executable and passed explicitly, no global.
6. **Type data:** recipe profiles stored once and referred to, not copied into each entity.
7. **Singe:** its ideas (access modifiers, phases), not its code.
8. **Where it lives:** this document; the rules of 6.9 and 7.2 in AGENTS.md; the steps of section
   8 in the roadmap. `/GT` (fiber-safe thread-local storage) in the global compile options.
9. **Component layout:** a component is a group of columns, one array per field, not an array of
   structs (section 6.2). Kept for now; the more usual struct-per-entity style may replace it
   later.

## 10. What this design deliberately leaves out

- A reflection library, runtime-defined native components, and observers or reactive systems:
  explicit systems and the reader registry do the same jobs at our scale.
- Automatic parallel scheduling from access sets: the schedule stays an explicit list; declared
  access is checked, not used to reorder.
- Change detection: no system needs it yet.
- Work stealing between per-worker queues: shared lock-free queues per priority first; stealing
  only if a profile asks (a job costs about 43 ns on one thread and 90-110 ns with workers sharing
  one queue).
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
  https://skypjack.github.io/2019-03-07-ecs-baf-part-2/,
  https://github.com/skypjack/entt/wiki/Entity-Component-System
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
- Singe source (the owner's engine, not public): its ECS (`core/src/ecs`) and data-structure
  (`dsa`) modules.

Second-hand or unverified (marked so in the notes): Overwatch details (translation), Naughty Dog's
thread and fiber counts (slide transcription), Recoil's multithreaded pathing, SupCom's
single-threaded simulation, StarCraft II's arithmetic.
