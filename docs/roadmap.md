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

Set by the owner on 2026-11-10: the base technical systems and M2's renderer come before combat
presentation and the player layer. Inserted by the owner on 2026-11-11, before item 2: the
game-object tags existing systems should honor, worked through tag by tag from
[xml-tag-status.md](xml-tag-status.md) (its `gap` and `partial` rows, family by family; the
rendering rows join item 2).

1. Done (2026-11-10): Audio, first stage (Data stage 2 and Audio stage 1): the effect archives, the
   WAV reader, the sound-event catalog, XAudio2 (optional FMOD) devices, and positional battle
   sounds from presentation.
2. M2's renderer (Rendering stages 1 and 2), in this order: damage states and LODs (detail levels
   done 2026-11-11: renderer-side selection by distance, screen size, or both, with cross-fades;
   damage states done 2026-11-13: chosen from health, with their effects, sounds, and rubble;
   hardpoint damage visuals belong to combat presentation, Simulation 1), depth-sorted transparency (done 2026-11-13), HDR with tone mapping and anti-aliasing (edge
   temporal, and hybrid (TAA then FXAA) anti-aliasing done 2026-11-13, with per-object motion vectors for models (blended parts and particles still
   reproject by camera motion, but particles mark a reactive mask that TAA and FXAA respect, 2026-11-13); HDR postponed by the owner (2026-11-13) to the later
   visual enhancement: it needs linear lighting, since the shading works in display space like the
   original's, so it starts the enhanced lighting profile with the physically based path),
   shadows, fog (done 2026-11-13: legacy and enhanced height fog, particles included; volumetric fog
   for light shafts and smoke later, the owner agreeing), decals (map and runtime; terrain decals in
   the terrain shader, scorch marks, footprints, and DynamicTrackFX trails done 2026-11-13;
   the one map with placed decals later; screen-space decals if the renderer becomes deferred), special materials, in the order of stock
   use measured by `texture-audit` (2026-11-13; 9,294 material instances): planets (`Planet`
   296: atmosphere rim, clouds, city lights), wind bend (`Tree` 185, `Grass` 14), shields
   (`MeshShield` 27, seen in every space battle), water (the owner's addition: legacy from the TED
   water data, since the published shaders hold no water shader, and an enhanced mode designed
   from research into current and RTS practice, [water-research.md](water-research.md), agreed
   2026-11-13: depth color, refraction from a scene copy, shoreline and foam, layered normals,
   sun glint, planar screen-space reflections with a sky fallback; general screen-space
   reflections join the enhanced lighting step, and water then moves onto them; done 2026-11-13:
   water drawn again (scenery), its own pass, enhanced depth color, shore fade, two normal layers,
   Fresnel toward a sky color, glint, refraction, foam, planar screen-space reflections; next:
   the TED water fields 0x17-0x22 (the owner will read their labels and Mon Calamari 02's values in
   the map editor), a sky to reflect where the screen has nothing, and live tuning: the enhanced
   water's constants (clarity, shore fade, foam depth and strength, refraction and reflection
   distortion, glint) as sliders in a Water section of the Graphics panel, so the owner can tune
   them by eye and the chosen values become the defaults), skydomes and nebulae (`Skydome` 19, `Nebula` 15),
   occluded units (chosen by the engine, not by materials), heat distortion (`MeshHeat` 3, plus
   heat particles), reflections (`MeshAdditiveReflection` 3; no stock material uses the reflective
   profiles); then weather and soft particles (both done 2026-11-13: soft particles enhanced; legacy
   weather from the map's scenario, with the cube size, fade-out distance, and fog plane still unused
   and the placement an engine choice for the owner to check), look tuning. Wind bend for trees and waving grass
   (second UV set) done 2026-11-13; grass's distance fade later. Occluded-unit silhouettes done 2026-11-13. Heat distortion from heat
   particles done 2026-11-13 (the 3 heat meshes remain); the enhanced heat mode from
   [heat-research.md](heat-research.md) done 2026-11-13 (shockwave rings later, as an option). Shields done 2026-11-13 (the
   material and hit flashes); the FoC stealth meshes' visibility (when the original shows them) and
   reading Shield_Flash_* from GameConstants remain. Legacy sky domes and nebulae done 2026-11-13;
   enhanced sky agreed 2026-11-13 from [sky-research.md](sky-research.md): cloud shadows (done, a
   legacy feature), a sky capture for the water's reflection fallback and a sky-tinted ambient, fog
   toward the horizon color (all done 2026-11-13); the sun disk, star intensity, and an atmosphere
   after HDR.
   Global illumination (the owner, 2026-11-13: the sky as a major contributor), in steps: sky
   irradiance (the captured sky projected to spherical harmonics, the ambient term; done 2026-11-13
   as the enhanced sky ambient), then sky
   visibility (how much sky each terrain point sees, from the heightfield once at load, applied to
   the terrain and to units standing on it; done 2026-11-13), then bounced light once lighting is
   linear (HDR). Open: objects do not occlude the sky yet (buildings, cliffs as models; a
   screen-space or per-object term later).
   Reordered by the owner on 2026-11-13: the job system and an entity-component design join M2 as
   general engine systems, and the battle issues the owner found come first where they need no
   rework. In this order:
   a. Visual fixes: the line across planets (done 2026-11-13: bone billboards), skydomes too bright (done 2026-11-13: bloom
      ignores the sky and fog, the sky ambient is balanced upward) for the lighting they feed
      (the sky capture, sky ambient, horizon fog), water that tiles visibly (done 2026-11-13: a broad
      anti-tiling layer in both modes; the TED repeat fields remain to be read).
   b. Bugs that need no rework: walker animation state changes (start from rest, turn, and stop
      play wrongly and slowly; done 2026-11-13: transitions play at the walk's and turn's rates); the T4-B's two weapons (missile launcher and laser cannons, with
      different ranges): their gun animations play out of range and loop instead of following the
      actual shots (done 2026-11-13: ATTACK plays with the shots only; the missile launcher is the
      ROCKET_ATTACK unit ability, not implemented yet, with the other unit abilities); walkers do not crush enemy infantry (done 2026-11-13: crushers and what they crush do not push each other); destroyed space hardpoints are not
      replaced by their destroyed versions (done 2026-11-13/14: scorch marks and damage effects appear, attached models go, and Death_Breakoff_Prop debris breaks off). Shield hits play
      only the absorb particle; the shield mesh flash waits for the shield abilities (2026-11-14).
      Walker locomotion cleanup before the ECS (the owner's review, 2026-11-14), one commit each:
      (1) the clip naming in one place and planned walks on their clips' first variants (done
      2026-11-14: `assets/clip_names`); (2) which stock types still take the presentation's old
      start and stop, then remove it or plan them; (3) `UnitAnimator::animate` split into stages
      in files of their own, behavior unchanged; (4) turns report their clip and progress like
      walks, composed of loop, HALF, and QUARTER cycles. MovementProfile's optional modes and the
      simulation's parallel storage go to the ECS design (c) instead.
   c. Research and one design document for an entity-component system (modern engines' ECS,
      data-driven composition, archetypes) and the job system: entities as ids, components in
      their own storage, systems over component sets with declared reads and writes, XML types as
      component recipes through a tag-to-component registry, deterministic iteration, and a
      hash-neutral migration path. Modder-defined component tags only get room in the design,
      not a design of their own yet. The owner reviews it before code.
   d. The job system: generic tasks, dependencies, and parallel loops; component systems become
      its client.
   e. The simulation's migration to entities and components, step by step with an unchanged
      battle hash, folding in the deferred determinism work (stable-id order, keyed random draws,
      a full-state hash).
   f. Real squads: a squad is one commandable unit made of its soldiers (no single soldier of it
      can be ordered).
   g. Pathfinding, group movement, and collisions reworked on squads and components, after
      research: each unit plans to its own destination slot instead of the group gathering at one
      point first; routes spread by congestion so groups do not queue single-file; blocked units
      plan again instead of bumping; pushing follows mass or priority (an infantry squad cannot
      move an AT-AT); walls and placed obstacles are hard limits.
   h. Enhanced water as an ocean on maps such as Kamino: waves (a tessellated surface displaced by
      wave functions), foam on crests.
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
   `sound-audit`); MP3 for music and speech comes with Audio stage 3 (a decoder is a new dependency
   or a reader of our own, the owner decides).
