# Pathfinding and group movement research

Published techniques, and what shipped real-time strategy games are reported to use, for route
planning, group movement, and local avoidance. The goal is movement at least as good as the
original's that stays cheap in large battles while the simulation stays deterministic. The design
chosen from this research is [pathfinding-design.md](pathfinding-design.md); what is built is in
[status.md](status.md). Research into the queues, deadlocks, and oscillation of group moves (2026-11), with
its sources and recommendations, is [group-movement-research.md](group-movement-research.md).

Confidence varies by source. Peer-reviewed papers and developer-written articles are strong
evidence. Forum posts and second-hand summaries of talks are marked as such. Nothing here is
evidence about the original game's own pathfinder; see the pathfinding entry in
[compatibility-research.md](compatibility-research.md).

## Why A* alone is not the answer for large battles

A* returns optimal paths when its distance estimate never overestimates, and the engine's never
does. The problem is not path quality. It is the cost of searching the full grid once per unit, and
the fact that a path is only one layer of movement. Every modern RTS design separates the work into
layers:

1. **Global route:** the cheapest path across the map for a movement class and size.
2. **Local path:** an exact path within a short horizon, refreshed as the unit moves.
3. **Group and formation layer:** a group shares one route, and members keep slots relative to a
   leader or a formation frame.
4. **Steering and local avoidance:** units avoid each other and small dynamic obstacles without
   replanning.

## Techniques

### Speeding up grid search

