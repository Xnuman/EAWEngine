# Architecture

How EAWEngine is built: its layers and libraries, how data travels from a game installation to the
screen, and the rules each part keeps. [status.md](status.md) records what is implemented and its
limitations, [roadmap.md](roadmap.md) the plan, and [AGENTS.md](../AGENTS.md) the working rules for
contributors.

## Product boundary

`EAWLauncher` chooses a game variant (Empire at War or Forces of Corruption), an installation, a mod
stack, and engine options, then starts `EAWEngine`. Neither program starts, injects into, or patches
the original executable. The engine reads the player's installation and mods in place; they are
read-only inputs, and anything the engine or the tools generate goes to a build directory or a
directory the user chose.

```text
owned game + mods (read-only)
        |
        v
layered VFS --bytes--> format readers --…Data--> loaders --> engine resources
                                                                  |
                                     +----------------------------+------------+
                                     v                                         v
                          deterministic simulation  --snapshots-->  presentation
                          (fixed ticks, commands)                   (animation, effects,
                                     ^                               cameras, input)
                                     |                                         |
                          orders from players, tools,                         v
                          and later AI and scripts                 render contract --> D3D12
```

## Layers and libraries

Every library and executable has its own directory under `src/` with its own `CMakeLists.txt`.
Dependencies point inward: a lower layer never includes or links a higher one.

| Layer | Library | Responsibility | Depends on |
| --- | --- | --- | --- |
| Foundation | `eaw_math` | Vectors, quaternions, matrices, transforms (scalar float, row vectors). | — |
| | `eaw_core` | `Status`/`Result` and fatal errors, the log, string utilities, whole-file I/O, CRC-32, little-endian reads, the PCG32 presentation random stream. | math |
| Data | `eaw_vfs` | Layered virtual file system, MEG archives, path normalization, installation discovery. | core |
| | `eaw_formats` | Lossless readers and writers of the original's file formats; no I/O. | core, math |
| | `eaw_assets` | The game asset layer: XML object database and catalogs, sound events, loaders, engine-native resources, the resource manager. | core, formats, vfs, math, render contract, audio |
| | `eaw_localization` | DAT string tables on disk, TSV workspaces, mod text scans, translation checks. | core, formats |
| | `eaw_image_compare` | Image regression: how two RGBA images differ (capture checks, `compare-images`). | core, formats |
| Runtime | `eaw_simulation` | Deterministic tactical simulation: entities, components, ticks, commands, snapshots. | core, math |
| | `eaw_script` | Sandboxed Lua 5.0 host: threads, engine events and callbacks, recording stubs for the unimplemented host API. | vfs, Lua |
| | `eaw_battle` | Battles and order logs, the battle runtime, scripts in battles, the battle side of maps, gameplay reads of XML types and models. No render contract. | assets, script, simulation |
| | `eaw_presentation` | Battle frames, scene presentation, rigs and unit animation, particles, effects, projectile visuals, battle sounds, debug drawing. No graphics or audio API. | battle, audio, render contract |
| | `eaw_scene` | Scene building: maps, models, terrain, materials, particles, and rigs as engine-native visual data. | battle, presentation, render contract |
| | `eaw_control` | Camera controllers and math, RTS selection and picking, the terrain height field. | simulation, render contract |
| | `eaw_sandbox` | The sandbox's testable parts and the headless battle command. | battle, presentation, scene, control |
| Presentation | `eaw_render_contract` | Engine-native visual data, one header per subject (`visual_vertex`, `visual_texture`, `visual_material`, `visual_mesh`, `visual_camera`, `visual_frame`, the `visual_scene` aggregate), header only. | math |
| | `eaw_render_core` | Rendering rules without a graphics API: detail-level selection (`lod_selection`), GPU resource lifetime (handles, deferred descriptor frees, fence retirement), and the frame graph's barrier planning (`frame_graph`), shared by backends and tested without a device. | core, render contract |
| | `eaw_render_d3d12` | The Direct3D 12 backend. | core, render contract, render core, formats (RGBA images for captures) |
| | `eaw_platform` | Win32 window, raw input, crash handlers, log sinks. | core |
| | `eaw_audio` | The engine-native `Sound` (16-bit PCM), the `AudioDevice` interface, spatial gains, the null device. | core, math |
| | `eaw_audio_devices` | The backends: XAudio2 (default), FMOD (only with `EAW_AUDIO_FMOD` and a local SDK). | audio, XAudio2, FMOD |
| Programs | `eaw_launcher` | Launch configuration, sandbox scenarios, child processes. | vfs |
| | `eaw_imgui_app` | Shared Win32 + Direct3D 11 + Dear ImGui shell of the desktop tools. | core, platform, imgui |

The executables sit on top: `EAWEngine` (`src/engine_app`), `EAWLauncher`, `EAWDevLauncher`, the
`EAWTools` workbench, the `EAWAssetInspector` and `EAWDatEditor` command-line tools, and the
`EAWEngineTests` suite. The tools use the same readers, VFS, and asset layer as the engine; a
capability one of them needs goes into a library, never into scraping another program's output.
Third-party code is pinned in `third_party/`: Dear ImGui (docking branch, by commit) and Lua 5.0.3
(MIT, by archive hash, built with only the base, string, table, and math libraries).

