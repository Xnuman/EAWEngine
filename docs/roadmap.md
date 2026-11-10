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
editor, and EAWTools on the same libraries. Audio and video formats were not part of M1; they are
stages of the Data track.

### M2: legacy-mode renderer complete for stock content (active)

The first deliverable (load and inspect a stock unit and a tactical map in the sandbox) is met. M2
now closes when the renderer shows stock content with every feature it was authored for.

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

Proposed on 2026-11-10 with this roadmap; it continues the owner's order of 2026-11-09 (foundation
review, combat presentation, sound, player layer) with sound and the player layer moved to M4, where
the skirmish needs them, and the open M2 render work placed by what depends on it:

1. Scripting stage 1: the script calls that need new simulation features (guarding, attack-move,
   invulnerability, direct damage, teleporting, suspending locomotion; they change the state hash);
   `Spawn_Unit`, `Find_Player`, and `Find_Nearest` follow the player layer.
2. Rendering stage 1, the part combat presentation needs: damage-state (`_ALT<n>`) and LOD
   switching, runtime decals, depth-sorted transparency.
3. Simulation stage 1's remainder: combat presentation and fidelity.
4. Rendering stage 1's remainder and stage 2: shadows, anti-aliasing, tone mapping, fog, special
   materials, look tuning. This closes M2 and, with step 3, M3.
5. M4: the player layer (Simulation stage 2), then the HUD, audio, and AI.

## Tracks

### Data

1. Done: VFS, MEG, and the M1 formats; the game asset layer with loaders and the resource manager.
2. Audio data: survey the stock sound-event XML and the sample, music, and speech formats, then
   readers and loaders for them (Audio stage 1 consumes them).
3. Cinematics: survey the movie format and decide between a reader and a pinned decoder (a new
   dependency, so the owner decides).
4. Steam Workshop mod discovery and mod-stack diagnostics (which layer wins, what is missing).
5. Resource IDs (generation-checked handles), once a consumer needs references that survive a
   reload: hot-swapping one resource in a running scene, scripts, saves, GPU residency.

### Rendering

1. Legacy-mode features content depends on: damage-state (`_ALT<n>`) and LOD switching,
   skinned-mesh and gameplay-driven visibility; depth-sorted transparency; map decals (no texture
   link established yet) and runtime scorch marks; shadows (the original uses stencil shadow
   volumes, an implementation artifact: which objects cast and receive is the content contract,
   the technique is the engine's choice); fog; shield, planet (atmosphere, clouds, city lights),
   nebula, heat-distortion, wind-bend, and occluded-unit materials; cube reflections (no stock
   material uses them); weather and soft particles.
2. Modern presentation without an original counterpart, chosen freely: anti-aliasing, HDR with
   tone mapping beside the existing bloom, and look tuning against the owner's screenshots (hotter
   engine-glow cores, more contrast on hulls).
3. Scale: per-asset geometry handles and several resident scenes, GPU-driven culling and indirect
   drawing, mesh LODs, clustered lighting, texture compression and streaming budgets.
4. A thin rendering hardware interface extracted from the Direct3D 12 backend, then Vulkan.
5. The physically based material path, with a documented conversion from the legacy profiles, once
   legacy mode is stable (also an engine extension for new content).

### Simulation

1. Tactical battles. Done: the core, spawning, RTS orders, terrain and passability, A*, group
   movement and formations, space combat with hardpoints, flight, headless battles and replays.
   Open: combat presentation and fidelity (hardpoint damage visuals, turret rotation,
   damage-specific death clones and wreck explosions, ground detonations, debris, death-projectile
   blasts, beams for instant-hit weapons), and game and mod identity in order logs.
2. Skirmish systems: the player layer (the local player's owner, factions and hostility from data
   instead of the provisional slot rule, team colours by owner, fog of war), build pads,
   reinforcements, abilities from `Unit_Abilities_Data` (the infantry crouch first; researched in
   the compatibility notes), transports, capture, retreat, victory conditions.
3. Galactic systems: planets, fleets, economy, construction, heroes, transitions between galactic
   and tactical play.
4. Scale: the dependency-aware job system once measured budgets need it, and the staged navigation
   plan in [pathfinding-research.md](pathfinding-research.md) (hierarchical search, flow fields or
   shared paths, clearance), with A* kept as the reference.

