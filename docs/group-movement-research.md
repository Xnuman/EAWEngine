# Group movement research (2026-11)

Research into why groups of ground units in the engine queue, deadlock, and oscillate, and what
published work and shipped games offer against it. It builds on
[pathfinding-research.md](pathfinding-research.md) (the general survey) and feeds
[pathfinding-design.md](pathfinding-design.md) (the agreed design). The owner chose the sources:
three papers kept as local reference copies (`Reference Materials/Pathfinding Materials/`, not in
the repository) and a set of web pages. Everything below is in our own words. Code was read only
under a permissive license, and none was copied.

Confidence varies. Papers and developer-written articles are strong evidence about their own
systems, never about the original game. Claims marked **(knowledge)** come from general literature
the sources did not cover; **(inference)** marks our reasoning about the engine. Two pages refused
automated fetching (Stack Overflow 76444431, gamedev.stackexchange 387), and Liquipedia's Dota 2
page returned HTTP 403; what is said about Dota 2 rests on secondary sources and is unverified.

## The problems

Observed by the owner in the sandbox (2026-11-17), with the code that explains them:

1. **Contention at shared points.** Members of a group follow one frame route; members whose slot is
   out of straight reach are sent by way of the frame's center. In a narrow passage they all steer
   for the same points. Who goes first is never decided: it falls out of `keep_apart` (whoever is
   behind is held), ORCA, and giving way (by remaining route, which can flip). The result is
   queues, and deadlocks when two units wait on each other.
2. **Route churn and oscillation.** A tank among tanks changes routes quickly; sometimes it turns
   left and right in place for a long time. One unit's route can be changed by five uncoordinated
   mechanisms: ORCA (every tick), an immediate re-plan when avoidance pushed it where its next step
   leaves passable ground (no cooldown), detours around movers (up to three a route), repair legs,
   and giving way (step aside and return). A vehicle facing more than 45 degrees from its waypoint
   turns in place, so each flip shows as turning without moving.
