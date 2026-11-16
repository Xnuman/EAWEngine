# Roadmap

The plan: where the engine is going and in which order. What is implemented, and how, is in
[status.md](status.md); this file keeps only goals, deliverables, open scope, and order. Update both
in the same change when scope or status changes.

The plan has two parts. **Tracks** are the engine's systems, each with ordered stages. **Milestones**
are integration points: each names the track stages it needs and one deliverable that can be
checked by running a program and an automated test. A track advances when a milestone needs its next
stage, so systems that no single game mode owns (audio, networking, saves) still have a place and a
time. Full campaign compatibility is a long-term engine project, not a renderer swap.

## Milestones

| Milestone | Goal | Status |
| --- | --- | --- |
| M0 | Repository and research discipline | Done |
| M1 | Data bootstrap: every stock format readable through the layered VFS | Done |
| M2 | Legacy-mode renderer complete for stock content | Active |
| M3 | Tactical battle slice | Active |
| M4 | Skirmish | Planned |
| M5 | Galactic conquest and campaign | Planned |
| M6 | Broad compatibility, multiplayer, and SDK | Planned |

### M0: repository and research discipline (done)

The independent-implementation boundary, the read-only installation policy, evidence rules, the
build, tests, and the launcher and engine split.

### M1: data bootstrap (done)

Discovery, the layered VFS, lossless readers for MEG, DAT, DDS, MTD, ALO, ALA, TED, XML (with the
optional eaw-schema), and Lua, each audited against the stock corpus; the asset inspector, DAT
editor, and EAWTools on the same libraries. Audio and video formats are stages of the Data track.

### M2: legacy-mode renderer complete for stock content (active)

The first deliverable (load and inspect a stock unit and a tactical map in the sandbox) is met. M2
closes when the renderer shows stock content with every feature it was authored for.

- Needs: Rendering stages 1 and 2.
- Deliverable: every stock material family and map environment renders with its features; captures
  of a reference set of scenes (space, land, planet, shield, damage states) are checked with
  `compare-images`, and feature ratios are compared with screenshots of the original where they
  exist (Verification track).

### M3: tactical battle slice (active)

A map battle in the sandbox that looks and behaves like a battle and stays deterministic.

- Needs: Simulation stage 1, Scripting stage 1, Rendering stage 1 (damage states and decals, for
  combat presentation).
- Deliverable: a recorded space battle and a land battle, with combat presentation, that replay
  headless to the same state hash, with capture references for both.

### M4: skirmish

Complete skirmish matches against the AI.

- Needs: Simulation stage 2, Scripting stage 2, Data stage 2 (audio data), Audio stages 1 and 2,
  UI stage 1, AI stage 1, Networking stage 1.
- Deliverable: a full land and space skirmish against the AI, from the start to victory, that
  replays to the same hash; the same match runs over the loopback lockstep.

### M5: galactic conquest and campaign

- Needs: Simulation stage 3, Scripting stage 3, Audio stage 3, UI stage 2, AI stage 2,
  Persistence stage 1, Data stage 3 (cinematics).
- Deliverable: one stock campaign playable end to end, saved and loaded across versions.

### M6: broad compatibility, multiplayer, and SDK