### Scripting

1. Foundation. Done: the sandboxed Lua 5.0 host, script threads on ticks, recording stubs, the
   engine event queue and script callbacks, values between the engine and scripts, `Create_Thread`
   control and `GameRandom`, and `lua-survey` driving AI plans, story events, task force events, and
   callbacks over the stock scripts; engine objects as userdata through a provider, and scripts in
   battles (`BattleScripts`: one immutable view per tick, per-script order buffers merged in a fixed
   order as validated commands, blocks, script orders outside the log); game-object scripts started
   on their entities from XML `Lua_Script`, map hints (`Find_Hint`), and proximity events. Open:
   the rest of the tactical API by survey rank, starting with calls that need no players;
   `Spawn_Unit` waits for the player layer (its owner comes from `Find_Player`) and will return
   provisional objects that the merge maps to the spawned entities (agreed 2026-11-10), so a script
   can order them at once while they read as not valid for one tick; decide whether running script state must be saveable (it
   shapes Persistence), whether the instruction budget should be lower (a runaway resume costs about
   73 ms), and how to cut a script's start (about 20 ms, mostly loading the framework libraries).
2. The tactical host API, ranked by the survey, replacing stubs as Simulation stage 2 provides the
   systems.
3. The galactic and story API.
4. Corpus-wide coverage over stock and popular mods, and the namespaced extension API.

### Audio

1. Sound events and samples played from presentation, never from the simulation, with positional
   sound for units and effects.
2. A mixer with categories and options (effects, voices, music), unit responses and weapon sounds
   tied to battle events.
3. Music, speech, and their localized variants.

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
2. Verification with high-polling-rate mice, input timestamps, controllers (GameInput), key
   rebinding (with UI stage 2).

### Code health (ongoing)

Not a stage plan: leftovers are fixed while features touch the code around them, and the owner's
periodic whole-project reviews add to this list or turn it into a plan of steps (AGENTS.md, "Code
health"). Remove an entry when it is fixed. Known leftovers after the reorganization of 2026-11:

- the libraries still on `include/eaw/` (simulation, render, platform, launch, imgui_app)
  and the engine without an include directory, against the short-include rule;
- the minimal second XML reader in `game_installation` (kept so the VFS does not depend on the
  formats; noted in the code);
- `MegArchive`, whose index decoding and entry reads were left together on purpose;
- model lights and dazzles, decoded but not part of `Model` yet;
- the `pe_*` and hardpoint-bone role rules, still spread over `scene_particles`, `scene_models`, and
  `combat_content` instead of one place (see Engine extensions, explicit asset metadata).

## Performance gates

Each milestone sets measured budgets for its deliverable; the numbers are set when the benchmark
exists, not guessed in advance.

- The headless battle (`EAWEngine battle`) is the simulation benchmark; it stays ahead of renderer
  optimization.
- Presentation at the display's rate with the fixed simulation tick (30 Hz, an engine choice until
  the original's rate is measured); a configurable tick is an open option.
- No fixed texture dimension, vertex count, entity count, or 32-bit address-space assumption.
- Automated budgets, reported by the tools, for frame time, tick time, streaming throughput,
  memory, draw and dispatch counts, and worst-case path requests.

## Verification against the original

Provisional engine choices to check against the original, by black-box observation and the
capture-and-compare workflow ([tools.md](tools.md#captures-and-image-regression); features are
compared by measured ratios from screenshots, never by recreated scenes):

- the ×30 XML speed scale;
- shield refresh units, and how shields and armor combine;
- ranges measured in the ground plane;
- the owner-slot hostility rule;
- world handedness;
- whether unit `Scale_Factor` (applied to models, attached effects, model-derived footprints, and
  hardpoint positions) also scales collision and selection, and whether `Custom_Soft_Footprint_Radius`
  is meant before or after it (the engine leaves it unscaled);
- the RTS camera's angles, zoom range, and pan speeds;
- fighter flight angles, heights, and periods;
- why `W_LASER_INVISIBLE.ALO` (the land model of 19 melee and environment projectiles) is invisible
  in the original although its mesh is an unhidden additive quad with a visible texture;
- a test map built in the original's map editor, if one can be made, for matching static placement.

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