Every library and executable keeps its headers under `include/<directory>/` and its sources
(with private headers) under `src/`, and headers are included by short paths
(`"core/status.hpp"`, `"simulation/simulation.hpp"`, `"battle/battle.hpp"`). A directory with
two targets shares one include folder (`render/` for the contract and the D3D12 backend, `audio/`
for the contract and the devices). Debug builds of every library and executable carry the `_d`
suffix.

## Resource pipeline

A resource reaches the engine in three stages, each owned by one layer:

1. **Bytes.** The VFS resolves a virtual path through its layers and returns the winning entry's
   bytes: a MEG entry, or a loose file read through `core/file_io`.
2. **File-level data.** A reader in `eaw_formats` turns the bytes into a plain `…Data` structure,
   losslessly and without I/O: `read_alo<AloModelData>`, `read_ala`, `read_ted`, `read_dds`,
   `read_dat`, and so on. A format that holds several kinds of data is read through a template
   (`read_alo<AloGenericData>` tells a model from a particle system). Unknown chunks, flags, and
   parameters survive; offsets, counts, and sizes are validated before anything is allocated.
3. **Engine resource.** A loader in `eaw_assets` applies the interpretation rules (legacy materials,
   `pe_*` proxies, `_ALT<n>`/`_LOD<n>` names, particle fidelity) and builds the engine-native
   resource: `Model`, `Skeleton`, `Animation`, `ParticleEffect`. Resources know nothing of their
   source format; another format means another reader and loader producing the same resource.

`ResourceManager::get<T>(path, ...)` sits above the loaders. Each resource type says once how it
loads by specializing `ResourceLoader<T>` (its cache kind, key, and load function); extra arguments
reach both, so a particle effect is cached per fidelity profile. The asset layer specializes it for
models, animations, particle effects, and TED map data; the engine for textures, the XML catalog,
movement classes, and combat content. Generation-checked resource IDs are deferred until a consumer
needs references that survive a reload (hot-swapping one resource in a running scene, scripts,
saves, GPU residency).