3. Cinematics: survey the movie format and decide between a reader and a pinned decoder (a new
   dependency, so the owner decides).
4. Steam Workshop mod discovery and mod-stack diagnostics (which layer wins, what is missing).
5. Resource IDs (generation-checked handles), once a consumer needs references that survive a
   reload: hot-swapping one resource in a running scene, scripts, saves, GPU residency.

### Rendering

1. Legacy-mode features content depends on: damage-state (`_ALT<n>`) and LOD switching,
   skinned-mesh and gameplay-driven visibility; depth-sorted transparency; map decals (no texture
   link established yet) and runtime scorch marks; shadows (the original uses stencil shadow
   volumes, an implementation artifact; the engine uses stable cascaded shadow maps from each map's
   key light, land and space alike, and every model casts: the owner's choice of 2026-11-13 over
   the original's per-model shadow meshes, for quality; skies, backdrops, markers, blended parts,
   and particles do not; done 2026-11-13: cascades, the shadow pass, and receiving; open: per-cascade
   culling, cached distant cascades, blob shadows at low detail, PCSS as an enhanced option); fog; shield, planet (atmosphere, clouds, city lights),
   nebula, heat-distortion, wind-bend, and occluded-unit materials; cube reflections (no stock
   material uses them); weather (done 2026-11-13; tiled over the ground in view since the owner's report; open: the fog plane, the weather
   cube's size, distance, and fade-out, tiles that appear while panning filling from the top, weather kinds in gameplay through WeatherModifiers.xml,
   with the timeline then on the battle's ticks); soft particles (done 2026-11-13, enhanced option).
2. Modern presentation without an original counterpart, chosen freely: anti-aliasing, HDR with
   tone mapping beside the existing bloom, and look tuning against the owner's screenshots (hotter
   engine-glow cores, more contrast on hulls).