**Jump Point Search (JPS)**
- *Sources:* Harabor and Grastien,
  [Online Graph Pruning for Pathfinding on Grid Maps](https://ojs.aaai.org/index.php/AAAI/article/view/7994)
  (AAAI 2011);
  [The JPS Pathfinding System](https://ojs.aaai.org/index.php/SOCS/article/view/18254) (SoCS 2012),
  which adds a no-corner-cutting variant and the preprocessed JPS+;
  [Improving Jump Point Search](https://ojs.aaai.org/index.php/ICAPS/article/view/13633)
  (ICAPS 2014).
- *Result:* it skips straight runs of open cells, often making search an order of magnitude faster.
  It stays optimal and needs no extra memory.
- *Fit:* uniform-cost grids only. The engine's grid is binary passability, but its searches add a
  cost near walls and work per size bucket (design 4.2), which complicates pruning; not adopted.

**JPS+ with goal bounding**
- *Sources:* Rabin and Sturtevant,
  [Combining Bounding Boxes and JPS to Prune Grid Pathfinding](https://mlanthology.org/aaai/2016/rabin2016aaai-combining)
  (AAAI 2016); Rabin's GDC 2015 AI Summit talk "JPS+: Over 100x Faster than A*"
  ([reference code](https://github.com/kongnanlive/JPSPlusWithGoalBounding)).
- *Result:* about 200 times faster than A* on competition maps.
- *Cost:* the precomputation is quadratic, so it is only for static maps. Runtime walls and shields
  rule it out.

### Hierarchy

**HPA\***
- *Source:* Botea, Müller, and Schaeffer,
  [Near Optimal Hierarchical Path-Finding](https://webdocs.cs.ualberta.ca/~mmueller/ps/hpastar.pdf)
  (2004).
- *How it works:* the map is split into clusters, and crossing costs between cluster entrances are
  precomputed.
- *Result:* up to 10 times faster than optimized A*. After smoothing, paths are within 1% of
  optimal.
- *Dynamic changes:* only the affected clusters are rebuilt.

**HAA\* (Hierarchical Annotated A\*)**
- *Sources:* Harabor and Botea,
  [Hierarchical path planning for multi-size agents in heterogeneous environments](https://researchportalplus.anu.edu.au/en/publications/hierarchical-path-planning-for-multi-size-agents-in-heterogeneous/)
  (CIG 2008); the
  [clearance-based pathfinding](https://users.cecs.anu.edu.au/~dharabor/pathfinding.html) article.
- *How it works:* each cell stores a clearance value (how big a unit fits there) per terrain
  capability. One abstract graph then serves every unit size and movement class.
- *Why it matters here:* it matches the engine's problem directly. Movement classes are terrain
  capabilities, and an AT-AT is not an infantryman. The engine's clearance field follows this idea
  (design 4.1).
- *Known pitfall:* clearance depends on how a unit's footprint is anchored
  ([discussion](https://backiee.wasmer.app/http_gamedev_stackexchange_com/questions/53492/pathfinding-with-different-sized-units)).

**Factorio's abstract chunks**
- *Source:* [Friday Facts #317](https://cf-www.factorio.com/blog/post/fff-317) (2019).
- *How it works:* each connected component of a 32×32 chunk becomes an abstract node. The abstract
  search only supplies the heuristic to the exact search, so the exact search may leave the abstract
  route.
- *Why it's useful:* it is simple, robust, and fixes the "army walks around a lake" case. The
  engine's chunk graph uses the same components but holds the exact search to the corridor instead
  (design 4.1, 4.2).

**0 A.D. (Pyrogenesis)**
- *Sources:*
  [pathfinder design document](https://svn.science.uu.nl/repos/edu.3803627.INFOMOV/0AD/docs/pathfinder.pdf);
  [2015 rewrite coverage](https://gamingonlinux.com/2015/06/0-ad-open-source-strategy-game-has-a-new-faster-pathfinder).
- *How it works:* a long-range A* with JPS over navigation cells, a hierarchical connectivity layer
  that only answers "is it reachable", and a short-range pathfinder for precise local paths between
  long-range waypoints.
- *Why it's useful:* it is an open-source RTS with deterministic lockstep, so it is close to the
  engine's requirements.

### Groups and crowds

**Flow field tiles**
- *Sources:* Elijah Emerson,
  [Crowd Pathfinding and Steering Using Flow Field Tiles](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter23_Crowd_Pathfinding_and_Steering_Using_Flow_Field_Tiles.pdf)
  (Game AI Pro; Supreme Commander 2 and Planetary Annihilation). Developer forum posts describe
  cost, integration, and flow fields
  ([Planetary Annihilation forums](https://forums.planetaryannihilation.com/posts/697783);
  [PC Gamer](https://pcgamer.com/planetary-annihilation-devs-show-planet-creation-tech-clever-unit-pathfinding)).
- *How it works:* one field per destination, generated only for the tiles a coarse route passes
  through. Any number of units then read their direction from it.
- *Fit:* the right tool for large groups with one goal. It is wasteful for many units with distinct
  goals. Not adopted; an option if large group orders measure badly.

**Continuum Crowds**
- *Source:* Treuille, Cooper, and Popović,
  [Continuum Crowds](https://grail.cs.washington.edu/projects/continuum-crowds) (SIGGRAPH 2006).
- *How it works:* dynamic potential fields combine global planning and local avoidance.
- *Result:* more than 10,000 agents at interactive rates.
- *Fit:* the theoretical basis for flow fields, but heavier than RTS games usually need.

**StarCraft II** (second-hand)
- *Sources:* summaries of Anhalt, Kring, and Sturtevant, "AI Navigation: It's Not a Solved Problem –
  Yet" (GDC 2011);
  [GameDev.net thread](https://www.gamedev.net/topic/624135-does-starcraft2-use-navmesh-a-or-grid-a-for-pathfinding/).
- *Reported design:* A* on a constrained-Delaunay navigation mesh of terrain and buildings, a funnel
  pass, then Boids-style steering for units. Units are not part of the mesh.

**Company of Heroes (Relic)**
- *Sources:* Relic says CoH3 runs on a grid with A* to choose the squares to follow
  ([1.6.0 pathfinding post](https://coh3.companyofheroes.com/1-6-0-pathfinding)). Chris Jurney's
  article
  [Company of Heroes Squad Formations Explained](https://forum.arongranberg.com/uploads/short-url/rWod3K2KhNWcOEdjewnsLXQKU6A.pdf)
  describes the formation layer.
- *How squads move:* players order squads, not soldiers. The formation is chosen from terrain (wide
  wedge in the open, tight wedge in confined space, staggered column on roads).
- *Lesson:* believable group motion is a formation and squad layer above the path, not a better
  path search.

### Local avoidance

**ORCA / RVO2**
- *Sources:* van den Berg, Guy, Lin, and Manocha, "Reciprocal n-body collision avoidance" (2011);
  [ORCA project](https://gamma.cs.unc.edu/ORCA/);
  [RVO2 reference library](https://github.com/snape/rvo2).
- *How it works:* each unit takes half of the responsibility for avoiding each neighbor, solved as a
  small linear program. Motion is smooth, and it avoids the jitter and deadlock of summed separation
  forces.
- *Fit:* it sits under any global navigator. The engine uses it for ground avoidance (design 4.4).
- *Determinism:* this needs care. Neighbor sets must be gathered in a fixed order, and
  floating-point results are only reproducible within one build.

### Determinism

- Age of Empires' lockstep ("1500 Archers on a 28.8", GDC 2001) and later developer discussion
  stress that pathfinding is the hardest system to keep deterministic, especially when threaded
  ([GameDev.net discussion](https://gamedev.net/forums/topic/419969-wargamerts-network-architecture-a-danger-of-lock-step-commands-synchronization/)).
- The engine's rules cover this: integer costs; ordered ties; searches as pure functions of the
  immutable ground and the request; results applied at tick boundaries in request order, also when
  searches run in parallel.

## Group movement and formations

How shipped games and published sources move groups.

- **Age of Empires II.** Dave Pottinger, "Implementing Coordinated Movement", Game Developer, 1999
  ([article](https://www.gamedeveloper.com/programming/implementing-coordinated-movement)).
  - A formation stores an orientation and slots (offset, orientation, unit type) and is filled from
    the inside out; "a formation can't move until it's formed".
  - The group has a commander that pathfinds while the others follow, and it moves at its slowest
    unit's speed.
  - Around obstacles the formation scales its slots, breaks and re-forms past the obstacle, or
    halves and rejoins.
  - Units pushed aside by priority make way for the group; "unit overlap is unavoidable or, at best,
    incredibly difficult to prevent in all cases".
  - The [openage notes](https://simonsan.github.io/openage-webdocs/sphinx/doc/reverse_engineering/game_mechanics/formations.html)
    record the same slowest-unit speed rule.
- **StarCraft II** (second-hand; no primary Blizzard source found).
  - A group order keeps the units' relative positions when they lie within a fixed "magic box"
    ([Liquipedia](https://liquipedia.net/starcraft/Magic_Boxes)).
  - Overlapping separation radii push units apart
    ([TL.net](https://tl.net/forum/starcraft-2-hots/377527-no-fix-to-clumpy-unit-movement)).
  - The [AIIDE paper on StarCraft unit motion](https://ojs.aaai.org/index.php/AIIDE/article/view/12811)
    analyses the original game.
- **0 A.D.** An invisible formation controller moves like a unit and members hold offsets from it;
  a motion manager handles pushing between units and treats members of one formation specially
  ([forum](https://wildfiregames.com/forum/topic/44848-proposals-for-formations);
  [CCmpUnitMotionManager.h](https://sources.debian.org/src/0ad/0.27.1-3/source/simulation2/components/CCmpUnitMotionManager.h)).
- **Company of Heroes.** Squads take a formation chosen by terrain: a wide wedge in the open, a
  tight wedge in confined space, a staggered column on roads
  ([Jurney](https://forum.arongranberg.com/uploads/short-url/rWod3K2KhNWcOEdjewnsLXQKU6A.pdf)).
- **Steering.** Reynolds-style separation, cohesion, and alignment
  ([HowToRTS](https://howtorts.github.io/2014/01/03/steering-flocking.html)). Hierarchical slots
  (fire team, squad, platoon) suit large groups
  ([Georgia Tech lecture](https://faculty.cc.gatech.edu/~surban6/2018sp-gameAI/lectures/2018_02_08_formations.pdf)).

The stock XML describes the original's model directly:

- per-unit `FormationOrder` (rank), `FormationSpacing`, `FormationRaggedness`, `FormationGrouping`
  (Standard or Solo), and `WaitsForFormationFormup`;
- `UnitCollisionClass` (Infantry, Vehicle, Large Vehicle, Giant Vehicle, and obstacle classes); the
  stock data shows it does not measure size (design 4.1);
- companies listing their units (`Company_Units`, `Max_Squad_Size`).

## What the engine took

The design ([pathfinding-design.md](pathfinding-design.md)) draws on these sources as follows:

- clearance per movement-class mask, after HAA\* (4.1);
- chunk components in the manner of Factorio and HPA\*, with the exact search held to the chunk
  corridor (4.1, 4.2);
- the commander rule, slowest-unit speed, and route priority of Age of Empires II (4.3, 4.4);
- squads as the commandable unit and formation files in narrow places, after Company of Heroes and
  Age of Empires II (4.3);
- ORCA for local avoidance, with no pushing (4.4), unlike Age of Empires II and StarCraft II;
- searches budgeted per tick and run on the job system, committed in request order (4.2).

Left as options: JPS behind the same interface, tested against the A* kept as the reference; flow
field tiles for large group orders; a coarse graph for space maps, which are mostly open and keep
steering and obstacle avoidance until measurements show a need.

Black-box comparison with the original stays open for each part, for the behaviors players notice:
which ground each class may cross, how unreachable orders end, and how groups spread out and form
up (design section 6).
