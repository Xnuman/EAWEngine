# Coding-agent instructions

This file is the operating contract for coding agents working in this repository. Read it before changing code, then read the documents linked from the relevant section. Keep this file current when a milestone changes the architecture, workflow, or compatibility boundary.

## Mission and product boundary

EAWEngine is an independently developed, open compatibility engine and toolset for *Star Wars: Empire at War* (EAW) and *Forces of Corruption* (FoC). It reads a player's owned installation in place so existing resources, scripts, localization, audio, models, maps, and mods can be used without modifying them.

- `EAWLauncher` chooses the game profile, installation, mod stack, and engine options, then starts `EAWEngine`.
- The launcher and engine must never inject into, patch, wrap, or replace files in the original executable or installation.
- The owned game and mod directories are read-only inputs. Generated files belong in an explicit user-selected output directory or a build directory.
- Do not build a binary-compatible clone around undocumented accidents. Decode losslessly, normalize separately, and translate behavior behind versioned engine-native interfaces.
- Existing mods are the compatibility baseline. New mechanics and formats must use optional or namespaced extensions so old data remains readable.
- The project is not yet a playable replacement game. Do not imply otherwise in code, documentation, or release notes.

## Current state

### M0: repository and research discipline — complete

- CMake/Visual Studio build, tests, documentation, and separate launcher/engine programs exist.
- Architecture, compatibility-evidence rules, and the no-modification installation boundary are established.

### M1: data bootstrap — baseline complete

- Steam EAW/FoC installations are discovered automatically.
- MEG v1 archives, loose files, inherited EAW data, patches, and repeatable mod layers are exposed through one case-insensitive layered VFS.
- DAT localization supports lossless read/write, validation, sparse TSV workspaces, new-unit bundles, a CLI, and a Dear ImGui editor.
- DDS/MTD decoding covers stock textures, mip/subresource layout, BC1-BC5, common uncompressed/DX10 metadata, and mega-texture directories.
- ALO decoding covers render-ready geometry, skeletons, materials and raw parameters, skin palettes, bind transforms, attachments, proxies, lights, and dazzles.
- ALA decoding covers compressed animation tracks, pose sampling, visibility, and stepped tracks.
- TED decoding covers land terrain/elevation/passability/objects and space objects/tactical bounds. All 269 resolved stock maps decode in the current corpus.
- XML parsing handles Petroglyph fragments and builds a typed definition/reference index. Optional public `eaw-schema` data provides stronger validation.
- Lua inspection handles source dependencies and structural inspection of standard Lua 5.0 and Petroglyph persistent bytecode without executing it.
- `EAWAssetInspector`, `EAWDatEditor`, and the Dear ImGui/Direct3D 11 `EAWTools` workbench consume the same `eaw_core` decoders.

### M2: visual sandbox — active

The native Direct3D 12 sandbox currently has:

- a Win32 window/input layer;
- triple-buffered frame contexts and a waitable flip-model swap chain;
- independent direct and copy queues with monotonic per-queue fences;
- GPU-side copy-to-direct synchronization and deferred upload retirement;
- optional tearing, WARP, debug layer, GPU validation, DRED, and device-removal diagnostics;
- depth buffering, indexed ALO batches, DDS mip uploads, conservative blend translation, and an orbit camera;
- CPU ALA pose sampling and GPU skinning through persistently mapped, per-frame bone palettes;
- automatic `_IDLE_00.ALA` lookup, explicit `--model`/`--animation` selection, pause/reset controls, and a validation-triangle fallback;
- engine-owned cameras: `eaw_engine` (scene building and camera controllers, unit-tested) produces a `VisualCamera` for the renderer; `--map` uses a tactical camera and draws the active environment's sky dome and backdrop as a camera-centered background layer;
- instanced scenes: `VisualScene` meshes and instances (transform, tint, bone offset) drawn with one `DrawIndexedInstanced` per mesh draw, and `sandbox --map` loading every placed object of a TED map (space squadrons expanded at their `Squadron_Offsets`);
- render direction: the first target is **legacy mode**: running the original game on the new engine with rendering that approximates Alamo, driven by `LegacyMaterial` reference profiles and newly written shaders, then stabilizing behavior. A modern physically based shading (PBS) pipeline with engine-native materials comes after legacy mode is stable; the profiles will then also be conversion input for it;
- `GameObjectCatalog` resolves TED placed-object CRCs (CRC-32 of the uppercased type name) to XML definitions and models through `Variant_Of_Existing_Type`; `EAWAssetInspector map-objects` audits it (99.7% of stock placed objects reach an existing model). `build_map_object_layout` adds per-object world transforms, and the `EAWTools` map previews draw the resolved models;
- an evidence-based legacy shader inventory ([docs/legacy-shader-mapping.md](docs/legacy-shader-mapping.md)) and reference profiles in `LegacyMaterial` for all 39 readable `Shaders/` effects. The profiles record render phase, depth/cull/alpha-test state, lighting model, texture-channel roles, UV/vertex animation, and vertex requirements. Unreferenced names keep tagged name heuristics. The sandbox skips heat and occluded-unit materials with a warning until those passes exist.