3. **Unlike the original.** The retail game (the owner's observations,
   [compatibility-research.md](compatibility-research.md#group-moves-of-vehicles)) splits ten T2-Bs
   into two formations of five, each three in front and two behind in the gaps, gives the front
   slots to the nearest tanks, and breaks a formation into a column in narrow passages, re-forming
   after it with the leaders slowing while the last catch up. The engine keeps one block per
   selection part, lays it out four and one for five, and keeps the formation through passages.

Found while reading the code against the papers (checked, not measured):

- **ORCA sees realized velocities, not chosen ones.** `simulation_contact.cpp` stores each unit's
  velocity as its actual displacement over the tick, after `keep_apart` clipped it and separation
  moved it; next tick's ORCA lines use that value for the neighbour and, between moving allies, for
  the unit's own share (`simulation_avoidance.cpp`). ORCA's reasoning assumes the velocities units
  chose. Units in contact therefore plan against motions that are not their neighbours' intentions,
  every tick: the "inconsistent view" the ORCA chapter names as the source of flicker.
- **The keep-right rule acts before the solve.** When the first solve changed anything, the
  preferred velocity is turned right and the program solved again, with no check that this helped
  and no try of the other side. In the deadlock geometry below, turning the preferred velocity does
  not move the solution at all.

## Sources and findings

### "RVO and ORCA: How They Really Work" (Sunshine-Hill, Game AI Pro 3, ch. 19, 2017)

Local reference copy. An explanation of why velocity obstacles work in games although their
guarantees fail there, and how to tune them.

- Plain velocity obstacles oscillate: both agents dodge fully, both are surprised, both undo. RVO
  (each dodges half) is a damped gradient method: each frame corrects part of the remaining
  conflict, so it converges without overshooting. The weights are the step size; below one half
  each, agents drift and eventually "panic"; near one, they oscillate.
- What makes ORCA stable is **sidedness**: two agents passing keep the side they pass on. ORCA turns
  each neighbour into a half-plane that keeps the current side. Its failures: two or three
  half-planes can rule out every velocity (the fallback then picks a least-bad one that may still
  collide), and, worst in games, **cornering**. When the preferred direction changes every frame
  (corners, re-plans), ORCA either refuses to switch sides and sticks, or travels far off the path
  to keep the side.
- Fixes offered: substepping (several avoidance iterations per movement step with positions frozen,
  weights ramped up); time horizons per candidate, including a *cornering plane* beyond the next
  turn whose conflicts are ignored; a penalty for changing a neighbour's passing side; and a
  scenario rig with old variants kept for comparison, including the animation layer.

For us: avoidance as a preference with `keep_apart` as the guarantee is the right split, but the
layers that change the preferred velocity every few ticks are exactly the cornering failure, and
the realized-velocity input is the inconsistent view. **Adapt**: chosen velocities into ORCA,
sidedness memory, the cornering plane; substepping only if flicker remains.

### RORCAL (Gros-Lemesre, "Preventing Collision Avoidance Deadlocks in Multiagent Navigation for Deterministic Simulation")

Local reference copy (preprint). In a deterministic simulation with small time steps there is no
noise to break symmetry, and ORCA deadlocks systematically:

- two agents head-on only slow down, never turn, and stop nose to nose;
- three agents reaching a common point form a *reflex wedge*: inside it the solution is the
  wedge's apex whatever the preferred velocity is, so speed decays to zero or the three drift
  locked together. Smaller steps and slower agents keep the symmetry more reliably.

The method, after the normal solve: if the solution is constrained (slower than preferred), not
speeding up, and in the same direction as the current velocity (within about 5 degrees), turn it by
a fixed angle in one direction shared by all agents, clip it to the constraints, try the other
direction if that leaves less than 10% of top speed, and keep the original otherwise. No
randomness; about 2% extra cost; in the paper's 250-agent test 23% fewer iterations to clear. The
paper is not consistent about the angle (about 5 degrees, pi/20, and pi/18 appear).

For us: our 60 Hz tick, identical speeds within a type, and members sharing exact grid waypoints
are the regime where symmetry persists; members converging on a passage's mouth are the
three-agents case. **Adopt**, replacing the pre-solve keep-right. Note: a unit clipped by
`keep_apart` shows the same "slower, same direction" signature, so the detector will also fire in
contact; check it against giving way.

### ORCA-A* (Chauvin, Gianazza, Durand, SIDs 2024, paper 059)

Local reference copy. A* over static obstacles plans the route; ORCA avoids other agents and steers
to the next waypoint. The route is re-planned only when the next waypoint goes out of sight, never
because avoidance moved the agent off its line. A one-second horizon worked best once static
obstacles were out of ORCA. This is the engine's architecture already; **adapt its re-plan
trigger**.

### ORCA with fuzzy controllers (London, arXiv 2508.06722, 2025)

Each agent's share of the avoidance depends on distance, speed, and acceleration through a fuzzy
controller. In the paper's own results it oscillated near the goal at high speed and did worse than
plain ORCA at low speed. **Not relevant**; it supports keeping the shares fixed by size and side, as
the engine does.

### "Group Pathfinding & Movement in RTS Style Games" (Erridge, Game Developer, 2018)

From the RTS *The Maestros*:

- **A robust group center**: average positions, drop members beyond one standard deviation,
  average again. Outliers go alone and join; a group spread wider than its members' radii is not a
  formation.
- **Corners shifted by the group's width**: offsetting one shared path per member puts offsets in
  rocks, so each corner of the shared route is moved away from the nearer obstacle by a little more
  than the group's spread before the offsets apply. The author reports this removed about 90% of
  traffic jams.
- Pushing instead of routing around allies, and "touching an arrived unit means arrived", grew into
  about ten nested rules; the author names StarCraft II's step aside and return as the cleaner
  alternative.

For us: **adapt** the corner shift (with our clearance values instead of probes; where clearance
does not allow the shift is where the frame must narrow) and the robust center (forming the group
and its stragglers). Skip pushing and transitive arrival.

### rts-path-finding (github.com/guinzoo/rts-path-finding, MIT; code read, nothing copied)

A long path on the grid (A*, JPS, JPS+), then a short precise path to a point ahead on it. The
precise phase follows obstructions from the first contact in two branches, one keeping it on the
left and one on the right; **a branch never changes side**. It has a hard budget of 20 expansions
and falls back to the best point found. The README attributes the long and short split to Dota 2.

For us: **adapt** both ideas: a committed side per obstruction (the cure for turning left and
right), and a bounded short planner toward a point ahead on the long route.

### Dota 2 pathfinding (Liquipedia, refused; secondary sources, unverified)

Units have standard hull sizes. Reportedly every move uses two pathers: a long one over the static
grid only, and a short one that routes around units and dynamic blockers to a point on the long
path; the long path never changes because of units. **Adapt** the split: the long route changes
only for an order or a static ground change, and one short planner owns everything about other
units.

### Group-Movement (ap011y0n.github.io, a student project)

A survey (StarCraft, Age of Empires, Dawn of War, StarCraft II) and a flocking implementation
(path, separation, and cohesion with a dead zone, summed with weights). It names three group speed
constraints of rising difficulty; the last, **the same arrival time** (leaders slow, the last are
allowed to go faster), is what the owner saw in the original. It gives no algorithm. **Adapt** the
constraint as a continuous speed law instead of the frame's hold.

### Real-time navmesh editing (80.lv, HŌRU)

A showcase of an Unreal navmesh edited with boolean layers over a generated base. No data
structures, method, or timings. The idea of edits over a fixed base matches our open item of
rebuilding only the chunks a stamp touches; it adds no method.

### Slot assignment (Wikipedia, "Hungarian algorithm"; Stack Overflow 76444431 refused)

Pairing n members with n slots at least total cost is the linear assignment problem. The
shortest-augmenting-path Hungarian algorithm with potentials solves it optimally in O(n^3), with a
proof of optimality. **(knowledge)** Jonker-Volgenant is the same core with faster start-up, the
auction algorithm suits parallel and sparse cases, greedy is fast but can strand a member far from
its slot, and greedy with pairwise swaps reaches a local optimum.

- Cost: with summed Euclidean distance an optimal pairing never has two crossing straight paths
  (swapping would be shorter). Summed **squared** distance penalizes long individual walks, so
  arrival times even out, and in practice paths stay parallel **(knowledge)**; it matches "the
  nearest tanks take the front".
- Determinism: integer costs (positions quantized, squared into 64 bits), members in id order and
  slots in index order, ties to the lowest of each; store the result in hashed state.
- Re-assigning mid-move needs a margin (keep the old pairing unless the new is better by it), or
  members swap back and forth.
- At n = 100 the cost is in the order of a millisecond or less **(knowledge, not measured)**, paid
  only at events: an order, a frame starting, widening after a passage, arrival.

For us: **adopt** at events, one solve per footprint class. Our current rule (by forward position,
then left to right) is a good heuristic when members already stand in the target's shape and
crosses paths when they approach from the side.

### Flow fields (Emerson, Game AI Pro ch. 23, Supreme Commander 2; jdxdev 2020; Sanctuary: Shattered Sun; bjkarasek/flowfield-unity, MIT, README only; gamedev.stackexchange 387 refused)

Per sector a cost field, an integration field (cost to the goal), and a flow field (a direction per
cell). Routes over sector portals by A*, with *merging A** so units of one order reuse portals; a
line-of-sight pass near the goal; tiles cached by portal; one cost field and portal graph per
movement type, with walls widened for large units; a blur that adds cost near walls; dirty sectors
rebuilt under a per-tick cap.

- A field has no local minima and units never re-plan, so the push, re-plan, pushed back loop
  disappears; units on the ridge between two equal ways around an obstacle can still flip.
- Members spread over a corridor rather than aiming at one point, but jdxdev saw about 100 units
  at a corner fall into single file anyway and dropped fields for formations.
- Fields pay off when many units share one goal; per unit, or for one unit, A* is cheaper.

For us: **adopt the ideas, not the system**, and not for formation moves: follow the route as a line
with each member's lateral offset rather than steering at its points; never re-plan for an ally's
push; merging on the chunk graph so parts of one selection share corridors; rebuild budgets as fixed
cell counts, never time. A field may be measured later for very large same-goal orders of small
units. (The engine already adds cost near walls.)

### Flocking, for keeping and re-forming formations (inference from the sources above)

- Hold members to their **slots**, not to the group's centroid (centroid cohesion fights the
  shape).
- Add the frame's own velocity to slot following, so a member in its slot simply moves with the
  frame instead of lagging, sprinting, and overshooting; slow down on approach; leave a small dead
  zone at the slot.
- Slow the frame smoothly with its worst member's lag instead of holding and going.
- In passages, place column slots **along the route line** (member k a fixed spacing behind member
  k - 1), not as offsets turned with the frame, which swing through walls at corners; afterwards
  widen rank by rank, front first, with one re-assignment.

## Recommendations

The sources agree on the main point: too many mechanisms change a unit's route, each from fresh
state, with no one in charge. Shipped games use one long route over static ground and one short,
budgeted planner for everything about other units.

**Route changes (problem 2)**

1. Never re-plan because an ally pushed a unit; re-plan when the next waypoint is out of reach, with
   a delay.
2. Commit to a side of an obstruction (in hashed state); change sides only when that side is
   blocked.
3. At most one route-changing event per unit every few ticks, with a fixed priority between the
   mechanisms.
4. Structural, if 1 to 3 are not enough: one short planner toward a point ahead on the long route,
   on a fixed cadence and budget, absorbing detours, repair legs, and re-plans.

**Avoidance**

5. Feed ORCA the velocities units chose, not the realized ones.
6. RORCAL's symmetry breaker after the solve, replacing the keep-right rule.

**The formation layer (problems 1 and 3)**

7. Columns in passages with one pass order (the members nearest the mouth first), slots along the
   route, re-forming rank by rank after.
8. A continuous speed law: leaders slow and the last catch up, instead of holding.
9. Slot assignment by the Hungarian algorithm on squared distances, at events.
10. Corners of the frame route shifted by the frame's width; where clearance does not allow it, the
    frame narrows.
11. Splitting a selection into formations as the original does, once the owner's observations of
    6, 7, and 15 tanks settle the rule.

**Later**: rebuilding only the chunks a stamp touches; flow fields for very large same-goal orders.

## Order of work (agreed with the owner, 2026-11-17)

1. **Measure first.** A headless count of route changes, heading reversals, and stalls per unit,
   with small scenarios (a passage, a head-on pair, three units converging) beside the existing
   battles. The numbers of the baseline below are the reference every change is compared with.
2. **Cheap fixes**: recommendations 1, 2, and 5 together; then 6.
3. **The formation layer**: 7 and 8, then 9 and 10.
4. **Recommendation 4** only if churn remains.

## Baseline

The state before this work is tagged `baseline-2026-11-17` in the repository, so it can be
compared with and returned to. Its battle hashes are those of `scripts/check-battle.ps1` at that
tag. The measurements (step 1, [tools.md](tools.md#movement-measurements)) were added without
changing movement (battle hashes unchanged), so these are the baseline's numbers:

| Scenario (`movement-scenarios`) | Time | Reversals | Stalled of moving (unit-s) | Route changes (plan, repair, detour, frame) | Blocked |
| --- | --- | --- | --- | --- | --- |
| Ten tanks through a gap, formation | 33.8 s | 649 | 35.6 of 284.0 | 6, 46, 2, 1 | 6 |
| Ten tanks through a gap, group move | 18.9 s | 243 | 37.8 of 121.7 | 11, 38, 1, 0 | 1 |
| Head-on pair | 4.9 s | 4 | 0.0 of 9.2 | 2, 0, 0, 0 | 0 |
| Three converging | 4.4 s | 12 | 0.1 of 13.2 | 3, 0, 0, 0 | 0 |
| Two groups of five crossing | 31.7 s | 537 | 72.3 of 292.6 | 6, 0, 0, 2 | 0 |

| Battle on Utapau, auto-move (`battle`) | Reversals | Stalled of moving (unit-s) | Route changes (plan, repair, detour) | Blocked |
| --- | --- | --- | --- | --- |
| Reference (2 squads, 3 T2-Bs; 3,000 ticks) | 141 | 3.5 of 20.6 | 12, 0, 0 | 3 |
| Large (160 squads, 60 T2-Bs, 40 AT-STs; 1,800 ticks) | 55,439 | 3,925 of 8,860 | 1,728, 518, 94 | 1,371 |
| AT-ATs (4 AT-ATs, 6 squads, 4 T2-Bs; 3,600 ticks) | 328 | 37.4 of 86.9 | 30, 2, 1 | 4 |
| Ten T2-Bs (3,600 ticks) | 351 | 13.2 of 33.3 | 10, 0, 0 | 0 |

No shared routes or frames appear in the battles: auto-move gives a group move.

What the numbers show:

- Two groups of five crossing need 31.7 s for what alone takes about 5 s, a quarter of it stalled;
  ten tanks through a gap take 33.8 s as a formation against 18.9 s as a group move.
- Reversals are frequent wherever units meet: about 2.3 a tank each second moving through the gap,
  and about 10 in the ten-T2-B battle. A probe of one T2-B there (not kept) showed the owner's
  deadlock: from its 137th tick it stood at speed 0 while its heading flipped 10 degrees back and
  forth every few ticks; before that, moving, its heading also jumped 10 degrees back and forth.
- The symmetric cases (head-on, three converging) resolve in this engine; the losses are in groups.

## The movement-redesign branch

The owner judged the combined fixes still bad to look at (units bumping into each other, headings
swinging by 10 degrees all the time) and chose to redesign movement as a whole instead of fixing one
issue at a time (2026-11-18). The branch `movement-redesign` keeps route planning, clearance, and
footprints, and the hard no-overlap rule between units
([pathfinding-design.md](pathfinding-design.md#the-movement-redesign-branch)). Its measurements, the
floor a new design starts from (units that meet stop at contact and stay held):

| Scenario | Time | Reversals | Stalled of moving (unit-s) |
| --- | --- | --- | --- |
| Ten tanks through a gap (formation and group move alike) | 60 s, 9 left moving | 3 | 497.5 of 544.2 |
| Head-on pair | 60 s, both left moving | 0 | 116.1 of 120.0 |
| Three converging | 7.6 s | 0 | 1.6 of 22.1 |
| Two groups of five crossing | 60 s, 10 left moving | 0 | 577.3 of 600.0 |

| Battle on Utapau, auto-move | Tick | Reversals | Stalled of moving (unit-s) |
| --- | --- | --- | --- |
| Ten T2-Bs | 0.40 ms | 4 | 529.7 of 540.3 |
| Large | 2.78 ms | 154 | 7,353 of 10,095 |
| AT-ATs | 0.45 ms | 9 | 576.2 of 640.1 |
| Reference | 0.39 ms | 3 | 232.8 of 241.8 |

With the owner's single-unit movement built (steps 1 to 8,
[pathfinding-design.md](pathfinding-design.md#the-movement-redesign-branch); 2026-11-18). "Stopped"
counts the moves ended at a unit: in these scenarios and battles mostly units of one order meeting
at their common point (the battles spawn their groups around the map's middle and send them there,
so trips are short):

| Scenario | Time | Reversals | Stalled of moving (unit-s) | Around | Stopped |
| --- | --- | --- | --- | --- | --- |
| Ten tanks through a gap (formation and group move alike) | 4.3 s | 0 | 4.9 of 19.4 | 0 | 9 |
| Head-on pair | 5.6 s | 5 | 0.6 of 11.0 | 6 | 0 |
| Three converging | 7.0 s | 15 | 0.9 of 19.1 | 11 | 0 |
| Two groups of five crossing | 7.3 s | 0 | 0.9 of 25.9 | 2 | 9 |

| Battle on Utapau, auto-move | Tick | Reversals | Stalled of moving (unit-s) | Around | Stopped |
| --- | --- | --- | --- | --- | --- |
| Ten T2-Bs | 0.41 ms | 1 | 3.0 of 6.2 | 0 | 9 |
| Large | 1.61 ms | 1 | 1,028.5 of 1,356.9 | 0 | 442 |
| AT-ATs | 0.45 ms | 0 | 16.1 of 35.3 | 0 | 17 |
| Reference | 0.42 ms | 0 | 2.4 of 8.5 | 0 | 6 |

Tried and left out: units of one order going around each other like units of different orders
(their step 8 for all). It changed none of the scenarios but crossing groups (9.2 s, 125 ways
around) and made the large battle worse (9,619 ways around, stalled 2,841 of 3,574 unit-s, 2.15 ms a
tick).

With the owner's rule for the searches around units (2026-11-18: every unit that blocks and is not
of the same order is an obstacle, moving or standing; the second search to the destination around
them), the scenarios change to three converging 6.2 s (4 reversals, 5 ways around) and crossing
groups 2.2 s with all ten moves ended halfway (6 ways around, 1 reversal): the ways around lead tanks
into their own group's tanks, and two of one order blocking each other both end their moves. With
units of the same order as obstacles as well, crossing groups take 7.3 s (2 ways around, the 9 stops
arrivals). The passage, the head-on pair, and the four battles do not change (the battles need no
way around).

The group order's baseline (the owner's rules only, tag `baseline-2026-11-19`):

| Scenario or battle | Time or tick | Ways around | Frozen | Note |
| --- | --- | --- | --- | --- |
| Ten tanks through a gap (formation) | 60 s, 5 left moving | 3,512 | 5 | frozen at the gap |
| Head-on pair | 5.6 s | 6 | 0 | |
| Three converging | 6.2 s | 5 | 0 | |
| Two groups of five crossing | 13.7 s | 133 | 0 | |
| Utapau, six T2-Bs, auto-move | 0.41 ms | 3,599 | 2 | |
| Utapau, four T4-Bs, auto-move | 0.46 ms | 3,241 | 1 | |
| Utapau, ten T2-Bs, auto-move | 0.53 ms | 1 | 0 | |
| Utapau AT-ATs | 0.57 ms | 3,597 | 1 | |
| Large Utapau | 2.23 ms | 11,847 | 16 | |