The `AssetCache` under the manager is keyed by kind and key and tracks where every resource came
from. Loads read through the `AssetReader` interface, which the VFS implements; a
`RecordingAssetReader` records a fingerprint of every resolve (winning mount, container, entry
offset and size, and the container's write time), every lookup that found nothing, and a hash of
every listing. A cached resource keeps the dependencies gathered while it loaded, and reusing it adds
them to the caller's recorder, so a scene knows every file behind it. A resource is stale when any
fingerprint changes: an edited loose file, a new file in a higher-priority layer, or a rebuilt
archive. A MEG mount indexes its entries once, so a rebuilt archive is remounted (`replace_mount`)
between builds, never while a worker reads. Each build starts a cache epoch and evicts what it did
not use, so long hot-reload sessions do not accumulate replaced resources.

File-level data lives only as long as a loader or a tool needs it. The tools (the inspector,
EAWTools) show it directly; the engine keeps only engine resources.

### VFS precedence

The layer order is a public contract, covered by tests: manifest archives from low to high priority,
unlisted archives in lexical order, `Patch.meg`, numbered patch archives, `64Patch.meg`, loose files
over archives, Forces of Corruption data over inherited Empire at War data, and mods over game data
with the first supplied mod highest. Paths are case-insensitive and separator-normalized. Listings
show each virtual path once, with its winning layer; lower layers stay available for fallback.

## Simulation

Gameplay runs on a fixed tick (60 Hz, the owner's decision of 2026-11-15; the original's logic rate
is not measured, and XML values per 30 Hz frame are converted to seconds apart from it, in
`assets/legacy_units.hpp`); presentation runs at its own frame rate. Every duration the simulation
counts is in seconds and turned into ticks by the clock (`ticks_for`), so the rate is a parameter
(`--tick-rate` on headless battles). The world stores units in structure-of-arrays components
under stable entity ids and publishes immutable snapshots. Presentation-only data (visual keys,
effects, derived speeds) never enters the state hash and never feeds back into the simulation.
The state hash, recorded order logs, and `--check-determinism` guard determinism; random streams,
iteration order, floating-point order, and merge order are compatibility state.

### Tick layers

Each tick runs fixed layers in order. A layer reads what earlier layers decided and never does a
later layer's job, so AI, scripts, and new mechanics stay replaceable without touching movement or
combat rules.

1. **Commands** (`simulation_behavior.cpp`, `apply`): orders from players, tools, and later AI and
   Lua (move, formation move, attack, stop, set stance), applied in submission order.
2. **Behavior** (`simulation_behavior.cpp`, `run_behavior`): what each unit wants from its order, its
   `Stance`, and its target: a chase move request for an ordered attack or, for flyers, a steering
   intent (attack runs, flying to a destination, circling). Stances decide whether a unit picks
   targets and moves for them; orders always apply. Standing orders (attack-move, guard) decide here
   when to engage (a scan for hostiles on the target-search interval), when to break off (the
   leash), and where to go next; the engagement itself is an ordered attack.
3. **Navigation** (`simulation_navigation.cpp`): route requests under a per-tick search budget,
   shared formation routes, group cohesion, the local avoidance of space ships (a course for the
   tick that keeps clear of the neighbours' predicted motion, as a detour point), and on land giving
   way and each moving agent's ORCA velocity (`simulation_avoidance.cpp`).
4. **Locomotion** (`simulation_locomotion.cpp`): physical movement only (speed limits, turn rates,
   ground following and passability, flight above a minimum speed, following a detour point or an
   avoidance velocity, never moving deeper into a unit it touches, walks played as clips:
   `simulation_walk.cpp`); then contact (`simulation_contact.cpp`): crushing, and separation of
   flyers among their owner's (nothing pushes a ground unit).
5. **Combat** (`simulation_combat.cpp`): shields, target selection under the stance, weapons,
   projectiles, damage, and removal of the dead. Combat never moves a unit.

Every unit decides from the state at the start of the tick, so decisions do not depend on storage
order; behavior reads last tick's targets, so a target picked in combat steers a unit from the next
tick on.

### Battles

A `Battle` (`battle/battle.*`) sets a world up from a built map and takes orders in replayable
terms: XML type names, owner slots, points, and entity ids. Each order is validated, translated into
simulation commands for the next tick (a spawn order becomes a `spawn` command carrying the entities
laid out on a grid or in formation), and recorded with its tick. Entity ids are allocated
deterministically, so the same map, game data, and order log reproduce the same battle, in the
sandbox or headless (`EAWEngine battle`). Nothing the simulation reads depends on how a scene is
built for drawing: the ground (heights, passability, liquid level) and spawns come from map and XML
data and the models' geometry. `build_battle_map` builds only that side of a map (a
`BattleMapAsset`: entities, spawnable types, ground, damage table); `build_visual_map` runs the same
walk over the placements and then adds the look of each entity and type at the same index
(`MapEntityLook`, `SpawnableLook`), so a headless battle hashes like the sandbox.

Orders come from the RTS input layer. Box selection projects snapshot positions through the camera
(`camera_math`, matching the renderer's projection); a right-click finds its world point on the CPU
in the same frame by marching a ray across a `TerrainHeightField` sampled like the rendered terrain
and refining by bisection, so orders never wait for a GPU readback. Group orders become one command
per unit, spread in id order so the command stream stays deterministic.

## Scripting

`eaw_script` hosts the game's Lua 5.0 scripts. Each script gets its own sandboxed state: no file, OS,
or debug access, `print` to the log, `math.random` from its own seeded stream, `require` resolved
through the VFS, and compiled chunks refused (Lua 5.0 does not verify bytecode). Script threads are
coroutines woken on simulation ticks, and every call and resume runs under an instruction budget so
a script that never yields cannot hang the engine. Mods are untrusted, so the sandbox also holds
against hostile scripts: no `newproxy` and locked engine-object metatables (no forged objects, no
`__gc` outside the budget), a spent budget or a memory error that `pcall`, `xpcall`, and
`coroutine.resume` re-raise instead of swallowing, a memory cap per Lua state and a step limit on
pattern matching inside the Lua build (`third_party/lua/eaw_lua_config.h`), caps on `string.rep`,
pattern length, and script threads (`script_host.hpp`), a `collectgarbage` that only collects, and a
script that runs out of memory stopped for good. The host functions implemented so far are real;
every other engine function a script calls is a recording stub that logs the call and returns a
neutral object, so `EAWEngine lua-survey` can measure which host API the stock scripts use before the
engine implements it. The engine talks to a script through `ScriptValue`s: it sets globals (the
objects a script expects), reads definitions back (an AI plan's `TaskForce`, a story script's
`StoryModeEvents`), and queues events, which the framework's `PumpEvents` drains through `GetEvent`
in a script thread; script functions handed to engine functions become callbacks it can queue.

Real engine objects reach scripts through a `ScriptObjectProvider` that the engine implements and
the host only calls. Each object is a userdata (as in the original), one Lua value per object for
interned kinds so `==` and table keys work; a kind's method names resolve to numeric ids once, and a
call reads its arguments from the Lua stack and pushes its results without converting or
allocating. Names the provider lacks stay recording stubs. In battles the provider is
`BattleScripts` (`battle/battle_scripts.*`), one context per script, under three rules that keep
the script phase deterministic and ready for threads:

1. every script reads the same immutable view, the snapshot of the tick that just ran with indices
   by entity slot and by type built once before any script runs;
2. everything a script does to the battle goes into its own buffer; the buffers are merged after all
   scripts ran, in the order the scripts were added, as validated `BattleCommand`s for the next
   tick;
3. script orders are not logged: a replay runs the same scripts again, which is the lockstep model
   (only player input is exchanged).

Scripts also ask for presentation: `GameObject.Play_SFX_Event` buffers a sound request per script,
merged the same way into `BattleScripts::sound_requests()`, which the sandbox plays after each
script phase. Requests never reach the simulation, the hash, or the log; headless battles ignore
them. Music, speech, and weather requests are to use the same channel.

Game-object scripts start on their own: when an entity appears whose type names a `Lua_Script`,
`BattleScripts` starts that script with `Object` bound to the entity, and ends it when the entity is
gone. Placed objects keep their map hints for `Find_Hint`. Proximity handlers registered with
`Event_Object_In_Range` are evaluated when the object is serviced and queued as events, so they run
after the script's next yield. Headless battles and the sandbox both run the script phase after
every tick (`--no-scripts` turns it off).

An order returns a block; it compares the order's serial (Battle numbers every order, and the
simulation keeps the serial of the order each entity follows, outside the state hash) with the
entity's, so it can tell an order not applied yet, running, finished, and replaced. A game object is
generation-checked: a dead or reused entity makes `Is_Valid` false, queries on it give nil, and
orders to it are dropped with a warning (an engine choice). Scripts run on one thread today; the
rules above let the job system run them in parallel once measurements call for it. Scripts will act on the world only by enqueuing validated commands, never by
writing simulation storage.

## Presentation

Presentation turns snapshots into a frame and never decides gameplay. `ScenePresentation`
interpolates the last two snapshots and re-poses each entity's instance (its placed tilt and scale
kept, the heading change applied about +Z, the position from the snapshot); the renderer receives the
whole instance list every frame. Selection is a set of entity ids.