- Needs: Networking stages 2 and 3, Scripting stage 4, Tools stage 3, broad mod coverage (the
  Verification track's corpus runs over popular mods).
- Deliverable: popular mods run without edits; multiplayer skirmish over a network; a documented SDK
  for the engine extensions.

## Work order

Set by the owner: the base technical systems and M2's renderer come before combat presentation and
the player layer. Before item 2, and alongside it, the game-object tags existing systems should
honor are worked through tag by tag from [xml-tag-status.md](xml-tag-status.md) (its `gap` and
`partial` rows, family by family; the rendering rows join item 2).

1. Audio, first stage (Data stage 2 and Audio stage 1): done; see status.md.
2. M2's renderer (Rendering stages 1 and 2), with the job system and the entity-component design
   joining it as general engine systems (the owner's order). Done so far: detail levels, damage
   states, depth-sorted transparency, anti-aliasing, fog, shadows, decals, the special materials,
   weather, soft particles, and the enhanced sky and sky lighting (status.md). HDR is postponed to
   the enhanced lighting profile (Rendering stage 5, the owner's decision): it needs linear
   lighting, and the shading works in display space like the original's. What remains of the
   renderer is listed under Rendering stage 1; then look tuning (Rendering stage 2). The steps:
   a. Visual fixes (the line across planets, bright skydomes, tiling water): done. Open: the TED
      water repeat fields are still to be read.
   b. Bugs that need no rework (walker transitions, the T4-B's gun animations, crushing, destroyed
      space hardpoints, the walker locomotion cleanup): done. Open: the T4-B's missile launcher is
      the `ROCKET_ATTACK` unit ability, which waits for the unit abilities (Simulation stage 2);
      shield hits play only the absorb particle, and the shield mesh flash waits for the shield
      abilities.
   c. The entity-component design: done, [ecs-design.md](ecs-design.md) and
      [ecs-research.md](ecs-research.md). Modder-defined component tags only have room in the
      design, not a design of their own yet.
   d. The job system (ecs-design.md section 7) with its first client (pose sampling) and the
      profiling hooks (Tracy): done. Open: a background lane in place of the sandbox's
      `std::async` content builds, once content builds are split into short jobs.
   e. The simulation's migration to entities and components (ecs-design.md section 8). Done: steps
      up to the parallel systems (status.md). Open:
      - the remaining pools (after steps 6 and 7) and the systems not yet on typed views (commands,
        resolution, and removal stay member functions);
      - parallel systems, begun with ground avoidance, batched route searches, and combat's target
        choice and turrets; rebuilding only the navigation chunks a ground change touches waits for
        base building (stock battles reset the navigation data twice in 1,800 ticks, 25 ms in all);
      - a refactoring step last, its scope to be agreed with the owner;
      - the deferred determinism work folded in: stable-id order, keyed random draws, a full-state
        hash.
      Each step keeps the battle hash unchanged unless declared.
   f. Real squads (step 1 of [pathfinding-design.md](pathfinding-design.md)): done.
   g. Pathfinding, group movement, and collisions ([pathfinding-design.md](pathfinding-design.md),
      its order of work in section 5). Done: the audit, the footprint grid, clearance, chunks, ORCA
      avoidance with allies giving way, detours and route repair for held units, formation frames
      (status.md). Open:
      - sharing one footprint index between avoidance, contact, behavior, and combat (four grids
        are built each tick; pathfinding-design.md 4.5);
      - refining the rest of a route as a unit advances, and rebuilding only changed chunks;
      - companies queueing at narrow places.
      Enemies count for planning when seen; every enemy until fog of war exists. The measurements
      behind it (separation's broad phase sized by the largest unit, 63 candidates a query against
      8 with infantry only; about 13 overlaps a unit; the opening tick's 5,062 target searches,
      38-47 ms with 3,300 entities) are in its section 1.
   h. Enhanced water as an ocean on ocean maps (Kamino, Manaan): an FFT ocean with sea-state
      sliders and an ocean-maps/on/off setting (status.md). The owner judges
      the ocean far from done. Parked with waves off by default after stages 1-3 of water-research.md,
      "The ocean plan" (the owner): it resumes with new research, together with Rendering stage 5.
   This closes M2.
3. Input bindings (Platform and input stage 2).
4. Simulation stage 1's remainder: combat presentation and fidelity (closes M3 with M2).
5. Scripting stage 1's remaining calls as their systems arrive; then M4: the player layer
   (Simulation stage 2; `Spawn_Unit`, `Find_Player`, and `Find_Nearest` follow it), the HUD, the
   rest of audio, and AI.

## Tracks

### Data

1. Done: VFS, MEG, and the M1 formats; the game asset layer with loaders and the resource manager.
2. Audio data: done for effects and responses (the sound archives, WAV, the sound-event catalog,
   `sound-audit`). MP3 for music and speech comes with Audio stage 3 (a decoder is a new dependency
   or a reader of our own; the owner decides).
3. Cinematics: survey the movie format and decide between a reader and a pinned decoder (a new
   dependency, so the owner decides).
4. Steam Workshop mod discovery and mod-stack diagnostics (which layer wins, what is missing).
5. Resource IDs (generation-checked handles), once a consumer needs references that survive a
   reload: hot-swapping one resource in a running scene, scripts, saves, GPU residency.

### Rendering

1. Legacy-mode features content depends on. Done: damage-state (`_ALT<n>`) and LOD switching,
   depth-sorted transparency, fog, shadows, runtime decals, the special materials, weather, and
   soft particles (status.md). Shadows are stable cascaded shadow maps from each map's key light,
   land and space alike, with every model casting (the owner's choice over the original's stencil
   shadow volumes and per-model shadow meshes, an implementation artifact, for quality); skies,
   backdrops, markers, blended parts, and particles do not cast. Open:
   - shadows: per-cascade culling, cached distant cascades, blob shadows at low detail, PCSS as an
     enhanced option;
   - decals: the one stock map with placed decals (no texture link established yet); screen-space
     decals if the renderer becomes deferred;
   - water: the TED water fields 0x17-0x22 (the owner will read their labels and Mon Calamari 02's
     values in the map editor); live tuning of the enhanced water's constants (clarity, shore fade,
     foam depth and strength, refraction and reflection distortion, glint) as sliders in the
     Graphics panel, so the owner can tune them by eye and the chosen values become the defaults;
   - the distance fade's defaults (5,000 to 7,000 units, guesses), for the owner to tune with the
     Graphics panel's sliders;
   - heat distortion: shockwave rings as an enhanced option (heat particles and meshes are done);
   - shields: the FoC stealth meshes' visibility (when the original shows them), and reading
     `Shield_Flash_*` from GameConstants;
   - weather: the fog plane, the weather cube's size, fade-out distance, and fade; tiles that
     appear while panning filling from the top; weather kinds in gameplay through
     WeatherModifiers.xml, with the timeline then on the battle's ticks; the placement is an engine
     choice for the owner to check;
   - sky: the sun disk, star intensity, and an atmosphere, after HDR; volumetric fog for light
     shafts and smoke later (the owner agreeing);
   - global illumination: objects do not occlude the sky yet (buildings and cliffs as models; a
     screen-space or per-object term later); bounced light once lighting is linear (HDR).
2. Modern presentation without an original counterpart, chosen freely: anti-aliasing (done), HDR
   with tone mapping beside the existing bloom (postponed to stage 5), and look tuning against the
   owner's screenshots (hotter engine-glow cores, more contrast on hulls).
3. Scale: per-asset geometry handles and several resident scenes, GPU-driven culling and indirect
   drawing, mesh LODs, clustered lighting, texture compression and streaming budgets. To try (the
   owner's request): a LOD cross-fade driven by the selection's position within a border band
   around each level boundary instead of by time, so the blend follows the camera and holds while
   it stands still; compare it with the current timed fade (`VisualLodTracker`).
4. A thin rendering hardware interface extracted from the Direct3D 12 backend, then Vulkan. Done
   toward it: the contract split by subject, `eaw_render_core` (detail levels, resource lifetime,
   the frame graph's barrier planning, no graphics API), and the frame graph in the D3D12 backend.
   Open with it: generation-checked mesh and texture handles in the contract (the handle types
   exist in `gpu_resource_lifetime`; deferred by the owner until a second backend or a stale-index
   bug needs them), and transient targets with memory aliasing once shadows or HDR add passes.
5. The physically based material path, with a documented conversion from the legacy profiles, once
   legacy mode is stable (also an engine extension for new content), starting the enhanced lighting
   profile with HDR and linear lighting; general screen-space reflections join it, and water then
   moves onto them.

### Simulation

1. Tactical battles. Done: the core, spawning, RTS orders, terrain and passability, pathfinding,
   group movement and formations, squads, space combat with hardpoints, flight, walker locomotion,
   headless battles and replays (status.md). Open:
   - combat presentation and fidelity: turret rotation of hardpoint turrets (unit turrets are
     done); terrain surface kinds for SurfaceFX (dirt, grass, snow:
     ground hits and footprints know only liquids and default);
     `Should_Death_Clone_Play_Idle` (no stock effect; for mods whose clone models have an IDLE);
     the gap rows of [xml-tag-status.md](xml-tag-status.md) list the rest;
   - walkers' `_QUARTER`/`_HALF` turn clips: turns are composed of them, but the angle each stands
     for is inferred from the names, not measured (compatibility-research.md, "The turn kit");
   - game and mod identity in order logs;
   - with the simulation rewrite (the owner's decision): staged battles (a spawn facing,
     invulnerable targets, a per-hardpoint shot report) and, on them, the fire-cone regression
     cases from the recorded hardpoint shot counts by bearing (S-22 to S-42,
     compatibility-research.md).
2. Skirmish systems: the player layer (the local player's owner, factions and hostility from data
   instead of the provisional slot rule, team colours by owner, fog of war), build pads,
   reinforcements, abilities from `Unit_Abilities_Data` (the infantry crouch first, researched in
   the compatibility notes; `ROCKET_ATTACK` among them), transports, capture, retreat, victory
   conditions.
3. Galactic systems: planets, fleets, economy, construction, heroes, transitions between galactic
   and tactical play.
4. Scale: the job system and the hierarchical navigation are in place (work order 2e and 2g); what
   remains of the staged navigation plan in [pathfinding-research.md](pathfinding-research.md)
   (flow fields or shared paths), with A* kept as the reference.

### Scripting

1. Foundation. Done: the sandboxed Lua 5.0 host, script threads on ticks, the event queue and
   callbacks, engine objects as userdata, scripts in battles with validated commands, game-object
   scripts, map hints, and proximity events (status.md). Open:
   - the rest of the tactical API by survey rank, starting with calls that need no players;
   - `Spawn_Unit`, which waits for the player layer (its owner comes from `Find_Player`) and will
     return provisional objects that the merge maps to the spawned entities (agreed), so a script
     can order them at once while they read as not valid for one tick;
   - whether running script state must be saveable (it shapes Persistence);
   - whether the instruction budget should be lower (a runaway resume costs about 73 ms);
   - how to cut a script's start (about 20 ms, mostly loading the framework libraries).
2. The tactical host API, ranked by the survey, replacing stubs as Simulation stage 2 provides the
   systems.
3. The galactic and story API.
4. Corpus-wide coverage over stock and popular mods, and the namespaced extension API.

### Audio

1. Done: sound events and samples played from presentation, never from the simulation (status.md).
2. A mixer with categories and options (effects, voices, music, volumes in the UI), loop intros and
   outros (`Pre_Samples`/`Post_Samples`, fades), priorities and voice stealing, response rules
   (`Kills_Previous_Object_SFX`), ambient and impact sounds, and checking the 3D curve and the
   listener against the original.
3. Music, speech, and their localized variants: an MP3 decoder; `MusicEvent`s chosen by faction
   and environment kind, switching between ambient and battle music by combat and the peace delay;
   `SpeechEvent`s for advisor and HUD voice; scripts' `Play_Music`, `Stop_All_Music`,
   `Resume_Mode_Based_Music`, `Stop_All_Speech`.

Not tied to a stage yet:

- weather and environment ambience (`WEATHERAUDIO.XML`; needs the map's weather kind, which
  `MapWeather::state()` gives, and the environment kind from TED);
- scripts' `Weather_Audio_Pause` and the music and speech calls, as further requests on the script
  presentation channel (`Play_SFX_Event` uses it already);
- hardpoint destruction reports, `Land_Damage_SFX`, turret rotation of hardpoint turrets, ability
  and nebula sounds (each a small presentation rule over existing snapshots or a missing system);
- HUD, advisor, and GUI sounds with UI stage 1; movies with cinematics.

### UI

1. The tactical HUD: selection panels, health and command buttons, the minimap, text through the
   DAT localization layer.
2. Menus, options, the galactic interface, and key bindings.

The dev panels stay development tools; game UI is its own layer above the engine.

### AI

1. Tactical AI for skirmish, driven through the Lua host where the stock data does it that way.
2. Galactic and campaign AI.

### Networking

Deterministic lockstep: players exchange orders, every client simulates, and the state hash detects
divergence. Battles already record replayable order logs, which is most of the groundwork.

1. Lockstep over a local loopback, driven by order logs: proves determinism under multiplayer
   conditions without a network.
2. LAN and internet sessions: transport, lobby, joining, latency handling.
3. Desync diagnosis (per-tick hashes, state dumps) and replays of network matches.

Compatibility with the original's multiplayer protocol is an open question, not a goal yet.

### Persistence

1. A versioned save format for galactic campaigns and skirmishes, including script state if
   Scripting stage 1 decides it must be saveable. Whether original saves should load is open.

### Tools

1. Done: EAWAssetInspector, EAWDatEditor, EAWTools, EAWLauncher, EAWDevLauncher.
2. Profiling overlays and automated budget reports (Performance gates).
3. The SDK: documented extension points, authoring and import tools, a map editor.

### Platform and input

1. Done: Win32 window, raw input, crash handling, log sinks.
2. Input. Done: key rebinding ahead of the UI (the owner's choice), and the sandbox's input-load
   measurement with `scripts/check-input-load.ps1` (status.md). Open:
   - the run of `check-input-load.ps1` with a synthetic 8 kHz mouse, which waits for the owner (it
     needs the window to hold the focus);
   - default bindings as the original's hotkeys (transcribed in compatibility-research.md, "Default
     key bindings"): chords, order keys that arm the next click, arrow-key panning; applied with the
     in-game UI and the player layer, not before (the owner);
   - controllers (GameInput, a new dependency), which wait for the player layer (the owner);
   - input timestamps, left out until something consumes them (orders take effect on the next
     tick; nothing yet orders events within a frame).

### Code health

Ongoing, not a stage plan: leftovers are fixed while features touch the code around them, and the
owner's periodic whole-project reviews add to this list or turn it into a plan of steps (AGENTS.md,
"Code health"). Remove an entry when it is fixed. Known leftovers:

- two bounds for clip playback: `walk_clip_playback`'s 0.25-6 (battle) and the animator's 0.5-1.5
  for speed-scaled playback (presentation), one idea in two places;
- two rules for a definition's repeated field: `definition_field` (`xml_asset_database`, used by
  game objects and decals) takes the last occurrence, `sound_events`' private `field_of` the first;
  merge once the sound events' rule is checked against the data;
- the minimal second XML reader in `game_installation` (kept so the VFS does not depend on the
  formats; noted in the code);
- `MegArchive`, whose index decoding and entry reads were left together on purpose;
- model lights and dazzles, decoded but not part of `Model` yet;
- the `pe_*` and hardpoint-bone role rules (which bones a hardpoint's `Damage_Particles`,
  `Damage_Decal`, and `Engine_Particles` name), still read separately in `scene_particles` and
  `combat_content` instead of one place (see Engine extensions, explicit asset metadata); the
  hardpoint list (`type_hardpoints`) and the scorch-subtree rule (`bones_in_subtrees`) are shared;
- `using namespace d3d12;` in the renderer's private header, which imports every helper into every
  renderer source (about 180 uses to qualify; with the renderer restructuring, proposal G);
- the projectile reacquisition tie-break, which takes the higher dense index (storage order, like
  the other order dependences the simulation rewrite removes; changing it changes the hash);
- the script host's own SplitMix stream beside `core/random` (it is script state, so a move must keep
  its sequence);
- pose sampling's allocations: `UnitAnimator::sample` builds a vector per layer (the layer's pose,
  its first frame, the walked bones, the turns), the pose cache is a map of one vector per pose, and
  the job outputs are one vector per pose; one flat pose arena (offsets per key) would remove them
  (probably part of the serial half of `animate` in `EAWBenchmarks poses`; not profiled);
- the tick's speed bookkeeping in `SimulationWorld::step` writes its vector length out by hand (the
  hash depends on its order of operations, so a move onto `eaw_math` must keep it).

