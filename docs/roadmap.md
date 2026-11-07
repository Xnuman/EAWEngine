# Roadmap

Each milestone ends in an executable result and a compatibility corpus. Full
campaign compatibility is a long-term engine project, not a renderer swap.

## M0 — repository and research discipline (done)

- Clean-room boundary and no-asset repository policy.
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
- Next: M2 visual sandbox. Deeper per-shader render-state, lighting, and Lua
  host-API behavior are implemented incrementally against the compatibility
  inventories as their renderer and simulation consumers come online.
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
  translation, depth buffering, and orbit camera are implemented in the D3D12
  sandbox. Continue toward full legacy shader/material coverage and ALA skin
  animation, then replace the current ImGui inspection viewport.
- Camera, PBR approximation for legacy materials, selection IDs, debug drawing.
- Deliverable: load and inspect a stock space unit and a tactical map.

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

