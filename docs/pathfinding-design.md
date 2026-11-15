# Pathfinding and movement design

The agreed design (the owner, 2026-11-15) for how ground units plan routes, move in groups, and
keep out of each other, replacing work order 2g's open list. It comes before the entity-component
migration's step 8 (parallel systems), so that step parallelizes the systems we keep. The research
behind it is [pathfinding-research.md](pathfinding-research.md); what is built is in
[status.md](status.md), the order of work in [roadmap.md](roadmap.md).

## 1. Why

### Gameplay

1. Groups gather at one point first, then spread to slots; each unit should head for its own slot.
2. Groups queue single file through any passage.
3. Blocked units bump into each other instead of planning around.
4. Pushing ignores mass: an infantry squad can shove an AT-AT.
5. Walls and placed obstacles are not hard limits: the grid does not know unit size, so an AT-AT
   plans through a gap an infantryman fits through, and a push can move a unit through it.
6. Cohesion waits for members thousands of units away (an order to every unit of an owner is one
   group), and a walker's 3 s turn in place counts as a stall that drops it from its group.
7. Infantry in the original move as loose teams of about eight, the teams in a block (the owner's
   recording, 2026-11-12); the engine has no squads yet.
8. The opening tick's target search (5,000 searches, 38-47 ms with 3,300 entities).

### Performance (2026-11-15, Release, `benchmark-battle.ps1`, 500 units, 4,400 entities)

| Layer | Average ms/tick | Worst ms | What drives it |
| --- | --- | --- | --- |
| contact (separation) | 6.9 | 34 | the broad phase, sized by the largest unit |
| navigation | 3.4 | 8-29 | the 15,000-node budget, spent every tick; ~1,150 nodes a search |
| combat | 2.2 | 106-150 | the first tick: every unit searches for a target at once |
| behavior | 0.8 | 5-12 | standing-order scans |
| all | 11-14 | 110-150 | 40-116 of 600 frames over 16.7 ms |

Separation, probed: with vehicles on the map (largest radius 16.7) the 4,363 queries a tick return
274,780 candidates, 63 a query, of which 10% touch; with infantry only (largest radius 3.0) 7.8 a
query, 43% touching. Every query reaches its own radius plus the largest on the map, and the grid's
cells are sized by the largest too. The probe also counted about 28,000 touching pairs a tick: each
unit overlaps about 13 others. Units stream onto the same points and are pushed apart afterwards;
correction after overlap is both the cost and the jams of concerns 2 to 4.

## 2. Rules (the owner, 2026-11-15)

- **Units never overlap.** Pathfinding and avoidance keep every pair apart; nothing pushes a unit.
  The one exception is crushing: a crusher may move onto units it can crush, which die.
- **Allies give way; enemies never do.** A unit moving faster than allied units in its way (moving
  slower, or standing) has them step aside; they then go back to their stance, formation, and route.
  Enemy units never give way; they are obstacles to plan around.
- **Enemies count for planning when seen, and only then.** Until fog of war exists (the
  `Land_FOW_Reveal_Range` family of tags), every enemy counts as seen; the planning code takes the
  set of seen enemies as an input from the start, so fog of war changes only who fills it.
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

### 4.1 Navigation data: size classes, clearance, chunks

- **Sizes** (the owner, 2026-11-15, revising D4 after the stock data showed `UnitCollisionClass`
  does not measure size: "Vehicle" types range in footprint radius from 1 to 52, "Large Vehicle"
  from 3.5 to 33, "Giant Vehicle" from 32 to 63, and 49 mobile types set no class): each unit
  plans and moves with its own footprint radius against the clearance; connectivity caches are
  per whole-unit radius, and the chunk graphs of 4.2 are kept for four size buckets (radius up to
  7, 20, 35, and 65, from the stock footprints; a unit uses the smallest that holds it).
  `UnitCollisionClass` stays what the data says it is, collision behavior. Flyers do not use
  ground navigation.
