# Architecture

## Product boundary

`EAWLauncher` selects a game variant, installation, mod stack, and engine
settings. It launches `EAWEngine`; it never launches or injects into the legacy
game executable. `EAWEngine` reads the user's existing data in place and owns
the new runtime.

The original files are inputs only:

```text
owned game + mods
        |
        v
layered VFS -> format decoders -> immutable asset database
                                      |
                    +-----------------+------------------+
                    v                                    v
          deterministic simulation              presentation world
                    |                                    |
                    +---------- snapshots --------------+
                                                         |
                                                  render/audio/UI
```

## Module boundaries

1. **Platform**: windowing, input, files, clocks, crash reporting, CPU topology.
2. **VFS**: loose directories, MEG archives, mod priority, asynchronous reads.
3. **Formats**: XML, Lua 5.0 bytecode/source, ALO, ALA, TED, MTD, DAT, DDS,
   shaders, audio, and movies. Decoders produce versioned engine-native data.
4. **Asset runtime**: handles, dependency graph, background streaming, budgets,
   hot reload, and GPU upload queues.
5. **Simulation**: deterministic fixed ticks and data-oriented components.
   Parallel jobs operate on disjoint partitions; commands merge at explicit
   barriers. Render frame rate never changes gameplay results.
6. **Modes**: galactic, land tactical, space tactical, campaign/story, skirmish.
7. **Scripting**: compatible Lua surface plus namespaced new APIs. Scripts may
   schedule work but cannot mutate simulation storage outside command buffers.
8. **Presentation**: renderer, animation, effects, UI, audio, and cinematics.
9. **Tools**: launcher, asset inspector, map/model viewers, converter, profiler,
   compatibility test runner, and mod SDK.

## Simulation tick layers

Each tick runs fixed layers in order. A layer reads what earlier layers
decided and never does a later layer's job; this keeps AI, scripts, and new
mechanics replaceable without touching movement or combat rules.

1. **Commands** (`simulation_behavior.cpp`, `apply`): orders from players,
   tools, and later AI and Lua (move, formation move, attack, stop, set
   stance), applied in submission order.
2. **Behavior** (`simulation_behavior.cpp`, `run_behavior`): what each unit
   wants from its order, its `Stance`, and its target: a chase move request
   for an ordered attack, or, for flyers, a steering intent (attack runs,
   flying to a destination, circling). Stances decide whether a unit picks
   targets and moves for them; orders always apply.
3. **Navigation** (`simulation_navigation.cpp`): route requests under the
   per-tick search budget, shared formation routes, and group cohesion.
4. **Locomotion** (`simulation_locomotion.cpp`): physical movement only
   (speed limits, turn rates, ground following and passability, flight at a
   minimum speed), then separation.
5. **Combat** (`simulation_combat.cpp`): shields, target selection (subject to
   the stance), weapons, projectiles, damage, and removal of the dead. Combat
   never moves a unit.

`simulation.cpp` holds the world core: storage, spawning, the tick, the state
hash, footprints, and snapshots. Behavior reads last tick's targets, so a
target picked in combat steers a unit from the next tick on. Every unit decides
from the state at the start of the tick, so decisions do not depend on storage
order (fighters used to decide inside the movement loop, after units stored
before them had already moved).

Above the simulation, a `Battle` (`src/engine/battle.*`) sets a world up from
a built map and takes orders in replayable terms: XML type names, owner slots,
points, and entity ids. Each order is validated, translated into simulation
commands for the next tick (a spawn order becomes a `spawn` command carrying
the entities laid out on a grid or in formation), and recorded with its tick.
Entity ids are allocated deterministically, so the same map, game data, and
order log reproduce the same battle, interactive or headless. Nothing the
simulation reads may depend on how the scene is built for drawing: the ground
(heights, passability, liquid level) and spawns come from map and XML data,
and the state hash leaves out presentation keys, so a headless build without
textures or animations hashes like the sandbox. Presentation
draws order-spawned entities by their type's visual key, without knowing their
ids in advance.

