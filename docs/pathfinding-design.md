# Pathfinding and movement design

The agreed design (the owner's) for how ground units plan routes, move in groups, and keep out of
each other. It comes before the entity-component migration's step 8 (parallel systems), so that step
parallelizes the systems kept. The research behind it is
[pathfinding-research.md](pathfinding-research.md); what is built and measured is in
[status.md](status.md), the open work in [roadmap.md](roadmap.md) (work order 2g). Section 4 states
the design together with how it is built; section 5 says which steps are done.

## 1. Why

### Gameplay

The first movement slice (grid A*, push separation after movement, formation slots, cohesion
waits) had these problems, which the design answers; sections 4 and 5 refer to them as concerns:

1. Groups gathered at one point first, then spread to slots; each unit should head for its own slot.
2. Groups queued single file through any passage.
3. Blocked units bumped into each other instead of planning around.
4. Pushing ignored mass: an infantry squad could shove an AT-AT.
5. Walls and placed obstacles were not hard limits: the grid did not know unit size, so an AT-AT
   planned through a gap an infantryman fits through, and a push could move a unit through it.
6. Cohesion waited for members thousands of units away (an order to every unit of an owner was one
   group), and a walker's 3 s turn in place counted as a stall that dropped it from its group.
7. Infantry in the original move as loose teams of about eight, the teams in a block (the owner's
   recording); the engine had no squads.
8. The opening tick's target search (5,000 searches, 38-47 ms with 3,300 entities).

### Performance

The baseline the design was measured against (Release, `benchmark-battle.ps1`, 500 units, 4,400
entities, the build with push separation):

| Layer | Average ms/tick | Worst ms | What drives it |
| --- | --- | --- | --- |
| contact (separation) | 6.9 | 34 | the broad phase, sized by the largest unit |
| navigation | 3.4 | 8-29 | the 15,000-node budget, spent every tick; ~1,150 nodes a search |
| combat | 2.2 | 106-150 | the first tick: every unit searches for a target at once |
| behavior | 0.8 | 5-12 | standing-order scans |
| all | 11-14 | 110-150 | 40-116 of 600 frames over 16.7 ms |

Separation, probed on that build: with vehicles on the map (largest radius 16.7) the 4,363 queries
a tick returned 274,780 candidates, 63 a query, of which 10% touched; with infantry only (largest
radius 3.0) 7.8 a query, 43% touching. Every query reached its own radius plus the largest on the
map, and the grid's cells were sized by the largest too. The probe also counted about 28,000
touching pairs a tick: each unit overlapped about 13 others. Units streamed onto the same points and
were pushed apart afterwards; correction after overlap was both the cost and the jams of concerns 2
to 4.

## 2. Rules (the owner, 2026-11-15)

- **Units never overlap.** Pathfinding and avoidance keep every pair apart; nothing pushes a unit.
  The one exception is crushing: a crusher may move onto units it can crush, which die.
- **Allies give way; enemies never do.** A unit moving faster than allied units in its way (moving
  slower, or standing) has them step aside; they then go back to their stance, formation, and route.
  Enemy units never give way; they are obstacles to plan around.
- **Enemies count for planning when seen, and only then.** Until fog of war exists (the
  `Land_FOW_Reveal_Range` family of tags), every enemy counts as seen; the planning code takes the
  set of seen enemies as an input from the start, so fog of war changes only who fills it. Not built
  yet: the planning code has no seen-enemy input; every enemy counts.
- **A unit with an order reaches its destination or the closest point it can** (the owner); where
  its goal is unreachable or occupied, that is the closest point where it overlaps no other unit.
- **The simulation's budget** is 8 ms a tick at 500 units on the benchmark, and no tick over 16.7 ms
  (for now; the rest of the frame is rendering and presentation).
- Space is outside this design: space ships keep their avoidance until a space battle measures badly.

## 3. Constraints

- Determinism: same commands, same result, bit for bit, on one build; no order from hash
  containers or thread timing; searches are pure functions of the ground and the request.
- The content contract: movement classes (`MovementClassTypeDefs.xml` masks), `UnitCollisionClass`,
  the formation tags (`FormationOrder`, `FormationSpacing`, `FormationRaggedness`,
  `FormationGrouping`, `WaitsForFormationFormup`), crushing, companies and squads (`Company_Units`,
  `Max_Squad_Size`). Routes need not match the original's; what players see does: which ground a
  class crosses, how unreachable orders end, how groups move.
- Parallel-ready for step 8: route searches and chunk rebuilds as jobs over immutable inputs,
  results applied in request order; avoidance over a fixed neighbour order, each unit writing only
  its own velocity.
- Runtime walls, shields, and stamped building footprints change passability during a battle.

## 4. Design

Numbers named below are engine choices (named constants in `simulation_internal.hpp` and
`pathfinding.cpp`) unless the text says otherwise.

### 4.1 Navigation data: size classes, clearance, chunks

- **Sizes** (the owner's decision, after the stock data showed `UnitCollisionClass` does not
  measure size: "Vehicle" types range in footprint radius from 1 to 52, "Large Vehicle" from 3.5 to
  33, "Giant Vehicle" from 32 to 63, and 49 mobile types set no class): each unit plans and moves
  with its own footprint radius against the clearance. Connectivity caches (region labels) are per
  whole-unit radius; the chunk graphs of 4.2 are kept for size buckets every 2 units of radius from 2
  to 68 (`footing_buckets`, the owner's choice; coarser buckets kept units out of gaps they fit). A
  unit uses the smallest bucket that holds it, and graphs exist only for the sizes a map uses. A
  squad stands and plans with its largest soldier's footprint (`GroundMoverColumns::footing_radii`),
  so it goes single file where its soldiers fit; its destination is placed for its whole circle.
  `UnitCollisionClass` stays what the data says it is, collision behavior. Flyers do not use ground
  navigation.
- **Clearance** (`ClearanceField`, `clearance.*`): per movement-class mask in use, how far each
  cell's center lies from ground the class may not enter and from the map's edge; a unit fits a cell
  whose clearance is at least its radius. A unit plans and moves only where it fits, so a walker
  never plans through a trooper's gap, and no move crosses a gap too narrow for the unit. A field
  covers clearance up to the largest bucket (68) or the largest unit of its class on the map.
- **Chunks** (`ChunkGraph`, `chunk_graph.*`): per (mask, size bucket), the connected parts of each
  square of cells and which parts of neighbouring squares they touch, an abstract graph. Three
  layouts exist (`ChunkLayout`); the owner asked for the first two to be compared:
  - one level of 16 x 16 cells;
  - two levels (16 x 16 under 32 x 32);
  - refined (the owner's meaning of two levels): plan over the 32-cell parts first, then over the
    16-cell parts inside that corridor, then search cells part by part, each search held to its part
    and ending at the crossing into the next part nearest the line to the goal (chosen greedily).

  Measured on the path audit's 395,200 searches over the 76 stock land maps, against the plain
  search, reach unchanged in every layout:

  | Layout | Nodes | Time | Paths longer | Longer by more than 1% |
  | --- | --- | --- | --- | --- |
  | one level | 0.52 | 0.61 | 0.5% | 10% of paths |
  | two levels | 0.55 | 0.69 | 1.2% | |
  | refined | 0.12 (first stretch 0.035) | 0.38 | 1.5% | 21% of paths |

  Two levels expanded fewer nodes than one level on no single map; one level is slightly worse than
  the plain search on two small maps (Polus 02 1.10, Kuat Kidnapping 1.02); refined gains least on
  Kuat Kidnapping (0.40). The first stretch counts the nodes up to the first two parts
  (`refined_first_chunks`), what a lazily refined route would need before the unit moves. Refined is
  the battles' layout; one and two levels stay layouts of the pathfinder for comparison
  (`path-audit --layout`).
- **Changes**: the design is that a stamped footprint, wall, or shield marks its chunks dirty and
  they are rebuilt at the tick barrier (clearance within a margin, then the components) before
  navigation runs. As built, the clearance is updated within the stamp's reach at the start barrier
  (`refresh_clearance`), but a change drops the navigation data (region labels and chunk graphs,
  `NavigationSystem::ground_changed`) and they are rebuilt whole; rebuilding only the changed squares
  is open (for death clones and buildable objects, which stamp during a battle). A battle reserves
  its spawnable types' footings while the map loads and then builds every reserved footing's clearance
  field, region labels, and chunk graphs (`reserve_clearance`, `prepare_navigation`; about 4 ms a
  graph on Utapau's 97,000 cells), so no tick builds them whole. Before, each new movement class's
  field reset the navigation data prepared for the classes before it, the reserved size was the old
  circle rather than the footing, and the first tick erased fields no placed unit used, so the data
  was built again in the first ticks.
- **Diagonal steps** (the owner's report, 2026-11-17): a diagonal step's side cells need half a cell
  less room than the cells it joins (`diagonal_side_allowance`, `diagonal_side_footing`), in the cell
  search, the line checks' corner crossings, the region labels' and chunk parts' floods
  (`for_each_step_neighbor`), the chunk graph's links between squares that touch at a corner, the
  refined search's crossings, and `ground_stands`. Utapau's gate has a 42-clearance centre line running
  diagonally between side cells of 32; demanding the full 38 of the Gargantuan's footing at the sides
  closed it, and the route went 5,300 units around (1,430 through it now). A hull may graze a wall
  corner by up to half a cell on such a step. Passability is not relaxed.

### 4.2 Route planning

- **The destination first** (the owner's model): an ordered point moves to the nearest place the
  unit fits as a whole: ground with room for it (a squad by its extent), off every standing unit but
  those of the same order and their soldiers (`place_destination`, rings of the unit's size). For a
  formation move, the group's block of slots first shifts to the nearest offset where every slot fits
  (8 rings of the largest member's size), then each slot that still does not fit moves on its own,
  beside the slots placed before it (`place_block`). A group move on the ground packs instead (4.4,
  "Group moves pack"; `place_group`). Moving units do not count: a destination free at the order may
  be taken by the time the unit arrives, so the arrival rules of 4.4 stay.
- **Routes keep off walls where there is room**: a cell closer than 5 units beyond the unit's radius
  to ground it cannot enter costs one more straight step to enter, and smoothing straightens with
  that margin where the line can keep it, else with the unit's own room. With buckets every 2 units,
  routes otherwise hug corners at exactly the unit's radius and crowds jam there.
- A request plans on the chunk graph first: the chunk parts to the goal. Unreachable goals end
  there, at the nearest reachable part (the partial-path rule). A unit or group whose goal is
  unreachable or occupied stops at the point closest to the goal where it overlaps no other unit
  (the owner).
- The exact cell path is found by A* held to the corridor of parts the chunk route passes (refined:
  part by part). Every part is searched when the route is planned. Refining only the next few chunks
  (a window) and continuing as the unit gets there is not built: with the corridor a search already
  costs a fraction of the plain search's nodes, and continuing a route needs navigation to plan
  again on arrival; it comes when an audit shows long searches still costly. The first-stretch count
  of the audit measures what it would save.
- Seen enemies (2) are obstacles to the local refinement and to avoidance, never to the chunk graph:
  units move, terrain does not.
- JPS is left out for now: with many size buckets and several masks its pruning gets complicated,
  and the chunk corridor bounds the search already. It stays an option behind the same interface,
  tested against the A* kept as the reference.
- The queue and the node budget stay: requests (orders, chases, replans) wait in a FIFO queue planned
  up to 15,000 nodes a tick (at least one request; a unit waiting for its route holds still, a newer
  request supersedes). Searches run eight at a time on the job system and are given in request
  order under the same budget check, so the result is the serial one. A search is a pure function of
  (ground, request, seen obstacles).

### 4.3 Groups: one plan per group, slots that move with it

- **Squads** (the owner: first; `simulation_squads.cpp`): a squad is one commandable unit made of its
  soldiers, a team entity standing in its team container's XML `ContainerArrangement` (rows, or a
  center and circles around it); no single soldier of it can be ordered. A company's squads are
  slots in its block (the owner's recording).
- **The planning unit is the group**: a squad, a formation (a group order), or a unit alone. A
  formation plans one route for its largest member from its center (the commander rule: the member
  nearest the centroid).
- **Two kinds of group move** (the owner, after trying frames: they had companies standing for
  seconds while forming up and waiting, gathering far from some companies, and left companies far
  apart):
  - The default group move (`SimCommandKind::group_move`, `BattleOrder::move`, a right-click) sends
    each member from where it stands to its own slot at the target on a route of its own, all at the
    slowest member's speed (units with XML `FormationGrouping` Solo at their own speed, straight to
    the point); no gathering, no shared route, no waiting. On the ground the slots pack (4.4).
    Attack-moves to a point use it too.
  - The formation move (`formation_move`, `BattleOrder::formation_move`, Alt + right-click in the
    sandbox) moves with a formation frame, below. Ships in space keep per-member routes.
- **A formation frame** moves along the route: a position and a facing. Members steer to their slot
  in the frame, so the group moves as one body and arrives in shape (concern 1). Slots follow the
  formation rules (`FormationOrder` ranks, spacing from footprints and `FormationSpacing`).
- **Several companies** (the owner): companies ordered together pass a narrow passage one company
  after another; the members of one company move at once, under the avoidance and formation rules.
  Not built: the parts of a selection do not queue each other.
- **Narrow passages**: where the corridor (from clearance) is narrower than the formation, the frame
  narrows to as many files as fit and widens after (Age of Empires II, Company of Heroes); members
  pass by slot, not by who arrives first.
- **Cohesion is the frame's**: the frame waits for members that fall behind, within a distance
  bounded by the formation's size; an order splits a selection into groups by distance, so no member
  thousands of units away belongs to the formation. Stalls are measured against the frame's
  progress, and turning in place is not a stall (concern 6).
- **As built** (`simulation_frames.cpp`, `FormationFrame`):
  - A ground formation on land gets a frame per part of the selection (single linkage at 400 world
    units). Slots are laid out once for the whole selection at the target, so the parts arrive into
    one block.
  - The frame starts at the member nearest the part's centroid and gets one route (the path queue),
    planned for the largest vehicle's or soldier's footprint, not a squad's whole circle (planned for
    that, narrow ways looked closed and the route ended short).
  - It moves at 0.9 of its slowest member's speed, so members behind their slots catch up; turns at
    1.5 rad/s and holds while more than 0.6 rad off its way.
  - It forms on the move: each member's slot starts where the member stands in the frame and blends
    into its formation slot over the first 150 units of the way (all of it when shorter), so the
    frame never waits for the block to form and nobody walks back.
  - It holds while a member is more than 30 units (plus its footprint's diameter) behind its slot,
    at most 5 s at a stretch; a member that came no closer for 3 s leaves and goes on alone.
  - Where the clearance at the frame or at any member is narrower than the formation, the slots
    become as many files as fit (1.25 footprints apart), members in the order they stand along the
    frame's heading, so the formation stays in files until its whole body is through.
  - On arrival the members take the final slots by where they stand (front ones the front rank, left
    to right; among members of one footprint), so a column fans out without crossing. A final slot
    on ground a member cannot stand on moves to the nearest point it can (rings of its footprint).
  - Members walk to their slots by the rule squads' soldiers use (`follow_slot`); a member that
    cannot walk straight goes by way of the frame.
  - Members of a frame on its way do not ask each other to give way (they did, over and over), but
    may once it arrived. Arrival rules and the progress measure leave a member of a moving frame
    alone (its moving slot never comes closer); its stops on the way are not recorded as where its
    move ended; a member done at its final slot is not sent again.
  - Not built: splitting by anything but distance; companies queueing each other.

### 4.4 Local movement: avoidance without overlap

Built in `simulation_avoidance.cpp` and `orca.*` (the solver alone, tested by `test_orca` in `tests/test_navigation.cpp`).

- **The agents**: a ground unit, or a squad as one agent, the circle of its extent; its soldiers
  hold their places in it and are not kept apart themselves (the owner: they keep their arrangement
  while walking). With every soldier an agent, the large Utapau battle cost 39 ms a tick; with
  squads, 2 ms.
- **ORCA** (the owner's choice; van den Berg, Guy, Lin, and Manocha, "Reciprocal n-body collision
  avoidance", 2011): each moving agent picks its velocity for the tick, toward its next waypoint,
  inside the half-planes its nearest ten neighbours (within four of its radii) impose over one
  second, so units flow around each other (concerns 2 and 3). All from the state at the start of the
  tick (neighbours' velocities are last tick's, hashed: `MotionColumns::velocities`); neighbours in a
  fixed order; planned in chunks of four steering units on the job system. Two moving allies share
  the avoidance from their current velocities (ORCA's rule); against a standing unit or another
  owner's, the agent takes all of it from the velocity it prefers, so it goes around instead of
  slowing to a stop in front (the constraint at a resting unit's velocity only slows it). A unit that
  cannot go its way prefers a little to its right, so two meeting head-on pass instead of stopping
  nose to nose.
- **Responsibility by size and side**: of two moving allies, each takes the other's share of their
  summed footprint areas, so the larger gives way less (an AT-AT next to none against infantry,
  concern 4); against an enemy, the enemy takes none (enemies never give way).
- **Giving way** (2; `plan_giving_way`, `RouteColumns::give_ways`, hashed): a unit that wants to
  pass allies slower than itself, or standing, asks them to yield. The passing unit keeps its line;
  a yielding unit steps aside just far enough to clear the passing unit's corridor, to the side of
  the line it already stands on (two allies with too narrow a gap between them part, each to its own
  side; the owner's drawing). A unit dead on the line steps to the side with room, ties by id. It
  waits until the passing unit has passed and returns to its stance, its formation slot, and its
  route. Each of these rules came from a deadlock found in the tests:
  - Only a unit held up (slower last tick than half its speed) asks, and only the nearest unit in
    its corridor.
  - That unit gives way when it is standing, slower by nature (top speed below 0.9 of the asker's,
    not this tick's speed), all but at its destination, or behind the asker in the flow (more route
    left: Age of Empires II's priority, so a queue's leader never stands aside for those behind it),
    whatever its size (the owner: a rule against giving way to smaller units kept companies from
    giving way), and only where the point aside is free.
  - A unit gives way for at most 4 s. One that cannot reach the point aside walks back to its place;
    on its way back it waits for the way to clear (the unit it made way for often still stands in
    it) up to that limit (the owner: squads that gave up at once stayed aside).
  - A unit still moving at its speed straight along its way asks no one; one turned aside or slowed
    asks a standing ally in its corridor before it is held (without this, avoidance slid it around a
    line instead of the line opening).
  - A unit whose ask found no place asks again after a quarter of a second
    (`RouteColumns::ask_waits`; asking every tick cost 2.5 ms a tick at 500 units, nearly every ask
    failing in the crowds).
- **Giving way ahead** (the owner, 2026-11-17; agreed, being built): today a unit asks only once it
  is held up, and only the nearest ally in its corridor. Instead:
  - when a unit's route is planned, the idle allies standing along the whole route are found: those
    whose footprints reach its corridor (its extent across the way plus the give-way margin), through
    the avoidance grid along the route's segments; allies firing at an enemy hold their place (the
    owner) and count as obstacles;
  - each is asked at once whether it could step out of the way (a dry run of `make_way`, with the
    chain of up to `give_way_chain` units as in lanes), measured from the passer's place on its route
    beside it and the route's direction there;
  - if any cannot, the unit plans a detour around those at once instead of finding out on arrival
    (the owner);
  - those that can are asked for real when the unit is about 3 s of travel from them (an engine
    choice), step aside, wait until it has passed, and return to their places, as giving way does; the
    time limit counts from then;
  - moving allies keep today's rules (who is slower or behind in the flow yields).
- **Any direction, and lanes** (the owner: units may step diagonally and in other directions as long
  as the way is free, and go back to their place; lines open): the point aside is the shortest step
  out of the passer's corridor in any of 16 directions (sideways, diagonally, forward, or back, not
  within a sine of 0.2 of the passer's line, at most three clearances), shortest first, then toward
  the unit's own side of the line. The point must be free, on ground the unit stands on, reached by a
  straight line on that ground, and the way there must pass no unit closer than contact (nor closer
  than it already is: packed units slide past each other); a step back past the passer was taken,
  then held by it, and a queue at a passage's mouth jammed. Where no point is free but one is held by
  a single standing ally that may move, that ally steps on along the same direction just far enough
  to clear it, and so on up to three allies deep: a packed line opens a lane (`make_way`,
  `shift_on`).
- **Group moves pack** (the owner: a group ordered to a position takes it, not a block 50-100 meters
  across; a Canderous company's `FormationSpacing` 2.75 put its slots 169 units apart): on the ground
  each member's destination is the free point nearest the ordered point, members nearest it choosing
  first, on rings of the member's size and on its ring the point nearest the member (`place_group`);
  destinations keep a quarter of a radius free. Formation moves keep their formation's spacing.
- **The no-overlap guarantee**: ORCA can fail to find a velocity (an infeasible program in a dense
  crowd), and a tick's step is discrete. So movement ends with a hard check (`keep_apart`, also what
  keeps space ships apart): the part of a step that would go deeper into a unit is removed, so units
  slide along each other and no unit is moved by another, except a crusher moving onto units it can
  crush.
- **Blocked units plan around** (`plan_detours`; the owner's rule that a unit with an order reaches
  its destination or the closest point): a unit held up for 2 s beside a standing unit (within 1.5
  summed radii) plans a detour: a search without chunks, at most 16,384 nodes, in which the standing
  units within 300 units block the cells within their summed radii. It follows the detour waypoint by
  waypoint, then goes on to its goal. Where the detour ends where it stands, it has reached the
  closest point and stops; where its goal lies among the standing units, the arrival rules decide
  instead. At most 4 detours a tick.
- **Jams of moving units** (the owner's Kamino recording): a unit pushed around a corner so that it
  no longer sees its next waypoint plans a short leg back to it (`repair_routes`, at most 8 a tick);
  a unit held up by units that are themselves held (not only standing ones) detours around them, at
  most three times a route and not into a crowd at its goal; a way closed only by held movers does
  not end the order.
- **Arrival among others** (the owner: the closest point to the goal where it overlaps no one;
  `goal_taken`, `RouteColumns::closest_approaches`, `stalled_ticks`, `last_goals`, hashed):
  - A unit stops where it touches a standing unit on its destination, or, having come no closer for
    a second, at a standing ally between it and its destination that stopped at the end of the same
    order or at a destination within six summed radii of its own, and within two of its footprints'
    widths beyond contact (a crowd grows ring by ring, rings a little apart where avoidance kept its
    margins).
  - The crowd is made of allies that stopped short of their own goals; allies that reached theirs
    count only where the unit's own goal is occupied, or once it has come no closer for 3 s beside
    allies that stopped near its goal (a free goal enclosed among them).
  - In a group move each member has a slot of its own: group-mates standing in theirs, or units
    giving way, are no crowd (the owner's report of units stopping while others gave way). An ally
    standing in the way after a move elsewhere is no crowd either: without that condition companies
    sent through a standing squad stopped at it (the owner's report).
  - A unit that came no closer for a second within its diameter of its goal has arrived.
  - On its last leg a unit's preferred speed is what reaches its goal within the horizon: at full
    speed ORCA took a standing unit beyond the goal for a collision and held a unit still ten units
    short.
  - A unit never asks the unit on its destination to give way.
- **No separation**: ground units are never pushed apart after movement (flyers of one owner still
  are). Spawns, teleports, and company expansion place units on free ground (`free_ground`: the
  nearest point where the unit fits and touches no other ground unit, on rings around it), so no
  overlap exists to correct.
- Not built: soldiers kept apart from units outside their squad when they lag behind it (from
  status.md; unverified against the code).

### 4.5 One spatial index, by size

- The design: one index a tick, built once after movement and shared by contact, combat, and
  behavior; units binned by size class, small ones in a fine grid, the few large ones in a coarse
  one, each grid queried at its own reach (the measured 63 candidates a query should fall toward the
  infantry-only 8).
- As built: `SpatialGrid` can be built with radii, an item lying in every cell its circle covers and
  reported once, and `query_overlapping` finds the items whose circles reach a query's. Contact
  (16-unit cells) and avoidance (32-unit cells) use it, so a query reaches only its own footprint;
  this answers the reach problem the size bins were for, so whether the bins are still needed is
  open. Not built: sharing one index; contact, avoidance, behavior, and combat each build their own
  grid a tick.

### 4.6 Target search

- Retarget timers start at a phase from the entity's id (as shield refreshes do), so the opening
  tick spreads over the retarget interval instead of every unit searching at once (built:
  `CombatSystem::retarget_due`).
- Weapons whose unit's target is out of range share their unit's timer instead of searching every
  tick (built). Searches read the shared index once it exists (4.5).

### 4.7 Oriented footprints (agreed with the owner, 2026-11-17; built)

Why: a ground unit is a circle of radius (width + depth) / 4 (`model_footprint_radius`), so a long
unit needs a gap as wide as its average size: the Gargantuan (hull about 72 by 150) a gap of about
120 where its hull is 75 wide (the owner's report, 2026-11-17). The owner chose oriented shapes over
planning with the half-width or keeping circles, and then rectangles over capsules: a capsule's
radius, half the narrow side, is right for a long unit but wrong for a wide one, which sweeps its
whole width moving forward.

- **The shape** (the owner's scheme): an oriented rectangle per unit, a half-length along its
  heading and a half-width across it, about a center offset in its own frame. Taken from the
  model's collision mesh when it has one (an inference: the mesh the original names for collision;
  the Gargantuan's is 75 by 155), else from its drawn bounds with the attached hardpoint models
  (model forward -Y). A soft footprint radius (`Custom_Soft_Footprint_Radius`) is a square of that
  half-size.
- **Planning**: a ground vehicle moves along its heading, so the width it sweeps is the rectangle's
  side orthogonal to its heading; a unit plans and moves its centerline with half that side as its
  footing radius (`GroundMoverColumns::footing_radii`: size buckets, region labels, route lines and
  smoothing, plus the wall margin). The grid search knows positions, not headings, so the shape's
  length matters only where the unit turns and stops.
- **Moving**: a step needs only the footing, the centerline at half the orthogonal side (the
  owner's scheme). Testing the whole rectangle at the heading while going straight (within 20
  degrees) was tried and dropped: its corners swing out by the half-length times the angle's sine,
  beyond what the route keeps from walls, and the Gargantuan battle's blocked moves rose from 12 to
  82 and its routes from 26 to 98. A turn may overlap blocked ground (the owner's choice).
- **Placement** needs both sides: a unit's whole rectangle at the heading it arrives with (spawns,
  teleports, `free_ground`, `destination_free`); a group's members each at the group's facing, its
  slots spaced by their sides.
- **Squads** (the owner): route planning as before, a squad passing where one soldier fits (its
  largest soldier's width); for placement, formations, and collisions with other units, the
  rectangle bounding its soldiers in the squad's own frame (each slot's offset plus and minus the
  soldier's half-sizes, from the front-left corner of the front-left soldier to the back-right
  corner of the back-right one), turned with the squad's facing. As built: `team_footprint` (each
  soldier the square of his radius about his slot) in `SquadState::body`, refreshed whenever the
  squad's shape changes; avoidance measures a squad's room by it (ORCA, `keep_apart`, giving way,
  detours, arrival), and a squad's destination must hold it at the heading it arrives with. Its
  soldiers keep their circles among themselves. A group's slots are not yet spaced by their members'
  sides (formation slots still use radii).
- **Units among units** (as built): one measure for every spacing test, so placement and avoidance
  agree: two units keep between their positions each one's extent toward the other (the rectangle's
  support distance along the line between them, or the circle's radius; `footprint.hpp`:
  `footprint_extent`, `footprints_reach`). Side by side a rectangle keeps its half-width, end to end
  its half-length, at a slant a little more room than an exact test. Exact separating-axis tests were
  the first plan; mixing them with extents in avoidance would let placement put units where
  avoidance holds them. Placement (`overlaps_unit`), ORCA (each pair's reach), `keep_apart`, giving
  way, detours around standing units, and the arrival rules all use it (`agent_room`,
  `agent_extent`, with `Avoidance::bodies`, `reaches`, `facings`); broad phases use each unit's reach
  (the rectangle's half-diagonal plus its centre offset). Crushing is exact: what lies within the
  crusher's rectangle (`footprint_contains`). A squad's soldiers keep their circles; the squad is its
  bounding rectangle (step 5).
- **Hashes**: every battle with ground vehicles changes its hash; declared with each change.
- **Order of work**: (1) the rectangle on recipes and in `PlacementColumns` in place of the first
  slice's capsule, from the collision mesh or the bounds; (2) planning by the half side orthogonal
  to the heading, and ground fitting and placement by the rectangle; (3) contact, crushing, and
  overlap tests; (4) avoidance and giving way; (5) squads' rectangles; (6) the path audit by footing
  radius, its stale header comment fixed. Each step with its tests (`test_navigation`'s
  narrow-passage and footprint cases gain a long and a wide unit), the battle check, and a
  Gargantuan battle.
- **As built (steps 1 and 2)**: `FootprintRect` on recipes (`TypeRecipe::body` from
  `body_bounds`: `collision_model_bounds`, else the drawn bounds; `model_footprint_rect`) and in
  `PlacementColumns::bodies`; ground units stand and plan with its half-width
  (`GroundMoverColumns::footing_radii`); spawns, teleports, and destinations test the whole rectangle
  (`ground_rectangle_fits`, through `for_each_cell_in_rectangle`, which also stamps obstacles). The
  stock rectangles, halves along by across: Gargantuan 78.2 by 37.3, AT-AT 53.9 by 16.7 (its
  collision meshes are its legs, neck, and head; drawn 68.3 by 22.2), T2-B 23.3 by 10.0, AT-ST 12.1
  by 9.6.
- **Cost (steps 3 and 4)**: the 260-unit Utapau battle averages 5.5 ms a tick against 4.8 before
  (99th percentile 9.4 against 8.6 ms), measured on an idle machine against a build of the step
  before. The same code with every avoidance agent forced to a circle runs at 4.2 ms, so the cost is
  the battle the rectangles make (vehicles pass and pack closer, more of them slide along each other),
  not the measure's arithmetic; profiling found the same neighbour candidates and the time in
  `keep_apart` per call. Accepted (well within the frame budget). With squads as rectangles (step 5)
  the same battle runs at 3.8 ms: a squad's rectangle is tighter than its circle.

## 5. Order of work

Each step keeps the battles' outcomes or declares the change, and is measured with
`benchmark-battle.ps1` and `check-battle.ps1`.

1. **Squads** (work order f, the owner: first), on the movement of the time: a squad as one
   commandable unit of soldiers. Done (team entities; companies spawn as squads; the sandbox selects
   them by their soldiers).
2. **The pathfinding audit**: a command over stock land maps (seeded start and goal pairs per
   movement and size class; expansions, time, path length, reachability against the plain A*), the
   baseline every later step is measured against, and the comparison of the chunk layouts. Done:
   `EAWEngine path-audit` and `scripts/compare-path-audit.ps1` ([tools.md](tools.md#path-audit)).
   Its first baseline, at the first four size buckets: 76 maps, 98,800 pairs, 82% reached, 1,385
   nodes and 0.25 ms a search on average (95th percentile 6,722 and 1.1 ms).
3. **The shared, size-binned spatial index and staggered target timers** (4.5, 4.6). Partly done:
   the grid by footprint (contact and avoidance) and the staggered timers are built; one index
   shared by the systems is open.
4. **Clearance per size class** (4.1), with spawns and teleports on free ground. Done, with each
   unit's own radius instead of size classes.
5. **Chunks and windowed refinement** (4.1, 4.2): the layouts, measured by the audit on maps of every
   kind; one is kept. Partly done: the layouts are built and compared, refined is kept. Open:
   refining the rest of the route as the unit advances (the window), rebuilding only changed squares.
6. **ORCA, giving way, and the no-overlap check** (4.4); separation removed; blocked-unit replans.
   Moved before formation frames (the owner: overlaps and pushing are what play shows). Done, with
   squads as single agents and blocked units planning detours.
7. **Formation frames and splitting selections by distance** (4.3). Done. Open: companies passing a
   narrow place one after another.
8. Then the entity-component migration's step 8 (parallel systems). Under way
   ([ecs-design.md](ecs-design.md), section 8): ground avoidance and route searches run on the job
   system.

## 6. Black-box questions for the original (the owner's observations)

- How does a group pass a narrow gap? Company after company; the members of one company at once,
  under avoidance and formation rules (the owner; 4.3).
- Where does a group stop when its goal is unreachable or occupied? At the point closest to the goal
  where it overlaps no other unit (the owner; 4.2).
- Do the soldiers of a squad keep their arrangement while moving, or only at rest? They hold it
  while walking: the squad walks as a fixed shape (the owner).
- Does a faster unit pass slower allies, and how do they make room? They step sideways off its line,
  each to its own side, and it passes straight (the owner's drawing; 4.4).
