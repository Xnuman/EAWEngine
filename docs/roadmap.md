# Roadmap

Each milestone ends in an executable result and a compatibility corpus. Full
campaign compatibility is a long-term engine project, not a renderer swap.

## M0 — repository and research discipline (done)

- Independent-implementation boundary, read-only owned-installation policy, and
  explicit isolation rules for compatibility evidence.
- Build, tests, launcher/engine separation, architecture record.

## M1 — data bootstrap (baseline complete)

- Steam installation and EAW/FoC profile discovery.
- MEG v1 and layered loose-file/mod VFS.
- FoC profiles inherit the original game's archives at lower priority, matching
  the expansion's cross-runtime texture and model references.
- DAT strings, overlay-aware asset inventory, DDS headers, generic ALO/ALA/TED/
  MTD chunk inventory, Lua/XML localization references, and sparse localization
  workspaces are implemented.
- Semantic ALO model inventory now covers skeletons, meshes, bounds, shaders,
  texture parameters, attachment bones, proxies, lights, and dazzles. Semantic
  ALA inventory covers timing, compressed track blocks, animated bone channels,
  visibility, and stepped tracks.
- Render-ready ALO vertices, indices, skin palettes, bind transforms, decoded
  ALA frame transforms, and an initial textured/animated model viewport are
  implemented. The preview resolves texture overrides through the same VFS and
  supports adjustable geometry detail for large assets.
- Semantic TED decoding covers map properties, terrain/elevation/passability,
  texture layers, water, decals, tracks, environments, placed-object CRCs, AI
  zones, and embedded previews. The combined stock corpus currently decodes all
  269 resolved TED maps without warnings. Land maps have an initial orbitable,
  adaptively decimated heightmap viewport with elevation and passability views;
  space maps visualize decoded placed-object transforms and tactical bounds.
- The automated layered-VFS corpus covers manifest and unlisted archives,
  base/numeric/64-bit patches, loose overrides, EAW-to-FoC inheritance,
  first-mod-wins stacks, lower-layer fallback, path normalization, and unique
  winner enumeration.
- The initial schema-aware XML database parses Petroglyph multi-root fragments,
  preserves ordered fields, attributes, and source lines, indexes typed `Name`
  definitions, and diagnoses duplicate keys and unresolved XML-file references.
  The combined stock corpus parses all 638 resolved XML files and indexes
  21,283 named definitions across 185 element types without parse failures.
- Optional `eaw-schema` loading now layers EAW/FoC type, tag, enum, hardcoded
  token, and metafile definitions over the XML index. The audit validates field
  membership/repetition, common scalar and enum shapes, and typed XML-object
  references while retaining source-line diagnostics. Canonical elements,
  metafile registries, and conservative structural matching type 20,881 of the
  21,283 named stock definitions. Object-kind checks consume behavior, flag,
  membership, and variant-inheritance rules from the public schema.
- Semantic DDS decoding validates dimensions, mip/subresource layout, arrays,
  cube/volume metadata, legacy masks, BC1-BC5, and DX10 formats. Semantic MTD
  decoding covers atlas entries, rectangles, alpha flags, and name CRCs. The
  stock corpus decodes all 2,213 DDS files and the 1,107-entry command-bar MTD.
- Legacy ALO materials now normalize shader family, blend mode, and texture
  semantics while preserving every original shader parameter. All observed
  stock mesh shaders and texture parameter names have a classified fallback.
- Lua inspection covers source dependencies, function definitions/calls, and
  both standard Lua 5.0 and Petroglyph persistent bytecode prototypes. The
  stock layered corpus contains 370 source scripts (85,312 lines), with every
  detected dependency resolved and no inspection failures.
- M1 is now a stable baseline for M2 consumers. Deeper per-shader render-state,
  lighting, and Lua host-API behavior are implemented incrementally against the
  compatibility inventories as renderer and simulation consumers come online.
- Deliverable: asset inspector that can browse every stock and selected mod
  asset and report unresolved references.

## M2 — visual sandbox (complete)

Complete: a stock space unit and land and space maps load and can be inspected
through the D3D12 renderer. Remaining render work is the legacy-mode render
backlog listed in AGENTS.md; it is scheduled when gameplay or evidence needs
it.


- Window/input/platform layer and the initial Direct3D 12 RHI are implemented:
  a flip-model waitable swap chain, triple-buffered allocator contexts,
  independent direct/copy queue fences, asynchronous initial uploads, resize
  handling, optional tearing, WARP and GPU-validation modes, and DRED device
  removal diagnostics. A textured validation primitive remains available as a
  renderer-only fallback through the same default-heap upload path.
- Initial engine-native ALO geometry, base-color DDS mip uploads, legacy blend
  translation, depth buffering, orbit camera, and GPU ALA skinning are
  implemented in the D3D12 sandbox.