## Rendering direction

The first render target is legacy mode: newly written shaders driven by the
decoded legacy ALO materials and their reference profiles, approximating
original Alamo rendering so the game can run on the new engine and its behavior
can be stabilized. A modern physically based shading pipeline with
engine-native materials follows, using the same profiles as conversion input.
Legacy shaders are mapping evidence only and are never used at runtime.

Particle effects are presentation, like unit animation. The core decodes
particle ALOs into immutable systems; the engine's `ParticleWorld` plays them
on the frame clock from a seeded random stream per effect and emits sorted
`VisualParticle` sprites; the renderer expands those to quads on the GPU. The
simulation never reads particle state, so effects can change, be skipped, or
run at any frame rate without affecting state hashes. Effects attach to
entities through model proxies (`AttachedEffects`), keyed by entity id, and
follow the animated bone palette.

Post-processing runs after the scene and particles and before debug drawing
and the overlay: the scene renders into an offscreen colour target, bloom
works at reduced resolution, and a composite writes the back buffer. Debug
lines and panels therefore never bloom.

The renderer uses an RHI with explicit resources, command lists, queues,
barriers, descriptor tables, and pipeline objects. The first production backend
should be Direct3D 12 because the target game and primary audience are on
Windows. Vulkan follows through the same RHI.

The renderer should support bindless resources, GPU-driven culling, indirect
drawing, mesh LODs, texture streaming, modern compressed texture formats,
shadow atlases, clustered lighting, HDR, temporal anti-aliasing, and a scalable
effects system. High-resolution assets are a budget/streaming problem, not a
hard-coded dimension or polygon-count limit.

The Direct3D 12 backend starts with independent direct and copy queue timelines.
Each queue owns a monotonic fence. Swap-chain frame contexts own their command
allocators and may only recycle them after that frame's direct-queue fence has
completed. Uploads run on the copy queue; consumers enqueue a GPU-side wait on
the direct queue instead of stalling the CPU. CPU waits are limited to frame
latency pacing, reuse of a still-in-flight frame context, swap-chain resize, and
shutdown. GPU-backed objects that support submitted work remain retained until
their producing queue's fence completes.

Replacing resources follows the same rule without a CPU wait. The renderer
records the direct queue's next fence value when a scene is replaced (that
signal follows every frame and pending barrier that can still name the old
resources), then holds the old buffers, textures, and bone palettes in a
fence-keyed retirement queue. Textures live in a generation-checked handle pool,
so a stale handle stops resolving rather than aliasing a reused slot. Their
shader-table slots come from an allocator that returns a released slot to use
only after its fence completes and then overwrites it with a null view. These
primitives (`gpu_resource_lifetime.hpp`) contain no Direct3D types and are
unit-tested on their own.

Textures stream. `load_scene` uploads geometry and materials immediately but
only validates textures against their copy footprints; each frame then uploads
queued textures up to a byte budget (at least one, so oversized textures still
progress). Materials name logical texture ids rather than descriptor slots, and
every frame context owns a persistently mapped residency table mapping each
logical id to its slot, or to a fallback (flat normal for normal maps, white
otherwise) until the texture arrives. A texture is resident from the frame that
submits its copy, because the direct queue waits on the copy fence and applies
the transition before that frame's draws. Descriptors in the shader-visible
heap are therefore never rewritten while a frame may read them. Scene
construction itself runs on a worker thread in the sandbox; VFS reads are safe
there because each const read opens its own stream.