Debug and Release builds, unit tests, WARP/GPU-validation smoke tests, and human visual checks have passed for the current slice.

Known limitations are intentional, not invitations to hide special cases:

- legacy-mode shading covers fill plus key-light lighting (2x diffuse convention, exponent-16 highlight evaluated per pixel), normal maps, gloss from base/normal alpha or gloss-texture red, team-color masks, self-illumination masks, material Diffuse/Specular/Emissive/Color, alpha test/blend/additive, and UV scrolling. Maps use their active TED environment's key, two fill lights, specular, and ambient; team colors come from the type's affiliated faction `Color` (player-slot palette fallback). Not yet: cube reflections, planet atmosphere/clouds/city lights, shield distortion layers, nebula wobble, wind bend, heat distortion, occluded-unit silhouettes, shadows, fog, fog of war, and depth-sorted transparency;
- alpha ordering is coarse and material translation is conservative;
- shaders are newly written HLSL under `src/render/shaders` (D3D12, DXC, shader model 6.0, root signature declared in the HLSL) and `src/tools_gui/shaders` (D3D11 tools, FXC, shader model 5.0), compiled at build time into embedded bytecode; there is no runtime shader compilation. Shader reflection metadata is not generated yet, and the pipeline-state cache is opt-in (`--pipeline-cache <dir>`);
- `sandbox --model` lists every `<MODEL>_*.ALA` and Left/Right step through the bind pose and them; models show the intact `_ALT0` damage state and the highest `_LOD<n>` detail level. Model scenes animate bone visibility for rigid attachments through a second, visibility-collapsed palette half (meshes on bones hidden in the bind pose appear when an animation shows the bone); skinned-mesh visibility, damage-state and LOD switching, and gameplay-driven visibility are not modeled, and map scenes, which share one bind-pose palette, omit meshes on hidden bones;
- the sandbox builds a map or model on a worker thread while the window renders; `load_scene` may replace the scene at any time. Geometry and materials upload at once, while textures are validated up front and stream in under a per-frame byte budget (`--upload-budget`), drawn with role-appropriate fallbacks through a frame-buffered logical-texture residency table. Textures live in a generation-checked handle pool, take texture-table slots from a fence-deferred allocator, and replaced buffers, textures, and bone palettes retire on the direct-queue timeline (`gpu_resource_lifetime.hpp`; `sandbox --reload-every` exercises it). Scene builders read through the core `AssetReader` interface and an engine `AssetCache` (`src/engine/asset_cache.*`) of decoded models, textures, maps, and the XML object catalog; every cached asset records fingerprints of the files it consulted (including failed lookups and listings), and `sandbox --hot-reload` polls them and the MEG archives once a second on a worker, remounts rebuilt archives between builds, and rebuilds the scene from the cache. Decoded textures carry an identity (a fresh serial per decode), and `load_scene` keeps every resident texture whose identity the replacement scene names again, so a reload uploads only changed textures; geometry, instance, and material buffers are still per scene, the renderer holds one scene at a time, and MEG archives added to or removed from a layer need a restart;
- the descriptor and bone capacities are temporary implementation limits, not asset-format limits;
- land terrain renders as a textured heightfield (per-vertex layer index, three-layer blending, layer tiling, normal maps; layers without a color texture are not drawn, an inference from Cloud City and Kamino); water, lava, and ice surfaces render at the map's liquid level with their textures and highlights, resembling the original's low-end fallback because reflection and refraction are not modeled (tiling and scroll are engine choices); tracks render as unlit, alpha-blended ribbons draped on the terrain with start/middle/end opacity and texture scroll; vertex colors reach materials whose legacy profile reads them. Decals (one stock map, no established texture link), passability, fog of war, clouds, and the unexplained terrain layer and track header fields (two angles, two colors, flags) are not rendered or interpreted yet. Land teams/companies are not expanded (their formations are runtime behavior), map instances share one bind-pose palette, and transparent draws are not depth-sorted;
- world handedness is unverified: the renderer uses a left-handed look-at over Z-up data, so mirrored text or decals on models would indicate a flipped axis; background parallax and sky-dome scaling are inferred, not measured;
- `EAWTools` remains on Direct3D 11 and its preview renderer is not the production renderer;
- a complete renderer-agnostic RHI has not yet been extracted;
- simulation, gameplay Lua execution, audio, runtime UI, networking, and save compatibility are future milestones.