- **Clearance**: per movement-class mask in use, how far each cell's center lies from ground the
  class may not enter and from the map's edge; a unit fits a cell whose clearance is at least its
  radius. A unit plans and moves only where it fits, so a walker never plans through a trooper's
  gap, and no move can cross a gap too narrow for the unit (done 2026-11-15).
- **Chunks**: per (mask, size), the connected components of each chunk and which components of
  neighbouring chunks they touch, an abstract graph. Two layouts were built and compared (the
  owner, 2026-11-15): one level of 16 x 16 cells, and two levels (16 x 16 under 32 x 32). On the
  path audit's 395,200 searches over the 76 stock land maps, one level expands 0.52 of the plain
  search's nodes (time 0.61) with paths 0.5% longer; two levels 0.55 (time 0.69) and 1.2% longer,
  and on no single map fewer nodes than one level. A third layout, refined (the owner's meaning of
  two levels, 2026-11-15), plans over the 32-cell parts first, then over the 16-cell parts inside
  that corridor, and searches cells part by part, each search held to its part and ending at the
  crossing into the next part nearest the line to the goal. It expands 0.12 of the plain search's
  nodes (the first stretch, the first two parts a lazily refined route would need before the unit
  moves, 0.035), time 0.38, with paths 1.5% longer (21% of them
  longer by more than 1%; the crossing is chosen greedily). Refined is the battles' layout; one and
  two levels stay layouts of the pathfinder for comparison.
- **Changes**: a stamped footprint, wall, or shield marks its chunks dirty; they are rebuilt at the
  tick barrier (clearance within a margin, then the components) before navigation runs.

### 4.2 Route planning