Decoded assets live in the engine's `AssetCache`, keyed by kind and virtual
path. Builders read through the core `AssetReader` interface, which the VFS
implements; a `RecordingAssetReader` wraps it and records a fingerprint of every
resolve (winning mount, container, entry offset and size, and the container's
write time), every lookup that found nothing, and a hash of every listing. A
cached asset keeps the dependencies gathered while it loaded, and reusing it
adds them to the caller's recorder, so a scene knows every file behind it. An
asset is stale when any fingerprint changes: an edited loose file, a new file
in a higher-priority layer, or a rebuilt archive. A MEG mount indexes entry
offsets once, so a rebuilt archive must be remounted (`replace_mount`) before
anything reads from it; the sandbox does that on the main thread between
builds, never while a worker is reading. A rebuilt archive invalidates every
asset read from it, which is conservative but never serves stale bytes. Each build starts a cache epoch and, once it succeeds, evicts entries it did not use, so replaced assets do not accumulate during long hot-reload sessions.

The renderer reuses uploaded textures by identity. Each decode stamps the
`VisualTexture` with a new serial, so a texture served from the cache keeps its
identity and a re-decoded one does not. When `load_scene` replaces a scene, a
new texture whose identity matches a resident texture of the outgoing scene
takes over that texture and its descriptor slot; only unmatched textures stream,
and the remaining old textures retire on the direct-queue timeline. Geometry,
instance, and material buffers are still built per scene.

The debug overlay is drawn by the renderer as the last pass into the back
buffer: the engine supplies text lines and mouse input, and the renderer lays
them out with a Dear ImGui context (the panel moves, resizes, and minimizes;
the renderer reports when it wants the mouse so the scene ignores it) whose font texture takes a slot from the
texture-slot allocator. GPU frame time comes from begin/end timestamp queries
resolved into each frame context's readback buffer and read when that context is
reused, so measuring never waits. The ImGui DX12 backend waits on the CPU for
its own font-texture uploads; those happen only when the font atlas changes.

The sandbox runs every shown object as a simulation entity. Map objects spawn
from their placements and XML types; a model is one immobile entity. A
fixed-step clock (30 Hz, an engine choice until the original logic rate is
measured) steps the world, presentation interpolates the last two snapshots,
and `ScenePresentation` re-poses each entity's original instance: the original
tilt and scale are kept, the heading change rotates it about +Z, and the
position comes from the snapshot. The renderer receives the whole instance list
every frame (`set_instances`), grouped by mesh into a growable, frame-buffered
instance buffer; the loaded scene's meshes act as the mesh library. Selection
is a set of entity ids, and selected instances carry a highlight flag.

Commands come from the RTS input layer: box selection projects snapshot
positions through the camera (`camera_math`, matching the renderer's look-at
and perspective), and a right-click finds its world target on the CPU in the
same frame: a ray through the cursor is clipped to the map, marched across a
`TerrainHeightField` sampled like the rendered terrain (and its liquid
surface), and refined by bisection. Orders therefore never wait for a GPU
readback; the remaining delay is the next simulation tick and the one-tick
interpolation of presentation. GPU picks identify clicked objects. Orders become
simulation move commands, one per unit, spread on a grid in id order so the
command stream stays deterministic.

Errors are values, not exceptions; every target compiles with exceptions
disabled. Recoverable failures return `eaw::Status` / `eaw::Result<T>` with a
contextual message, and callers that continue log a warning and fall back (a
map or model that cannot be built leaves the validation triangle or the current
view; the renderer is created through `D3d12Renderer::create` so a missing GPU
is reported cleanly). Programming errors (`EAW_ASSERT_MSG`) and unrecoverable
runtime failures (`eaw::fatal_error`, for example a D3D12 call failing after
startup, with the device-removed diagnostics) are not handled: they report and
end the process. The platform's crash handlers route `std::terminate`, C++
exceptions raised by code outside the engine (decoded from the MSVC exception
record before unwinding), access violations and other structured exceptions,
pure virtual calls, and invalid CRT parameters into the same report. The report
goes to stderr and, for interactive runs, to an error window.

Selection uses a second render target holding a pick id per pixel: the scene
instance index plus one, zero for nothing, and a reserved value for terrain.
Opaque, alpha-tested, and terrain pipelines write it; blended and background
pipelines mask it. Each pick request gets an id and copies one texel into a
placement-aligned slot of the frame context's readback buffer (up to eight per
frame; later requests wait for the next frame). Values are read when that
context is next reused, after its fence has completed, and returned in request
order, so selection never adds a CPU wait and rapid clicks are never dropped.

