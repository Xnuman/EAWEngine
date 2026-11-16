# Research behind the ECS and job system design

Findings from engine documentation, talks, papers, and developer write-ups, collected for
[ecs-design.md](ecs-design.md) (its section 4 summarizes them). Markers: [S] a source states it,
[2nd] second hand (forums, translations, press), [Inferred] or [I] the research's own inference.

## ECS storage and scheduling designs

Not read: Mertens' Medium "Building an ECS" series (403), Acton's talk itself, the flecs meta
manual (503), EnTT's snapshot section.

### Archetype / table storage

- flecs (https://www.flecs.dev/flecs/Queries.html, Relationships.html, ComponentTraits.html,
  PrefabsManual.html, ObserversManual.html, Systems.html, FlecsScript.html;
  https://github.com/SanderMertens/ecs-faq): tables (components = columns, entities = rows);
  cached queries keep matched tables (cheap iteration, costly creation), uncached ones are
  stateless; no general row-order guarantee, explicit order by `order_by` (sorts tables, merges
  slices; expensive, resorts on writes) or `group_by`; change tracking per table and component, and
  in/out terms bump counters whether data changed or not; pairs (relationship, target) as one
  64-bit id, more tables (fragmentation; `DontFragment`), ChildOf deletes children; per-component
  traits: Sparse (stable pointers, cheap add/remove, slower queries), DontFragment, CanToggle
  (bitset instead of table moves), OnInstantiate (Copy, Inherit, DontInherit); prefabs and IsA
  (inherited components stored once, overrides copy and re-expose the prefab's value on removal,
  child trees instantiated, variants); observers (OnAdd/OnRemove on real changes, OnSet on set or
  modified, mostly at sync points); pipelines (fixed phases ordered by DependsOn; systems ordered
  by entity id "to ensure deterministic order", recycled ids can break it; declaration order
  within a phase; world read-only while systems run, commands per thread; sync points only where
  an [in] follows a possible [out]; multithreaded systems slice tables, the same entity on the same
  thread until the next sync point); Flecs Script declares entities, prefabs (`a : B {}` = IsA),
  and component types by name (values need reflection).
- Unity Entities 1.3 (https://docs.unity3d.com/Packages/com.unity.entities@1.3/manual/...):
  16 KiB chunks, packed arrays, removal moves the chunk's last entity into the gap, new entities
  take the first free index; structural changes (create, destroy, add, remove, set shared value)
  on the main thread only, forcing sync points; EntityCommandBuffer (temporary entities remapped on
  playback; ParallelWriter needs a sort key such as ChunkIndexInQuery for deterministic playback);
  component kinds (unmanaged, managed, shared (splits chunks by value), cleanup, tag, buffer
  (inline 128 bytes, then out of chunk), chunk, enableable (for frequent toggles), singleton);
  change filters per chunk, "whether a system with write access ran", not whether data changed;
  no documented query order (forum: order follows chunk history).
- Bevy 0.20 (https://docs.rs/bevy_ecs/latest/bevy_ecs/, ScheduleBuildSettings, Query, Commands,
  change_detection, Entity): tables by default, SparseSet per component (for frequently toggled
  markers); swap_remove; Entity = index + generation (bits not stable across releases; persist your
  own id component); automatic parallel execution from parameter access; conflicting queries in
  one system rejected unless disjoint (`Without`, `ParamSet`); ambiguity detection (conflicting
  access without order) and a shuffle seed for testing; Commands applied at ApplyDeferred;
  iteration order "not guaranteed"; change ticks with periodic clamping; reflection registry for
  data-driven use.

### Sparse sets: EnTT

(https://skypjack.github.io/2019-03-07-ecs-baf-part-2/, part-3,
https://github.com/skypjack/entt/wiki/Entity-Component-System, Runtime-reflection-system): sparse
+ packed arrays, O(1) membership and insertion, iteration without indirection; "only one order can
be imposed on an array" (easy sorting with independent pools, hard with archetypes); "play in the
same league" as archetypes; 32-bit id = index + version, implicit free list; swap-and-pop removal
(in_place_delete for stability); multi-type views iterate the smallest pool (order depends on pool
sizes unless `use<T>()`); full-owning groups pack owned pools at the front (fastest
multi-component iteration), partial and non-owning groups; in-place sorting by comparator or by
another pool; creating during iteration allowed, removing from the current entity allowed,
destroying others undefined; `entt::meta` reflection from real C++ types, no string lookups.

### Other designs and critiques

- Our Machinery
  (https://ruby0x1.github.io/machinery_blog_archive/post/syncing-a-data-oriented-ecs/index.html):
  component bitmask types stored together in big blocks; notifier callbacks on add/remove
  (problematic); external systems mirrored brute force with dirty flags.
- Adam Martin
  (https://t-machine.org/index.php/2007/09/03/entity-systems-are-the-future-of-mmog-development-part-1/,
  part 2): entities are names, components data, systems logic; close to a relational database.
- Mike Acton, CppCon 2014
  (https://isocpp.org/blog/2015/01/cppcon-2014-data-oriented-design-and-c-mike-acton): programs
  transform data; understand the data to understand the problem (secondary).
- ECS FAQ (https://github.com/SanderMertens/ecs-faq): archetype, sparse set, bitset, reactive
  designs; ECS poor at problems needing specialized structures (spatial grids, pathfinding).
- Dennis Gustafsson (https://blog.voxagon.se/2025/03/28/thoughts-on-ecs.html): components of a
  meaningful system scattered in memory, archetypes move data and invalidate pointers, hard to
  debug; static entity types built from component structs often suffice; ECS fits generic
  data-driven engines. Also https://infinitivecode.substack.com/p/skip-the-ecs,
  https://prog.world/you-may-not-need-ecs.

### Data-driven composition and modding

flecs alone has native prefabs with inheritance and runtime-declared components; Unity bakes
offline [Inferred]; EnTT needs real C++ types (a mod component would need a property-bag layer)
[Inferred]; Bevy reflects Rust types, dynamic components exist [Inferred]. [Inferred] EaW's XML
types already behave like prefabs with inheritance (`Variant_Of_Existing_Type`): a split between
values shared through the type and copied per instance, and a registry from XML tags to component
builders, both work without runtime-defined C++ types.

### Determinism

- Removal reorders storage in all three main designs (last-into-gap, swap_remove, swap-and-pop):
  iteration order depends on the add/remove history, reproducible for an identical command stream
  on one thread, not an order by id [Inferred].
- Guarantees are rare: Bevy none; flecs system order by entity id only; Unity ECB deterministic
  with schedule-independent sort keys; EnTT views by smallest pool unless told, explicit sorting.
- Deferred structural changes need a deterministic merge order (sort keys, stable per-thread
  slices) [Inferred for slicing].
- Hash in a canonical order (stable ids), not storage order, so worlds with equal state but
  different histories hash alike; include generations and the free list if id assignment must
  replay [Inferred].
- Bevy's ambiguity detection and order shuffling as tools to prove schedule determinism.

### Comparison

| | flecs | Unity Entities | Bevy | EnTT | Plain SoA |
|---|---|---|---|---|---|
| Storage | tables; per-component sparse | 16 KiB chunks | tables + sparse per component | sparse sets, groups | arrays per type |
| Add/remove | table move (sparse cheap) | structural change, main thread | table move (sparse cheap) | O(1) one pool | flags |
| Iteration | fastest on stable tables | fastest per chunk | fast tables | one pool or group fastest | fastest |
| Order | history; `order_by` | last-to-gap; ECB keys | not guaranteed | swap-and-pop; explicit sort | yours |
| Structural changes | deferred, auto sync points | ECB, sort keys | commands, ApplyDeferred | immediate | n/a |
| Scheduling | phases, threads slice tables | jobs, manual deps | automatic from access, ambiguity detection | none | yours |
| Prefabs | native IsA | baking | reflection, scenes | meta only | yours |
| Complexity | high | high, engine-bound | high, Rust | medium | low |

## Deterministic RTS simulations and ECS in deterministic games

### Shipped games

- Age of Empires ("1500 Archers on a 28.8",
  https://www.gamedeveloper.com/programming/1500-archers-on-a-28-8-network-programming-in-age-of-empires-and-beyond):
  [S] commands synced, not state, scheduled two turns ahead; world, objects, pathing, targeting
  checksummed (RTS3: per-system sync macros); seeded synced RNG with an exact number of calls,
  non-sim randomness kept out; recorded games deterministic, used as repros.
- Supreme Commander (Forrest Smith,
  https://www.gamedeveloper.com/design/opinion-synchronous-rts-engines-and-a-tale-of-desyncs,
  https://www.forrestthewoods.com/blog/synchronous_rts_engines_2_sync_harder/): [S] Sim layer
  10 Hz with everything important, User layer up to 60 fps interpolating; full state hashed every
  second; desyncs found by binary search over a printed memory hash; uninitialized variables and
  dangling pointers the usual causes; strict IEEE floats (`_controlfp` set and asserted every tick,
  via Gaffer). [2nd] sim single-threaded (FAForever wiki).
- Planetary Annihilation
  (https://forrestthewoods.com/the-tech-of-planetary-annihilation-chronocam/): [S]
  server-authoritative 10 Hz with keyframed curves, not lockstep (counterexample).
- StarCraft II: no primary source found (fixed point is [2nd], unverified).
- Factorio: [S] CRC against replay or server (FFF-47), "heuristic" coverage; FFF-215 parallel
  trains, power, and belts limited by cache invalidation, ordering of Lua calls and inserters an
  obstacle; FFF-421 "must remain fully deterministic", coupled networks in one update group,
  multithreading electric networks rejected (memory bound), global RNG removed for parallelism,
  biggest wins from not updating (sleeping entities, stored intentions, bucketed updates); FFF-415
  a threading determinism bug lived 2017-2024; float issues only from different sin
  implementations (dev AMA).
- Spring/Recoil (BAR, Zero-K): [S] lockstep, synced versus unsynced state (unsynced reads synced,
  never writes), synced and unsynced RNGs, streflop for floats, checksums on synced writes,
  SYNCDEBUG traces with backtraces diffed against the host. [2nd] multithreaded or background
  pathfinding (QTPFS); merge not verified.
- OpenRA (World.cs): [S] actors in a SortedDictionary by monotonic ActorID (iteration in id
  order); SyncHash folds actors by position and id, ISync trait fields, synced effects,
  SharedRandom.Last (LocalRandom excluded); structural changes via AddFrameEndTask drained at the
  end of Tick.
- Stormgate/SnowPlay: [S, relayed] deterministic lockstep with optional rollback, time as an input.
- They Are Billions, Diplomacy is Not an Option, Total War: only [2nd] marketing or press (custom
  multi-core engine; DOTS for pathfinding; fixed sim rate).

### ECS in deterministic games

- Overwatch (Tim Ford, GDC 2017, https://gdcvault.com/play/1024001; detail [2nd] via a
  translation): components without functions; about 40% singleton components holding shared
  system state; deferment (systems queue side effects such as impacts and damage into a singleton,
  a later system resolves them: one call site, batching, threading); utility functions touching
  few components; fixed polling order; read/write tags let read-only systems run in parallel;
  fixed-step command frames. Server-authoritative, not lockstep.
- Unity Entities ECB
  (https://docs.unity3d.com/Packages/com.unity.entities@1.3/manual/systems-entity-command-buffer-playback.html):
  [S] parallel jobs record commands with a sort key (ChunkIndexInQuery) consistent regardless of
  scheduling; playback sorted, deterministic; one buffer per job.
- Photon Quantum (https://doc.photonengine.com/quantum/v2/quantum-intro): [S] deterministic ECS,
  sparse sets, blittable state, fixed point Q48.16.
- Bevy (https://github.com/bevyengine/bevy/issues/4377): [S] parallel systems' order
  nondeterministic unless specified; ambiguity = conflicting access without an order; ambiguity
  reporting, checked in CI.

### Determinism techniques

- Gaffer, "Floating Point Determinism" (https://gafferongames.com/post/floating_point_determinism/)
  and "Deterministic Lockstep": same binary on the same architecture is achievable, across
  compilers or architectures very hard; transcendentals differ between vendors; FMA changes
  results; DLLs can change the FPU control word; debug and release may differ.
- Bruce Dawson (https://randomascii.wordpress.com/2013/07/16/floating-point-determinism/): only
  + - * / sqrt correctly rounded; hazards: per-thread FP state, /fp:fast reassociation, FMA
  contraction, rcp/rsqrt estimates, transcendentals, CPU-dispatched paths, uninitialized data;
  avoid /fp:fast, disable contraction, own transcendentals, check FP state per thread.
- RNG: synced and unsynced streams separate, synced state in the hash, no global RNG in parallel
  code.
- Iteration: ascending entity id (OpenRA); parallel recording fixed by sort keys (Unity).

### Parallelism

- Parallelized: read-mostly work, independent subsystems, pathfinding ([2nd]). Serial or failed:
  memory-bound updates, order-dependent side effects, SupCom's whole sim ([2nd]).
- Merging: coupled data in one partition, deferred commands with sort keys, side-effect queues
  resolved by one system, read-only systems in parallel by declared access.
- [Inferred] Pathfinding: requests issued serially with ids, workers read an immutable navigation
  snapshot, results applied at a barrier in request order on a deterministic tick.

### Lessons

1. Sim authoritative, presentation read-only.
2. Stable monotonic ids, id-order iteration, no hash-container iteration.
3. Deferred structural changes in key order.
4. Side effects as data resolved by one system.
5. Shared state as explicit singletons, hashable.
6. Declared reads and writes, fixed order, conflicts without order are errors.
7. Partition along real coupling; measure (memory-bound gains nothing).
8. Prefer not updating (sleep, stored intentions, buckets).
9. Per-purpose synced RNG streams in the hash.
10. Control floating point (no /fp:fast, no FMA contraction, own transcendentals, assert FP state).
11. Hash broadly, per-system sub-hashes, a sync-debug mode.
12. Async pathfinding on immutable snapshots, applied at a fixed tick in request order.
13. Prevention and tooling, not desync recovery.
14. Order scripts.

Not found: SC2, CoH/Essence, Total War internals, the DINO talk; Recoil's merge and Overwatch
details are second hand.

## Game job systems

- Naughty Dog (Gyrling, GDC 2015,
  https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine): fiber job system
  for TLOU Remastered at 60 fps [S]; 6 locked workers, 160 fibers, 3 priority queues, atomic
  counters with WaitForCounter parking the fiber [2nd, slide transcription]. FiberTaskingLib
  (https://github.com/RichieSams/FiberTaskingLib): counters, waiting yields the worker, fibers
  resume on other threads (OS mutexes across waits are unsafe). Our Machinery
  (https://ruby0x1.github.io/machinery_blog_archive/post/fiber-based-job-system): near-identical
  API, all memory preallocated, lock-free MPMC queue "failed miserably". MSVC `/GT` for fiber-safe
  TLS (https://learn.microsoft.com/en-us/cpp/build/reference/gt-support-fiber-safe-thread-local-storage).
- Destiny (Genova, GDC 2015, https://www.gdcvault.com/play/1022164/Multithreading): the whole
  engine as a job graph; "job fibers" are job chains, not OS fibers; runtime validation that every
  read, write, creation, and destruction happens at a safe point.
- Frostbite (Andersson 2010,
  https://www.slideshare.net/slideshow/parallel-futures-of-a-game-engine-v20/4345460):
  asynchronous jobs on a work-stealing JobManager, 200-300 per frame, graphs built in batches, no
  runtime allocations; shared-state concurrency the main problem.
- id Tech 7: only jobs, a worker per core [2nd].
- Molecular Matters Job System 2.0
  (https://blog.molecular-matters.com/2015/08/24/... parts 1-5): N-1 workers plus the main
  thread; per-worker deque (LIFO own, FIFO steal); job = function, parent, atomic unfinished
  count, cache-line padded; Wait runs other jobs; idle threads sleep on an event; preallocated
  per-thread job rings (overflow silently overwrites: a pitfall); Chase-Lev deque details (fences,
  last-item race); parallel_for by recursive halving with a count or data-size splitter;
  continuations.
- enkiTS (https://github.com/dougbinks/enkiTS): task sets with ExecuteRange, priorities, pinned
  tasks (IO, main thread), dependencies, WaitforTask helps while waiting, custom allocators, no
  allocations while scheduling, C API, profiler callbacks, default threads = hardware - 1,
  m_MinRange; "the thread index should not be used for changing processing of data", only for
  per-thread output buckets; no fibers, no exceptions.
- oneTBB: partitioners (simple bounds chunks by grain only; auto and affinity adapt; static depends
  on resources); parallel_deterministic_reduce performs the same splits and joins whatever the
  thread count (simple or static partitioner only); work isolation: a waiting thread may run outer
  tasks (thread-local state, deadlock) unless isolated
  (https://uxlfoundation.github.io/oneTBB/main/tbb_userguide/work_isolation.html).
- Taskflow (https://taskflow.github.io/taskflow/index.html): static and dynamic graphs,
  header-only, C++20, work stealing, corun only from workers, observers, uses exceptions
  (conflicts with `/EHs-c-`).
- Marl (https://github.com/google/marl): threads plus fibers, tasks stolen, fibers pinned,
  std::mutex across a yield can deadlock.
- Unity jobs: the safety system tracks NativeContainer reads and writes and rejects conflicting
  independent writers at schedule time; ECS infers dependencies from component access (per system:
  over-waiting).
- Bevy: parallel executor from declared access; ApplyDeferred sync points; ambiguity detection off
  by default; unordered conflicting systems may change order every frame.

### Determinism

- Factorio FFF-415 (https://direct.factorio.com/blog/post/fff-415): chunk generation used all
  cores and its output depended on the core count: a desync between machines with different core
  counts, present since 2017.
- FFF-215: parallel near-independent systems slower from cache invalidation; read-only work
  parallelized well.
- oneTBB parallel_deterministic_reduce: fixed split and join tree. Rayon: reduce order unspecified
  (non-associative operations nondeterministic; floats).
- Gaffer: same compiler, instruction set, FPU settings.
- [I] Chunk boundaries a function of the problem size and a constant grain, never the worker
  count; per-chunk output slots; serial merge in chunk order at a barrier; completion order never
  decides output order; atomics for scheduling only.

### Recommendations of the research

The design took these except the first: the owner chose fibers (ecs-design.md, decision 2).

1. No fibers at first: a thread pool with help-while-waiting.
2. Deterministic parallel_for as the simulation's primitive (fixed grain, per-chunk slots, ordered
   merge, fixed reductions).
3. Declared reads and writes per system, validated in Debug; ambiguity an error.
4. A small own scheduler or enkiTS (a dependency: the owner's call): N-1 workers plus main,
   preallocated checked job storage, padding, sleeping idle workers.
5. No lock-free work stealing until a profile asks.
6. Help-while-waiting is a hazard: no thread-local state in jobs, no locks held while waiting.
7. Presentation and IO at lower priority or pinned.
8. Profiling hooks from the start; measure (parallel can be slower).
9. Pin compiler and FPU settings; order reductions.