- A request plans on the chunk graph first: the chunk components to the goal. Unreachable goals end
  there, at the nearest reachable component (today's partial-path rule). A unit or group whose goal
  is unreachable or occupied stops at the point closest to the goal where it overlaps no other unit
  (the owner, 2026-11-15).
- The exact cell path is found by A* held to the corridor of parts the chunk route passes
  (2026-11-15). Refining only the next few chunks (a window) and continuing as the unit gets there
  is not built: with the corridor a search costs about half as many nodes already, and continuing
  a route needs navigation to plan again on arrival; it comes when an audit shows long searches
  still costly.
- Seen enemies (2) are obstacles to the local refinement and to avoidance, never to the chunk graph:
  units move, terrain does not.
- JPS is left out for now: with four sizes and several masks its pruning gets complicated, and the
  window bounds the search already. It stays an option behind the same interface, tested against
  the A* kept as the reference.
- The queue and the node budget stay; a search is a pure function of (ground, request, seen
  obstacles).

### 4.3 Groups: one plan per group, slots that move with it

- **Squads** (the owner: first): a squad is one commandable unit made of its soldiers, standing in
  its team container's XML `ContainerArrangement` (rows, or a center and circles around it); no
  single soldier of it can be ordered. A company's squads are slots in its
  block (the owner's recording).
- **The planning unit is the group**: a squad, a formation (a group order), or a unit alone. A group
  plans one route for its largest size class from its center (today's commander rule).
- **A formation frame** moves along the route at the slowest member's speed: a position and a
  facing. Members steer to their slot in the frame, so the group moves as one body and arrives in
  shape (concern 1). Slots follow today's rules (`FormationOrder` ranks, spacing from footprints and
  `FormationSpacing`).
- **Several companies** (the owner, 2026-11-15): companies ordered together pass a narrow
  passage one company after another; the members of one company move at once, under the avoidance
  and formation rules.
- **Narrow passages**: where the corridor (from clearance) is narrower than the formation, the frame
  narrows to as many files as fit and widens after (Age of Empires II, Company of Heroes); members
  pass by slot, not by who arrives first.
- **Cohesion is the frame's**: the frame waits for members that fall behind, within a distance
  bounded by the formation's size; an order splits a selection into groups by distance (an engine
  choice), so no member thousands of units away belongs to the formation. Stalls are measured
  against the frame's progress, and turning in place is not a stall (concern 6).

### 4.4 Local movement: avoidance without overlap

- **ORCA** (the owner's choice): each moving unit picks its velocity for the tick, toward its slot or
  next waypoint, inside the reciprocal velocity obstacles of its neighbours over a short horizon, so
  units flow around each other (concerns 2 and 3). Neighbours are gathered in a fixed order; the
  solver is deterministic on one build, as the rest of the simulation is.
- **Responsibility by size and side**: of two allies, the larger takes less of the avoidance (an
  AT-AT none against infantry, concern 4); against an enemy, the enemy takes none (enemies never
  give way).
- **Giving way** (2): a unit that wants to pass allies slower than itself, or standing, asks them
  to yield. The passing unit keeps its line; a yielding unit steps sideways, across that line, just
  far enough to clear the passing unit's corridor, to the side of the line it already stands on (two
  allies with too narrow a gap between them part, each to its own side; the owner's drawing,
  2026-11-15). A unit dead on the line steps to the side with room, ties by id. It waits until the
  passing unit has passed and returns to its stance, its formation slot, and its route. Yielding is state, so it is hashed and deterministic.
- **The no-overlap guarantee**: ORCA can fail to find a velocity (an infeasible program in a dense
  crowd), and a tick's step is discrete. So movement ends with a hard check: a step that would
  bring two units into contact is shortened to the contact (no unit is moved by another), except a
  crusher moving onto units it can crush. A unit whose step comes back empty while it should move is
  blocked: it asks for a local replan around the units in its way (as obstacles), and after a few
  failures its order ends as unreachable, as today.
- **No separation**: the post-movement push goes. Spawns, teleports, and company expansion place
  units on free ground (the nearest free point of the right size class), so no overlap exists to
  correct.

### 4.5 One spatial index, by size

- One index a tick, built once after movement and shared by contact, combat, and behavior (three
  builds today).
- Units are binned by size class: small ones in a fine grid, the few large ones in a coarse one; a
  query checks each grid at its own reach (the measured 63 candidates a query should fall toward the
  infantry-only 8).

### 4.6 Target search

- Retarget timers start at a phase from the entity's id (as shield refreshes do), so the opening
  tick spreads over the retarget interval instead of every unit searching at once.
- Weapons whose unit's target is out of range share their unit's timer instead of searching every
  tick; searches read the shared index.

## 5. Order of work

Each step keeps the battles' outcomes or declares the change, and is measured with
`benchmark-battle.ps1` and `check-battle.ps1`.

1. **Squads** (work order f, the owner: first), on today's movement: a squad as one commandable
   unit of soldiers.
2. **The pathfinding audit**: a command over stock land maps (seeded start and goal pairs per
   movement and size class; expansions, time, path length, reachability against today's A*), the
   baseline every later step is measured against, and the comparison of the two chunk layouts.
3. **The shared, size-binned spatial index and staggered target timers** (4.5, 4.6).
4. **Clearance per size class** (4.1), with spawns and teleports on free ground.
5. **Chunks and windowed refinement** (4.1, 4.2): both layouts, measured by the audit on maps of
   every kind; one is kept.
6. **ORCA, giving way, and the no-overlap check** (4.4); separation removed; blocked-unit replans.
   Moved before formation frames (the owner, 2026-11-15: overlaps and pushing are what play shows).
7. **Formation frames and splitting selections by distance** (4.3).
8. Then the entity-component migration's step 8 (parallel systems).

## 6. Black-box questions for the original (the owner's observations)

- How does a group pass a narrow gap? Company after company; the members of one company at once,
  under avoidance and formation rules (the owner, 2026-11-15; 4.3).
- Where does a group stop when its goal is unreachable or occupied? At the point closest to the goal
  where it overlaps no other unit (the owner, 2026-11-15; 4.2).
- Do the soldiers of a squad keep their arrangement while moving, or only at rest? They hold it
  while walking: the squad walks as a fixed shape (the owner, 2026-11-15).
- Does a faster unit pass slower allies, and how do they make room? They step sideways off its line,
  each to its own side, and it passes straight (the owner's drawing, 2026-11-15; 4.4).