## Immediate roadmap

Work in dependency order unless the user explicitly reprioritizes it.

1. ~~Inventory the legacy shader families in `Shaders/` and create an evidence-based mapping from legacy techniques, parameters, and render states to engine-native material features.~~ Done; see [docs/legacy-shader-mapping.md](docs/legacy-shader-mapping.md).
2. (In progress; the core legacy-mode surface model is implemented.) Implement legacy-mode material rendering in the D3D12 sandbox from `LegacyMaterial` profiles, using newly written HLSL: SH plus key-light lighting, normal/gloss/emissive/team-color channels, UV animation, cull/depth/alpha rules, and special families such as shields and heat where evidence supports them. Keep the material interface engine-native so a PBS path can be added later without changing decoders.
3. ~~Move modern shader sources out of inline C++ to newly written HLSL. Introduce DXC/offline compilation, stable root-signature layouts, and PSO caching.~~ Done; reflection metadata is deferred until material shaders need generated bindings.
4. Add selection IDs, debug drawing, and camera polish (animation selection and rigid-attachment visibility are done). The PBS material model and its documented conversion from legacy profiles follow once legacy mode is stable.
5. ~~Add D3D12 tactical-map rendering.~~ Space maps and land terrain render; water, decals, and tracks remain. Defer further tool-only model/map preview polish until it can share code with the production renderer and the later map editor.
6. (In progress; handle pool, fence-deferred texture-slot allocation, direct-timeline retirement with runtime scene replacement, background scene construction, budgeted texture streaming, the engine asset cache with dependency tracking, hot reload of loose files and rebuilt MEG archives, and identity-based texture reuse across scene loads are implemented.) Remaining: per-asset geometry handles and several concurrently resident scenes, which M3 runtime spawning will need.
7. Finish M2 by loading and inspecting both a stock space unit and a tactical map through the new renderer.
8. Begin M3 only after the visual/data contracts are stable enough: ECS/storage, fixed-step scheduler, dependency-aware job system, deterministic commands/snapshots, transforms, spatial queries, movement, formations, targeting, damage, hardpoints, fog of war, XML construction, and a minimal Lua host surface.

Later milestones remain:

- **M4:** complete land/space skirmish systems, pathfinding, reinforcements, abilities, tactical AI, UI, audio, and deterministic replays.
- **M5:** galactic mode, campaigns, economy, heroes, story events, campaign AI, persistence, and versioned saves.
- **M6:** broad XML/Lua compatibility, Steam mod discovery, multiplayer/replays, authoring tools, profiling, and a documented namespaced mechanics SDK.

