# Roadmap

Each milestone ends in an executable result and a compatibility corpus. Full
campaign compatibility is a long-term engine project, not a renderer swap.

## M0 — repository and research discipline (done)

- Clean-room boundary and no-asset repository policy.
- Build, tests, launcher/engine separation, architecture record.

## M1 — data bootstrap (in progress)

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
- Next: load the external type/tag schema for value and reference validation,
  then remaining DDS/MTD formats, legacy shader/material translation, and Lua
  bytecode inventory.
- Deliverable: asset inspector that can browse every stock and selected mod
  asset and report unresolved references.

## M2 — visual sandbox

- Window/input/platform layer and Direct3D 12 RHI.
- Production GPU texture/material translation and ALO/ALA model/animation
  viewer, replacing the current ImGui inspection viewport.
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