Animated visual scenes preserve model-wide bone indices in immutable vertex
buffers. ALA poses are sampled on the CPU into skin matrices, while every
in-flight frame owns a persistently mapped bone-palette buffer. The CPU updates
that palette only after the frame's direct-queue fence has completed, avoiding
both per-frame resource creation and writes into memory still consumed by the
GPU.

Shaders are engine-written HLSL source files compiled at build time: DXC
(shader model 6.0) for Direct3D 12 and FXC (shader model 5.0) for the Direct3D 11
tool previews, embedded as bytecode in generated headers. The D3D12 root
signature is declared in the HLSL, validated by DXC, and created from the
compiled vertex shader, so there is one definition of the binding layout. An
opt-in `ID3D12PipelineLibrary` cache, keyed by a hash of the embedded bytecode
and a layout version, persists pipeline states between runs; driver or shader
changes discard it and the file is replaced atomically.

Legacy-mode surfaces are described by engine-native `VisualMaterial` data that
the scene builder translates from legacy material profiles and exact ALO
parameter values. The renderer uploads materials once into a structured buffer
and indexes textures through one shader-visible table (resource binding tier 2),
so a draw binds only two root constants: its first instance and its material.
Lighting arrives as a `VisualLightRig` in a per-frame constant buffer, next to
the camera and scene time. A later physically based path can consume the same
scene data with different materials and shaders.

The engine owns the camera: controllers in `eaw_engine` turn input into a
`VisualCamera` (eye, target, up, field of view, near/far planes) that the
renderer consumes. An orbit controller frames single models and a tactical
controller pans, rotates, and zooms over a map within its bounds. Background
instances (sky domes and star backdrops) are drawn first, centered on the camera
by transforming them as directions, pinned to the far plane, and without depth
testing, so they never occlude or parallax against the world.

Terrain is a separate part of the visual scene: independent triangles whose
vertices repeat their triangle's three corner layers with barycentric weights,
so one terrain pipeline blends three layer materials per pixel and lights the
result with the same shared legacy lighting code as meshes.

A visual scene groups draws into meshes (one per model) and places them with
instances. Meshes can be added to a loaded scene at runtime (`add_meshes`):
each addition is a geometry block with its own vertex and index buffers, its
materials join the material buffer, and its textures stream like the scene's,
so spawning a type the map was not built with needs no scene reload. Each instance carries a world transform, a tint, and a bone-palette
offset in an immutable structured buffer. Instances are sorted by mesh at load
so every draw of a mesh is issued once with `DrawIndexedInstanced` over a
contiguous instance range; the range start is a root constant because
`SV_InstanceID` restarts at zero for every draw. The bone palette is set per
frame: map scenes start from every mesh's rest pose, and the engine's
`UnitAnimator` appends the poses its units show this frame (chosen from
simulation snapshots, shared between units at the same clip and pose slot) and
points each instance at one. Poses blend in bone space: up to three weighted
clips (two neighbouring locomotion clips picked by the unit's speed, and an
attack fading in or out), with weights rounded to eighths so units in the same
state still share a pose. Units killed in combat (listed in the snapshot of
their tick) leave corpses that presentation keeps for their DIE clip and a
short linger; they are drawn but are no longer entities. Animation is
presentation: it never feeds back into the simulation.

## Multithreading direction

"Use all cores" is not an architecture by itself. The engine uses a dependency
aware job system and divides work into deterministic phases:

1. collect input and script commands;
2. update spatial queries and perception;
3. update AI planning at controlled cadences;
4. solve movement/path requests in parallel;
5. resolve weapons, projectiles, damage, and abilities;
6. publish an immutable presentation snapshot.

Large unit counts require structure-of-arrays storage, stable entity IDs,
batched queries, hierarchical spatial indices, flow fields or shared paths for
groups, AI level-of-detail, and performance budgets. Adding threads to the
legacy object model would not be sufficient.