- The owner-provided legacy Direct3D 9 reference corpus in `Shaders/` is
  inventoried in [Legacy shader mapping](legacy-shader-mapping.md). All 39
  readable effects have reference profiles (render phase, depth/cull/alpha-test
  state, lighting model, gloss/colorize/opacity channel roles, UV and vertex
  animation, vertex requirements) that cover 84% of stock material instances;
  the rest keep tagged name heuristics. New shaders are independently written
  modern HLSL; the reference corpus is never compiled, shipped, or required at
  build/runtime.
- Legacy-mode surfaces are implemented in the D3D12 sandbox: engine-native
  materials (lighting model, gloss source, team-color and self-illumination
  masks, Diffuse/Specular/Emissive/Color, UV scroll, base/normal/gloss textures)
  shaded with a fill plus key-light approximation, a bindless-style texture
  table, a material structured buffer, and per-frame constant buffers. Maps are
  lit by their active TED environment (key, two fills, specular, ambient) and
  team colors come from faction XML. Reflections, planets, shields, and effects
  remain.
- Legacy mode first: render materials from the legacy reference profiles with
  newly written shaders so the original game can run on the new engine and its
  behavior can be stabilized. Add normal/gloss/emissive/team-color and special
  material behavior in that mode. A physically based material path with a
  documented conversion from legacy profiles follows once legacy mode is stable,
  on top of the offline shader pipeline: HLSL sources compiled by DXC (D3D12,
  shader model 6.0, root signature in HLSL) and FXC (D3D11 tools) into embedded
  bytecode, plus an opt-in `ID3D12PipelineLibrary` cache. Reflection metadata is
  deferred until material shaders need generated bindings.
- Add animation selection/visibility handling, selection IDs, debug drawing,
  resource handles, descriptor allocation, streaming, and hot reload. Animation
  selection is done: `sandbox --model` cycles the model's animations with
  Left/Right, and rigid attachments follow animated bone visibility through a
  visibility-collapsed second palette half. Selection IDs are done: a GPU pick
  target and per-frame readback select the instance or terrain under a click
  without stalling, and the sandbox highlights the selection. Debug drawing is
  done: world-space line lists (bounds, axes) drawn depth-tested from a
  frame-buffered buffer; the sandbox outlines the selection and toggles every
  object's bounds with B. The sandbox cameras are inspection tools, not the
  game camera; gameplay camera and input belong to M3/M4 (see there).
- On-screen debug overlay: done. A movable, resizable, minimizable Dear ImGui D3D12 panel (F1)
  shows FPS, CPU and GPU frame time, draw calls, triangles, instances, video
  memory, texture streaming, scene, camera, animation, selection, and hot-reload
  status. Interactive in-engine tools can build on it later. Liquid surfaces may
  get their own pick id so clicking water reports it. Started:
  the D3D12 renderer keeps textures in a generation-checked handle pool, takes
  texture-table slots from an allocator that reuses a slot only after its
  release fence, and retires replaced scene resources on the direct-queue
  timeline, so `load_scene` can replace a scene while frames are in flight.
  Scenes build on a worker thread, and textures stream in under a per-frame
  upload budget with fallbacks until they arrive. An engine asset cache keeps
  decoded models, textures, maps, and the XML catalog with recorded file
  dependencies, and `sandbox --hot-reload` rebuilds the scene when a loose file
  or MEG archive behind it changes. Decoded textures carry an identity, so a
  reload keeps unchanged textures on the GPU and uploads only the changed ones
  (Kamino: 0.5 s rebuild, 1 of 67 textures uploaded). Per-asset geometry handles
  and several resident scenes remain.
- Camera, selection IDs, debug drawing; PBS conversion of legacy materials after
  legacy mode is stable.
- Placed TED objects resolve to XML object types by uppercased-name CRC-32 and
  to models through variant inheritance (99.7% of stock placed objects).
  Space squadrons expand into their members at their `Squadron_Offsets`, and
  `EAWEngine sandbox --map` renders every placed object of a map as one
  instanced D3D12 scene with its environment sky dome/backdrop and an
  engine-owned tactical camera (pan, rotate, zoom within the map bounds). Land maps render their textured terrain heightfield, water, lava, or ice surfaces, and track ribbons. Land teams/companies (runtime formations), decals,
  transparency sorting, and streaming are not implemented yet.
- Add production D3D12 tactical-map rendering. Further tool-only map/model
  preview polish is deferred until it can support the renderer and map editor.
- Deliverable: load and inspect a stock space unit and a tactical map. Both
  load in `EAWEngine sandbox` (`--model`, `--map` for space and land), with
  resource handles, streaming, and hot reload.

## M3 — tactical simulation slice (active)