The authoritative milestone description is [docs/roadmap.md](docs/roadmap.md). Update it and this summary together when scope or status changes.

## Repository map and dependency direction

- `src/core`: VFS, formats, compatibility data, and reusable asset logic. It must not depend on graphics or Win32 presentation code.
- `src/platform`: operating-system window, input, clock, and related platform services.
- `src/render`: rendering interfaces, visual-scene data, and graphics backends. It must consume engine-native visual data, not parse MEG/ALO/ALA/TED files or know VFS precedence.
- `src/engine`: runtime orchestration and the bridge from decoded assets to visual/runtime representations.
- `src/launcher`: configuration and process launch only.
- `src/asset_inspector` and `src/dat_editor`: command-line tools built on `eaw_core`.
- `src/tools_gui`: the Dear ImGui workbench. Tool UI may present core data but must not duplicate parsers.
- `tests`: automated decoder, VFS, compatibility, and runtime-contract tests.
- `docs`: architecture, evidence, roadmap, and tool workflows.
- `Shaders`: owner-provided legacy Direct3D 9 shader reference corpus; never a runtime or build dependency.
- `build`: generated output; never edit generated project files as source.

Dependencies point inward: platform/renderer/adapters depend on stable engine-native contracts; core data code never reaches outward into a renderer or GUI. If a tool needs a capability, add it to a reusable library instead of scraping another executable's output or spawning a subprocess.

## Legacy `Shaders/` reference boundary

The repository owner intentionally added the original Alamo Direct3D 9 `.fx`, `.fxh`, and `.fxo` files under `Shaders/` as a restricted compatibility reference. This is the sole explicit exception to the project's earlier no-original-asset wording. It does not authorize adding any other original source, executable, data archive, art, audio, or binary.

- Treat the directory as read-only evidence unless the owner explicitly requests a change to its documentation.
- Source `.fx`/`.fxh` files may be consulted to identify shader families, parameter meanings, transforms, blend/depth/cull state, sampler behavior, skinning conventions, and special effects.
- Write new modern HLSL from the derived behavior and engine-native material contract. Do not copy or mechanically port whole legacy functions, comments, or files.
- Never include, compile, embed, install, or ship a legacy shader file as part of `EAWEngine`, its tools, tests, generated shader cache, or release package.
- Do not make CMake configuration, normal tests, or runtime startup depend on `Shaders/`; a source checkout without it must still build and run.
- Treat `.fxo` files as opaque binary evidence. Do not execute or disassemble them without explicit owner authorization and a documented lawful need.
- Record mappings and uncertainty. A legacy shader name should map to an engine-native material family plus independently implemented features; it should not leak through as a renderer architecture.
- Do not blindly preserve Direct3D 9 syntax or assumptions such as fixed-function state blocks, half-pixel offsets, sampler coupling, register limits, or implicit color-space behavior.
- `Shaders/` is local reference material and is never distributed. It is git-ignored; never commit, push, package, or publish any of its contents, and never quote legacy code in committed files.
- Legacy shaders and materials are mapping evidence only. They describe how a model was meant to look in Alamo; they are not the engine's material system.

See [Shaders/README.md](Shaders/README.md) for the local directory policy and [docs/compatibility-research.md](docs/compatibility-research.md) for the evidence policy. Do not describe the renderer as strictly clean-room while this reference corpus is available to contributors.

## Compatibility and asset-decoding principles

### Layered VFS precedence is a public contract

Preserve and test the observed order:

1. manifest archives from low to high priority;
2. unlisted archives in lexical order;
3. `Patch.meg`;
4. numerically ordered patch archives;
5. `64Patch.meg`;
6. loose files over archives;
7. FoC data over inherited EAW data;
8. mod layers over game data, with the first supplied mod at highest priority.

Paths are case-insensitive and separator-normalized. Enumerations expose unique winning virtual paths while lower layers remain available for fallback. A precedence change requires a focused regression test and compatibility evidence.

### Decode losslessly, interpret separately