- **Animation.** `UnitAnimator` chooses each unit's clips from its snapshot state and samples poses
  on the CPU (`assets/pose`): up to three weighted clips (two neighbouring locomotion clips by speed,
  and an attack fading in or out) blend in bone space, with weights rounded to eighths so units in
  the same state share a pose. Units killed in combat leave corpses that presentation keeps for
  their death clip and a short linger.
- **Particles.** `ParticleEffect`s, loaded under a fidelity profile, play in the engine's
  `ParticleWorld` on the frame clock from a seeded random stream per effect and emit sorted
  `VisualParticle` sprites, which the renderer expands to quads on the GPU. Effects attach to units
  through model proxies (`AttachedEffects`) and follow the animated bone palette; `BattleEffects`
  derives a battle's effects (attached effects, impacts, deaths, projectile bolts) from its
  snapshots.
- **Cameras.** Controllers in `eaw_control` turn input into a `VisualCamera`: an orbit camera for
  single models, an RTS camera over maps within their bounds, and a free debug camera.

## Audio

Sound follows the resource pipeline. The stock effects and responses are WAV files in the sound
archives (`Data/Audio/SFX/sfx3d_non_localized.meg`, `sfx2d_non_localized.meg`, and
`sfx2d_<language>.meg`), which store bare names and are not in `megafiles.xml`: `mount_game_assets`
mounts them under `DATA/AUDIO/SFX/` for each layer (after its archives, below its loose files), the
language archive of `--language` only, and archive discovery skips that folder. `read_wav` (formats)
keeps the file's chunks; `decode_sound` (assets) recognizes a file's format from its first bytes and
converts it to the engine-native `Sound`, so a new format (MP3, Ogg) is one reader and one case
there, and backends only ever receive PCM. `SoundEventCatalog` reads the XML `SFXEvent`s with their
presets; `resolve_sound_sample` finds a sample under the SFX folder or the language's speech folder.

`AudioDevice` (`eaw_audio`) plays PCM voices in categories (effects, voice, music, interface) with
volume, pitch, pan, and looping; `create_audio_device` (`eaw_audio_devices`) picks XAudio2, FMOD, or
the null device, and the sandbox falls back to the null device when a backend cannot start.
`BattleAudio` (`eaw_presentation`) is presentation like `BattleEffects`: from each tick's snapshot it plays
weapon fire and deaths, each frame it keeps engine (moving or idle) and ambient loops on the nearest units, places voices
by the listener (the camera's focus raised by AUDIO.XML's pullback), and applies the event rules
(chance, instance limits, overlap groups, sample order, volume and pitch ranges, predelays, chains).
Animations carry sounds too: `ANIMATIONSFXMAPS.TXT` places sound events and surface triggers
(footsteps, resolved through `SURFACEFX.XML`) at frames of animations; `UnitAnimator` reports the
frames each unit's clips passed in a frame, and `BattleAudio` plays what they reach. Its random
choices come from its own `Pcg32` stream; nothing feeds back into the simulation.

## Renderer

The renderer consumes engine-native data from the render contract (`render/visual_*.hpp`): meshes,
materials, textures, instances, particles, lights, cameras, debug lines, with colors as `Color` and `ColorRgb` (`color.hpp`). It never parses game files
or knows VFS precedence. The first target is **legacy mode**: newly written HLSL driven by the
decoded legacy ALO materials, approximating the original's look so content behaves as authored. A
physically based path with engine-native materials follows, using the same material profiles as
conversion input. The original's Direct3D 9 shader sources, published by Petroglyph with the
official mod tools, are the reference for that mapping
([legacy-shader-mapping.md](legacy-shader-mapping.md)); they stay out of the repository and are
never compiled, embedded, or shipped.