## Performance gates

Each milestone sets measured budgets for its deliverable; the numbers are set when the benchmark
exists, not guessed in advance.

- The headless battle (`EAWEngine battle`) is the simulation benchmark; it stays ahead of renderer
  optimization.
- Presentation at the display's rate with the fixed simulation tick (60 Hz, the owner's decision;
  `--tick-rate` sets another for headless battles).
- 60 frames per second at the least (the owner's decision): a frame, any simulation ticks it runs
  included, within 16.6 ms. The opening tick of a 3,300-entity battle broke it (about 51 ms,
  mostly its 5,000 target searches, measured before combat targeting ran on the job system; not
  re-measured since). `scripts/benchmark-battle.ps1` measures it under constant orders; on its last
  run (tools.md, "Benchmarks"), no frame went over at 10 to 500 units (500 units: 6.0 ms average,
  13.9 ms worst). The route-search budget is 15,000 nodes a tick (`path_budget_per_tick`, about 2.5 ms;
  the owner's choice), so orders may wait longer for their routes.
- No fixed texture dimension, vertex count, entity count, or 32-bit address-space assumption.
- Automated budgets, reported by the tools, for frame time, tick time, streaming throughput,
  memory, draw and dispatch counts, and worst-case path requests.

## Verification against the original

Provisional engine choices to check against the original, by black-box observation and the
capture-and-compare workflow ([tools.md](tools.md#captures-and-image-regression); features are
compared by measured ratios from screenshots, never by recreated scenes):

- which definition wins when two registered XML files define one name (to do, needs a small test
  mod: `GameObjectFiles.xml` listing two files that define the same unit with different build
  costs; the schema says the first listed wins for game objects and the last for hardpoints;
  compatibility-research.md, "XML loading");
- whether the game mounts MEG archives that `MegaFiles.xml` does not list (to do, same kind of mod:
  an unlisted archive overriding one file; the engine mounts them in lexical order, and the
  reference tools disagree);
- idle animations: whether units play a random IDLE variant after each one ends, and how long a
  lone idle that does not loop (`Loop_Idle_Anim_00` No) rests before playing again (the engine:
  2-4 s, the owner's choice);
- whether melee units attack on the move: most infantry stop to attack (the owner's recollection,
  which the engine follows), but `Dark_Trooper_PhaseI` sets `Is_Stationary_When_Attacking` to No,
  the only explicit No in stock;
- the original's logic rate, which every per-frame XML value is read at (30 per second,
  `assets/legacy_units.hpp`: speeds, turn rates, accelerations, frame counts);
- shield refresh units, and how shields and armor combine;
- ranges measured in the ground plane;
- the owner-slot hostility rule;
- world handedness;
- whether unit `Scale_Factor` (applied to models, attached effects, model-derived footprints, and
  hardpoint positions) also scales collision and selection, and whether `Custom_Soft_Footprint_Radius`
  is meant before or after it (the engine leaves it unscaled);
- the RTS camera's angles, zoom range, and pan speeds (the engine's own cameras for now, by the
  owner's choice; the TacticalCameras.xml tags are postponed);
- attack-move and guard: the acquisition range (1.25 x weapon range), the leash (2 x weapon
  range), the guard radius (weapon range, at least 150 units), and how far a followed object may
  move before the unit follows (50 units);
- fighter flight angles, heights, and periods;
- acceleration units (`OverrideAcceleration` as speed per frame per frame), capital ships' turn
  speed fraction (0.25), and the angle at which ground units stop to turn (45 degrees);
- shot inaccuracy: whether `Targeting_Fire_Inaccuracy`/`Fire_Inaccuracy_Distance` are a scatter
  radius around the aim point (the engine's reading) and whether a stray shot hits what it lands on;
  how often infantry, tanks, and capital ships hit in the original;
- whether `Object_Max_Speed_Multiplier_*` also scales projectile speeds;
- team movement: whether company members take their team container's speed and acceleration;
- area blasts: whether blasts hurt friendly units (`Friendly_Damage_Amount`), how the dropoff tiers
  divide the damage, and whether the direct victim takes the blast besides the shot;
- crushing: which units run over infantry (the engine: vehicle collision classes and
  supercrushers) and whether friendly infantry are crushed;
- whether ground units stop to turn when starting from rest: GameConstants'
  `CloseEnoughAngleForMoveStart` is 180 (never stop) while the engine stops them beyond 45 degrees
  after the owner's observation; types that set `Close_Enough_Angle_For_Move_Start` (the AT-AT 30)
  apply either way (on the move units turn while walking, seen in the owner's recording);
- space avoidance: the look-ahead (3 s of travel, at least 4 radii), the margin (a quarter radius),
  the course steps (11.25 degrees), the formation spacing of ships (slots 4.5 radii apart), which
  units are small enough to fly past (below a fifth of the radius), and that a unit at another
  height is passed when the heights differ by more than both radii;
- where weapons fire from: whether shots cycle through a unit's `MuzzleA_nn` bones one per shot
  in number order (the engine's reading), whether infantry fire from their animated muzzle, and
  how long the TIE Crawler's hidden `MuzzleA_nn_flash` meshes show;
- the times in `ANIMATIONSFXMAPS.TXT` (read as hundredths of a frame);
- `Movement_Animation_Speed` and `Walk_Animation_Speed` as the speeds at which the move and walk
  clips play at their own rate (the AT-AT's legs then run at 3.7 times at its top speed), and
  whether walkers stand still during `MOVESTART` (`Locomotor_Has_Animation_Priority`);
- turret weapons firing from `Barrel_Bone_Name` and hardpoints under the turret bone turning with
  it (the T4-B's missiles);
- how the original picks detail levels (`LOD_Bias`, `MeshDetail`): the engine's distances, pixel
  sizes, hysteresis, and fade time are its own;
- why `W_LASER_INVISIBLE.ALO` (the land model of 19 melee and environment projectiles) is invisible
  in the original although its mesh is an unhidden additive quad with a visible texture;
- a test map built in the original's map editor, if one can be made, for matching static placement.

### Play-feel audit (owner, pending)

The owner checks how play feels in one pass later instead of after each change. Changes waiting for
it, oldest first:

- shields refreshing in 3 s steps; in space, the speed multiplier also scaling acceleration and
  turning;
- hardpoint damage: the hull following its parts, ships dying with their last destroyable part;
- ships turning in place slower by Space_Layer, ordered ships turning to their target's hardpoint,
  space ships turning only at full speed by default, spawned ships at their layer's height;
- decorations drawn but not simulated (Fondor 02 has 532);
- the debug overlay's own texture uploads (the F1 panels);
- turret motor loops; wreck end explosions and the longer wrecks of AT-AA and the Dark Troopers;
- idles: random variants one after another, lone idles pausing 2-4 s;
- scorch marks after explosions and walkers' footprints (size, darkness, orientation, how long they
  stay);
- infantry and vehicle trails (bump decals: barely visible at overview zoom; how far behind they
  fade);
- edge anti-aliasing on by default (`--no-aa` to compare), temporal anti-aliasing (`--aa taa`), and
  both (`--aa hybrid`): ghosting behind particles and blended parts, softness against FXAA, the
  hybrid mode;
- transparent model parts drawn back to front (canopies, glass, glows on models);
- distance fog from the map environments (`--fog off` to compare, `--fog enhanced` for height
  fog); its ranges suit the original's closer camera, so zoomed far out distant ground fades (cap
  the zoom, or scale the fog to it?);
- key-light shadows (on by default, `--shadows off` to compare): their darkness, softness, and
  reach (backdrops such as planets receive none, the owner noted);
- damage states on land: damaged meshes and their `_ALT` effects by health, the state sounds, and
  rubble that stays (structures without a death animation keep their last state for good).

## Engine extensions (opt-in, outside legacy mode)

Each becomes available once the track stage it builds on exists. Stock data and existing mods never
need them.

- **Player-controlled stances.** The original gives players no stance control; the simulation's
  `Stance` exists for AI and scripts, so a control can be offered outside legacy mode.
- **Explicit asset metadata.** Legacy mode infers the role of model parts from naming and placement:
  `pe_*` proxies are engine effects, `_ALT<n>` and `_LOD<n>` tags mark damage states and detail
  levels, proxies under a hardpoint's `Damage_Particles` bone are damage effects, a `Damage_Decal`
  bone carries scorch meshes, and an emitter's mesh is the one on its proxy's parent bone. Optional,
  namespaced fields (in XML or a sidecar beside the asset) will declare these categories (engine,
  damage, state, detail level, and later lights or weapon muzzles), overriding the inferred ones,
  with unknown categories reported, not fatal. Effects come first. The tag rules are in
  `assets/alo_interpretation`; the hardpoint-bone and `pe_*` rules are still in the engine
  (`scene_particles`, `scene_models`, `combat_content`) and move to one place before this starts, so
  both paths feed the same engine-native categories.
- **Physically based materials** (Rendering stage 5).
- **Modern model and animation formats** beside ALO/ALA: glTF 2.0 and FBX import of meshes,
  skeletons, skinning, and clips with blending, layering, additive clips, retargeting, root motion,
  and blend shapes; imported materials use the physically based model; the same VFS layers, cache,
  and hot reload apply. FBX needs an importer whose license fits (an open-source reader such as
  ufbx, or the Autodesk FBX SDK); the choice is open.
- **A namespaced mechanics API** for new gameplay, with documented extension points (Scripting
  stage 4, Tools stage 3).
