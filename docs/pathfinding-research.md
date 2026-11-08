# Pathfinding and group movement research

This note collects published techniques and what shipped real-time strategy
games are reported to use, and derives a staged plan for EAWEngine. The goal is
movement that is at least as good as the original's and stays cheap in large
battles, while the simulation stays deterministic.

Confidence varies by source. Peer-reviewed papers and developer-written articles
are strong evidence. Forum posts and second-hand summaries of talks are marked as
such. Nothing here is evidence about the original game's own pathfinder; see the
pathfinding entry in [compatibility-research.md](compatibility-research.md).

## Where EAWEngine stands

`GridPathfinder` (`src/simulation/pathfinding.*`) works as follows:

- It runs A* over the TED passability cells, using each unit's movement-class
  mask.
- The grid is 8-connected, with integer costs and no corner cutting.
- Ties are fully ordered, so the result is deterministic.
- A line-of-sight pass then smooths the path.
- Connected regions are labeled once per mask, so an unreachable goal costs
  almost nothing.

In Release, a path across a map expands about 7,000 nodes in about 1 ms. A
15-unit order across the Alderaan map costs about 18 ms in one tick. That is
correct but does not scale to large armies, and it ignores:

- unit size;
- other units;
- formations;
- runtime walls and shields.

## Why A* alone is not the answer for large battles

A* returns optimal paths when its distance estimate never overestimates, and
ours never does. The problem is not path quality. It is the cost of searching
the full grid once per unit, and the fact that a path is only one layer of
movement. Every modern RTS design separates the work into layers:

1. **Global route:** the cheapest path across the map for a movement class
   and size.
2. **Local path:** an exact path within a short horizon, refreshed as the
   unit moves.
3. **Group and formation layer:** a group shares one route, and members keep
   slots relative to a leader or a formation frame.
4. **Steering and local avoidance:** units avoid each other and small dynamic
   obstacles without replanning.

## Techniques

### Speeding up grid search