**Damage states and detail levels.** A `VisualMesh` may list `states` (the legacy `_ALT<n>`
number, 0 first), each with detail levels (most detailed first), as ranges of the scene's draws:
untagged parts appear in every range, so geometry is shared and only draw records repeat. The
engine chooses a unit's state (`VisualInstance::state`, gameplay); the renderer chooses its level
(`render/visual_lod.hpp`), by distance, projected screen size, or a hybrid of the two (or off),
scaled by the instance's `lod_bias` (XML `LOD_Bias`) and the mesh-detail setting, with a hysteresis
band. Instances with a stable `key` (the entity key) cross-fade between levels: both levels are
drawn for `fade_seconds` with complementary 4x4 ordered-dither masks (`GpuInstance::fade`, a
discard in the pixel shader), so each pixel shows exactly one of them. `VisualLodTracker` holds the
per-key state; the selection and tracking are pure and tested without a device.

### Direct3D 12 backend

- **Queues and fences.** A direct and a copy queue, each with one monotonically increasing fence.
  Frame contexts own their command allocators and recycle them only after that frame's fence
  completes. Uploads run on the copy queue; the direct queue waits on the GPU, never the CPU. CPU
  waits are limited to waitable-swap-chain pacing, reuse of an in-flight frame context, resize, and
  shutdown. The swap chain is flip-model and waitable, with tearing support queried.
- **Lifetimes.** Anything submitted work references stays alive until the fence of the last queue
  that used it completes. Replacing a scene records the direct queue's next fence value and retires
  the old buffers, textures, and palettes against it, without a CPU wait. Textures live in a
  generation-checked handle pool, so a stale handle stops resolving instead of aliasing a reused
  slot, and a released descriptor slot returns to use only after its fence completes. These
  primitives (`gpu_resource_lifetime.hpp`) contain no Direct3D types and are unit-tested alone.
- **Streaming.** Geometry and materials upload with the scene; textures stream within a per-frame
  byte budget. Materials name logical texture ids, and each frame context owns a persistently mapped
  residency table that maps an id to its descriptor slot, or to a fallback until the texture
  arrives, so shader-visible descriptors are never rewritten while a frame may read them. A replaced
  scene takes over the resident textures whose identity it shares, and only the rest stream. Scene
  construction runs on a worker thread; each VFS read opens its own stream.
- **Binding and materials.** Legacy-mode surfaces are `VisualMaterial` records in one structured
  buffer; textures are indexed through one shader-visible table (resource binding tier 2), so a draw
  binds only two root constants: its first instance and its material. Lighting is a `VisualLightRig`
  in a per-frame constant buffer.
- **Draws.** Instances are sorted by mesh, so every mesh draws once with `DrawIndexedInstanced` over
  a contiguous range. Skinned meshes keep model-wide bone indices; every in-flight frame owns a
  persistently mapped bone palette, written only once that frame's fence has completed. Meshes can be
  added to a loaded scene (`add_meshes`), so spawning a type the map was not built with needs no
  reload. Terrain is drawn as triangles that blend three layer materials per pixel; sky domes and
  star backdrops draw first, centered on the camera at the far plane.
- **Passes.** The scene and particles render into an offscreen colour target; bloom works at
  reduced resolution and a composite writes the back buffer; debug lines and the Dear ImGui overlay
  draw last, so they never bloom.
- **Picking and timing.** A second render target holds a pick id per pixel. Each pick copies one
  texel into the frame context's readback buffer and is read when that context is next reused, after
  its fence, so selection never waits and rapid clicks are never dropped. GPU frame time comes from
  timestamp queries read the same way.