3. Scale: per-asset geometry handles and several resident scenes, GPU-driven culling and indirect
   drawing, mesh LODs, clustered lighting, texture compression and streaming budgets. To try
   (owner's request, 2026-11-11): a LOD cross-fade driven by the selection's position within a
   border band around each level boundary instead of by time, so the blend follows the camera and
   holds while it stands still; compare it with the current timed fade (`VisualLodTracker`).
4. A thin rendering hardware interface extracted from the Direct3D 12 backend, then Vulkan. Done
   toward it (2026-11-13): the contract split by subject, `eaw_render_core` (detail levels,
   resource lifetime, the frame graph's barrier planning, no graphics API), and the frame graph in
   the D3D12 backend. With it: generation-checked mesh and texture handles in the contract (the
   handle types exist in `gpu_resource_lifetime`; deferred by the owner until a second backend or
   a stale-index bug needs them), and transient targets with memory aliasing once shadows or HDR
   add passes.
5. The physically based material path, with a documented conversion from the legacy profiles, once
   legacy mode is stable (also an engine extension for new content).

### Simulation

1. Tactical battles. Done: the core, spawning, RTS orders, terrain and passability, A*, group
   movement and formations, space combat with hardpoints, flight, headless battles and replays.
   Open: AT-AT animations are broken (the owner, 2026-11-13: in the sandbox an AT-AT can stay in its
   idle clip while ordered to move); combat presentation and fidelity (hardpoint damage visuals, turret rotation of hardpoint
   turrets (unit turrets done 2026-11-12); walkers' `_QUARTER`/`_HALF` turn clips (cycles between
   BEGIN and END, compatibility-research.md "The turn kit"; next: compose turns from them as walks
   are planned by their clips, 2026-11-14),
   `Rotation_Animation_Speed`, and `Locomotor_Has_Animation_Priority` (turn-in-place, begin/end,
   start and stop transitions done 2026-11-12)),
   damage-specific death clones and wreck explosions, ground detonations, debris, death-projectile
   blasts, beams for instant-hit weapons), and game and mod identity in order logs. With the
   simulation rewrite (owner, 2026-11-13): staged battles (a spawn facing, invulnerable targets, a
   per-hardpoint shot report) and, on them, the fire-cone regression cases from the recorded
   hardpoint shot counts by bearing (S-22 to S-42, compatibility-research.md).
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

1. Done: sound events and samples played from presentation, never from the simulation: weapon
   fire, deaths, engine loops, and unit responses in the sandbox, positioned by AUDIO.XML's mode
   factors, through XAudio2 (default) or FMOD (built with a local SDK).
2. A mixer with categories and options (effects, voices, music, volumes in the UI), loop intros and
   outros (`Pre_Samples`/`Post_Samples`, fades), priorities and voice stealing, response rules
   (`Kills_Previous_Object_SFX`), ambient and impact sounds, and checking the 3D curve and the
   listener against the original.