**Jump Point Search (JPS)**
- *Sources:* Harabor and Grastien,
  [Online Graph Pruning for Pathfinding on Grid Maps](https://ojs.aaai.org/index.php/AAAI/article/view/7994)
  (AAAI 2011);
  [The JPS Pathfinding System](https://ojs.aaai.org/index.php/SOCS/article/view/18254)
  (SoCS 2012), which adds a no-corner-cutting variant and the preprocessed
  JPS+;
  [Improving Jump Point Search](https://ojs.aaai.org/index.php/ICAPS/article/view/13633)
  (ICAPS 2014).
- *Result:* it skips straight runs of open cells, often making search an
  order of magnitude faster. It stays optimal and needs no extra memory.
- *Fit:* uniform-cost grids only, which our binary passability is.

**JPS+ with goal bounding**
- *Sources:* Rabin and Sturtevant,
  [Combining Bounding Boxes and JPS to Prune Grid Pathfinding](https://mlanthology.org/aaai/2016/rabin2016aaai-combining)
  (AAAI 2016); Rabin's GDC 2015 AI Summit talk "JPS+: Over 100x Faster than
  A*" ([reference code](https://github.com/kongnanlive/JPSPlusWithGoalBounding)).
- *Result:* about 200 times faster than A* on competition maps.
- *Cost:* the precomputation is quadratic, so it is only for static maps.
  Runtime walls and shields would rule it out.

### Hierarchy

**HPA\***
- *Source:* Botea, Müller, and Schaeffer,
  [Near Optimal Hierarchical Path-Finding](https://webdocs.cs.ualberta.ca/~mmueller/ps/hpastar.pdf)
  (2004).
- *How it works:* the map is split into clusters, and crossing costs between
  cluster entrances are precomputed.
- *Result:* up to 10 times faster than optimized A*. After smoothing, paths
  are within 1% of optimal.
- *Dynamic changes:* only the affected clusters are rebuilt.

**HAA\* (Hierarchical Annotated A\*)**
- *Sources:* Harabor and Botea,
  [Hierarchical path planning for multi-size agents in heterogeneous environments](https://researchportalplus.anu.edu.au/en/publications/hierarchical-path-planning-for-multi-size-agents-in-heterogeneous/)
  (CIG 2008); the
  [clearance-based pathfinding](https://users.cecs.anu.edu.au/~dharabor/pathfinding.html)
  article.
- *How it works:* each cell stores a clearance value (how big a unit fits
  there) per terrain capability. One abstract graph then serves every unit
  size and movement class.
- *Why it matters here:* this matches our problem directly. Movement classes
  are terrain capabilities, and an AT-AT is not an infantryman.
- *Known pitfall:* clearance depends on how a unit's footprint is anchored
  ([discussion](https://backiee.wasmer.app/http_gamedev_stackexchange_com/questions/53492/pathfinding-with-different-sized-units)).

**Factorio's abstract chunks**
- *Source:* [Friday Facts #317](https://cf-www.factorio.com/blog/post/fff-317)
  (2019).
- *How it works:* each connected component of a 32×32 chunk becomes an
  abstract node. The abstract search only supplies the heuristic to the exact
  search, so the exact search may leave the abstract route.
- *Why it's useful:* it is simple, robust, and fixes the "army walks around a
  lake" case.

**0 A.D. (Pyrogenesis)**
- *Sources:*
  [pathfinder design document](https://svn.science.uu.nl/repos/edu.3803627.INFOMOV/0AD/docs/pathfinder.pdf);
  [2015 rewrite coverage](https://gamingonlinux.com/2015/06/0-ad-open-source-strategy-game-has-a-new-faster-pathfinder).
- *How it works:* a long-range A* with JPS over navigation cells, a
  hierarchical connectivity layer that only answers "is it reachable", and a
  short-range pathfinder for precise local paths between long-range
  waypoints.
- *Why it's useful:* it is an open-source RTS with deterministic lockstep, so
  it is close to our requirements.

### Groups and crowds

**Flow field tiles**
- *Sources:* Elijah Emerson,
  [Crowd Pathfinding and Steering Using Flow Field Tiles](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter23_Crowd_Pathfinding_and_Steering_Using_Flow_Field_Tiles.pdf)
  (Game AI Pro; Supreme Commander 2 and Planetary Annihilation). Developer
  forum posts describe cost, integration, and flow fields
  ([Planetary Annihilation forums](https://forums.planetaryannihilation.com/posts/697783);
  [PC Gamer](https://pcgamer.com/planetary-annihilation-devs-show-planet-creation-tech-clever-unit-pathfinding)).
- *How it works:* one field per destination, generated only for the tiles a
  coarse route passes through. Any number of units then read their direction
  from it.
- *Fit:* the right tool for large groups with one goal. It is wasteful for
  many units with distinct goals.

**Continuum Crowds**
- *Source:* Treuille, Cooper, and Popović,
  [Continuum Crowds](https://grail.cs.washington.edu/projects/continuum-crowds)
  (SIGGRAPH 2006).
- *How it works:* dynamic potential fields combine global planning and local
  avoidance.
- *Result:* more than 10,000 agents at interactive rates.
- *Fit:* the theoretical basis for flow fields, but heavier than RTS games
  usually need.

**StarCraft II** (second-hand)
- *Sources:* summaries of Anhalt, Kring, and Sturtevant, "AI Navigation: It's
  Not a Solved Problem – Yet" (GDC 2011);
  [GameDev.net thread](https://www.gamedev.net/topic/624135-does-starcraft2-use-navmesh-a-or-grid-a-for-pathfinding/).
- *Reported design:* A* on a constrained-Delaunay navigation mesh of terrain
  and buildings, a funnel pass, then Boids-style steering for units. Units are
  not part of the mesh.

**Company of Heroes (Relic)**
- *Sources:* Relic says CoH3 runs on a grid with A* to choose the squares to
  follow
  ([1.6.0 pathfinding post](https://coh3.companyofheroes.com/1-6-0-pathfinding)).
  Chris Jurney's article
  [Company of Heroes Squad Formations Explained](https://forum.arongranberg.com/uploads/short-url/rWod3K2KhNWcOEdjewnsLXQKU6A.pdf)
  describes the formation layer.
- *How squads move:* players order squads, not soldiers. The formation is
  chosen from terrain (wide wedge in the open, tight wedge in confined space,
  staggered column on roads).
- *Lesson:* believable group motion is a formation and squad layer above the
  path, not a better path search.

### Local avoidance

**ORCA / RVO2**
- *Sources:* van den Berg, Guy, Lin, and Manocha, "Reciprocal n-body collision
  avoidance" (2011);
  [ORCA project](https://gamma.cs.unc.edu/ORCA/);
  [RVO2 reference library](https://github.com/snape/rvo2).
- *How it works:* each unit takes half of the responsibility for avoiding
  each neighbor, solved as a small linear program. Motion is smooth, and it
  avoids the jitter and deadlock of summed separation forces.
- *Fit:* it sits under any global navigator.
- *Determinism:* this needs care. Neighbor sets must be gathered in a fixed
  order, and floating-point results are only reproducible within one build.

### Determinism

- Age of Empires' lockstep ("1500 Archers on a 28.8", GDC 2001) and later
  developer discussion stress that pathfinding is the hardest system to keep
  deterministic, especially when threaded
  ([GameDev.net discussion](https://gamedev.net/forums/topic/419969-wargamerts-network-architecture-a-danger-of-lock-step-commands-synchronization/)).
- Our rules already cover this:
  - integer costs;
  - ordered ties;
  - searches as pure functions of the immutable ground and the request;
  - results applied at tick boundaries in command order.

## Plan for EAWEngine

The order follows what large battles need first. Each stage keeps the current
contract: a deterministic plan applied at a tick boundary, and results
independent of thread count.

1. **Measurement first.** Add a pathfinding audit to `EAWAssetInspector`:
   - random but seeded start and goal pairs per stock land map and movement
     class;
   - expansions, time, and path length reported against the A* baseline.

   Every later stage must beat this baseline on the same data and must return
   paths of the same length when it claims optimality.
2. **Unit size.** Compute clearance per movement-class mask, HAA\*-style, and
   take each type's footprint from its XML. Large walkers then stop squeezing
   through infantry gaps.
3. **Faster exact search.** Use JPS (online, no preprocessing, so runtime
   walls and shields stay possible) as the low-level search, with the
   existing A* kept as the reference implementation in tests.
4. **Hierarchy.** Build an HPA\*-style cluster graph per mask and clearance,
   rebuilt per cluster when walls, shields, or buildings change. Long routes
   are planned on the graph and refined lazily in a short window ahead of the
   unit. The region labels already in place become the cluster
   connectivity.
5. **Group orders.** When many units share a destination, generate flow field
   tiles along the hierarchical route instead of one path per unit. Squads
   and formations follow a leader or formation frame, with the formation
   chosen from terrain width, Company of Heroes-style. Small orders keep
   individual paths.
6. **Local avoidance.** Use ORCA-style reciprocal avoidance between units,
   with neighbors from the spatial index in a fixed order. Units are never
   part of the path graph.
7. **Scheduling.** Searches become jobs on the dependency-aware job system,
   with a per-tick budget. A unit whose plan is not ready keeps its previous
   intent, and plans are committed in command order, so delay is
   deterministic.
8. **Space.** Space maps are mostly open. Steering and obstacle avoidance
   around asteroids, debris, and capital ships come first, with a coarse
   graph only where measurements show it is needed.

Black-box comparison with the original stays on the list for each stage:

- which ground each class may cross;
- how unreachable orders end;
- how groups spread out and form up.

Those are the behaviors players notice.

## Group movement and formations

How shipped games and published sources move groups. This is the basis for
EAWEngine's group movement, which is built in steps.

- **Age of Empires II.** Dave Pottinger, "Implementing Coordinated Movement",
  Game Developer, 1999
  ([article](https://www.gamedeveloper.com/programming/implementing-coordinated-movement)).
  - A formation stores an orientation and slots (offset, orientation, unit
    type) and is filled from the inside out; "a formation can't move until
    it's formed".
  - The group has a commander that pathfinds while the others follow, and it
    moves at its slowest unit's speed.
  - Around obstacles the formation scales its slots, breaks and re-forms
    past the obstacle, or halves and rejoins.
  - Units pushed aside by priority make way for the group; "unit overlap is
    unavoidable or, at best, incredibly difficult to prevent in all cases".
  - The [openage notes](https://simonsan.github.io/openage-webdocs/sphinx/doc/reverse_engineering/game_mechanics/formations.html)
    record the same slowest-unit speed rule.
- **StarCraft II** (secondhand; no primary Blizzard source found).
  - A group order keeps the units' relative positions when they lie within a
    fixed "magic box"
    ([Liquipedia](https://liquipedia.net/starcraft/Magic_Boxes)).
  - Overlapping separation radii push units apart
    ([TL.net](https://tl.net/forum/starcraft-2-hots/377527-no-fix-to-clumpy-unit-movement)).
  - The [AIIDE paper on StarCraft unit motion](https://ojs.aaai.org/index.php/AIIDE/article/view/12811)
    analyses the original game.
- **0 A.D.** An invisible formation controller moves like a unit and members
  hold offsets from it; a motion manager handles pushing between units and
  treats members of one formation specially
  ([forum](https://wildfiregames.com/forum/topic/44848-proposals-for-formations);
  [CCmpUnitMotionManager.h](https://sources.debian.org/src/0ad/0.27.1-3/source/simulation2/components/CCmpUnitMotionManager.h)).
- **Company of Heroes.** Squads take a formation chosen by terrain: a wide
  wedge in the open, a tight wedge in confined space, a staggered column on
  roads
  ([Jurney](https://forum.arongranberg.com/uploads/short-url/rWod3K2KhNWcOEdjewnsLXQKU6A.pdf)).
- **Steering.** Reynolds-style separation, cohesion, and alignment
  ([HowToRTS](https://howtorts.github.io/2014/01/03/steering-flocking.html)).
  Hierarchical slots (fire team, squad, platoon) suit large groups
  ([Georgia Tech lecture](https://faculty.cc.gatech.edu/~surban6/2018sp-gameAI/lectures/2018_02_08_formations.pdf)).

The stock XML describes the original's model directly:

- per-unit `FormationOrder` (rank), `FormationSpacing`, `FormationRaggedness`,
  `FormationGrouping` (Standard or Solo), and `WaitsForFormationFormup`;
- `UnitCollisionClass` (Infantry, Vehicle, Large Vehicle, Giant Vehicle, and
  obstacle classes);
- companies listing their units (`Company_Units`, `Max_Squad_Size`).

EAWEngine's steps:

1. **Footprints, separation, and turn rates** (done).
   - Units push apart by their footprint, deterministically.
   - Immobile entities are never pushed, idle units yield to moving ones, and
     nobody is pushed onto ground they may not enter.
   - A unit pushed off its route plans again.
   - Units turn at their XML rate and slow down while facing away.
2. **Formation slots for group orders** (done).
   - Ranks follow `FormationOrder`, spacing follows footprints and
     `FormationSpacing`, and units are assigned to slots deterministically.
   - The group moves at its slowest member's speed.
3. **Company expansion** (done). Placed and spawned companies become their
   units, in formation. This also applies when the company names a model of
   its own, as most stock companies do.
4. **Later.**
   - A formation moving as one body (a controller with one shared path, as
     in Age of Empires II and 0 A.D.), breaking and re-forming at narrow
     passages.
   - Terrain-dependent shapes, raggedness, and form-up waiting.
   - Reciprocal avoidance between moving units (see above).
