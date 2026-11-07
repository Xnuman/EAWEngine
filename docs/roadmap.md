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

## M2 — visual sandbox

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
  resource handles, descriptor allocation, streaming, and hot reload. Started:
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
- Deliverable: load and inspect a stock space unit and a tactical map. Both now
  load in `EAWEngine sandbox` (`--model`, `--map` for space and land); M2 stays
  open until resource handles, streaming, and the remaining map layers exist.

## M3 — tactical simulation slice

- ECS/storage, fixed-step scheduler, job system, transforms, spatial index,
  movement, formations, targeting, projectiles, damage, hardpoints, fog of war.
- Minimal Lua compatibility surface and XML object construction.
- Deliverable: deterministic headless battle plus rendered space skirmish slice.

## M4 — space and land modes

- Navigation/pathfinding for both modes, build pads, reinforcements, abilities,
  transports, shields, capture, retreat, tactical AI, UI and audio.
- Deliverable: complete skirmish matches with replay determinism.

## M5 — galactic and campaign

- Planets, fleets, economy, construction, heroes, transitions to tactical,
  story events, campaign AI, persistence and save versioning.
- Deliverable: one stock campaign playable end to end.

## M6 — compatibility and SDK

- Broad XML/Lua API coverage, Steam mod discovery, mod stack diagnostics,
  multiplayer protocol, replays, authoring/import tools, profiling overlays.
- New namespaced mechanics API and documented extension points.

## Performance gates

- Headless deterministic simulation benchmark before renderer optimization.
- 60 Hz presentation with configurable simulation tick.
- No fixed texture dimension, vertex count, entity count, or 32-bit address-space
  assumptions in new formats.
- Automated budgets for frame time, simulation tick, streaming throughput,
  memory, draw/dispatch counts, and worst-case path requests.