Deliverable: a deterministic headless battle and a rendered space skirmish
slice. Status as of the first combat slice:

Done:

- Simulation core (`src/simulation`, no graphics, platform, or file access):
  generation-checked entity ids, structure-of-arrays components, a fixed-step
  scheduler, commands applied at tick boundaries, immutable snapshots, and a
  state hash for determinism checks.
- Map objects spawned as entities from their XML types; a renderer instance
  API fed from interpolated snapshots.
- Legacy RTS camera and input following the original: no rotation (fixed pitch
  and heading), zoom, and keyboard panning. Left button click and box
  selection; right button move and attack orders; raw input. Exact angles,
  zoom range, and pan speeds still need measurement. A free camera may become
  an engine extension later; the sandbox's free debug cameras stay for
  inspection.
- Terrain following and passability by movement class
  (`MovementClassTypeDefs.xml`; the Obstacle bit and runtime wall and shield
  bits are unconfirmed). Flyers keep their altitude.
- Deterministic grid A* per movement class, with region labels and smoothed
  waypoints. The staged plan toward large battles is in
  [pathfinding-research.md](pathfinding-research.md); it is deferred until
  the game runs stably.
- First combat slice:
  - health, regenerating shields, and armor types with `GameConstants.xml`
    damage modifiers;
  - one projectile weapon per unit;
  - a spatial grid, automatic targeting, and attack orders that chase;
  - homing shots and death.

  Map owner slot 3 is provisionally neutral.

Gaps toward the deliverable, in planned order:

1. **Group movement.** Steps 1 (footprints, separation, turn rates), 2
   (formation slots for group orders, group speed), and 3 (company
   expansion on maps and as spawns) are done, with queueing at narrow
   passages, re-forming, static obstacle footprints, a per-tick search
   budget, and one shared route per formation (ordering every unit on Utapau
   costs at most about 12 ms in one tick)
   ([plan and sources](pathfinding-research.md#group-movement-and-formations)).
   Covers:
   - separation, so units do not stack;
   - formations for group orders;
   - turn rates (`Max_Rate_Of_Turn`, 442 stock uses);
   - expanding land companies and teams into their units. `Company_Units`
     occurs in 179 definitions, and 358 company placements in owner slots 0
     and 1 are skipped today, so maps start without their infantry.
2. **Space combat.** Step 1 is done: weapon hardpoints fire, entities carry
   several weapons, ships separate, and squadrons expand fully and spawn.
   Step 2 is done: fire cones from the hardpoint fire bones, per-weapon
   targeting, and hardpoints as targetable, destroyable parts (shield
   generators, engines, weapons). Fighter movement is done: fighters
   (`FIGHTER_LOCOMOTOR`) never stop below their `Min_Speed`, circle when idle,
   and make attack runs that pass the target and turn back. The simulation
   then separated its tick into layers (commands, behavior, navigation,
   locomotion, combat; [architecture](architecture.md#simulation-tick-layers)),
   so decisions such as chasing, attack runs, and engaging on one's own live in
   the behavior layer, with an engine-native `Stance` (aggressive, hold
   position, hold fire) that AI and Lua can later drive through commands.
   Next, from observing the original (compatibility notes, "Fighter flight"):
   fighters fly in 3D (a pitch beside the heading; presentation poses pitch
   and bank), separate from squadron mates, reverse direction through a
   vertical half-loop, and dogfight, chasing each other above and below their
   plane before returning to it.
   Covers:
   - weapons on `HardPoints` (94 stock uses);
   - space movement (turning, speed, and the space layer);
   - squadron behavior.
3. **Headless battle.** An `EAWEngine` command runs a map or a scripted
   skirmish without a window for N ticks and prints the state hash and
   statistics. Spawning becomes a deterministic command, so a run can be
   replayed from its command log. This is also the headless benchmark the
   performance gates require.
4. **Unit animation on maps.** Per-instance bone palettes, with idle, move,
   fire, and death animations chosen from simulation state. Models on maps
   are still frozen in their bind pose.
5. **Runtime mesh registration,** so types not requested at build time can be
   spawned.
6. **Remaining M3 systems:**
   - the dependency-aware job system, once measured budgets need it;
   - fog of war;
   - a minimal Lua host surface;
   - a local-player concept (which owner the user commands, and team colors
     by owner).

Known provisional choices to verify against the original:

- the ×30 XML speed scale;
- shield refresh units, and how shields and armor combine;
- ranges measured in the ground plane;
- the owner-slot hostility rule;
- world handedness.

## M4 — space and land modes

- Navigation/pathfinding for both modes, build pads, reinforcements, abilities,
  transports, shields, capture, retreat, tactical AI, UI and audio.
- Deliverable: complete skirmish matches with replay determinism.

## M5 — galactic and campaign

- Planets, fleets, economy, construction, heroes, transitions to tactical,
  story events, campaign AI, persistence and save versioning.
- Deliverable: one stock campaign playable end to end.

Engine extensions (non-legacy mode, opt-in): player-controlled unit stances.
The original gives players no stance control; the simulation's `Stance`
already exists for AI and scripts, so a stance control can later be offered as
an option outside legacy mode.

## M6 — compatibility and SDK

- Broad XML/Lua API coverage, Steam mod discovery, mod stack diagnostics,
  multiplayer protocol, replays, authoring/import tools, profiling overlays.
- New namespaced mechanics API and documented extension points.
- Modern model and animation formats as optional, engine-native assets beside
  ALO/ALA: FBX and glTF 2.0 import of meshes, skeletons, skinning, and
  animation clips, with modern animation features (blending, layering,
  additive clips, retargeting, root motion, blend shapes). Imported materials
  map to the PBS material model. ALO/ALA stay the compatibility baseline and
  existing mods keep working unchanged; new formats load through the same VFS
  layers, asset cache, and hot reload. FBX needs an importer whose license fits
  the project (an open-source reader such as ufbx, or the proprietary Autodesk
  FBX SDK); that choice is open.

## Cross-cutting engineering work

- **Raw input (done).** `src/platform` reads mouse movement and the keyboard
  through `WM_INPUT` (`RegisterRawInputDevices`): unaccelerated device deltas in
  `InputState::raw_mouse_delta_x/y` (absolute devices such as tablets and remote
  desktop are converted to deltas) and raw key state. Cursor position, buttons,
  and wheel stay on window messages, so clicks outside the client area never
  reach the scene. Queued raw input is drained in batches with `GetRawInputBuffer` once per frame before the message loop, the
  approach Microsoft's raw input guidance and Godot's fix for 4-8 kHz mice
  describe (per-message dispatch made high polling rate mice collapse the frame
  rate there); `GetRawInputBuffer` removes the messages it reads, so nothing is
  counted twice. Not yet verified with a high polling rate mouse.
  The sandbox rotates cameras with raw deltas; `--no-raw-input` falls back to
  window messages. Input timestamps and GameInput (controllers) are later options.
- **Exception-free error handling (done).** No exceptions: every target
  compiles with `/EHs-c-` and `_HAS_EXCEPTIONS=0`. Recoverable failures
  (malformed data, missing assets, failed map or model builds, invalid input, a
  window or device that cannot be created at startup) return `eaw::Status` /
  `eaw::Result<T>` with contextual messages and are logged as warnings
  (stderr, the debugger output window, and a per-run log file) where the
  caller falls back. Programming errors are `EAW_ASSERT_MSG`; unrecoverable
  runtime failures (D3D12 calls after startup, device removal) are
  `eaw::fatal_error` with their diagnostics; both end in the crash handlers'
  report. Phases: (A) status types, log, sinks, done; (B) core decoders and
  VFS, done; (C) engine, cache, simulation, done; (D) renderer (startup through
  `D3d12Renderer::create`, `load_scene` returns `Status`), done; (E) tools,
  done; (F) compile with exceptions disabled and replace throwing
  standard-library calls (`std::from_chars`, `std::error_code` filesystem
  overloads, hand-written parsing instead of `std::regex`), done.
  Background from the original assessment: the codebase reported errors by
  throwing (about 270 `throw` sites and 66 `catch` blocks, half of the throws in
  core decoders). The goal is explicit error values: a project `Result` type
  carrying a contextual error (virtual path, section, offset), returned by
  decoders, the VFS, the renderer, and the engine. (Handlers at boundaries to
  throwing code turned out to be unnecessary: the standard library is used in
  its non-throwing forms and Dear ImGui does not throw.) Migration is incremental: introduce the type and a policy, write
  new code with it, then convert module by module (core decoders first, then
  the renderer's HRESULT paths, the engine, and the tools), each with its tests.
  The project type stays on C++20; exceptions end disabled, with allocation
  failure fatal. Warnings may later also be shown while the engine, game, and
  mods load.

- **Development launcher (done).** `EAWDevLauncher` edits, saves, and starts
  sandbox scenarios (installation, mods, map or model, debug spawns with
  owners, engine options) and tracks the runs it started. It shares the window
  shell with EAWTools (`eaw_imgui_app`) and the scenario model and process
  start with `EAWLauncher` (`eaw_launch`, unit-tested in the `launcher` test
  module).

## Performance gates

- Headless deterministic simulation benchmark before renderer optimization.
- 60 Hz presentation with configurable simulation tick.
- No fixed texture dimension, vertex count, entity count, or 32-bit address-space
  assumptions in new formats.
- Automated budgets for frame time, simulation tick, streaming throughput,
  memory, draw/dispatch counts, and worst-case path requests.