- **Shaders and pipelines.** Shaders are engine-written HLSL compiled at build time (DXC, shader
  model 6.0, for Direct3D 12; FXC, shader model 5.0, for the tools' Direct3D 11 previews) and
  embedded as bytecode; nothing compiles at runtime. The root signature is declared in HLSL, so there
  is one definition of the binding layout. An opt-in `ID3D12PipelineLibrary` cache, keyed by the
  bytecode hash and a layout version, persists pipeline states between runs and is discarded when
  the driver or shaders change.

WARP with GPU-based validation is the automated correctness gate; captures (`--capture`) and
`EAWEngine compare-images` guard the look ([tools.md](tools.md#captures-and-image-regression)).

## Errors and diagnostics

Errors are values; every target compiles with C++ exceptions disabled. Recoverable failures
(malformed or missing data, a map or model that cannot be built, invalid input, a device that cannot
be created at startup) return `eaw::Status` or `eaw::Result<T>` with a contextual message naming
the virtual path, layer, section, and offset where known; callers that continue log a warning and
fall back (the sandbox keeps its current view or the validation triangle). Programming errors
(`EAW_ASSERT_MSG`) and unrecoverable runtime failures (`eaw::fatal_error`, for example a failed
Direct3D 12 call after startup, with device-removed diagnostics) end the process. The platform's crash
handlers route these, `std::terminate`, exceptions raised by code outside the engine, access
violations, pure virtual calls, and invalid CRT parameters into one report on stderr, in the run's log
file, and, for interactive runs, in an error window.

## Compatibility policy

Compatibility is measured, not assumed. A supported format or behavior gets a synthetic fixture for
its exact invariant, malformed and truncated cases, a stock corpus audit where the installation is
available, and a note of the evidence in [compatibility-research.md](compatibility-research.md).
Extensions use optional tags or namespaced files, so an original mod stays readable without edits.

### Content contract versus legacy implementation

EAWEngine runs content made for the original; it does not reimplement the original's internals.
Every original behavior falls into one of three kinds:

1. **Content contract: follow it.** What a data field means as content was authored against it;
   changing it breaks existing assets. Examples: particle lifetime and size variances only shorten
   and shrink, the sign of "inward" speed, atlas slots, a hardpoint's `Damage_Particles` and
   `Damage_Decal` bones, `Max_Speed` per 30 Hz frame.
2. **Implementation artifact: modernize freely.** How the old engine happened to compute a result:
   fixed-function, low-dynamic-range blending, CPU-built quads, emission clumped per frame, unsorted
   transparency, a single global frame step. The engine does the modern thing (sub-frame emission,
   sorted particles, GPU expansion, bloom, frame-rate-independent motion) without a switch.
3. **Look-defining quirk: one named rule per fidelity profile.** Arguably wrong behavior that content
   was tuned against, such as additive particles ignoring alpha, or draw order. Each lives as one
   named rule in an interpretation layer with its evidence (for particles, `particle_rules` in
   `src/assets`): decoders keep the file's facts, normalization produces an engine-native description
   under the **legacy** or **enhanced** profile, and runtimes and renderers see only the result. A
   quirk needs evidence from the game, not from an editor's limitation.

**Legacy** reproduces the original for content that does not know this engine and is the default.
**Enhanced** may deviate where the result improves and authored content still works. Content made
for this engine will declare itself through explicit metadata (see the roadmap's engine extensions),
which overrides legacy inference. Legacy mode identifies model parts by naming and placement (`pe_*`
proxies, `_ALT<n>`/`_LOD<n>` tags, hardpoint bones); each such rule stays in one place per subject so
the planned metadata can produce the same engine-native categories.

## Direction

Planned, not built:

- **Jobs and scale.** A dependency-aware job system dividing each tick into deterministic phases
  (input and script commands; spatial queries and perception; AI planning at controlled cadences;
  movement and paths in parallel; weapons, damage, and abilities; publishing the presentation
  snapshot). Parallel work runs on disjoint partitions or immutable inputs and merges at explicit
  barriers. Large unit counts also need hierarchical spatial indices, shared paths or flow fields for
  groups, AI level of detail, and measured budgets.
- **Renderer.** A thin rendering hardware interface so a Vulkan backend can follow Direct3D 12;
  GPU-driven culling and indirect drawing, mesh LODs, shadow atlases, clustered lighting, HDR,
  temporal anti-aliasing, and the physically based material path. High-resolution content is a
  budget and streaming problem, never a hard-coded dimension or polygon limit.
- **Modes.** Skirmish, galactic conquest, and campaigns ([roadmap.md](roadmap.md) M4 and M5), with
  audio, UI, and saves.

## Source files by subject

The file-level split of each module.

- `src/math` (`eaw_math`): `vector`, `quaternion`, `matrix`, `transform` (compose and decompose),
  and `scalar` (angle constants and conversions).
- `src/core` (`eaw_core`), by category: `error_handling` (`status`, `fatal_error`), `log`,
  `string_utils`, `file_io`, `checksum`, `byte_order`, and `random` (`Pcg32`).
- `src/jobs` (`eaw_jobs`): `job_system` (fibers, counters, workers, the scheduling loop, idle sleep
  on an atomic), `parallel` (`parallel_for`, `chunk_count`), and the private `mpmc_queue` (Vyukov's
  bounded lock-free queue); see [ecs-design.md](ecs-design.md), section 7.
- `src/vfs` (`eaw_vfs`), by category: `file_system` (`virtual_file_system`: readers, mounts, and
  layering), `archive` (`meg_archive`, `asset_path`), and `installation` (`game_installation`, with
  its own minimal `megafiles.xml` reader so the VFS does not depend on the formats; and
  `asset_environment`).
- `src/formats` (`eaw_formats`), by category: `alamo` (`alamo_chunks`, the bounds-checked chunk
  container shared by the Alamo readers; `alo_generic`; `ala_animation` and its helpers; `ted_map`),
  `model` (`alo_model` and its helpers), `particle` (`alo_particle_system`), `texture`
  (`dds_texture`, `mtd_atlas`), `image` (`png_image`, `rgba_image`), `xml` (`xml_document`,
  `xml_helpers`), `localization` (`dat_string_table` and its helpers), `script`
  (`lua_bytecode`), `audio` (`wav_audio`), and `animation` (`animation_sfx_map`).
- `src/assets` (`eaw_assets`), by category: `game_data` (`game_object_catalog`, `type_fields` (typed
  reads of a type's fields through its variants: one number rule, comma lists, yes/no values, and
  diagnostics naming the declaring variant), `spawn_catalog`,
  `map_object_layout`, `surface_effects`, `terrain_decals`, `dynamic_tracks`, `weather_scenarios`, `xml_asset_database`, and `XmlSchema`, split into `xml_schema_load.cpp` for
  the optional eaw-schema YAML and `xml_schema_validate.cpp`), `model` (`model`, `model_geometry` (which geometry is drawn, and its bounds:
  one rule for scene building and the footprints gameplay measures), `legacy_material`,
  and `alo_interpretation` with the `_ALT<n>`/`_LOD<n>` rules and default mesh visibility),
  `animation` (`skeleton`, `animation`, and `pose`: sampling, blending, composition, retargeting by
  bone name; `animation_events`: sounds and surface triggers at frames of animations; `clip_names`:
  the clip roles and the stock clip naming, both ways, for scene, battle, and presentation), `particle` (`particle_rules`: `ParticleEffect`, the fidelity profiles, and
  `load_particle_effect`), `loaders` (`alamo_loaders`: `load_model`, `load_skeleton`,
  `load_animation`), `audio` (`sound_events`: the catalog and sample resolution; `sound_loaders`:
  format recognition and conversion to `Sound`), and `resources` (`asset_cache`, `resource_manager`,
  `resource_loaders`).
- `src/audio`: `eaw_audio` (`sound`, `audio_device`, `spatial_audio`, `null_audio_device`) and
  `eaw_audio_devices` (`audio_devices`, the backend choice; `xaudio2_audio_device`;
  `fmod_audio_device`, built only with `EAW_AUDIO_FMOD`).
- `src/localization` (`eaw_localization`): `localization_workspace` (TSV workspaces, DAT rows, unit
  keys, DAT string tables on disk), `localization_scan` (TEXT_* references in mod XML), and
  `localization_validation` (a translation against its reference).
- `src/simulation` (`eaw_simulation`): `SimulationWorld` split by tick layer: `simulation.cpp`
  (storage, spawning, the tick, the state hash, footprints, snapshots), `simulation_schedule.cpp`,
  `simulation_behavior.cpp`, `simulation_squads.cpp`, `simulation_navigation.cpp`,
  `simulation_avoidance.cpp` (giving way, ORCA velocities, keeping units apart, arrival among
  others), `simulation_locomotion.cpp` (with `simulation_walk.cpp`, walks played as clips),
  `simulation_contact.cpp`, and `simulation_combat.cpp`, sharing `simulation_internal.hpp`; with
  `ground_map` (passability), `clearance`, `chunk_graph`, `pathfinding`, `orca` (the avoidance
  solver), `formation`, `spatial_grid`, `component_pool`, `system_view`, and `combat` (weapon and
  damage data).
- `src/script` (`eaw_script`): `script_host` (the sandboxed state, `require` through the VFS,
  budgeted protected calls, the thread scheduler, the implemented primitives), `script_events` (the
  event queue behind `GetEvent`, callbacks), `script_objects` (engine objects as userdata, the
  provider calls, `ScriptCall`), `script_values` (`ScriptValue` to and from Lua, with read limits),
  `script_stubs` (recording stubs), and `host_api_recorder` (what scripts called), sharing the
  private `script_host_internal.hpp`.
- The engine is five libraries that depend one way: scene building on presentation and battles,
  presentation on battles, the sandbox's parts on all of them; cameras and control stand alone.
  - `src/battle` (`eaw_battle`): `battle` (setup, replayable orders, the order log, spawn grids), `battle_map` and
    `battle_map_builder` (the battle side of a map: placements, companies, spawnable types, ground,
    with the walk the visual build shares in `battle_map_build.hpp`), `battle_runtime` (a battle and
    its scripts on the fixed clock), `battle_log` (reading and writing logs), `battle_scripts`
    (scripts in battles: the view, the provider, blocks, and the script phase), `combat_content`
    (XML combat data translated into the simulation's combat profiles), and `object_types` (what
    gameplay reads of XML types and their models: the cached catalog, movement classes, type
    fields, model footprints, hardpoint models);
  - `src/presentation` (`eaw_presentation`): `visual_rig` (rigs, clips, palettes), `entity_look` (how entities and spawnable
    types are drawn), `damage_states` (a land model's `_ALT` state by health), `battle_frame` (a running
    battle's tick events and frames),
    `scene_presentation`, `unit_animation` (the animator, its pose cache, cues, and corpses, with one
    file per stage of an entity's frame, in order: `unit_animation_locomotion` (state, idles, the
    speed blend, the stride), `unit_animation_attack` (aiming and firing),
    `unit_animation_transitions` (walkers' turns, starts, stops, and walks played as clips), and
    `unit_animation_pose` (the pose key and the turret), sharing `unit_animation_internal.hpp`),
    `particle_effects` (`ParticleWorld`), `attached_effects`,
    `battle_effects`, `debris`, `map_weather`, `battle_audio`, `projectile_visuals`, and `debug_draw`;
  - `src/scene` (`eaw_scene`), behind `visual_scene_builder.hpp` and sharing `scene_builder_internal.hpp`:
    `scene_materials` (textures, materials, lighting), `scene_models` (models and rigs),
    `scene_particles` (particle effects and effect objects), `scene_projectiles`, `scene_debris` (hardpoint breakoff props), `scene_terrain`
    (terrain, liquids, tracks), `type_looks` (what XML types say about their look), and
    `visual_map_builder` (maps and runtime-spawnable packages);
  - `src/control` (`eaw_control`): `camera_controller`, `camera_math`, `rts_control`, `terrain_height`;
  - `src/sandbox` (`eaw_sandbox`), the sandbox's testable parts and the headless command: `sandbox_options` (command line),
    `sandbox_content` (background builds and hot-reload checks), `sandbox_debug_lines` (a pure
    function of a view struct), `battle_runner` (headless runs), and `text_format` (numbers for
    console and overlay text).
- `src/render`: `eaw_render_contract` holds the visual data by subject: `visual_vertex` (model and
  terrain vertices, all the asset layer needs), `visual_texture`, `visual_material`, `visual_mesh`
  (draws, damage states, detail levels, terrain), `visual_camera` (all control needs), `visual_frame`
  (instances, lights, particles, debug lines), and `visual_scene` (the whole scene), with `color`,
  `visual_lod` (the detail-level strategy and settings), and `visual_anti_aliasing` (off, FXAA, TAA). `eaw_render_core` holds `lod_selection`
  (levels, hysteresis, cross-fades), `gpu_resource_lifetime`, and `frame_graph` (passes declare how
  they use each resource; the graph orders the transitions in neutral access kinds, which the backend
  maps to its resource states). The D3D12 backend's public
  interface is `d3d12_renderer.hpp`; the implementation class in
  `d3d12_renderer_impl.hpp` is defined by subject in `d3d12_renderer_device.cpp` (startup, swap
  chain, buffers, waits, retirement), `_pipelines.cpp` (pipeline states and cache),
  `_resources.cpp` (uploads, scenes, texture streaming), `_frame.cpp` (per-frame state and
  `render()`), and `_overlay.cpp`; `d3d12_support` holds HRESULT, queue, and handle helpers. Shaders
  are in `shaders/` (`sandbox`, `terrain`, `particle`, `post`, `debug`, sharing
  `legacy_common.hlsli`).
- `src/platform` (`eaw_platform`): `win32_window` (window and raw input), `win32_crash_handler`, and
  `win32_log_sinks`.
- `src/engine_app` (`EAWEngine`): `main.cpp` (startup, discovery, the doctor probe, dispatch), the
  `lua_survey_command` and `image_compare_command`, and the sandbox session (`SandboxSession` in the
  private `sandbox_session.hpp`), defined by subject: `sandbox_app.cpp` (lifetime, the frame loop,
  the run summary), `sandbox_session_content` (showing finished builds, hot reload),
  `sandbox_session_input` (cameras, selection, picks, orders), `sandbox_session_spawning` (runtime
  types, automation), `sandbox_session_world` (ticks, presentation, animation, effects), and
  `sandbox_session_ui` (panels and debug lines); with `frame_watchdog` (stalled frame steps),
  `sandbox_overlay` (statistics text), and `sandbox_panels` (Dear ImGui panels).
- `src/launcher` (`eaw_launcher`): `sandbox_scenario` (scenarios and presets) and `child_process`.
- `src/asset_inspector` (`EAWAssetInspector`): one file per command group (`inspect_asset`,
  `audit_textures`, `audit_lua`, `audit_maps`, `audit_particles`, `audit_xml`) with
  `inspector_options` and `inspector_common`, declared in `inspector.hpp`.
- `src/dat_editor` (`EAWDatEditor`): `dat_editor_options` (command line), `dat_commands` (one DAT:
  info, list, get, set, remove, validate), and `workspace_commands` (workspaces and mods: export,
  diff, build, scan-mod, add-unit), over `eaw_formats` and `eaw_localization`.
- `src/tools_gui` (`EAWTools`): one file per panel (`gui_asset_workspace`, `gui_map_view`,
  `gui_model_view`, `gui_localization`); `GuiApp::State` in `gui_internal.hpp` with its methods in
  `gui_state.cpp`, shared helpers in `gui_support.cpp`, and the Direct3D 11 `preview_renderer`.
- `tests` (`EAWEngineTests`): one module per subject in `test_<module>.cpp`, each its own ctest
  entry: `formats`, `vfs`, `xml`, `foundation`, `math`; `simulation`, `navigation`, `combat`,
  `flight`; `scene`, `presentation`, `battle`, `effects`, `sandbox`; `script`, `launcher`, and
  `render`. Shared synthetic fixtures (MEG, ALO, ALA, DDS, TED, DAT, particle files) live in
  `test_support.*`.