- Preserve unknown chunks, flags, parameters, source names, ordering, and values whenever practical.
- Keep parser facts separate from normalized hints and renderer/simulation policy. For example, an ALO material retains its raw shader and parameters even after it gains a translated material family.
- Validate offsets, sizes, counts, integer overflow, nesting, allocation budgets, and truncation before allocating or indexing.
- Malformed mod data must return contextual diagnostics, not crash, hang, or request unbounded memory.
- Avoid hard-coded stock limits for texture dimensions, vertices, bones, entities, or map size. Temporary GPU capacities must be explicit, checked, diagnosed, and replaceable.
- Never execute inspected scripts or shader binaries in a decoder/test process.
- Use corpus observations as evidence, not as proof that other mods cannot contain a valid variant.

### Evidence before compatibility rules

Use, in descending confidence: documented public formats/APIs, owner-authorized reference material, focused black-box observations, broad stock/mod corpus measurements, then clearly marked inference. Preserve uncertainty in names and diagnostics. Do not turn a shipped-data anomaly or current parser warning into a rejection rule without evidence.

For each nontrivial format/behavior change, prefer:

- a small synthetic fixture for the exact invariant;
- malformed/truncated/overflow cases;
- a stock corpus audit where the user's installation is available;
- a recorded expected result that does not redistribute protected data;
- a short note in `docs/compatibility-research.md` when the finding changes our model.

## Simulation and concurrency principles

- Gameplay runs on a deterministic fixed tick; presentation frame rate is independent.
- Stable entity IDs, structure-of-arrays storage, batched queries, and explicit ownership are preferred over a pointer-heavy legacy object graph.
- Parallel work operates on disjoint partitions or immutable inputs and publishes commands/results at explicit deterministic barriers.
- Scripts enqueue validated commands; they do not mutate simulation storage from arbitrary callbacks or worker threads.
- Rendering consumes immutable presentation snapshots and must not become an authority for gameplay state.
- Use a dependency-aware job system and measurable budgets. “Put it on another thread” is not a design.
- Random streams, iteration order, floating-point choices, and merge order are compatibility state. Make them explicit and testable.

## Direct3D 12 invariants

These are correctness rules, not optional optimizations.

- Each command queue owns exactly one monotonically increasing fence timeline. Never reuse, rewind, or ambiguously share fence values across queues.
- Use one command allocator per in-flight frame/recording context. Reset it only after the fence value for its last submission has completed.
- Never add a per-frame queue flush or unconditional CPU wait. CPU waits are limited to waitable-swap-chain pacing, reuse of an in-flight context, resize, orderly shutdown, and explicit recovery/diagnostics.
- Express copy-to-direct and other inter-queue dependencies with GPU queue waits. Do not block the CPU merely to sequence GPU work.
- Copy-queue destination resources begin in `COMMON`; after copy completion/decay and a direct-queue GPU wait, transition them on the direct queue to their consuming state.
- Upload buffers, descriptors, command data, and resources remain alive until the fence for the last queue that references them completes. Retire them by fence value.
- Dynamic per-frame constants and bone palettes are frame-buffered and preferably persistently mapped. Write a frame's region only after that frame context is reusable.
- Keep the swap chain flip-model and waitable. Query tearing support; do not assume it. Frame latency should normally be one or two.
- Check every relevant `HRESULT`, name important D3D12 objects, propagate contextual errors, and preserve DRED/device-removal diagnostics.
- Destructors must not throw. Partial construction and device failure paths must be safe through RAII (`ComPtr`, scoped handles, and owned containers).
- Keep GPU object creation and resource-state ownership explicit. Avoid hidden global state, implicit lifetime extension, or synchronization encoded only by call order.
- WARP plus GPU validation is the automated nonvisual correctness gate. A clean validation run does not prove visual correctness; the user performs visual acceptance unless they explicitly ask the agent to do it.

## Code and design style