## Compatibility policy

Compatibility is measured, not assumed. Every supported format and behavior
gets a corpus test, a decoder test, and—where possible—a golden result captured
from public data or black-box observation. Extensions use new optional tags or
namespaced files so an original mod remains readable without edits.

### Content contract versus legacy implementation

EAWEngine is a modern engine that runs content made for the original, not a
reimplementation of the original's internals. Every original behavior the
engine meets falls into one of three kinds, and is handled accordingly:

1. **Content contract: follow it.** What a data field means, as the original
   interprets it. Content was authored and tuned against that meaning, so
   changing it breaks every existing asset. Examples: particle lifetime and
   size variances only shorten and shrink, the sign of "inward" speed, atlas
   slots, a hardpoint's `Damage_Particles` and `Damage_Decal` bones,
   `Max_Speed` per 30 Hz frame. Establishing these is what the evidence work
   is for.
2. **Implementation artifacts: modernize freely.** How the old engine
   happened to compute a result: fixed-function and low-dynamic-range
   blending, CPU-built quads, emission clumped per frame, unsorted
   transparency, a single global frame step. The engine does the modern thing
   by default (sub-frame emission, sorted particles, GPU expansion, bloom,
   frame-rate-independent motion) and needs no switch.
3. **Look-defining quirks: one named rule, chosen per fidelity profile.**
   Original behaviors that are arguably wrong but that content was tuned
   against: additive particles ignoring alpha, draw order. (Linked
   particles following position but not rotation looked like one, but the
   evidence only described editor previews that never rotate, and the result
   was visibly wrong, so it was dropped: a quirk needs evidence from the game,
   not from a tool's limitation.) These are never
   scattered through runtime code. They live in one explicit interpretation
   layer with their evidence: decoders keep the file's facts, normalization
   turns them into an engine-native description under a **legacy** or
   **enhanced** profile, and runtimes and renderers only see the result.

**Legacy** reproduces the original for content that does not know this
engine; it is the default while legacy mode is the goal. **Enhanced** may
deviate where the result improves and authored content still works. Content
made for this engine will say so explicitly (the planned asset metadata, see
the roadmap's engine extensions), and explicit declarations override legacy
inference. Particle effects are normalized this way first
(`src/engine/particle_rules.*`); projectiles, impacts, and materials follow
the same pattern (the legacy material profiles already feed the future
physically based pipeline).

## Source files by subject

How the larger modules are split into files. `AGENTS.md` keeps the directory-level map and the
dependency rules; this is the file-level detail.

- `src/core`: one file per format or subject. `XmlSchema` (`eaw/xml_schema.hpp`) is split into
  `xml_schema_load.cpp` (the optional eaw-schema YAML, its base and FoC layers, accessors) and
  `xml_schema_validate.cpp` (validating a resolved XML database against it), sharing
  `xml_schema_internal.hpp`. Localization is `localization_workspace` (TSV workspaces, DAT rows,
  unit keys), `localization_scan` (TEXT_* references in mod XML), and `localization_validation`
  (a translation against its reference).
- `src/render` (D3D12 backend): the public interface is `include/eaw/d3d12_renderer.hpp`; the
  implementation class lives in `d3d12_renderer_impl.hpp` with its member functions defined by
  subject in `d3d12_renderer_device.cpp` (startup, swap chain, buffers, waits, retirement),
  `_pipelines.cpp` (pipeline states and cache), `_resources.cpp` (uploads, scene loading, texture
  streaming), `_frame.cpp` (per-frame state and `render()`), and `_overlay.cpp`;
  `d3d12_support.*` holds the HRESULT, queue, handle, and description helpers.
- `src/simulation`: `SimulationWorld` is split by tick layer (see
  [Simulation tick layers](#simulation-tick-layers)): `simulation.cpp` (world core),
  `simulation_behavior.cpp` (commands, stances, and what each unit wants),
  `simulation_navigation.cpp` (routes and group cohesion), `simulation_locomotion.cpp` (physical
  movement and separation), and `simulation_combat.cpp` (targeting, weapons, damage), sharing
  `simulation_internal.hpp`.
- `src/engine`: `battle.*` holds battles above the simulation (setup from a built map, replayable
  orders, the order log), shared by the sandbox and headless runs; `battle_runner.*` runs them
  headless and `battle_log.*` reads and writes order logs. The sandbox's testable parts are
  `sandbox_options` (command line), `sandbox_content` (background scene builds and hot-reload
  checks), and `sandbox_debug_lines` (what is drawn, a pure function of an explicit view struct).
  The scene builders behind `visual_scene_builder.hpp` are `scene_materials` (textures,
  materials, lighting), `scene_models` (models, palettes, animations), `scene_particles`
  (particle effects, projectile visuals, effect objects), `scene_terrain` (terrain, liquids,
  tracks), `object_type_properties` (movement and speed fields of XML types), and
  `visual_map_builder` (the map and spawnable-package builds), sharing
  `scene_builder_internal.hpp`. Particles are `particle_rules` (interpretation under a fidelity
  profile), `particle_effects` (`ParticleWorld`), `attached_effects` (effects on model bones),
  and `battle_effects` (`BattleEffects`: a battle's effects from its snapshots, meaning attached
  effects of drawn units, the Particle objects that impacts and deaths name, and bolts; no
  graphics API, so the game and tests use it as the sandbox does).
- `src/engine_app` (`EAWEngine`): `main.cpp` handles startup, discovery, the doctor probe, and
  dispatch to `battle` or `sandbox`. The sandbox session (`SandboxSession`, declared in the private
  `sandbox_session.hpp`) is defined by subject: `sandbox_app.cpp` (lifetime, the frame loop, the
  run summary), `sandbox_session_content` (showing finished builds, hot reload),
  `sandbox_session_input` (cameras, selection, picks, orders), `sandbox_session_spawning`
  (runtime types, command-line automation), `sandbox_session_world` (simulation ticks,
  presentation, animation, effects over time), and `sandbox_session_ui` (panels, statistics,
  debug lines); `frame_watchdog` reports stalled frame steps, `sandbox_overlay` builds the
  statistics text (a pure function of a view struct), and `sandbox_panels` the Dear ImGui panels.
- `src/asset_inspector`: one source file per command group (`inspect_asset`, `audit_textures`,
  `audit_lua`, `audit_maps`, `audit_particles`, `audit_xml`) plus `inspector_options` and
  `inspector_common`, declared in `inspector.hpp`.
- `src/dat_editor` (`EAWDatEditor`): a front end over `eaw_core`'s DAT, workspace, scan, and
  translation-validation functions (`localization_workspace`, `localization_scan`,
  `localization_validation`, which EAWTools uses too); `dat_editor_options` parses the command
  line, `dat_commands` holds the commands on one DAT (info, list, get, set, remove, validate),
  `workspace_commands` those on workspaces and mods (export, diff, build, scan-mod, add-unit),
  and `main.cpp` dispatches.
- `src/tools_gui`: each panel has its own file (`gui_asset_workspace`, `gui_map_view`,
  `gui_model_view`, `gui_localization`); `GuiApp::State` is declared in `gui_internal.hpp` with
  its methods in `gui_state.cpp`, and shared helpers live in `gui_support.cpp`.
- `tests`: one module per subject, each `test_<module>.cpp` with its own ctest entry: `formats`,
  `vfs`, `xml`, `foundation` (core); `simulation` (core and snapshots), `navigation`, `combat`,
  and `flight` (the simulation); `scene` (scene building), `presentation`, `battle`, `effects`,
  and `sandbox` (the engine); `launcher` and `render`. Shared fixtures (synthetic MEG, ALO, ALA,
  DDS, TED, DAT, and particle files) live in `test_support.*`.