3. Music, speech, and their localized variants: an MP3 decoder; `MusicEvent`s chosen by faction
   and environment kind, switching between ambient and battle music by combat and the peace delay;
   `SpeechEvent`s for advisor and HUD voice; scripts' `Play_Music`, `Stop_All_Music`,
   `Resume_Mode_Based_Music`, `Stop_All_Speech`.

Not tied to a stage yet: weather and environment ambience (`WEATHERAUDIO.XML`, needs the map's
weather kind, which `MapWeather::state()` now gives, and the environment kind from TED); scripts' `Weather_Audio_Pause` and
the music and speech calls, as further requests on the script presentation channel
(`Play_SFX_Event` uses it already);
hardpoint destruction reports, `Death_SFXEvent_End_Die`, `Land_Damage_SFX`, turret rotation,
ability and nebula sounds (each a small presentation rule over existing snapshots or a missing
system; projectile detonation and `SFXEvent_Ambient_Moving` are done); HUD, advisor, and GUI sounds with UI stage 1;
movies with cinematics.

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

- `UnitAnimator::animate` (`unit_animation.cpp`), one function of about 580 lines over a state of
  about 42 fields: role choice, the speed blend, the stride, aiming and firing, cues, walker
  transitions, turns, turrets, and the pose key (work order 2b, step 3);
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
  hardpoint list itself (`type_hardpoints`) and the scorch-subtree rule (`bones_in_subtrees`) are
  shared now;
- `using namespace d3d12;` in the renderer's private header, which imports every helper into every
  renderer source (about 180 uses to qualify; with the renderer restructuring, proposal G);
- the projectile reacquisition tie-break, which takes the higher dense index (storage order, like
  the other order dependences the simulation rewrite removes; changing it changes the hash);
- the script host's own SplitMix stream beside `core/random` (it is script state, so a move must keep
  its sequence);

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

- which definition wins when two registered XML files define one name (to do, needs a small test
  mod: `GameObjectFiles.xml` listing two files that define the same unit with different build
  costs; the schema says the first listed wins for game objects and the last for hardpoints;
  compatibility-research.md, "XML loading");
- whether the game mounts MEG archives that `MegaFiles.xml` does not list (to do, same kind of mod:
  an unlisted archive overriding one file; the engine mounts them in lexical order, and the
  reference tools disagree);
- idle animations: whether units play a random IDLE variant after each one ends, and how long a
  lone idle that does not loop (`Loop_Idle_Anim_00` No) rests before playing again (the engine:
  2-4 s, the owner's choice of 2026-11-13);
- whether melee units attack on the move: most infantry stop to attack (the owner's recollection,
  2026-11-13, which the engine follows), but `Dark_Trooper_PhaseI` sets
  `Is_Stationary_When_Attacking` to No, the only explicit No in stock;
- the original's logic rate, which every per-frame XML value is read at (30 per second, `assets/legacy_units.hpp`: speeds, turn rates, accelerations, frame counts);
- shield refresh units, and how shields and armor combine;
- ranges measured in the ground plane;
- the owner-slot hostility rule;
- world handedness;
- whether unit `Scale_Factor` (applied to models, attached effects, model-derived footprints, and
  hardpoint positions) also scales collision and selection, and whether `Custom_Soft_Footprint_Radius`
  is meant before or after it (the engine leaves it unscaled);
- the RTS camera's angles, zoom range, and pan speeds (the engine's own cameras for now, by the
  owner's choice of 2026-11-13; the TacticalCameras.xml tags are postponed);
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

The owner checks how play feels in one pass later instead of after each change (2026-11-13). Changes
waiting for it, oldest first:

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
- infantry and vehicle trails (bump decals: barely visible at overview zoom; how far behind they fade);
- edge anti-aliasing on by default (`--no-aa` to compare), temporal anti-aliasing (`--aa taa`), and both (`--aa hybrid`):
  ghosting behind particles and blended parts, softness against FXAA, the hybrid mode;
- transparent model parts drawn back to front (canopies, glass, glows on models);
- distance fog from the map environments (`--fog off` to compare, `--fog enhanced` for height fog); its ranges suit the original's
  closer camera, so zoomed far out distant ground fades (cap the zoom, or scale the fog to it?);
- key-light shadows (on by default, `--shadows off` to compare): their darkness, softness, and
  reach (backdrops such as planets receive none, owner 2026-11-13);
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