- C++20; Windows builds use `/W4 /permissive- /EHsc /utf-8`. New warnings are defects.
- `.clang-format` is authoritative: four spaces, project brace rules, and the configured 200-column limit. Format every changed C/C++ file before validation.
- Follow the established style in neighboring files: descriptive names, small cohesive types, early validation, explicit ownership, and comments that explain invariants or evidence rather than restating code.
- Prefer standard-library and Win32/D3D12 primitives already used by the project. Add a dependency only when it removes substantial risk or work, and pin it reproducibly.
- Avoid singletons, global mutable state, renderer access from core decoders, and format parsing in presentation code.
- Use strong enums/types for units and states where confusion is plausible. Use checked conversions at binary-format and API boundaries.
- Return diagnostics with the virtual path, layer/source, format section, and relevant offset/name whenever available.
- Preserve forward compatibility: unknown optional data should normally survive or be ignored with a diagnostic, not become an assertion.
- Keep public interfaces narrow and const-correct. Prefer immutable decoded assets and explicit builders/translators for runtime data.

## Normal workflow

1. Read `git status --short` and preserve all pre-existing user changes. Never reformat or “clean up” unrelated files.
2. Locate code with `rg`/`rg --files`; read the owning module, its tests, and the relevant docs before editing.
3. State the compatibility assumption and identify which layer owns the change. If ownership crosses boundaries, fix the contract instead of adding a shortcut.
4. Implement the smallest coherent slice, including diagnostics and negative cases.
5. Add or update focused tests. For format work, include malformed input and a corpus audit when possible.
6. Run `clang-format` on changed C/C++ files only.
7. Build Debug and Release when shared/core/renderer code changes; run tests and the relevant smoke/corpus command.
8. Run `git diff --check`, inspect the complete diff, and update docs/roadmap/status in the same change.
9. Make cohesive commits only when requested. Do not stage or commit the user's unrelated changes, personal game paths, build output, decoded assets, or generated caches.

Do not use destructive Git commands (`reset --hard`, forced checkout, clean) on a shared worktree. Do not rewrite existing commits unless the user explicitly asks. Use `apply_patch` for hand edits; generated formatting or build output is the exception.

## Build and validation

From the repository root:

```powershell
cmake --preset vs2022-x64
cmake --build build/vs2022-x64 --config Debug --parallel
cmake --build build/vs2022-x64 --config Release --parallel
ctest --preset debug --output-on-failure
git diff --check
```

Format changed C/C++ files with the Visual Studio LLVM formatter (adjust the installation path only if necessary):

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe' -i <changed-files>
```

Useful runtime checks:

```powershell
.\build\vs2022-x64\Debug\EAWEngine.exe doctor --game foc
.\build\vs2022-x64\Debug\EAWEngine.exe sandbox --game foc --warp --gpu-validation --vsync off --frame-limit 8
.\build\vs2022-x64\Debug\EAWEngine.exe sandbox --game foc --model DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO --warp --gpu-validation --frame-limit 75
.\build\vs2022-x64\Debug\EAWEngine.exe sandbox --game foc --model DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO --warp --gpu-validation --vsync off --frame-limit 40 --reload-every 3
```

Use `--game-root` when discovery is unavailable. Never hard-code a contributor's Steam path in source, tests, CMake, or committed documentation. A machine without the game must still be able to configure, compile, and run synthetic unit tests.

For asset changes, use the matching `EAWAssetInspector` audit command described in [docs/tools.md](docs/tools.md). Public `eaw-schema` is an optional local checkout, not a configure-time or runtime network dependency.

## Definition of done

A change is complete when all applicable items are true:

- ownership and dependency direction remain correct;
- Debug and Release compile without new warnings;
- focused tests and `ctest` pass;
- relevant corpus/runtime/WARP GPU-validation checks pass;
- no per-frame CPU/GPU stall, hidden asset limit, or unbounded input allocation was introduced;
- error paths and malformed inputs have actionable diagnostics;
- legacy assets and the user's installation remain untouched;
- documentation and roadmap status match the implementation;
- `git diff --check` is clean and the diff contains no unrelated changes;
- visual changes are explicitly handed to the user for visual verification unless they requested agent-run visual testing.

Do not mark a milestone complete because a happy-path demo works. Its executable deliverable, automated contract, failure behavior, and documentation must all be present.

