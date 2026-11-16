# Implementation status

What is implemented, system by system: how it works and its known limitations. The plan, open
stages, and work order are in [roadmap.md](roadmap.md); the structure in
[architecture.md](architecture.md); how to run the tools in [tools.md](tools.md); the dated record of
how the work got here in [history.md](history.md). Update this file and the roadmap in the same change as the code
they describe.

Numbers marked as engine choices or guesses are not the original's behavior; they wait for evidence
or for the owner's judgment in play.

## Milestones

- **M0, repository and research discipline: complete.** CMake/Visual Studio build, tests,
  documentation, separate launcher and engine programs, the architecture, the compatibility-evidence
  rules, and the no-modification boundary of the installation.
- **M1, data bootstrap: complete.** The layered VFS and the readers of the original's formats
  ([Data and formats](#data-and-formats)).
- **M2, legacy-mode renderer: active.** Stock units and tactical maps load and can be inspected in
  the Direct3D 12 sandbox; M2 stays open for the features content was authored for (the Rendering
  track in [roadmap.md](roadmap.md#rendering)). See [Rendering](#rendering).
- **M3, tactical battle slice: active.** A deterministic simulation with movement, pathfinding,
  group movement, combat, headless battles with replayable order logs, scripts in battles, unit
  animation, particles, projectiles, impact and death effects, and battle sound. See
  [Simulation](#simulation) and the sections after it. Combat presentation is in progress; the
  player layer belongs to M4.
- Not started: runtime UI, the player layer, galactic play, networking, and save compatibility.

The order of the remaining work is the roadmap's [work order](roadmap.md#work-order).

## Data and formats

### Installations and the VFS

- Steam EAW and FoC installations are discovered automatically. `check_installation_root` refuses
  an explicit root that is not an installation (and names a mod folder as one) in EAWDevLauncher,
  EAWEngine, EAWLauncher, and EAWAssetInspector; without the check, discovery silently fell back to
  the Steam installation without the mod.
- MEG v1 archives, loose files, inherited EAW data, patches, and repeatable mod layers form one
  case-insensitive layered VFS in the order AGENTS.md fixes. Relative mod paths resolve as the
  game's MODPATH (`resolve_mod_path`: under the runtime root, then as given).
- The sound archives mount under `DATA/AUDIO/SFX/` for each layer, with only the `--language`
  archive (archive discovery skips their folder).
- Malformed data: `megafiles.xml` entries inside XML comments are not listed (the stock manifests
  have none); a MEG header whose counts need more bytes than the archive holds is rejected before
  anything is reserved; a path listed twice in one archive is counted and reported when mounting
  and resolves to the later record (an engine choice; no stock archive has one).
- `EAWAssetInspector extract` copies resolved files out of the VFS into a chosen folder
  (`asset_output_path` refuses archive names that could escape it).

### Formats

Readers and writers live in `eaw_formats` (bytes to plain `...Data` types, no I/O):

- **DAT** localization: lossless read and write, validation, sparse TSV workspaces, new-unit
  bundles, a CLI, and the Dear ImGui editor.
- **DDS/MTD**: stock textures, mip and subresource layout, BC1-BC5, common uncompressed and DX10
  metadata, mega-texture directories.
- **ALO models**: render-ready geometry, skeletons, materials with raw parameters, skin palettes,
  bind transforms, attachments, proxies, lights, dazzles, the second texture-coordinate set
  (`TEXCOORD1`), and bone billboard modes. `inspect` prints every shader parameter, each bone's
  billboard mode, and each submesh's second-UV range.
- **ALO particle systems** (`formats/alo_particle_system.hpp`, `read_alo<AloParticleSystemData>`):
  every stock particle ALO (root chunk 0x0900) with emitters, raw properties, shape groups, keyframe
  tracks, and child links; the 0x1500 plugin format is detected and rejected
  (`EAWAssetInspector particle-audit`).
- **ALA animations**: compressed tracks, visibility, stepped tracks. A track counts as animating its
  bone only when its keys change (flagged in the file and moving by more than about 0.1 degree or
  0.001 units; `AnimationTrack::animated`).
- **TED maps**: land terrain, elevation, passability, and objects; space objects and tactical
  bounds; each placed object's hint (`TedPlacedObject::hint`, from its 0x482 container; 2,450 hinted
  stock objects, 1,713 distinct hints). Per environment: fog (color 0x0f, switch 0x10, start 0x11,
  end 0x12), wind (direction 0x2b in degrees, speed 0x2c on the editor's 1-10 scale, a reading of the
  stock values), and the cloud layer (texture 0x2f, repeat 0x2d in world units, drift speed 0x2e;
  the drift follows the wind direction, which the terrain chunk's 0x10 velocity supports). All 269
  resolved stock maps decode. `map-audit` reports every environment field by type, size, and value
  range. Unread: the water fields 0x17-0x22, the terrain layer and track header fields (two
  angles, two colors, flags), and the cloud field that may weaken cloud shadows (perhaps 0x24).
- **XML**: Petroglyph fragments, a typed definition and reference index, optional validation
  against a local public `eaw-schema`; element nesting is bounded at 256 levels with a diagnostic
  (stock nests at most 5).
- **Lua inspection**: source dependencies and structural inspection of standard Lua 5.0 and
  Petroglyph persistent bytecode, without executing it.
- **WAV** (`read_wav`, every chunk kept) and **ANIMATIONSFXMAPS.TXT** (`read_animation_sfx_map`,
  rows of fields).
- **PNG and RGBA images** (`formats/rgba_image.hpp`, `formats/png_image.hpp`: uncompressed PNG,
  read back only in that form) for captures; the comparison itself is `eaw_image_compare`.

### XML loading

`XmlAssetDatabase` reads the files `GameObjectFiles.xml` and the other registries list, in list
order, then the rest, so a duplicated name resolves by its registry's rule: first registered for
game objects, last for hardpoints and the other last-wins registries (from the public eaw-schema,
to be confirmed by a black-box test). Definitions in files nothing reaches are left out when their
type is one the game loads only through lists (stock FoC: EAW's campaigns and story events, 4,813
definitions in 208 files). `xml-audit` reports registry entries naming no file and unreached files.
Without registries (synthetic data) every file loads. Details in compatibility-research.md, "XML
loading".

### Corpus at the end of M1

All 638 resolved stock XML files parse, indexing 21,283 named definitions across 185 element types
(20,881 typed with eaw-schema); all 2,213 DDS files and the 1,107-entry command-bar MTD decode; 370
Lua source scripts (85,312 lines) resolve every detected dependency.

## Code organization

The code was reorganized on 2026-11-09 and 2026-11-10 in six steps, each with an unchanged
simulation hash, bit-identical WARP captures, and byte-identical stock-data audits; since then
tidying is continuous (AGENTS.md, "Code health"; leftovers in the roadmap's Code health list). The
engine's categories became libraries of their own on 2026-11-13 under the same checks.

- `eaw_math`: the only vector, quaternion, matrix, and transform types (row-vector convention);
  matrices and quaternions have no default value (`identity()`, `zero()`).
- `eaw_core`: the foundation only: error handling, the log, `string_utils` (one ASCII case
  folding, trimming, search, and UTF-8/UTF-16 conversion, replacing about 50 local copies),
  `file_io` (size-limited reads, atomic writes through an exclusively created staging file), and
  `checksum` (one CRC-32).
- `eaw_formats`: readers and writers of the original's formats, bytes to plain `...Data` types
  (`read_alo<AloModelData>`, `read_alo<AloGenericData>`, `read_ted`, `read_dat`/`write_dat`, ...),
  with per-format helper files; no I/O.
- `eaw_vfs`: the layered VFS, MEG archives, path normalization, and installation discovery.
- `eaw_assets`: the game asset layer (XML database and schema, catalogs, map object layout, legacy
  materials, ALO naming rules, clip names, legacy units, typed type fields) and the engine-native
  resources `Model`, `Skeleton`, `Animation`, `ParticleEffect`, and `Sound`, with pose sampling and
  the particle fidelity rules. The `ResourceManager` loads and caches every resource by type over
  `AssetCache` (hot reload intact); resource IDs are deferred until a consumer needs them.
- `eaw_localization`: the DAT editor's and EAWTools' localization tools.
- `eaw_battle`, `eaw_presentation`, `eaw_scene`, `eaw_control`, `eaw_sandbox`: battles, the
  presented battle, scene building, cameras and RTS control, and the sandbox's testable parts, each
  included by its directory (`"battle/battle.hpp"`). A library includes only what it links, so the
  build enforces the direction: scene building depends on presentation and battles, presentation
  on battles, the sandbox on all; the battle library includes no render header and reaches the
  render contract only through the asset layer, which uses it for vertex data.
- Gameplay reads of XML types and models are on the battle side (`object_types.*`: the cached
  catalog, movement classes, combat content loaders, type fields, model footprints, hardpoint
  models; `battle_map_build.hpp`, the shared map walk; `spawnable_types`; `grid_targets`); the look
  fields of types are with scene building (`type_looks.cpp`); rigs, clips, palettes, and looks are
  in presentation (`visual_rig.*`, `entity_look.hpp`).
- Build: `build.ps1` (configuration, clean, tests switch, C++20 at configure); Debug libraries and
  executables carry the `_d` suffix, `DebugValidation` ones `_dv`, and the launchers start the
  engine by its built file name.

## Game asset layer

- **Typed type fields** (`assets/type_fields.hpp`, `TypeFields`): a game object type's fields read
  through its variant chain with one number rule (like C's atof, the stock "2.0f" suffix accepted,
  other trailing text reported), comma lists, and yes/no values; diagnostics name the type, field,
  value, and the variant that declared it. Combat content, object type properties, and projectile
  visuals read through it. XML booleans go through one helper (`parse_xml_bool`).
- **Legacy units** (`assets/legacy_units.hpp`): the original's per-frame values are converted in
  one place (`speed_per_frame`, `acceleration_per_frame`, `degrees_per_frame`, `frames`, at
  `frames_per_second` 30, a guess), keeping the readers' order of operations. Plain degree-to-radian
  conversions are units, not guesses, and stay where they are.
- **Game object catalog**: `GameObjectCatalog` resolves TED placed-object CRCs (CRC-32 of the
  uppercased type name) to XML definitions and models through `Variant_Of_Existing_Type`
  (`EAWAssetInspector map-objects`: 99.7% of stock placed objects reach an existing model);
  `build_map_object_layout` adds per-object world transforms.
- **Spawn catalog** (`assets/spawn_catalog`, `list_spawn_types`): the unit types with a model, plus
  companies and squadrons without a model of their own where a member has one (the hero companies
  such as `Darth_Team`, `Han_Solo_Team`; 36 in FoC). Death clones (types a `Death_Clone` entry names,
  or that carry `Specific_Death_Anim_Index` or `Death_Persistence_Duration`) are wrecks, not units,
  and are left out (770 land types became 494). `EAWAssetInspector spawn-types` lists them as the
  launcher and sandbox do (stock FoC 2,710; with New_Order_Mod_Dev 6,063; with Republic at War
  10,092).
- **Models** (`Model`, `Skeleton`): every `_ALT<n>` damage state and `_LOD<n>` detail level kept
  (`ModelMesh::damage_state`, `detail_level`); `drawn_model_bounds` (`assets/model_geometry.hpp`:
  drawn meshes and submeshes, rigid meshes on their bone, hardpoint attachments intact) is the one
  model extent gameplay and the scene use; `has_drawn_geometry` (a drawn submesh with a whole
  triangle) decides whether a placement or spawnable type exists (on the stock maps 59 placements do
  not: 42 particle files placed as props, 16 swamp bug swarms, one traffic shadow).
  `assign_billboard_centers` computes each grass billboard's center (the mean x, y of each connected
  piece of a billboard submesh), since the stock grass files leave their second UV set empty.
- **Animations**: `sample_local_pose`, `blend_pose` (normalized-lerp rotations, linear translation
  and scale), and `compose_pose` blend in bone space (`decompose_transform` splits bind transforms).
  Clip roles, `parse_clip_name`, and `clip_file_stem` are in `assets/clip_names`; clips follow XML
  `Land_Model_Anim_Override_Name` (27 stock uses through the variant chain: civilians, slaves, and
  field commanders play RI_INFANTRY, EI_TROOPER, or NI_GMale_Urban_A clips), retargeted by bone name
  (`ala_retarget_by_bone_name`); land models without clips: 290 (buildings and props).
- **Particles**: `particle_rules` normalizes each particle system into a `ParticleEffect` of
  `ParticleEmitterDesc`s under a `ParticleFidelity` profile ([Particles](#particles)).
- **Effect catalogs**: `TerrainDecalCatalog` (`assets/terrain_decals`, `TerrainDecalFX.xml`: mode,
  opacity, size, angle and randomness, fade, permanence, atlas cells and span; malformed fields and
  cells outside the 4x4 atlas reported; the atlas layout is in compatibility-research.md);
  `DynamicTrackCatalog` (`assets/dynamic_tracks`, `DynamicTrackFX.xml`: texture, mode, width,
  opacity, segment length, fade distances and fade per second; a track without texture, width, or
  segment length reported and left out); `SurfaceEffectCatalog` (XML `SurfaceFX` with their
  `Surface_Settings`, each surface's `Dynamic_Track_Name` and its `_Left_Bone`/`_Right_Bone`);
  `WeatherScenarioCatalog` and `sample_weather` (`WeatherScenarios.xml`).
- **Sound**: `decode_sound` recognizes the format from the first bytes and converts PCM of 8 to 32
  bits and 32-bit float to the engine-native 16-bit `Sound` (MP3 and Ogg are recognized and
  reported as not supported yet); `ResourceLoader<Sound>` caches sounds; `SoundEventCatalog` reads
  the XML `SFXEvent`s with presets; `resolve_sound_sample` finds samples under SFX or the language's
  speech folder, trying `.MP3`; `AnimationEventTable` turns ANIMATIONSFXMAPS' SOUND and SURFACE
  rows into events by animation stem (times in hundredths of a frame, an inference; NONE left out).
  On stock FoC all 1,152 rows read and every name resolves.

## Rendering

The Direct3D 12 sandbox renders maps and models in **legacy mode**: rendering that approximates
Alamo, driven by `LegacyMaterial` reference profiles and newly written shaders. A modern
physically based pipeline with engine-native materials comes after legacy mode is stable; the
profiles will then be conversion input for it. Features marked **enhanced** are the engine's own
improvements, off or optional unless stated; legacy is the default.

### Device, frames, and resources

- A Win32 window, triple-buffered frame contexts, and a waitable flip-model swap chain; optional
  tearing, WARP, the debug layer, and GPU validation.
- Independent direct and copy queues with monotonic per-queue fences; copy-to-direct
  synchronization on the GPU; uploads retired by fence value.
- DRED in every build and device-removal diagnostics: a failed call on the device or any of its
  objects reports the removal reason, each unfinished command list with its last completed
  operation, and the page-fault address (`check_device_hresult`). Resources retired before the
  render that records their upload transitions drop those transitions (`retire_uploaded`), so a
  resize flush in between cannot free a resource a barrier names.
- The render contract is split by subject (`visual_vertex`, `visual_texture`, `visual_material`,
  `visual_mesh`, `visual_camera`, `visual_frame`, `visual_scene`); rules without a graphics API live
  in `eaw_render_core` (`lod_selection`, `gpu_resource_lifetime`, `frame_graph`, `shadow_cascades`,
  `compute_sky_visibility`, `wind_bend`), tested without a device.
- `render()` builds its frame as frame-graph passes whose declared uses place every barrier;
  passes run in declared order and targets stay persistent. The graph knows `common` (buffers
  return to it after every submission), `copy_dest`, `depth_read`, `compute_read`, and
  `unordered_access`.
- Textures live in a generation-checked handle pool, take texture-table slots from a fence-deferred
  allocator, and stream in under a per-frame byte budget (`--upload-budget`), drawn with
  role-appropriate fallbacks through a frame-buffered logical-texture residency table. Replaced
  buffers, textures, and bone palettes retire on the direct-queue timeline
  (`gpu_resource_lifetime.hpp`; `sandbox --reload-every` exercises it). Geometry, instance, and
  material buffers are per scene, and the renderer holds one scene at a time.
- Runtime mesh registration: `D3d12Renderer::add_meshes` appends a mesh fragment (vertices,
  indices, textures, materials, draws, meshes; no instances or terrain) as a geometry block of its
  own (the draw loop rebinds per block), rebuilds the material buffer from a CPU copy, streams new
  textures and shares those whose identity is already present, and returns the first new mesh
  index; existing indices stay valid, and `load_scene` drops added blocks. Not yet: removing meshes.
- Instances: `set_instances` submits each frame's instances (any count) through a growable
  frame-buffered buffer, one `DrawIndexedInstanced` per mesh draw. The bone palette is set per frame
  (`set_bone_transforms` before `set_instances`, up to `max_bone_transforms()`, 262,144 matrices;
  frame buffers grow on demand); last frame's palette is uploaded after the current one in the same
  buffer for motion vectors.
- Shaders are newly written HLSL under `src/render/shaders` (DXC, shader model 6.0, root signature
  declared in the HLSL) and `src/tools_gui/shaders` (D3D11 tools, FXC, shader model 5.0), compiled
  at build time into embedded bytecode. Shader reflection metadata is not generated; the
  pipeline-state cache is opt-in (`--pipeline-cache <dir>`).
- The descriptor and bone capacities are temporary implementation limits, not format limits. A
  complete renderer-agnostic RHI has not been extracted.

### World and cameras

- The world is right-handed with Z up; the view and the CPU camera math (`camera_math`) are
  right-handed, matched to the owner's screenshots of the original's editor. Background parallax
  and sky-dome scaling are inferred, not measured.
- Alamo models face -Y (muzzle and fire bones, compatibility-research.md): an entity heading is the
  direction of its model's -Y axis (`model_forward_heading`, `identity_model_heading`). A placed
  object's Z rotation is its facing (counter-clockwise from +X, as the simulation's headings), so
  `placed_object_transform` turns the model a quarter first (`placed_model_facing`); squadron offsets
  are in the facing frame (+X ahead) and take the inverse turn.
- Cameras are engine-owned (`eaw_control`): a `VisualCamera` from the fixed RTS camera (fixed pitch
  and heading, zoom, keyboard pan along the true right; the pitch and heading are provisional), the
  debug camera, and a free flying camera (`FlyCamera`).

### Materials and lighting

- `LegacyMaterial` holds reference profiles for all 39 readable effects of the shader sources
  published with Petroglyph's official mod tools (kept locally in `Reference Materials/Shaders/`):
  render phase, depth, cull, and alpha-test state, lighting model, texture-channel roles, UV and
  vertex animation, vertex requirements ([legacy-shader-mapping.md](legacy-shader-mapping.md)).
  Unreferenced names keep tagged name heuristics. Material translation is conservative.
- Shading: fill plus key-light lighting (2x diffuse convention, exponent-16 highlight per pixel),
  normal maps, gloss from base or normal alpha or a gloss texture's red, team-color masks,
  self-illumination masks, material Diffuse/Specular/Emissive/Color, alpha test, blend, and
  additive, UV scrolling, and vertex colors where the legacy profile reads them. Maps use their
  active TED environment's key, two fill lights, specular, and ambient; team colors come from the
  type's affiliated faction `Color` (player-slot palette fallback).
- Bone billboards (236 stock meshes: planet glows, tree shadow cards, sky-dome suns, flags; see
  compatibility-research.md, "Bone billboards"): meshes on a bone with a billboard mode keep their
  vertices relative to the bone (`VisualMaterial::billboard`, `billboard_anchor`), and the vertex
  shader turns them about the bone: toward the camera, about the vertical toward the camera, light,
  or wind, or, for sun glows and suns, at the bone's offset turned toward the light and facing the
  camera. `texture-audit` counts meshes by mode.
- Transparency: blended parts (alpha and additive, which write no depth) are drawn after the opaque
  and alpha-tested ones, instance by instance from the farthest bounding-sphere center, each model's
  parts in their own order; backgrounds keep scene order and particles follow in their own
  back-to-front order. One draw per blended part and instance.
- Detail levels: the renderer picks each instance's `_LOD<n>` level by the `--lod` policy
  (`distance`, `screen`, `hybrid` by default, or `off`), scaled by the type's `LOD_Bias` and
  `--mesh-detail`, with hysteresis, and cross-fades level changes of keyed instances over 0.25 s
  with complementary dither masks. Distances, pixel sizes, the hysteresis, and the fade time are
  engine choices. Damage states are chosen by presentation ([Damage states](#damage-states)).

### Special materials

- **Planets** (`VisualLightingModel::planet`, 296 stock instances): the surface lit through its
  normal map and brightened toward the atmosphere color at the rim, city lights (base alpha)
  switching on past the terminator under the clouds, and a cloud layer scrolling along U, lit by its
  own normal map, blended over all by its alpha (behavior from the published Planet shader, written
  anew). Planets and shields share generic layer fields in `GpuMaterial` (144 bytes).
- **Shields** (`VisualLightingModel::shield`, `MeshShield`, 27 uses): the energy texture shifted
  by a quarter of the distortion texture, times the wave texture, times the vertex color softened
  toward vertical edges (saturate(object normal z + edge brightness), per vertex) and the material
  color, added unlit. The legacy shader samples its distortion layer with the scale and scroll named
  for the wave and the other way round; the contract names layers by what they do and the
  translation keeps the observed pairing. Used by the hidden `shield` meshes of capital ships and
  base shield generators (flashed by the shield abilities), the `stealth` meshes of the FoC stealth
  ships (visible in their models; when the original shows them is not established), and
  always-visible energy spheres (the Gungan projectile, the droideka's shield).
- **Sky domes** (`VisualLightingModel::sky`, `Skydome`, 19 uses): lit by every map light through the
  turned-around normal, times the vertex color, plus `Emissive`, with the cloud layer (scrolled
  along U, then scaled) over the base by cloud alpha x vertex alpha, never shadowed.
- **Nebulae** (`VisualLightingModel::nebula`, 15 uses): vertices wobble in model space by a wave of
  world position and time; brightness 2 x (view-space normal z)^8 + 0.1.
- **Trees, wind bend** (`Tree`, 185 uses): tree vertices move by BendScale x the bend offset x their
  height above the model's origin squared / 40^2, as the legacy Tree shader shapes it, in the scene
  and shadow vertex shaders; `wind_bend` sets the offset (6 units in a strong wind, in proportion to
  the speed, leaning downwind and swelling and easing by half over a 4 s gust: engine choices; a
  42-unit Dantooine tree in the default wind leans about 1 unit at the top). Last frame's bend moves
  the motion vectors.
- **Grass** (`Grass.fx`, 14 uses): each vertex turns about its clump's center to face the camera
  around the model's up axis, the clump's top waves with the map's wind (faster and further in a
  stronger wind, phase-shifted across the map), the color swings between `Diffuse` and `Diffuse1`
  with the wave, lit as facing up. `GRASS_SWAY_SCALE` (0.15) keeps the sway to a few units, an engine
  choice.
- **Distance fade** (the legacy Grass, BatchMeshAlpha, and BatchMeshGloss shaders fade linearly with
  distance from the camera, by values the original's engine supplied at run time): materials with the
  profile's `distance_fade` feature (`VisualMaterial::distance_fade`) are whole within
  `distance_fade_start` of the camera and gone from `distance_fade_end` on (5,000 and 7,000 world
  units by default, engine choices; Graphics panel sliders "Grass and props fade from" and "gone
  at"). Blended ones (grass, BatchMeshAlpha) fade their opacity; the opaque BatchMeshGloss keeps the
  share of its pixels by the screen's 4x4 dither, which TAA smooths, instead of the legacy shader's
  blend (an engine choice; faded props still cast shadows).
- **Occlusion silhouettes** (the legacy OccludedUnit shaders, on by default, Graphics panel): a unit
  whose type sets `Occlusion_Silhouette_Enabled` (86 stock objects) shows through what hides it in
  its team color at half strength (the legacy default color's alpha; which color the original used
  for players is not established). A `silhouettes` pass redraws opaque and alpha-tested draws into a
  mask (max blend), keeping front faces where the pick target shows another object at least 0.5
  units nearer; a composite blends it before the transparent part. A hardware depth-greater test let
  every visible unit through (redrawn geometry does not reproduce its stored depth exactly), hence
  the shader test.
- **Heat distortion** (170 stock heat-particle emitters, 3 heat meshes): legacy (the default) blends
  heat particles' bump colors by opacity into a heat buffer cleared to "no shift" (depth tested), and
  a `heat apply` pass redraws the scene copy shifted by it, a bump color of 1 or 0 moving the sample
  1.5% of the screen height (an engine choice). Enhanced (`--heat enhanced`, the owner's choice from
  heat-research.md): signed shifts into an RG16F buffer, sized in world units (2 at full bend) and
  turned into a screen shift by distance, capped at 12 pixels, with a faint drifting second layer
  (shimmer, a third as strong); the apply pass fades near the screen's edges (3%) and where the
  shifted sample is nearer than the pixel (over 4 units); TAA weighs the new frame up to 0.6 where
  heat shifts the image. Only when the scene renders into its own target (bloom or anti-aliasing
  on). Heat meshes (`MeshHeat`) are still skipped. All constants engine choices.
- Not yet: cube reflections, fog of war, and the materials the roadmap's Rendering track lists.

### Terrain, water, and tracks

- Land terrain renders as a textured heightfield: per-vertex layer index, three-layer blending,
  layer tiling, normal maps; layers without a color texture are not drawn (an inference from Cloud
  City and Kamino). TED tracks render as unlit, alpha-blended ribbons draped on the terrain with
  start, middle, and end opacity and texture scroll. TED map decals (one stock map, no established
  texture link) are not rendered.
- Cloud shadows (legacy, on by default): the terrain and terrain-mesh materials
  (`VisualMaterial::cloud_shadow`) multiply their lit color by the drifting cloud texture before
  fog. The stock W_Clouds00 averages 0.74 and reaches black, so the terrain darkens by about a
  quarter under the literal formula; whether the original weakened it is not known. Units take no
  cloud shadows in legacy.
- Water, lava, and ice surfaces render at the map's liquid level. Water is a grid (cells of about 12
  units, at most 512 a side; `append_water`; lava and ice stay one quad) whose vertices keep the
  depth beneath them. The scene is drawn in three passes: the opaque part, the water (materials
  marked `VisualMaterial::water`, reading the opaque depth through a read-only view), and the
  transparent part. The map's own surfaces no entity draws (the water plane, TED tracks) are kept
  as scenery by scene presentation.
- **Legacy water** (`--water legacy`, the default) resembles the original's low-end fallback, since
  reflection and refraction are not modeled. Its bump map and base color repeat every 200 world
  units (an engine choice; the TED water fields may hold the original's repeat), with a broad layer
  of both at about five times the repeat and an unrelated angle so no grid lines up (`water_broad_*`
  in sandbox.hlsl).
- **Enhanced water** (`--water enhanced`, the owner's plan in water-research.md; every constant an
  engine choice): color and opacity from the view ray's path down to the opaque depth (Beer-Lambert),
  a shore fade over the first 3 units of depth, light bent by two layers of the bump map, Schlick
  Fresnel, a sharp key-light glint; refraction from a copy of the opaque scene (2% of the screen
  height at full strength, growing over the first 4 units of depth, unshifted where the shifted
  pixel shows something above the water); planar screen-space reflections (a compute pass mirrors
  every opaque pixel above the water plane and keeps, per pixel, the source lowest on the screen
  through `InterlockedMax`; one-pixel holes filled from neighbouring rows), the captured sky where
  nothing landed; shore foam within 6 units of depth, 35% over a flat bottom and full where the
  bottom is steep, with lines rolling in every 2.5 units of depth. Refraction and reflections only
  when the scene renders into its own target (bloom or anti-aliasing on). At the tactical camera's
  pitch reflectance is 2-6%.
- **Ocean waves** (enhanced water on ocean maps, off by default since the owner parked it; the Graphics
  panel's Waves setting turns it on; docs/water-research.md, "The ocean plan"): an FFT
  ocean of three cascades (patches of 1000, 263, and 67 world units without a common factor, bands of
  200-1000, 50-200, and 4-50 units; `render/ocean_spectrum.hpp`). Their starting spectra (JONSWAP
  limited by depth, Hasselmann spreading around the map's wind, a swell term; seeded) upload when the
  sea state changes; each frame a compute pass (`ocean.hlsl`) evolves them (deep-water dispersion,
  gravity 30), runs 256-point inverse FFTs along rows and columns on eight real fields carried two to
  a complex value, and packs half-float displacement (choppy x, y, height) and derivatives (slopes,
  Jacobian), copied into six textures (`d3d12_renderer_ocean.cpp`; about 3 MB of spectra and 6 MB of
  fields). The two long cascades move the 12-unit water grid and bend its normal; all three bend the
  pixel's normal, each fading as its patch shrinks on screen with its slopes moved into the glint's
  width (`water_layer_presence`). Foam persists: each cascade's foam (the derivative textures'
  fourth channel, read back from the packed buffer the next frame) halves every 2 seconds and gathers
  where the cascade folds (its Jacobian below the panel's threshold, 0.6, fully 0.5 below it); the
  pixel shader takes the most of the three cascades' foam, each faded like its slopes, broken up by
  a fine value noise (`ocean_whitecaps`). The
  water's body on open sea is lit with the flat surface's normal and a tenth of the key light's
  direct term (`legacy_lit_color_scaled`), scaled by 1 plus eight times the change in the key
  light's facing between the wave's normal and the flat surface, never below 0.35 of it, so the sea
  keeps the legacy water's deep blue (a flat wave normal facing the light had washed it grey) while
  slopes toward the light brighten and slopes away darken (engine choices, by eye against the legacy
  water). The
  scene texture table and the anisotropic sampler are visible to vertex shaders for it, and the frame
  graph has an `all_shader_read` access. Faded in from the calm depth to
  the full depth so shores and units in the shallows keep the flat plane, which the simulation keeps
  too. The Graphics panel tunes the ocean (`VisualOceanWaves`): the sea state (wind speed,
  fetch, swell; a change uploads new spectra), choppiness, a height scale (at most 2), the calm and
  full depths (0 and 10 by default, the owner's tuning), the Jacobian below which crests foam, and
  Defaults. The mesh's bounds allow 40 units of wave height (`water_wave_reach`).
  Waves run only on open sea (the owner's choice): `ocean_map` names the ocean planets by the map's
  file name, Kamino and Manaan, and sets `VisualLightRig::ocean`; the panel's setting is ocean maps,
  on, or off (the default while the ocean is parked). A corpus survey showed why a measure cannot decide it: the share under water
  puts the MP Dagobah swamp (57% at least 5 deep) beside Kamino (60%) and Mon Calamari (61%). With
  waves full from shallow depths, a shelf in the sea floor shows as a sharp line between calm and
  waves. Not yet: wave motion in the motion vectors (water is blended, so TAA reprojects it by
  depth; ghosting is the owner's to judge).

### Sky, fog, and global illumination

- Land maps draw the active environment's sky dome and backdrop as a camera-centered background
  layer; backdrops (`In_Background`: planets, nebulae, ring stations) and background instances
  receive no shadows (the owner's choice; `VisualInstance::receives_shadows`).
- **Fog** (`--fog off|legacy|enhanced`, legacy the default): legacy blends models and terrain toward
  the environment's fog color linearly with view distance; additive surfaces and backgrounds are not
  fogged, as in the legacy shaders. Particles are fogged in both modes: alpha particles blend toward
  the fog, modulate ones lose their darkening, and additive light is left alone in legacy and dimmed
  in enhanced. Enhanced fog is exponential height fog integrated in closed form beyond the map's
  start distance, its density at the floor (the terrain's lowest point) set so level fog reaches 95%
  at the map's end distance, constant below the floor and thinning by e per 400 units above,
  brightening toward the key light and leaning halfway toward the captured sky just above the
  horizon (engine choices). The ranges were authored for the original's camera limits
  (TacticalCameras.xml, not read yet): zoomed out farther, as the sandbox's overview camera is,
  distant ground fades into the fog. The original's land camera (460 units away, pitch 50, far clip
  7000, objects faded out at 1100-1300) saw only about 1000 units (an estimate), so its fog was
  atmosphere and color, not the optimization. Volumetric fog is for later.
- **Sky capture and sky light**: each frame a `sky capture` pass draws the background draws into six
  64-texel cube faces in a sky atlas, and a `sky light` compute pass projects it to second-order
  spherical harmonics. Enhanced water reflects it; enhanced fog leans toward it; **sky ambient**
  (enhanced, off by default, `--sky-ambient`) blends three quarters of the fill light toward the
  sky's irradiance for the surface's normal, balanced straight up against the legacy fill, so
  upward surfaces keep the legacy brightness and the sky reshapes the light on slopes.
- **Sky visibility** (enhanced, off by default, `--sky-visibility`): `compute_sky_visibility` finds,
  for each terrain height sample, the horizon's angle in 16 directions out to 600 units and averages
  cos^2 of it, once per map in scene building (`VisualTerrain::sky_visibility_texture`); the fill
  light of every world surface is multiplied by the visibility under it. Engine choices.

### Shadows

Cascaded shadow maps for the key light: `shadow_cascades` covers the view from the near plane to
the scene's far edge (rounded up to a step of sqrt 2), split between even and logarithmic spacing (3
cascades, blend 0.75, 2048 texels, at most 20000 units), each cascade a bounding sphere of its slice
(radius rounded to 1/16) with its origin snapped to whole texels, and depth over the whole scene
along the light. A pass draws every cascade's depth map before the scene: the terrain (holes such as
crater openings cast nothing) and every opaque and alpha-tested model, skinned, with a slope-scaled
bias; backgrounds and blended parts do not cast. Receivers pick the cascade by view depth, with a
normal offset of 1.5 texels, a 12-tap Poisson PCF of 1.5 texels turned per pixel and frame, dithered
into the next cascade over the last tenth of each slice and faded out at the far end. Shadows take
away only the key light's diffuse and highlight, as the original's shadow volumes did. All numbers
engine choices; `--shadows off` reproduces the unshadowed look. Every caster is drawn into every
cascade (a marked shortcut).

### Post-processing and anti-aliasing

- **Bloom** (on by default; `bloom_threshold` 0.6, `bloom_strength` 0.8, an engine stand-in for the
  original's glow option tuned by eye; `--bloom <strength>`, `--no-bloom`): the scene renders into
  its own target; a bright pass thresholds each texel of a 4x4 block with a soft knee and mixes the
  block mean half and half with its brightest texel (so glows a few pixels wide still bloom) into a
  quarter-resolution `R11G11B10_FLOAT` target; two rounds of separable nine-tap Gaussian blur; a
  composite writes scene plus bloom before debug lines and the overlay. Bloom ignores the sky
  (backgrounds at the far plane) and takes the rest scaled by how little fog covers it.
- **Anti-aliasing** (`--aa`, FXAA by default; the owner's choice: no MSAA): FXAA-style edge pass
  written for the engine; TAA (sub-pixel jitter from 8 points of the Halton (2, 3) sequence, history
  at 10%, reprojection by depth and motion vectors using the nearest depth of the 3x3 block, history
  clipped to the block's color spread, mean +/- 1.25 sigma in YCoCg; the history resets after a
  resize and in frames without TAA); hybrid (TAA, then the edge pass over its result, the history
  keeping TAA's own result). Debug lines use the unjittered camera. Thresholds and weights are
  engine choices.
- **Motion vectors**: a third scene target (RGBA16F) holds each pixel's screen motion from the
  unjittered current and previous clip positions; instances carry their last transform and palette
  offset (per instance key). Opaque and alpha-tested models write motion; backgrounds, blended
  parts, terrain, and particles leave a no-motion marker and are reprojected by depth and camera.
- **Reactive mask**: particles raise the motion target's blue channel to their strength (added light
  by its brightest channel, blended particles by opacity, modulating smoke not at all); TAA weighs
  the new frame up to 0.85 there and the edge pass leaves those pixels alone, so thin, fast bolts and
  sparks are neither averaged away nor blurred (measured on Kamino at 1080p: the bolts' peak under
  TAA 180 against 199 without anti-aliasing).

### Particles

- `ParticleWorld` (`presentation/particle_effects.*`, presentation only, a seeded PCG32 stream per
  effect, never reading file fields) plays `ParticleEffect`s on the frame clock, following the
  MIT-licensed GlyphX particle editor where it differs from alo-viewer: bursts and continuous
  emission, initial delay, skip and freeze time, lifetime and size variances that only shorten and
  shrink, color, scale, texture-cell, and rotation tracks, gravity, acceleration, inward speed and
  acceleration, link to system (rigid: translation and rotation), ground collision, trail and death
  children, and an explicit live-particle capacity with drops counted. Emission is sub-frame: each
  particle spawns where the emitter was at its emission time and lives only the rest of the update,
  so fast movers leave even trails. The stock data has no separate trail system: engine and missile
  trails are world-space particles of moving emitters.
- Fidelity (`sandbox --particles legacy|enhanced`, `MapBuildOptions::particle_fidelity`): legacy, the
  default, draws additive ignoring alpha and effects with emitters in list order; enhanced scales
  additive by alpha and sorts every particle back to front. Linked particles follow their emitter's
  position and rotation under both (the editors' "position only" reflects previews that never
  rotate).
- The renderer draws `VisualParticle`s (`set_particles`): billboard, ground-plane, and
  velocity-stretched kite quads expanded in `particle.hlsl` from a growable frame-buffered structured
  buffer; additive, alpha, modulate, and heat blends, depth-tested or not, no depth or pick writes;
  textures from the scene's logical texture table (`add_textures`).
- **Soft particles** (enhanced, off by default, `--soft-particles`): depth-tested particles fade out
  as the surface behind them comes within 20 world units (an engine choice).
- Not yet: wind acting on particles.

### Decals

Terrain decals are applied inside the terrain shader, clustered-decal style (the owner's option A
of three): each frame the renderer sorts the live `VisualDecal`s into a 64x64 grid over the terrain
(at most 64 a cell), and the terrain pixel applies its cell's decals before lighting: diffuse ones
blend the albedo, bump ones bend the normal, additive ones add light; fog applies as to the ground.
The atlas's top level is sampled (a marked shortcut). Screen-space decals (option B) wait for a
decision on a deferred pipeline. What leaves decals is in [Battle effects](#battle-effects).

### Picking, debug lines, captures, and the overlay

- Picking uses a GPU pick target (`R32_UINT`, scene instance index + 1, terrain reserved) written by
  opaque, alpha-tested, and terrain surfaces; each request copies one texel (up to eight per frame;
  the rest wait, none are dropped) into the frame's readback buffer, read after that frame's fence,
  so results arrive a frame or two later without a CPU wait. Blended effects and backgrounds are not
  pickable.
- Debug lines (`set_debug_lines`, helpers in `presentation/debug_draw.*`) draw depth-tested over the
  scene from a frame-buffered, persistently mapped buffer with a temporary capacity of 32,768 lines
  a frame; lines beyond it are counted in `stats().dropped_debug_lines`.
- Captures: `request_capture`/`take_capture` copy the back buffer after the scene and
  post-processing, before debug lines and the overlay, into a per-frame readback (no CPU wait). With
  the sandbox's `presentation_now()` clock (wall clock, or `--fixed-frame-time`) held until textures
  are resident, a `--capture` run repeats bit for bit (checked on WARP with a Kuat battle, particles
  and bloom included). `EAWEngine compare-images` reports mean and largest differences and the
  changed share, with a diff image. Comparison with the original is by measured features from
  ordinary screenshots, not recreated scenes (see the roadmap).
- The Dear ImGui overlay: engine panels are submitted through `set_overlay_ui` (a callback inside the
  overlay's ImGui frame; button presses come back as actions applied next frame); typed characters
  and editing keys come through `set_overlay_input`, and while the overlay wants the keyboard or
  mouse (`overlay_wants_keyboard`, `overlay_wants_mouse`) the scene gets none. ImGui's textures (its
  font atlas grows when new glyphs appear) upload through the renderer's own path (copy queue, a GPU
  wait on the direct queue, slots and resources retired by fence value; the backend gets no texture
  requests). Documented exception for this debug overlay: it compiles its two shaders at startup
  (`D3DCompile`) and reuses its vertex and index buffers by frame index; an overlay renderer of our
  own would remove both.

### Known limitations

- Alpha ordering is per instance, not per triangle; material translation is conservative.
- The model view (`sandbox --model`) lists every `<MODEL>_*.ALA`, and Left/Right step through the bind
  pose and them; it shows the intact `_ALT0` state and the highest `_LOD<n>` level. Model scenes
  animate rigid attachments' bone visibility through a second, visibility-collapsed palette half
  (map meshes carry the same half); skinned-mesh visibility and gameplay-driven visibility are not
  modeled. A validation-triangle fallback stands in for a scene that cannot be built.
- The rest of the legacy-mode render backlog is the Rendering track of
  [roadmap.md](roadmap.md#rendering).

## Scene building and hot reload

- Scene builders (`eaw_scene`) read through the `AssetReader` interface and the `AssetCache` behind
  the `ResourceManager` (decoded models, textures, maps, the XML object catalog); every cached asset
  records fingerprints of the files it consulted (failed lookups and listings included).
- The sandbox builds a map or model on a worker thread while the window renders; `load_scene` may
  replace the scene at any time. Geometry and materials upload at once, textures stream. Decoded
  textures carry an identity (a fresh serial per decode; the process-wide texture identity counter),
  and `load_scene` keeps every resident texture the replacement names again, so a reload uploads
  only changed textures.
- `sandbox --hot-reload` polls the fingerprints and the MEG archives once a second on a worker,
  remounts rebuilt archives between builds, and rebuilds the scene from the cache; it waits for
  spawn package builds, and rebuilds keep runtime types. MEG archives added to or removed from a
  layer need a restart.
- The battle side and the look are built by one walk: `build_battle_map` (`battle_map_builder.cpp`)
  builds a `BattleMapAsset` (entities, spawnable types, ground, damage table, counts, warnings, and
  the placed models' extent, the frame of a map without ground) from the TED, the XML types, and the
  models without a scene builder; `build_visual_map` runs the same walk and then draws what it built
  (`VisualMapAsset::battle` beside the looks), so the two cannot drift. `MapEntity` and
  `SpawnableType` (`battle/battle_map.hpp`) hold gameplay data, `MapEntityLook` and `SpawnableLook`
  the look (scene instance or mesh, team color, scale, death, bank limit, animation, hidden), at the
  same index. An entity whose model has geometry but cannot be added to the scene stays in the battle,
  hidden, with a warning.
- Headless builds skip textures and animation clips (`MapBuildOptions`; Utapau builds in 0.45 s) but
  attach hardpoint models (`Model_To_Attach`), which existence and model-derived footprints measure.
  Nothing the simulation reads depends on drawing: the ground's liquid level comes from map data (a
  liquid type at or above the lowest ground), and placed entities carry no mesh index (the
  presentation finds them by id).
- Spawn packages: `scene_build::add_spawnable_type` (a `SpawnableBuild` context) is shared between
  map builds and `build_spawnable_package`, which builds one type (with its company or squadron
  members) into a `SpawnablePackage` (mesh fragment, rigs and rest palette, types with
  fragment-relative meshes, the requested type last). Not yet: sharing meshes between packages (a
  member type already loaded is built again).
- Map builds attach models' visible proxies' effects (`VisualRig::effects`), hardpoint models
  (`Model_To_Attach` at `Attachment_Bone`, 226 stock uses) merged into the host's mesh at their bone,
  rigid on its palette entry (`type_hardpoint_models`, `attached_mesh_key`), and the effects,
  projectile visuals and models, debris types, death-clone models, decal atlases, tracks, and weather
  the battle presents. Placed objects of XML `Is_Marker` types (flag models, script points, skirmish
  base positions) are not drawn (`MapEntityLook::hidden`) but stay entities.
- Unit `Scale_Factor` is applied (`type_scale_factor`, `scaled_model_transform`): models are drawn at
  it; model-derived separation radii, obstacle footprints, and hardpoint positions scale with it; and
  attached effects take the unit's scale, particle sizes included.

## Simulation

`eaw_simulation` is the deterministic tactical simulation: no graphics, platform, or file access.
Its structure follows [ecs-design.md](ecs-design.md); the migration's steps and what remains are
there and in the roadmap.

### Tick, schedule, and storage

- Entities have stable generation-checked ids. The simulation ticks at 60 Hz (the owner's
  decision, with the budget of 60 frames a second): one constant, `default_ticks_per_second`
  (`simulation/simulation.hpp`), the default of the clock, the world, `Battle`, and `BattleRuntime`.
  XML values per 30 Hz frame of the original are converted apart ([Game asset
  layer](#game-asset-layer)). Durations are seconds turned into ticks by the clock (the retarget
  interval 1/3 s, the chase replan 1 s, the group cohesion stall 3 s, separation's push limit 15
  radii a second).
- `SimulationWorld::step` runs one table, `schedule_` (`simulation_schedule.cpp`), of nine systems,
  each with its tick layer and the data it reads and writes (`SimData`): commands, behavior,
  navigation, movement, contact, combat, resolution, removal, publish. Behavior, navigation,
  movement, contact, combat, and publish are systems over typed views (`simulation/system_view.hpp`:
  `view.get<T>()` is const for `Read`, mutable for `Write`, and undeclared data does not compile),
  each with its own scratch, grids, and counters; commands, resolution, and removal keep full
  access, since they spawn and remove entities. Helpers shared between systems are static templates
  over any data with `get<T>()` (`simulation_internal.hpp`).
- Storage: 16 component groups of columns (`IdentityColumns`, `PlacementColumns`,
  `LocomotorColumns`, `RouteColumns`, and the rest), one array per field; swap-removal is one loop
  over `for_each_column`; spawn checks that every column grew by one, and `check_entity_table` that
  the column and group lists name every member. Optional components are pools
  (`ComponentPool<Columns>`: packed columns with a sparse index from slot to row; queries walk the
  dense table and look a member up by slot, so the pool's order never decides an iteration order):
  walk and turn plans played as clips, flight (`FlightColumns`: minimum speed, cruise height,
  manoeuvres; `flies(data, index)` is the flyer test), footprints, crush profiles, formation teams
  (`FormationColumns`), group cohesion (`GroupColumns`), combat (`CombatColumns`, one `CombatState`
  per entity with health, a shield, weapons, or hardpoints; others read `no_combat()`), and squads
  (`SquadColumns`, `SquadMemberColumns`). World resources are named structs with their `SimData` id
  (`GroundState`, `PathQueue`, `ProjectileState`, `TickEvents`, `SlotTable`, `Rules`, `Clock`,
  `SpaceAvoidance`, `CombatCounters`, `CombatRandom`).
- Recipes: `EntityRecipe` holds a type's data and `EntitySpawn` is a recipe id plus the instance's
  values; `SimulationWorld::add_recipe` validates a recipe once and keeps equal recipes as one (a
  company member with its team's speed, or a placement scaled differently, is a variant recipe).
  Entities point at their recipe's combat and movement profiles, and their type name is their
  recipe's.
- Structural changes: combat and contact queue their damage (`DamageQueue`: hits and misses with
  their blasts, crushes), and the resolution system applies it in queue order; a shot whose victim
  died earlier in the tick strikes the body without damage (the owner's rule: two units firing at
  once both hit). Deaths follow in dense order, each entity once per tick, in rounds until a round
  kills nothing, so a death blast and the blasts it sets off kill in the tick it goes off. The dead
  leave at the end of the tick in one stable pass (`remove_entities`), so the dense order is the
  spawn order; a script's destroy command uses the same pass at once.
- The state hash goes component by component, pooled components only for their members, recipes
  once each. It leaves out presentation keys and derived outputs (snapshot speeds, the fields marked
  "not hashed" below). `SimulationWorld::state_hashes` hashes by subject (`StateSubject`: bodies,
  routes, combat, clips, flight, formations, orders, projectiles, random) in the same pass;
  `battle --check-determinism --hash-trace` names the first tick and subjects where two runs differ.
- Timing: `step` times its layers into `Stats::phase_seconds` and `slowest_phase_seconds`, which
  `battle` prints.
- `DebugValidation` (`build.ps1 -Configuration DebugValidation`) is kept, empty, for checks too slow
  for Debug; the typed views replaced its former data-access copy check.
- Snapshots are immutable outputs for presentation: entities (position, heading, pitch, roll, yaw
  rate, speed, turret yaw, the order serial they follow, their squad, walk and turn clip state),
  shots with their weapon's sound and creation effect, projectiles (speed, serial, flown and
  remaining distance), impacts, deaths, hardpoint deaths, and spins; everything only presentation
  reads is not hashed.

### Parallel systems

The simulation takes an optional job system (`SimulationWorld::set_job_system`, not owned, not
state; without one everything runs serially with the same results). Parallel today, each chunk
with its own scratch and writing only its own entries:

- ground avoidance: ORCA velocities in chunks of 4 (`avoidance_grain`; chunks over the whole entity
  table left nearly all the work to one chunk, since spawned units of a type are neighbours in dense
  order). Measured: 1.48 ms a tick serially, 0.43 ms on 7 workers at 500 units;
- combat's target choice and turrets (`update_target`, `aim_turret`, in chunks of 8, `aim_grain`),
  which read the others' state (unchanged in this phase, since damage waits in its queue); the
  weapons then fire serially in entity order, since they draw from the one combat stream. Measured:
  combat 1.2 to 0.82 ms a tick, its slowest tick 6.2 to 2.3 ms;
- route searches in batches of `route_batch` (8): single-unit requests at the front of the queue and
  a shared request's members are planned into `PlannedRoute`s reading the state only, then given in
  order under the same budget check (`give_route`); routes the budget does not reach are dropped and
  planned again later, so the result is the serial one. A full batch runs on the job system, one
  search a chunk (`RouteScratch`, about 16 bytes a ground cell each, made only when needed); smaller
  batches run serially. The large Utapau order's worst route-search tick fell from 18 to 10-12 ms; the
  remaining spike is serial (the group's shared search and chunk graphs built the first time a size
  appears).

The headless battle creates a job system (`--workers`, default the hardware threads less one) and
`--check-determinism` runs its second run serially, so `check-battle.ps1` compares parallel against
serial in every battle (`test_parallel_avoidance`).

### Spawning and the ground

- Placed TED objects become entities built from their XML types (owner, position, heading, speeds);
  runtime spawning is the `spawn` command (`SimCommand::spawns`, rejected spawns counted).
  `make_spawn(recipe, instance)` is the one construction path from XML types
  ([Battles](#battles)).
- Placed objects whose type sets `Is_Decoration` are not simulation entities (the XML comment:
  decorations are idle and unsynchronized); presentation draws and animates them under ids from
  `decoration_id_base`, and they count toward the map's extent.
- `GroundMap` carries the heightfield, liquid level, and raw TED passability. Units whose XML
  `Space_Layer` is `Land` or `None` follow the surface on land maps; flyers keep their altitude.
  Passability bits by `MovementClass` (`MovementClassTypeDefs.xml`): 0x1 Clear, 0x2 Steep, 0x20 Water,
  0x80 InfantryOnly, 0x100 Impassable, 0x4 Obstacle with medium confidence; runtime walls and shields
  have unverified bits. Sandbox G draws the types.
- Immobile obstacles on land (structure kinds, and types whose `UnitCollisionClass` names an
  obstacle) carry a `GroundFootprint`: their XML rectangle (`Obstacle_Width`/`_Height` from the
  `Obstacle_X`/`_Y_Offset` corner, else the `Custom_Hard_X`/`_YExtent` half extents around their
  offsets, through `Obstacle_Proxy_Type`; axes and units in compatibility-research.md), else their
  model bounds as a rotated rectangle (an engine choice). The simulation stamps it into its copy of
  the passability with the XML `OverridePassability` type (walls: RebelWall, EmpireWall) or Obstacle,
  and removes it when the entity is destroyed; a footprint moves and turns with a teleport. A building
  spawned at runtime blocks the ground like a placed one. Units use XML `Custom_Soft_Footprint_Radius`
  (infantry 3.5) when present, else their model's extent.
- Spawns and teleports land on free ground (`free_ground`: ground the unit fits and off every other
  ground unit, the nearest point on rings around it, through an occupancy grid built at the tick's
  first spawn).

### Pathfinding

The design is [pathfinding-design.md](pathfinding-design.md) (agreed: units never overlap, allies
give way); research in [pathfinding-research.md](pathfinding-research.md). This is an engine-native
navigation, not a reproduction of the original's.

- `GridPathfinder` (`simulation/pathfinding.*`) runs A* over the TED passability cells
  (8-connected, integer costs, no corner cutting, fully ordered ties, so it is deterministic), then
  drops waypoints a straight line can skip. Connected regions are labeled per footing, so a goal the
  unit cannot reach moves to the nearest cell of its region; a unit standing where it may not be
  first leaves for the nearest enterable cell. Plain A* stays as the reference implementation.
- Clearance (the owner's decision after the stock data showed `UnitCollisionClass` does not measure
  size): each unit plans and moves with its own footprint radius. `ClearanceField` (`clearance.*`)
  holds, per movement class, how far each cell's center lies from ground the class may not enter and
  from the map's edge; a unit fits where the clearance is at least its radius. `Footing` (mask,
  field, radius) is what searches, straight-line checks, and nearest-cell searches take; size
  buckets run every 2 units from 2 to 68 (`footing_buckets`). The world keeps a field per movement
  class its ground units use (`refresh_clearance` at the start barrier; a footprint stamp recomputes
  only the cells it can affect), and a battle builds its spawnable types' fields while the map loads
  (`reserve_clearance`). Movement, walk clips, and pushes never step onto ground a unit does not fit
  (`stands_at`). Route searches add a cost to cells within 5 units beyond the unit's radius of a wall,
  and smoothing keeps that margin where it can.
- Chunks (`ChunkGraph`, `simulation/chunk_graph.*`): the ground in squares of cells, per square the
  connected parts a footing fits and which neighbouring parts touch. The battles' layout is
  `ChunkLayout::refined`: route over the 32-cell parts, then over the 16-cell parts inside that
  corridor, then search cells part by part, each search held to its part (`search_cells`) and ending
  at the crossing into the next part nearest the line to the goal; smoothing straightens the result.
  Over the 76 stock land maps (`path-audit --layout`, 395,200 searches): nodes 0.12 of the plain
  search, the first stretch (`first_stretch_expanded`, the first 2 parts) 0.035, time 0.38, paths
  1.5% longer (21% longer by more than 1%), reach unchanged; least gain on Kuat Kidnapping (0.40).
  A single 16-cell level gives nodes 0.52, time 0.61, paths 0.5% longer; two unrefined levels are
  worse than one on every map.
- Navigation data shared: a ground's region labels and chunk graphs live in `NavigationData`, which
  searches read and `GridPathfinder::prepare` builds ahead (4 ms a graph on Utapau's 97,000 cells);
  building while it is frozen (parallel searches) is a programming error. Navigation's three
  pathfinders (routes, short legs, detours) share one `navigation_data_`; a pathfinder given none
  keeps its own.
- Route requests (orders, chases, replans) wait in a FIFO queue planned up to 15,000 nodes a tick
  (`path_budget_per_tick`, the owner's choice; at least one request a tick); a unit waiting for its
  route holds still, a newer request supersedes. A formation plans one shared search per movement
  class for its largest member, then per member a straight or short (4,096-node) leg; a shared
  request resumes over several ticks when it exceeds the budget. Intermediate waypoints are passed
  once the next one is in straight reach.
- Not yet: planning only the first stretch and refining the rest as the unit advances; rebuilding only
  the chunk squares a footprint change touches (a change drops the graphs and the next search
  rebuilds one in its tick; measured and left for base building); the cell search's window.

### Group movement and avoidance

- **Group moves** (the default move, `SimCommandKind::group_move`, `BattleOrder::move`; attack-moves
  to a point too): members are packed around the ordered point (`place_group`: members nearest the
  point first, each on the free point nearest it on rings of its size, the one nearest the member on
  that ring, `group_slot_gap` 0.25 of a radius apart) and each goes from where it stands to its own
  slot on its own route at the slowest member's speed. Formation spacing applies only to formation
  moves.
- **Formation moves** (`formation_move`, Alt + right-click in the sandbox): `formation_slots` lays
  the units out in ranked rows (lower XML `FormationOrder` in front, pitch from footprint and
  `FormationSpacing`, about twice as wide as deep, centered on the target, slots assigned by where
  units stand across the facing so paths do not cross). On land a formation moves as one body:
  `form_frames` splits the selection by distance and gives each part a `FormationFrame` (hashed): one
  route for the part (`PathRequest::frame`) planned for its largest vehicle's or soldier's footprint,
  a point and facing moving along it at 0.9 of the slowest member's speed, waiting for laggers (at most
  5 s at a stretch; a lagger that comes no closer for 3 s leaves). The formation forms on the move:
  slots blend from where members stood into the formation over the first 150 units
  (`GroupColumns::start_ahead`, `start_aside`). `follow_frames` (behavior, `simulation_frames.cpp`)
  lays the slots out each tick, in files where the ground at the frame or any member is narrower than
  the formation, and sends the members by `follow_slot` (shared with squads; it keeps a detour path
  until the slot moved 40 units). On arrival members take the final slots by where they stand; a
  final slot a member cannot stand on moves to the nearest point it can. Members of a moving frame do
  not give way to each other (`in_moving_frame`).
- Solo units: types with XML `FormationGrouping` Solo (AT-ATs, the Gargantuan, the Juggernaut,
  snowspeeders, the Lancet, the HTT, rancors, wampas) take no slot: they go to the ordered point on
  their own, at their own speed (a reading of the tag name and its users). `WaitsForFormationFormup`
  and `Formation_Formup_Wait_Style` need nothing: the engine has no formup step, and GameConstants
  skips land formup (`ShouldSkipLandFormup`).
- Group cohesion (`GroupColumns`): members of a formation group whose progress stalls for 3 s leave
  it.
- **Squads** (the owner's choices: a team entity, and attacks on a team meaning any of its members):
  a team is an entity whose recipe says so (`EntityRecipe::team`), without a body: no footprint, no
  combat, never hit. A company with a team container (XML `Create_Team_Type`, which carries TEAM,
  SELECTABLE, and UNIT_AI and its own Max_Speed) spawns a team per `Max_Squad_Size` members
  (`build_team_recipe`: the container's speed, turn rate, acceleration, movement class, and
  formation fields; the team at its members' center, spawned before them), and its members join it
  (`EntitySpawn::squad`), not selectable on their own. Members stand in the team's ring
  (`team_shape`, shared with formation slots: by the container's `ContainerArrangement`, Rows a
  near-square grid, Dartboard, the default, a center and rings of 6, 12, ...; each moved by its type's
  `FormationRaggedness` times its slot pitch in a direction from its id, the scale an engine choice).
  Orders to a member go to its team, each team once; a stance set on a team reaches its members
  (`for_squads`). The team plans the route and chases an ordered target until its members' shortest
  weapon reaches it (`pursue_squad_target`); members walk straight to their slots and keep a way they
  planned around a blockage until their slot moves 4 units; soldiers whose slot lies where they
  cannot stand head for their team. The team waits for members more than its extent plus 40 units
  behind, at most 2 s at a stretch, and under a standing order while its members fight (engine
  choices). An attack on a team has each attacker take the nearest living member and ends with the
  team (`CombatState::ordered_squad`). A team leaves with its last member, without a death; destroying
  a team destroys its members; the rings of teams that lose members close up. A squad's team stands
  and plans with its largest soldier's footprint (`GroundMoverColumns::footing_radii`). Not yet:
  `Create_Team` (single units wrapped in a team).
- **Avoidance without overlap** (the owner's rules: no pushing, allies give way, enemies never do;
  the design's 4.4): nothing pushes a ground unit. Navigation picks each moving ground agent's
  velocity by ORCA (`simulation/orca.*`, `simulation_avoidance.cpp`), from the state at the start of
  the tick, and movement never ends a step inside another agent (`keep_apart`). A squad is one agent,
  the circle of its extent; its soldiers hold their places in it. Crushers and what they can crush (a
  living hostile unit squashable by its kind, `can_crush`) ignore each other, so a walker walks over
  standing infantry and infantry may stand at a still walker's feet, as in the original. Last tick's
  velocities are hashed (`MotionColumns::velocities`).
- **Giving way**: a held-up unit asks allies in its way to step aside (`make_way`): the point aside
  is the shortest step out of the passer's corridor in 16 directions, then the unit's own side; it
  must be free, standable, reached on its ground, with no unit closer than contact on the way there
  (`way_clear`). Where one standing ally holds the point, it steps on the same way, up to three allies
  deep (`shift_on`). A moving unit turned aside or slowed asks a standing ally ahead before it is held
  (one going straight at 95% of its speed asks no one). A unit whose ask found no place waits a quarter
  of a second before asking again (`RouteColumns::ask_waits`); each candidate is checked once, cheap
  checks first. A unit that gave way walks back when the way is clear, waiting up to the giving-way
  limit.
- **Detours**: a unit held up for 2 s beside standing units, or held movers, plans around them
  (`plan_detours`; `GridPathfinder::set_obstacles`; its own pathfinder without chunks, at most 16,384
  nodes, at most 4 detours a tick; detours around held movers at most 3 times a route,
  `RouteColumns::mover_detours`) and follows the detour waypoint by waypoint. A unit held up again
  before passing a detour waypoint has reached the closest point and stops; a way closed only by held
  movers does not end the order (a second search around standing units decides). A goal among
  standing units is left to the arrival rules, and moving jams do not detour. A unit that can no
  longer see its next waypoint (avoidance pushed it around a corner) plans a short leg back to it
  (`repair_routes`, at most 8 a tick).
- **Placing destinations and arrival** (the owner's model: the destination fits the unit, the route
  respects its size): a move's point moves to the nearest place the unit fits (`place_destination`,
  a squad by its extent, off standing units but those of the order); a group's block first shifts to
  where all of it fits, then single slots move (`place_block`). A unit stops at a standing unit on its
  destination, or, after a second without progress, at a crowd of standing allies there: allies that
  stopped at the end of the same order or at destinations within six summed radii of its own and two
  footprints' widths beyond contact (`RouteColumns::last_goals`); units giving way count neither as
  standing nor as a crowd. A unit held a second within its radius of its goal has arrived; on its last
  leg the preferred speed reaches the goal within the horizon.
- Known limits: companies do not queue each other at a narrow place; soldiers lagging behind their
  squad are not kept apart from units outside it; the navigation and movement cost of whole armies in
  formation is not profiled. The sandbox's `--auto-move` orders every mobile unit of an owner,
  placed units included, as one group.

### Space movement

- Space ships do not push each other (the original avoids contact). Each moving ship tries a fixed,
  ordered set of courses each tick (`avoid_course_candidates`, 11.25 degrees apart, the side it
  already faces first, else right) against every neighbour's predicted motion (toward its target at
  its travel speed, slowed while it turns), ignoring units at another height (more than both radii
  apart) and much smaller ones, and flies the first that keeps its clearance until it arrives or for
  3 s, else one clear of contact, else the latest contact. `keep_apart` removes the part of a step
  that would take a ship deeper into one it touches, so ships slide along each other and never
  overlap. A destination inside a stopped ship's clearance (or a moving ship's with a lower id) moves
  to the nearest free point of it; formation slots in space are at least 4.5 radii apart
  (`space_formation_spacing`). `test_space_crowds` covers four crowd cases. Known limit: in a ten-ship
  formation move through idle ships one ship may take minutes to reach its slot (a local planner has
  no global route around a crowd).
- Space obstacles: immobile objects take a radius ships steer around: XML `Space_Obstacle_Radius` for
  `SPACE_OBSTACLE` types other than asteroid fields, nebulae, and ion storms
  (`type_space_obstacle_radius`), else `Custom_Soft_Footprint_Radius` (starbases); small craft without
  a soft radius use `Custom_Footprint_Radius`. An immobile object spawned at runtime keeps ships clear
  the same way.
- Units spawned in space take their type's `Layer_Z_Adjust` as height.

### Locomotion

- Speeds: XML `Max_Speed` through the legacy units, times GameConstants' per-mode multipliers
  `Object_Max_Speed_Multiplier_Land`/`_Space` (1.56, 1.2; stated by developer comments); in space the
  multiplier also scales acceleration, deceleration, and turn rates (land: not observed). Projectile
  speeds are unscaled (an open question).
- Acceleration: movers carry a `MovementProfile` (hashed): they accelerate and brake at XML
  `OverrideAcceleration`/`OverrideDeceleration` (also the team containers' `Override_` spelling; read
  as Max_Speed units per 30 Hz frame per frame, a guess; types without them change speed at once) and
  brake ahead of their destination so they stop on it. A Stop order brings a ground unit that brakes
  to a halt over its braking distance straight ahead.
- Turning: units turn at XML `Max_Rate_Of_Turn` (per 30 Hz frame), slowing while facing away. Below
  `Min_Speed_Fraction_For_Turn` of their speed, ground units turn at `Stopped_Rate_Of_Turn` (walkers;
  else their turn rate) and space ships cannot turn; a mover keeps at least that fraction of its speed
  in a turn, so a ship that cannot turn while slow gathers speed straight ahead and slides onto its
  destination only within twice its slowest turn radius. Space ships that name no fraction take 1,
  turning only at full speed (`default_space_turn_speed_fraction`, from recordings). A ship without a
  move order turns in place at its rate divided by the TurnInPlaceSlowdown of its Space_Layer (corvette
  2 recorded; frigate 3, capital 4 from data; `MovementProfile::turn_in_place_rate`).
- Ground units turn in place only when they start from rest: when their way is more than their
  type's `Close_Enough_Angle_For_Move_Start` off (degrees: the AT-AT 30, the AT-ST and rancor 45, the
  dust devil 180), else 45 (`ground_turn_in_place_angle`, an engine choice). On the move they turn
  while walking, slowed by how far they face away, so a corner sharper than 90 degrees still brings
  them to a stop first (from the owner's recording of an AT-AT swinging 90 degrees while walking).
  GameConstants' `CloseEnoughAngleForMoveStart` is 180, against the owner's observation that land units
  do not turn while moving off; the engine keeps the observation, and the conflict is on the roadmap's
  verification list.
- Not yet: braking ahead of a sharp corner (a heavy unit slides a little past it), `Max_Thrust`,
  `Begin_Turn_Towards_Distance`, a secondary locomotor (`Secondary_Locomotor_Name`, the jetpacks), and
  acceleration in the avoidance planner's predictions.
- **Flyers**: types whose `SpaceBehavior` names `FIGHTER_LOCOMOTOR` have a minimum flight speed
  (`Min_Speed` on the `Max_Speed` scale, else half of it) and never stop: they fly along their heading
  at their turn rate toward their target, destination, or the point they circle when idle or arrived,
  and make attack runs (inside a third of their weapon range they hold their heading, pass, and turn
  back outside). Flight is 3D: flyers pitch (at most 60 degrees) toward the behavior layer's point,
  which keeps them at their cruise height (their spawn height), and push apart from overlapping flyers
  of their owner sideways (3D overlap, in-plane push). A goal more than 135 degrees behind and more
  than four turn circles away starts a half-loop (through the vertical, out heading back, unwinding the
  half roll); flyers return to their plane within a few turn circles. Against any flyer (by minimum
  speed, never by name) a flyer dogfights: it steers at the target in 3D, weaving 60 units above and
  below it on an 8 s period with a per-unit phase, within 150 units of its plane, and loops when the
  target is far behind; against anything else it keeps attack runs in its plane. All values are engine
  choices pending measurement.

### Walkers played as clips

Walkers whose start, stop, and turn clips carry their motion are planned in the simulation by those
clips (`simulation_walk.cpp`; the owner's decision). That is every walker with transition clips: XML
`Locomotor_Has_Animation_Priority` (the AT-ATs and Veers', the AT-ST, the Scavenger droid, the two pod
walkers, the Destroyer droid, which has no transition clips and gets only the overlay attack), plus
the 24 stock types with `Walk_Transition` or `Has_Pre_Turn_Anim` (the Rancor, SPMAT, AT-AA, Night
Sister, speeder bike, Juggernaut, Tauntaun, ground beetle): battle's `apply_animation_locomotion`
plans walks for Walk_Transition and turns for Has_Pre_Turn_Anim (or the priority) whenever the clips
exist, reading clip lengths by role, with the clips at their own rate when `Movement_Animation_Speed`
or `Rotation_Animation_Speed` is absent. Evidence (`EAWAssetInspector gait`, `clip-joins`) is in
compatibility-research.md, "Walker clip joins" and "The pod walker's raised leg".

- **Walks** (`MovementProfile::walk_start_seconds`, `walk_stride_seconds`, `walk_stop_seconds`,
  `walk_stop_parts`, from MOVESTART_00, MOVE_00, and the MOVE_END clips at the walk's playback): the
  walker speeds up over MOVESTART, tracks its stride phase by the distance walked (a stride is top
  speed times its MOVE time), decides a stop only where a stride's part begins (as MOVESTART ends, or
  at each 1/n of the stride, n the model's MOVE_END count: the AT-AT's four MOVE_END clips start at the
  quarters of its stride, the AT-ST's three at its thirds, within 0.0007 units), taking the part that
  lands closest to the destination (within half a part, 4.4 units on the AT-AT) and playing variant
  (part mod n), and brakes linearly over one whole MOVE_END clip, straight ahead. Nothing turns or sets
  off during a stop; a stop order or a group's wait walks on to the next part and stops there; waiting
  for a route walks on. A walk shorter than its start and stop at travel speed goes at a lower speed
  with the clips at their rate (an engine choice; the feet slide a little). Walks walk on MOVE_00 and
  settle into IDLE_00 from its first frame (the clips the timing and the joins were measured on).
- **Clip rates**: `Movement_Animation_Speed` is the ground speed at which the move clip plays at its
  own rate (the AT-AT's planted feet move 0.137 units a frame in MOVE against 0.1366; the stormtrooper
  and Bantha agree once their Scale_Factor is counted); walk transitions play at the move clip's rate at
  top speed (the AT-AT 3.7x: MOVESTART in 1.5 s). Turn clips play at the turn rate over
  `Rotation_Animation_Speed` (read by analogy, an inference: the turn clips' planted feet do not show
  the body's turn), both within 0.25x to 6x.
- **A new way**: a new destination while a walk is under way decides the turn once, on its first way
  (`WalkPlan::new_way`, not for a route planned again): more than the type's turn-in-place angle off,
  the walker holds its heading, stops at its stride's next part, and turns in place with its clips; a
  route's corners it turns while walking. Which angle the original uses for types without
  `Close_Enough_Angle_For_Move_Start` (AT-AA, AT-ST, pod walker, Rancor; the engine's 45) is not
  observed.
- **The pod walker's leg**: its walk and turn clips keep its middle leg raised and IDLE keeps it down,
  so it raises the leg (IDLE_LEGLIFT_00) before it sets off or turns from standing and lowers it
  (IDLE_LEGDROP_00) once it stands with nothing to do (`MovementProfile::walk_raise_seconds`,
  `walk_lower_seconds`; clip roles `idle_leg_lift`, `idle_leg_drop`). Its walk joins show a constant
  offset of about 13 units on one foot bone, probably an artifact of that bone, so its stops are
  unverified.
- Snapshots carry the walk clip, the stop's MOVE_END clip, the turn clip, its side, and the progress
  through each (`EntitySnapshot::turn_clip`, `turn_left`, `turn_progress`, interpolated between
  ticks), and presentation plays exactly that, with no transition fades. Presentation keeps only the
  turn loop of units whose turns are not planned (infantry, the Juggernaut, which has loops but no
  BEGIN or END), told by `EntitySnapshot::turns_as_clips`.
- Open: the simulation settles onto its destination in its last ticks, so a stop's END can outlast
  the braking slightly; types with several IDLE clips may leave the stop into an IDLE other than
  IDLE_00; `Rotation_Animation_Speed` for turn clips is an inference.

### Turns composed of cycles

A turn in place played as clips is BEGIN, whole loops, then at most one HALF and one QUARTER cycle,
and END (`MovementProfile::turn_begin_seconds`, `turn_loop_seconds`, `turn_end_seconds`,
`turn_half_seconds`, `turn_quarter_seconds`, per side, from TURNL/TURNR_BEGIN, the loop, _HALF_00,
_QUARTER_00, and END; clip roles `turn_left_half` and the like): the cycles that come closest to the
turn at the stopped rate in quarters of a loop (a coin change). Each cycle turns at its nominal rate
(the loop at the stopped rate, HALF and QUARTER through a half and a quarter of the loop's angle in
their own time: an inference from their names, the foot sweeps agreeing in order); BEGIN winds up to
the first cycle's rate and END down from the last one's, and one scale makes the turn end exactly on
the way (`turn_as_clips`, `turn_point`). Every clip starts from its first frame, where the last one
ended (`clip-joins` showed TURN_END meets the loop only at the loop's end). Traced on Hoth (the
Rancor: TURNR_BEGIN, loop, HALF, QUARTER, END, MOVESTART, MOVE_ENDONE, IDLE_00) and Utapau (the AT-AA
and SPMAT; the pod walker: IDLE, IDLE_LEGLIFT, TURNR_BEGIN, loop, QUARTER, END, MOVESTART, MOVE,
MOVE_ENDTHREE, IDLE_LEGDROP, IDLE_00). A ground unit turning in place for 3 s counts as a group
cohesion stall (to be reworked with group movement).

### Behavior, stances, and standing orders

- Behavior is a layer of its own: each tick, after commands, `run_behavior` turns every unit's order,
  stance, and target into a chase move request or a flyer's steering intent, before navigation,
  locomotion, and combat. Decisions belong here; locomotion and combat never decide where a unit goes.
- `Stance` (aggressive, hold_position, hold_fire; `EntitySpawn::stance`, `set_stance`) decides
  whether a unit picks targets and moves for them; orders always apply. Engine-native; the original's
  stances are not mapped, and the sandbox has no stance control.
- Standing orders (`StandingOrder`: none, attack_move, guard, with a point or a followed entity),
  decided in `run_standing_order`: a hostile scan every target-search interval, staggered by entity
  index; engagement as an ordered attack, a leash, a resume, and the end of the order (an attack-move
  on arrival or when its object is gone, a guard when its object dies); every move, stop, attack, and
  formation move clears it. An attack-mover reacts within its XML `Attack_Move_Response_Range` and a
  guard chases at most its `Guard_Chase_Range` from its anchor (world units, as given); without them
  the engine's tuning applies (`acquire_range_factor` 1.25 of the weapon range, `leash_range_factor`
  2, `minimum_guard_radius` 150, `follow_tolerance` 50; engine choices to check in play).
  `Idle_Chase_Range` waits for idle units that chase attackers (idle units only fire at what is in
  range).
- Body aiming: ground units whose behaviors (`LandBehavior`, `Behavior`) do not name TURRET (stock
  infantry) turn to face their target while standing, and fire only while they stand and face it
  within `body_aim_tolerance` (20 degrees, after the stock `Crouch_Move_Fire_Angle_Cutoff`), pausing a
  burst while they move or turn away.
- A space ship with an attack order and its target in range turns in place toward the target's
  nearest live hardpoint; idle ships keep their heading.
- Scripted conditions: commands `set_invulnerable`, `set_cannot_be_killed`, `suspend_locomotor`,
  `damage` (past shields and armor), and `teleport` (optional heading). All combat damage goes
  through `wound`, which applies invulnerability and the health floor (`unkillable_health_floor` 1,
  or `unkillable_health_fraction` of the health).

### Combat

- Engine-native combat data (`simulation/combat.hpp`): health, shields, armor and shield armor
  types, aim height, any number of weapons each with its range, recharge, pulse bursts, damage type,
  speed, and flight limit; a `DamageTable` of damage-versus-armor multipliers; a symmetric
  `Hostility` over owner slots (map owner slot 3 is provisionally neutral, all others hostile).
  `combat_content` (`src/battle`) translates the XML and `GameConstants.xml` damage modifiers.
  Object and hardpoint health are multiplied by `Object_Max_Health_Multiplier_*` (1.0 land, 1.5
  space; stated by developer comments).
- Each tick after movement: shields refresh in steps, `Shield_Refresh_Rate` once per GameConstants'
  `ShieldRechargeIntervalInSecs` (3 s) on a per-unit phase from the entity id (recorded from the FoC
  debug build); armed entities choose targets, fire bursts, shots fly, shields then armor take damage,
  and the dead are removed at the end of the tick. Losing every shield generator stops shield refresh;
  losing every engine slows a unit to a quarter (provisional).
- **Targets** go through one rule (`choose_target`): a type's `Targeting_Priority_Set` ranks targets
  by category or property (lower first), exclusions apply to unranked targets only, the rest rank
  last; the AI-only `Hard_Point_Priorities` are not read. A self-chosen target is weighed again after
  its XML `Targeting_Stickiness_Time_Threshold` (5 s in 113 of 125 uses) and replaced when the
  priorities prefer another; without the tag it is kept while valid (as recorded: S-02, S-03, pinned
  by a test). A unit's first search waits for a phase from its slot, and a weapon its unit's target is
  out of reach of looks for its own victim on the retarget beat (`retarget_due`). `Is_Valid_Target`
  No keeps an object from being chosen or kept; `Immune_To_Damage` makes it take no damage. Weapons
  skip targets of their excluded categories (a weapon hardpoint's `Fire_Category_Restrictions` plus
  its unit's `Fire_Category_Restrictions` and `Attack_Category_Restrictions`) and, for a unit's own
  weapon, targets inside `Targeting_Min_Attack_Distance` (artillery); target choice passes over
  hostiles no weapon may fire at (`engages`). Entities carry their `CategoryMask` and
  `Property_Flags` bits (from `Data/XML/Enum`).
- **Weapons**: a unit's own weapons from `Projectile_Types`; a shooter without its own
  `Projectile_Fire_*` cadence takes its projectile's (Bossk, MAL, the Disruptor merc); a second
  `Projectile_Types` entry waits for the SWAP_WEAPONS ability. Ship weapons come from `HardPoints`
  (every weapon entry: `Fire_Projectile_Type`, `Fire_Range_Distance`, `Fire_Pulse_Count`/
  `Fire_Pulse_Delay_Seconds`, recharge the midpoint of `Fire_Min/Max_Recharge_Seconds`, hardpoint
  `Damage_Type` overriding the projectile's). Targeting uses the longest range; each weapon fires at
  the entity's target when in its range and arc, else at the nearest hostile within them. The
  shooter's `Projectile_Appearance_Delay_Frames` (27 units; the first of "15,20" forms; frames of 1/30
  s, a guess) holds each burst's first shot for its fire animation.
- **Inaccuracy**: weapons carry their inaccuracy by target category (`Targeting_Fire_Inaccuracy`,
  else `Fire_Inaccuracy_Distance`, then `Targeting_Fire_Inaccuracy_Fixed_Radius`; hardpoints their
  own `Fire_Inaccuracy_Distance`); each shot lands uniformly within that radius of its aim point from
  a seeded stream that is part of the state (`combat_random_seed`); it misses when that lies outside
  the target's footprint and then hits the nearest hostile collidable entity it lands on
  (`Collidable_By_Projectile_Living`). That reading is inference: it fits the data (infantry blasters
  miss air units, turbolasers fighters) but is unverified.
- **Misses on land** (the owner's choice): a shot that misses, or whose target died, flies on past
  its aim point along its line until it meets the surface (the terrain, or the liquid over it:
  `GroundMap::surface_crossing`, sampled every half cell and bisected), its maximum flight, the map's
  edge, or 2,000 units past the aim point (`stray_flight_limit`, an engine choice, so level shots
  end); an instant weapon's miss traces the same line at once. Meeting the surface is a ground
  impact (`ImpactKind::ground`, counted as `ground_hits`; its blast goes off there). Shots flying on
  hit nothing on the way. In space a miss is still lost at its aim point.
- **Muzzles**: a unit's own weapon fires from its model's `MuzzleA_nn` bones, one shot each in number
  order (the T4-B's two barrels, the T2-B's four guns, an X-wing's four), else from a turret's
  `Barrel_Bone_Name`, else from the body at `Ranged_Target_Z_Adjust` (compatibility-research.md,
  "Muzzles"). Infantry and land heroes keep the body: their bind pose spreads the arms (an engine
  choice until shots leave from the animated pose; the Droideka and the Phase III dark trooper use
  their bind-pose muzzles). `MuzzleB_nn` bones serve hardpoints and are not cycled. A weapon hardpoint
  with `Fire_Bone_B` alternates its shots between its two fire bones (an inference from the name), its
  arc measured from bone A.
- **Hardpoints**: every `HardPoints` entry is a part (`HardpointProfile`: kind from `Type`, `Health`
  unless `Is_Destroyable` No, `Is_Targetable`, offset from the model's `Attachment_Bone` in the
  entity's frame, scaled with the unit). Weapon hardpoints fire from `Fire_Bone_A` within
  `Fire_Cone_Width` about that bone's X axis; a destroyed weapon hardpoint silences its weapon. Shots
  aim at the victim's nearest live targetable hardpoint; a shot at a destroyable hardpoint damages
  only the part, the hull follows the parts at min(1, S/T + 0.2) of its health, and the entity dies
  with its last part (`Should_Be_Destroyed_When_All_Hardpoints_Destroyed`; recorded from the FoC debug
  build). `Projectile_Damages_Random_Hard_Points` (the hypervelocity gun) hits a random intact
  hardpoint from the combat stream. The hardpoint list is resolved once for the engine and the tools
  (`type_hardpoints`).
- **Turrets** (the owner's request: no firing backwards or sideways): a type with XML
  `Turret_Bone_Name` (41 types: T2B, AT-ST, AT-AT, turbolaser towers, anti-air turrets, the MPTL)
  carries a `TurretProfile` (`Turret_Rotate_Speed` per frame, scaled like turn rates, a guess, 5
  without it; half of `Turret_Rotate_Extent_Degrees` each way; `Should_Cause_Limited_Turrets_To_Turn`,
  default yes). Its direction relative to the hull (`CombatState::turret_yaw`, hashed) turns toward
  what its own weapon can reach (its burst victim, else the entity's target in range, else the nearest
  hostile in it; `turret_victim`) within the arc, and the weapon starts a burst and fires each pulse
  only while the turret points at the victim within 5 degrees (`turret_aim_tolerance`, an engine
  choice). The turret's weapon fires from its barrel bone turned with the turret; hardpoint weapons
  whose fire bone hangs under the turret bone (the T4-B's missiles) turn with it
  (`WeaponProfile::on_turret`). A target outside a limited arc turns the standing hull toward it,
  unless the type's turret does not turn it (snowspeeder, Lancet, HTT). Hardpoint turrets
  (`Is_Turret`) and `Turret_Rest_Angle` are not turned yet.
- **Projectiles**: shots home at their speed. A shot is lost after its XML `Projectile_Max_Lifetime`
  (14 grenades and slow blobs; `lifetime_ticks`), setting off its blast like the flight limit; a guided
  shot whose target dies takes the nearest hostile within its `Projectile_Max_Scan_Range` (19
  projectiles).
- **Area blasts**: a shot with `Projectile_Blast_Area_Damage` and `_Range` (63 projectiles) damages,
  where it ends (hit, miss, or flight limit), every hostile damageable entity whose footprint comes
  within the range; with `Projectile_Blast_Area_Dropoff` the damage falls off in `_Dropoff_Tiers`
  equal rings (the n-th of N from the center takes (N - n) / N, an inference). The direct victim takes
  the blast besides the shot. Friendly units are spared except by a blast's `Friendly_Damage_Amount`
  (only the remote bomb: 200), which supports blasts sparing friends otherwise;
  `Projectile_Blast_Area_Immune_Faction` is not read. A type's `Death_Projectiles` (four stations and
  a prop) sets off that projectile's blast where it dies, and reports its detonation there
  (`ImpactKind::detonated`): presentation plays the projectile's lifetime detonation particle (else
  its object, else its ground one, as for death explosions naming a projectile) and its
  `Projectile_SFXEvent_Detonate`, besides the type's own `Death_Explosions`.
- **Crushing**: in contact, a moving ground crusher kills each hostile entity it may crush whose
  center lies within its footprint radius (`CrushProfile`; counted in the combat counters). XML
  `Is_Supercrusher` types (both AT-ATs, the Gargantuan, the HTT) crush `Is_Squashable` and
  `Is_Squashable_By_Supercrusher` entities; other types crush only `Is_Squashable` ones when their
  `UnitCollisionClass` is Vehicle, Large Vehicle, or Giant Vehicle. Who crushes infantry and that
  friendly units are spared are engine choices; the kill goes through `wound`. `Squash_Damage_Type`
  is not read.
- **Spinning away**: a type with `Spin_Away_On_Death` (22: fighters, corvettes) spins away with its
  `_Chance` (0.2 or 0.4, on the combat stream) instead of dying: it loses its orders and target, stops
  firing, takes no further damage, and dies after `_Time` (1 or 2 s), playing
  `Spin_Away_On_Death_Explosion` instead of its death effect. Presentation rolls it from the spin's
  start (`ScenePresentation::start_spins`) at one and a half turns a second, the direction by its
  entity id (`spin_roll_rate`, an engine choice), while it flies on out of control.
- **Survivors**: an object with `Destruction_Survivors` ("type, weight" lines: prisons, barracks,
  skiffs) leaves those units where it died, owned as it was: `Battle::step` orders the spawn for the
  next tick as a script order (replays reproduce it unrecorded). Every stock weight is 1.0, so each
  listed type spawns once; the one count range (a prison: 4 to 8) spawns its minimum (engine
  choices). Types added at runtime leave only survivors whose types the battle already has.
- Not yet: garrisons, abilities (SWAP_WEAPONS, ROCKET_ATTACK, the shield abilities), AI behavior,
  death clones as wrecks the simulation knows (`Death_Clone_Is_Obstacle`), and bombs' fall. Beams
  come only with abilities: no stock weapon fires an instant beam (the nine projectiles without a
  speed are ability blasts and two bombs); the beams are the superlasers, laser defense, the energy
  weapon, shield leech, tractor beams, and drain life (`Super_Laser_Ability` and the rest).

## Battles

`eaw_battle` holds battles above the simulation.

- `Battle` owns the `SimulationWorld`. It sets the world up from a `BattleMap` (ground, damage table,
  hostility, the placed entities in order, then one tick) and takes `BattleCommand`s in replayable
  terms (spawn TYPE COUNT OWNER at a point, move, formation move, attack, attack-move, guard, stop,
  set stance, remove, and the scripted conditions; type names, owner slots, points, entity ids),
  validating them, translating them into simulation commands for the next tick, and recording each
  with its tick in a `BattleLog`. `Battle::submit` returns each order's serial, and snapshots carry
  the serial each entity follows (`EntitySnapshot::command_serial`, not hashed).
- `BattleRuntime` (`battle_runtime.*`) owns a battle, its game-object scripts, and the fixed clock;
  its `step` is the battle's tick and then the script phase, timed, and it passes its tick length to
  the script host. The sandbox and the headless runner both step through it.
- Recipes: `TypeRecipe` (`battle/type_recipe.hpp`, `build_type_recipe`) holds what a type gives
  every entity of it, read once per type and model, so its warnings appear once; `make_spawn(recipe,
  instance)` returns the simulation recipe and the instance (`PreparedSpawn`; position, heading,
  owner, look, whether there is a ground, a placed object's transform; `placement_transform` gives a
  runtime spawn the transform a placement would have). A type behaves the same however it appears
  (the owner's choice). `Battle` registers recipes as it builds the map and as spawn orders come.
- Companies and squadrons: land companies (`Company_Units`: GroundCompany, HeroCompany,
  SlaveCompany) expand into their members even when the company names a model of its own (4,665
  members on the stock maps), with their teams ([Group movement](#group-movement-and-avoidance)). A
  repeated `Squadron_Units` continues the list (1,474 squadron members on the stock maps); a squadron
  without `Squadron_Offsets` takes its team container's, the rest lining up astern 40 units apart (an
  engine choice). Type-name lists split on commas and whitespace (one stock FoC list has no commas).
- Spawn placement: spawn orders put ground units on free ground ([Spawning](#spawning-and-the-ground));
  headless battles gather at the open ground nearest the map's middle (Utapau's middle is ground no
  unit may enter).
- What scripts read of a type is one `ScriptTypeInfo` (Lua_Script, the folded CategoryMask names,
  `AI_Combat_Power`) on `SpawnableType` and `MapEntity`, kept by `Battle::type_info`.

### Headless battles, logs, and checks

- `EAWEngine battle` (`battle_runner.*`) builds only the battle side of the map with the types its
  orders name, spawns the `--spawn TYPE[:N][@OWNER]` groups at the center, optionally orders every
  mobile unit there (`--auto-move`), runs `--ticks` ticks (default 6000, 100 s at 60 Hz) without a
  window, and prints the state hash, tick timing per layer, combat, path, and script statistics, and
  frames against the 60 FPS budget (over budget, average, 99th percentile, worst; the spawn tick not
  measured). `--tick-rate <hz>` runs at another rate (a log replays only at the rate it was recorded
  at, which the log does not record yet); `--no-scripts` turns the script phase off.
- Order logs (`battle_log.*`): one order per line with its tick, shortest exact float text that
  reads back bit-identically, an `end` line with the final tick and state hash. `--record` writes them
  from battle and sandbox runs, `--replay` runs one and checks its hash (exit 4 on a mismatch), and
  `--check-determinism` runs twice (the second serially). Replays build every type their log names, so
  a sandbox session with runtime types replays headless with a matching hash. Not yet: logs carry no
  game or mod identity (a replay must use the same data).
- `scripts/check-battle.ps1` runs the reference battle (Utapau, two opposing groups ordered to the
  center, 3000 ticks, 50 s at 60 Hz) and, with `-Extended`, a large Utapau battle (3,300 entities),
  fighters at Kuat, capital ships at Kuat, and AT-ATs on Utapau, each twice; the script holds the
  expected state hashes, which a change that alters them on purpose updates and explains.
- `--stress-orders` (`sandbox/src/battle_stress.*`) gives the spawned units a new order every second
  per group of eight (move, attack-move, attack, guard, to fixed-seed random points or hostile
  targets, staggered), and they cannot be killed, so the load stays constant;
  `scripts/benchmark-battle.ps1` runs it 10 seconds at 10, 50, 100, and 500 units (4,400 entities at
  500) and fails on any frame over 16.7 ms.
- `EAWEngine path-audit` plans seeded start and goal pairs per movement class and size bucket on
  every stock land map (`sandbox/path_audit.*`); `scripts/compare-path-audit.ps1` compares two runs
  pair by pair ([tools.md](tools.md#path-audit)). The plain grid A* baseline: 76 maps, 98,800 pairs,
  82% reached, 1,385 nodes and 0.25 ms a search on average (95th percentile 6,722 and 1.1 ms).

### Performance

Measured in Release on 8 hardware threads (7 workers), late in the current work:

- the 500-unit stress benchmark: about 6 ms a tick, 0 to 3 of 600 frames over budget between runs
  of the same battle (this machine's noise); route searches about 0.16 ms a tick on average,
  budget-limited; giving way 0.7 ms; 100 units about 1.4 ms;
- the large Utapau battle: about 5-6 ms a tick, 2 of 1,798 frames over budget, its worst navigation
  tick about 13 ms (the group's shared search);

Each simulation change's measurements are in [history.md](history.md).

## Presentation

`eaw_presentation` presents a running battle; it depends on battles and never feeds back into them.

### Battle frames

`present_tick` plays a tick's events (the scripts' sound requests, corpses, impact and death
effects, battle sounds), and `build_battle_frame` builds a `BattleFrame` (instances, their entities
and clip profiles, shots drawn as models, the bone palette, animation cues, attached effects) from
the runtime's interpolated snapshots through `BattlePresenters` (the scene presentation and
optionally the unit animator and battle audio, which the sandbox owns). A test builds a frame
without a window. `ScenePresentation` re-poses instances from interpolated snapshots, draws
command-spawned entities by their spawnable visual, highlights a selected team's soldiers, and keeps
the map's own surfaces no entity draws as scenery.

### Unit animation

- Map meshes get a `VisualRig` each (model, inverse bind pose, rest palette range, role clips by the
  stock naming `<MODEL>_<ROLE>_<NN>.ALA`); the scene palette starts as every mesh's rest pose.
  `UnitAnimator` (`presentation/unit_animation.*`) runs each entity's frame in five stages in files
  of their own: state and idles; locomotion by speed with the stride
  (`unit_animation_locomotion.cpp`); aiming and firing (`unit_animation_attack.cpp`); the frame's
  animation cues and walkers' transitions (`unit_animation_transitions.cpp`); the pose key with the
  turret (`unit_animation_pose.cpp`).
- Poses are keyed by mesh, up to three (clip, slot, weight in eighths) layers, sampled at pose
  slots (the clip's frames, at least 30 a second) shared by every entity at the same key and cached
  across frames; units beyond the palette budget stay at rest. New poses are sampled with
  `parallel_for` (4 to a job, `JobPriority::frame`) and copied in request order, so the frame is the
  same with or without jobs (`EAWBenchmarks poses`: 256 new 40-bone poses 4.7 ms serially, 2.3 ms on
  7 workers).
- Locomotion: the snapshot speed is smoothed (0.15 s) and placed on IDLE - WALKMOVE (40% of top
  speed) - MOVE (top speed), or IDLE - MOVE by a quarter of top speed without WALKMOVE, WALKMOVE and
  MOVE on one shared phase played 0.5x to 1.5x by speed. Per type (`AnimationProfile`): move and walk
  clips play at their own rate at XML `Movement_Animation_Speed` and `Walk_Animation_Speed` in
  `Max_Speed`'s units (an inference from the corpus, compatibility-research.md "Clip speeds"); the
  idle clip at `Idle_Anim_00_Rate_Mod` (the huge asteroid masses spin at 8); a type with neither a
  move nor a walk clip keeps its idle while moving when `Allow_Idle_When_Moving` (birds). Whether the
  original bounds the resulting rate is not known. Walkers played as clips show the simulation's clip
  ([Walkers played as clips](#walkers-played-as-clips)).
- Idles: a type with `Loop_Idle_Anim_00` loops IDLE_00; any other unit plays a random IDLE variant
  each time one ends (the owner's recollection); a model with a single IDLE clip plays it again after
  a random 2-4 s pause, holding its first frame (the owner's choice).
- Attacks: snapshots carry each entity's last shot tick; the animator fades ATTACKIDLE in and out
  over 0.2 s while a standing unit has a target, plays ATTACK from its start on every shot (each pulse
  of a burst), and fades back over 0.15 s after it ends. A mesh without ATTACKIDLE shows no aiming
  pose (the T4-B has only ATTACK_00, its gun recoil). XML `Attack_Animation_Is_Overlay` plays ATTACK
  and ATTACKIDLE over the locomotion as additive overlays: each animated bone's change since the
  clip's first frame is added at the layer's weight (`conjugate` in `math/quaternion.hpp`), and bones
  a moving locomotion clip below moves are left alone, so a walking pod walker keeps only its guns'
  recoil and a standing one the whole attack.
- Turret aiming: a turret's bone subtree turns about the model's up axis to the simulation's
  direction (`EntitySnapshot::turret_yaw`), and its `Barrel_Bone_Name` subtree pitches toward the
  target within half of `Turret_Elevate_Extent_Degrees` (presentation only, a guess), back to rest
  without a target. The aim joins the pose key in steps of about 2.8 degrees. The axis tags
  (`Apply_Y_Turret_Rotate_To_Axis`, `Apply_Z_Turret_Rotate_To_Axis`) are not read.
- Flight poses: pitch from the snapshot, a bank from the yaw rate (`flight_bank`) at most the type's
  XML `Bank_Turn_Angle` (fighters 40, capital ships 5), 60 degrees without one (`default_bank_limit`,
  an engine choice, as is the bank per turn rate), and the loop's roll. Capital ships do not bank yet
  (only flyers report a yaw rate); `Max_Rate_Of_Roll` is not read.
- Not yet: directional and ability deaths (FW_DIE, FL_DIE, FIRE_DIE, FTK_DIE wait for the abilities
  and damage types that cause them), flinch and crouch animations, `_QUARTER`/`_HALF` turn clips for
  units whose turns are not planned, and Lua `Play_Animation`.

### Deaths and wrecks

- Snapshots list the entities combat destroyed in their tick (`SimSnapshot::deaths`: id, position,
  heading, pitch, visual, owner, type, death effect, whether crushed); destroy commands are not deaths.
  The sandbox turns each into a corpse (`ScenePresentation::corpse_instance`) that
  `UnitAnimator::add_corpse` keeps (at most 4,096) when the mesh has a DIE clip: the DIE variant its
  normal death clone names (`DeathStyle` from `Death_Clone "Damage_Normal, <clone>"`: the clone's
  `Specific_Death_Anim_Index`, else a variant by id), for the clone's `Death_Persistence_Duration`
  (the AT-ST's 30 s), else holding the last frame 4 s; drawn after the living with an invalid entity
  id (not pickable).
- A unit run over plays its `Damage_Crush` death clone's clip (`Specific_Death_Anim_Type` Crushed or
  DIE; `CRUSHED_nn` clips) when its model has it.
- 184 stock types die into a death clone with a model of its own (every capital ship, station, and
  transport: `EV_STARDESTROYER_D.ALO`, `RV_CORVETTE_D.ALO`, the stations' `_BREAK` models), which
  breaks apart in its DIE clip: the scene build loads it with its clips (`death_clone_mesh`;
  `normal_death_clone` is the one place the clone is chosen) into `DeathStyle::mesh`, and the wreck
  is drawn with it in state 0. A third attached-effects set keyed by the dead unit's id plays wrecks'
  proxy effects (41 on the Star Destroyer's break-apart model, 46 on a station's; land rubble's `_ALT`
  fire), apart from the units' set.
- A type with `Remove_Upon_Death` (281 of 286 uses are true) leaves no corpse. A dead mesh without a
  DIE clip but in a damage state stays as rubble at rest pose for its `Death_Persistence_Duration` or
  for good (an engine choice). When its persistence ends, a wreck plays its `Death_Explosions_End`
  Particle object and `Death_SFXEvent_End_Die` (18 stock types; read as the final explosion's sound,
  an inference). Both come from the death clone or, for the five stock types that leave none (AT-AA,
  the Dark Troopers, the MPTL spotter, the gas droid), from the type itself.
- Not yet: `Death_Fade_Time` (the renderer has no per-instance fade).

### Damage states

On land, an entity draws the `_ALT<n>` state its health fraction reaches in
`Land_Damage_Thresholds`/`_Alternates` (`presentation/damage_states`: the last threshold the fraction
is at or below, so build pads' `1`/`3` show ALT3 always); its `_ALT<n>` proxy effects play only in
their state (placed after the intact ones, so existing effects keep their seeds); entering a state on
the way down plays its `Land_Damage_SFX`; a wreck shows the state for no health. Space types have no
damage states.

Destroyed hardpoints (`HardpointDeathSnapshot`, through the type's `HardpointLook`) change the
entity's bone mask (`UnitAnimator::destroy_hardpoint`): the scorch meshes (its `Damage_Decal` bone and
below, 237 stock hardpoints), hidden while intact, appear; the attachment bone itself collapses (the
attached model is bound to it alone; on the Star Destroyer the scorch and damage-emitter bones hang
under it); the damage effects (proxies under its `Damage_Particles` bone, hidden while intact) start.
Such an entity draws with a palette of its own; its pose key carries the mask's content hash, so
ships damaged alike share.

### Particles and attached effects

- Map builds attach the effects of models' visible proxies; `_ALT1`+ proxies are damage states.
  Emitters with `emit_from_mesh` spawn on the mesh on the proxy bone's parent (the Star Destroyer's
  `pe_stardestroyerengines` glows at the three vertices of its `engines_big` triangle), on every
  vertex, a random vertex, or a random surface point, pushed out along the normal. `AttachedEffects`
  keeps them at their bones from the animated palette (hidden bones stop emission) and stops them when
  the entity is no longer drawn.
- Engine effects (proxies named `pe_*` or under a hardpoint's `Engine_Particles` bone) follow their
  unit's speed: speed smoothed over 0.5 s, size from 50% and opacity from 15% at rest to full at top
  speed (engine choices fitted to the owner's screenshots of the original).
- `sandbox --effect NAME` plays an effect at the map center.

### Projectile visuals

- Shots carry a presentation-only `projectile_visual` key (the CRC-32 of the projectile type name).
  `projectile_visuals.*` reads `Projectile_Custom_Render` 1 or 2, `Projectile_Width`,
  `Projectile_Length`, `Projectile_Texture_Slot` (column,row of the 4x4 `W_LASER_PILL` atlas), and
  `Projectile_Laser_Color`. Render 1 draws as a `beam` particle (the diagonal pill along the flight);
  render 2 as a procedural `teardrop` in the laser color, `Projectile_Length` widths long (inferred
  from a modder's description and the owner's screenshot).
- Model projectiles (no custom render: 62 of the 115 fired stock projectiles) draw their model
  (`Space_Model_Name`, else `Land_Model_Name`, the reverse on land) at the projectile's
  `Scale_Factor`, nose along the flight, never picked, in rest pose; missile trails are the model's
  proxies (`BattleEffects::follow_projectiles`, keyed by shot serial), and projectiles whose model is
  a particle file play it along the flight.
- Flight shapes (presentation only; shots fly straight in the simulation): `projectile_pose` adds a
  lob to grenades (g/2 t (T - t), g = `Projectile_Grenade_Gravity` x `_Lob_Mod` per frame squared,
  scaled like accelerations, a guess) and a sideways sway to rockets (straight for
  `Projectile_Rocket_Straight_Distance`, then up to `Projectile_Rocket_Curve_Offset` in a half sine,
  the side alternating by shot, at most 50 units: one stock 180 is of unknown unit). Not yet: bombs'
  fall, `Projectile_Rocket_Curve_Distance`, sticky grenades.
- `Projectile_Object_Creation_Particle` plays at the muzzle. Headless builds skip all of it.

### Battle effects

`BattleEffects` (`presentation/battle_effects.*`) keeps attached effects, bolts, debris, decals, and
tracks.

- Impacts and deaths: gameplay effects are XML `Particle` objects (`Space_Model_Name`/
  `Land_Model_Name`, `Scale_Factor`). Snapshots list each tick's impacts (position, direction,
  projectile key, the entity struck, `ImpactKind` shield, hull, hull_reduced (armor modifier below 1,
  provisional), expired, or ground); builds resolve the projectiles' `Projectile_Absorbed_By_Shields_Particle`,
  `_Object_Detonation_`, `_Object_Armor_Reduced_Detonation_`, `_Lifetime_Detonation_`, and
  `_Ground_Detonation_Particle` objects, and on land the particles their
  `Projectile_Ground_Detonation_SurfaceFX` names per surface kind. A ground hit's surface is the
  map's liquid (water, lava, ice: `GroundMap::liquid_at`) where the terrain lies below it, else
  default (terrain surface kinds such as dirt or snow are not known); over a liquid it plays that
  surface's particle alone (a splash), on ground the ground detonation and the SurfaceFX's default
  particle when it is another one (`ground_impact_effects`; how the two tags combine is an engine
  choice). The rest: objects and the types' death explosions (the first `Death_Explosions` entry; one naming a
  Projectile plays its detonation particle) and spin-away explosions (`load_effect_objects`); each
  plays once at its point, upright, sized by `Scale_Factor`. Objects that cannot play are reported.
- Damaged smoke: a type's `Damaged_Smoke_Asset_Name` (a particle ALO: the Snowspeeder, the Lancet,
  the Gallofree transport, and the Y-wing, TIE bomber, and Skipray bombing runs) trails from its
  model's origin while it is below half its health (`damaged_smoke_fraction`, an engine choice: the
  tag says only "when damaged"). `ScenePresentation::build` lists the smoking entities it draws and
  `BattleEffects::follow_smoke` keeps one emitter on each, sized by the type's scale
  (`without_uniform_scale`, shared with attached effects); healed, dead, or undrawn, it stops and
  its smoke lives out.
- Shield hits show the projectile's absorb particle; the shield-mesh flash (growing through 1.0, 1.1,
  and 1.25 for 0.1 s each: GameConstants' `Shield_Flash_Scale` and `Shield_Flash_Duration`, read as
  one step per scale, an interpretation, values written in) is kept for the shield abilities
  (SHIELD_FLARE, MISSILE_SHIELD, DEFEND; not implemented yet), the owner's reading of the original.
- Hardpoints: a destroyed hardpoint plays its `Death_Explosion_Particles` object (259 hardpoints) at
  its position, and 169 stock hardpoints break off their `Death_Breakoff_Prop` (a SpaceProp with
  SpaceBehavior DEBRIS whose model is the hardpoint's attached model, with `Debris_Movement_Vector`,
  `Debris_Facing_Rotate_Vector`, a 15-25 s lifetime, a trailing `Debris_Attached_Particle`, and
  `Death_Explosions`): `BattleDebris` draws it at its unit's transform, drifting and tumbling about the
  hardpoint, trailing its fire, exploding when its life ends; never picked, never seen by the
  simulation. The vectors' units are engine choices (world units, and degrees per 30 Hz frame, in the
  unit's frame).
- Scorch marks: an explosion object's `Explosion_Scorch_Mark` resolves through SurfaceFX's default
  surface to its decal; the mark is left where the object plays on land within 40 units of the ground
  (random cell, turn, and size as the decal says; fades 2 s after its `Fadeout_Time` unless permanent;
  at most 2,048 marks; engine choices).
- Footprints: every surface event an animation cue passes (an AT-AT's `AT_AT_FOOTPRINT` at
  `P_FootPuff_*`) leaves its trigger's default-surface decal at the event's bone, its +X along the
  bone's X axis on the ground (the unit's facing the fallback), as a bump decal. Infantry foot plants
  name no decal in the stock data. Cues name the instance that played them
  (`UnitAnimator::bone_position`, `visit_cue_events`).
- Dynamic tracks: each land type whose `SurfaceFX_Name` list names a track gets a layer per tread
  bone (one under the unit without bones); `BattleEffects::lay_tracks` adds a point under the bone
  each segment length, a jump of more than four segments starts a new trail, at most 256 points a
  trail (engine choices); each point pair is a terrain decal with the whole texture over it (a
  reading: repeated once per width, the footprints ran into a bumpy line), faded from the fade begin
  to the fade end and at the fade rate while the unit stands. In the stock data the tracks are bump
  decals (`Footprint_Bump`, 3 units wide; `Track_Bump`, `Track_Bump_Half`), a pixel or two wide at
  overview zoom. Units on platform models lay trails on the hidden terrain beneath.
- The sandbox prints the marks and trails at exit.

### Weather

A land map's active environment names a weather scenario (`WeatherScenarioCatalog`), and `MapWeather`
plays it: the phases' timeline on the world clock, looping, each phase easing into the next over its
ease-out; the scenario's effect (rain, snow, ash, sandstorm) played once per tile of a world-aligned
grid over the ground in view (a tile as wide as the effect's spawn volume, hanging high enough for its
particles to reach the ground; at most 16 x 16 tiles, the rate thinned to stay under 40,000 particles;
tiles leaving the view stop and their particles live out), at the phase's intensity
(`ParticleWorld::set_emission_rate`); lightning as flashes of the ambient and key light. Placement, the
ease-out reading, and the flash are engine choices; the cube size, fade-out distance, and fog-plane
value are unused, their meaning unknown. The weather does not yet reach gameplay (WeatherModifiers.xml)
or sound (WeatherAudio.xml).

## Script host

`eaw_script` (research in [lua-runtime-research.md](lua-runtime-research.md)).

- **Runtime**: Lua 5.0.3 builds as C with only base, string, table, and math. One sandboxed state per
  script: no io, os, debug, or file loaders; `print` to the log; `math.random` and `GameRandom`
  (call, `Get_Float`, `Free_Random`) from a seeded stream; `require` through the VFS (inside a
  thread it runs within its resume); compiled chunks refused (Lua 5.0 does not verify bytecode).
- **Limits** (engine choices; none trips on the stock scripts): every call and resume under an
  instruction budget, coroutines included, and sticky (`pcall`, `xpcall`, and `coroutine.resume`
  re-raise a spent budget or a memory error); 64 MiB per state through `l_realloc`; pattern matching at
  5,000,000 matcher steps (a checked configure-time copy of `lstrlib.c`); `string.rep` results 16 MiB,
  patterns 1,024 characters, script threads 256; script output `max_script_output` lines with a count.
  `newproxy` is gone and engine objects' metatables are locked (`__metatable`), so a script can neither
  forge an engine object nor give one a `__gc`; `object_method` checks its object, kind, and method;
  `collectgarbage` only collects; a script out of memory is stopped for good. Not yet bounded: the stub
  metatables and callbacks a script can create, and small leaks where a Lua error unwinds over C++
  frames.
- **Framework primitives**: the stock framework (`PGBase.lua`, `PGBaseDefinitions.lua`,
  `PGStateMachine.lua`) builds `Sleep`, `ScriptExit`, and `PumpEvents` in Lua on engine primitives the
  host implements: `Create_Thread` (a table with `Kill`, which ends a running thread at its next yield,
  and `Is_Thread_Active`), `GetThreadID`, `_ScriptExit`, `GetCurrentTime`, `ThreadValue` (`Set`,
  `Reset`), and `GetEvent` (`Params`, `Reset`). Threads are coroutines serviced every `ServiceRate`
  seconds; yielding false ends one. The message sinks (`_ScriptMessage`, `_MessagePopup`,
  `_OuputDebug`) write to the script's output; the `Script` object's `tostring` names the script file,
  and `Debug_Should_Issue_Event_Alert` is false.
- **Values and calls**: the engine talks to scripts through `ScriptValue` (nil, boolean, number,
  string, engine object, function, table): `ScriptHost::set_global`, `global` (nesting and size limits,
  keys sorted), and `queue_event`/`queue_callback`, drained by the framework's `PumpEvents` through
  `GetEvent` (argument tables carry `n`, so nil arguments survive `unpack`); script functions passed to
  engine stubs become callbacks, once per function. Engine objects (`ScriptObject`: kind, index,
  generation, point; `ScriptCall` reads arguments from and pushes results onto the Lua stack without
  conversion or allocation; `ScriptCall::truthy`) are userdata with one metatable per kind (method names
  resolved once; bound closures, since scripts call with a dot), interned per kind in a weak table with
  a generation check. Everything not implemented is a recording stub (callable and indexable); names any
  stock script defines are script names, never stubs. The recorder is optional
  (`ScriptHostOptions::recorder`); `run_definitions` is the creation call shared by the survey and
  battles.
- **Scripts in battles** (`BattleScripts`, `battle/battle_scripts.*`): a `BattleScriptView` rebuilt
  after each tick (snapshot, slot table, entities by type, type ids in first-seen order); one provider
  context per script with its own order buffer and a ring of 4,096 blocks; orders merged in script
  order through `Battle::submit(command, BattleOrigin::script)` (not logged); phase timing
  (`BattleScriptStats`). `run` starts the `Lua_Script` of every entity that appears whose type names
  one (`Object` bound to it; `type_lua_script` through the variant chain), ends scripts whose object is
  gone, and reports a script that cannot start once per name. Starting a script costs about 20 ms
  (loading the framework libraries); a script tick about 2.3 microseconds per script on average.
- **Implemented calls**: `Find_Object_Type`, `Find_First_Object`, `Find_All_Objects_Of_Type` (type
  name or object type, optional player), `Find_Hint`, `Find_All_Objects_With_Hint`, `Get_Game_Mode`
  ("Land" or "Space" by whether the battle has ground). Game objects: `Is_Valid`, `Get_ID`, `Get_Type`,
  `Get_Owner`, `Get_Position`, `Get_Hull`, `Get_Shield` (the shield's share), `Get_Distance` (ground
  plane), `Is_Category` (a `|` list matches any), `Move_To`, `Attack_Target`, `Attack_Move`,
  `Guard_Target`, `Stop`, `Despawn` (the `remove` order: the object leaves without dying),
  `Prevent_Opportunity_Fire` (the hold_fire or aggressive stance, a mapping), `Make_Invulnerable`,
  `Set_Cannot_Be_Killed`, `Suspend_Locomotor`, `Take_Damage`, `Teleport_And_Face`, `Teleport`,
  `Play_SFX_Event`, `Event_Object_In_Range` (a callback with a range and an optional real player as
  filter), `Cancel_Event_Object_In_Range`, and `Service_Wrapper` (queues the handler with (self,
  other) for every object in range in the ground plane). Object types: `Get_Name`, `Is_Hero` (a hero
  category, an inference), `Get_Combat_Rating` (`AI_Combat_Power`, an inference). Players: `Get_ID`
  (the owner slot). Blocks: `IsFinished`, `Result` (an attack-move's ends with the order, a guard's
  only when it ends). Stub destinations are dropped with a warning.
- **Survey**: `EAWEngine lua-survey` runs all 370 stock scripts headless (load, `Base_Definitions`,
  `main` for 600 ticks, 10 s of script time; game-object scripts get `Object` bound to a stub) and
  drives AI plans (task force objects and `<Name>_Thread`, task force events with the `Default_`
  fallback), story events through `Story_Event_Trigger`, and callbacks, binding `PlayerObject` and
  `Target` by script kind: about 2,900 events, 167 engine functions and 129 methods called, 317 of
  370 scripts without errors; most remaining errors come from story states fired out of campaign
  order and stubs where numbers are expected. The 3,074-name upper bound is far above what runs.
- Not yet: `Find_Player` and `Spawn_Unit` wait for the player layer (`Spawn_Unit` will return
  provisional objects mapped to the spawned entities at the merge); story, cinematic, and task force
  calls wait for their systems.

## Audio

- `eaw_audio` holds `Sound`, the `AudioDevice` interface (voices with category, volume, pitch, pan,
  loop; categories effects, voice, music, interface), equal-power panning and the distance curve
  (`spatial_audio`, engine choices), and the null device; `eaw_audio_devices` holds XAudio2 (the
  default, a submix voice per category) and FMOD (built only with `EAW_AUDIO_FMOD=ON` and `FMOD_ROOT`;
  written but not built or tested here, without an SDK).
- `EAWAssetInspector sound-audit` resolves and decodes every sample: on stock FoC 3,073 events, 9,539
  of 9,610 samples decoded, 71 missing from the stock data, none undecodable.
- `BattleAudio` (`presentation/battle_audio.*`, presentation only) plays, with the events' chance,
  instance limit (a new one-shot replaces the oldest), overlap groups, sample order, volume and pitch
  ranges, predelays, and chains, voices following their sources and the camera:
  - weapon fire (a hardpoint's `Fire_SFXEvent`, else the type's `SFXEvent_Fire`; one play per shooter
    and sound per tick), deaths and death explosions, shot impacts (`Projectile_SFXEvent_Detonate`, or
    `_Reduced_By_Armor`; lapsed shots play nothing, an engine choice; ground hits play
    `Projectile_Ground_Detonation_SFXEvent` and their SurfaceFX's sound for the surface, a ricochet
    on ground), destroyed hardpoints'
    `Death_Explosion_SFXEvent`, spin-away starts (`Spin_Away_On_Death_SFXEvent_Start_Die`), damage-state
    sounds, and wreck ends;
  - loops on the 16 units nearest the listener: `SFXEvent_Engine_Moving_Loop` while moving (for a type
    with a secondary locomotor only on that locomotor, which the engine lacks, so Boba Fett's and the
    Dark Troopers' jetpack loops are silent; a developer comment marks Boba's loop as the jet pack's),
    `SFXEvent_Engine_Idle_Loop` while still, `SFXEvent_Ambient_Loop` either way, and
    `SFXEvent_Turret_Rotating_Loop` while the turret's direction changed in the last tick;
  - `SFXEvent_Ambient_Moving` every min..max seconds while a unit moves, only for types whose behaviors
    include `AMBIENT_SFX` (a developer comment requires it);
  - `SFXEvent_Health_Low_Warning` and `_Critical_Warning` once as health falls to a third and a tenth
    (the stock GameConstants thresholds, fixed for now), and hardpoint loss reports
    (`SFXEvent_Hardpoint_Destroyed` entries "HARD_POINT_<kind>, event" for the hardpoint's `Type`;
    empty entries silent); both for every owner until the player layer decides whose voice is heard;
  - unit responses to the sandbox's selections and orders (select, move, attack, guard); for an order to
    more than one unit, a member whose type has a group line answers (`SFXEvent_Group_Move`,
    `SFXEvent_Group_Attack`), else the first unit (who speaks is an engine choice); then the first unit
    of another type in the order with an assist line (`SFXEvent_Assist_Move`, `_Assist_Attack`) answers
    0.2 s after the response has finished (`play_assist_response`: the pause restarts while any answer
    to the order plays or waits, or a voice of its own overlap name plays; dropped after 5 s; from the
    owner's account of the original);
  - sounds scripts ask for (`Play_SFX_Event`, through `BattleScripts::sound_requests()`, at the
    object);
  - animation sounds: `UnitAnimator::cues()` reports which frames of which clip a unit played (its
    leading locomotion clip, an ATTACK, a corpse's DIE), and `play_animation_cues` plays the sounds
    those frames pass (footsteps through a SURFACE trigger's `default` surface, walker hydraulics,
    death falls). Only some models have rows (stormtroopers, heroes, walkers, creatures; not rebel
    soldiers).
- The sandbox takes `--audio xaudio2|fmod|none`, `--no-audio`, `--volume` (master, default 0.2, an
  engine choice after the owner found full volume too loud), and `--language`; its sound panel has
  master and per-category volume sliders; it falls back to silence when a backend cannot start and
  reports sounds played, skipped, and missing.
- Not yet: the surface kind under a unit, the particles of surface effects, the stop response
  (`SFXEvent_Stop`, waiting for a stop order in the sandbox), advisor lines (`SFXEvent_Unit_Lost`,
  `_Unit_Under_Attack`, for the player layer), `SFXEvent_Fleet_Move` (presumably galactic, the owner's
  guess), muzzle flashes (hidden `_flash` meshes), loop intros, outros, and fades, priorities and voice
  stealing, `Kills_Previous_Object_SFX`, music and speech (MP3), and a check of the distance curve
  against the original.

## Platform and input

- `src/platform`: the Win32 window, input, crash handling, and log sinks.
- Raw input: the mouse and keyboard are registered for `WM_INPUT`; `InputState::raw_mouse_delta_x/y`
  carry unaccelerated movement (absolute devices such as tablets and remote desktop converted to
  deltas), keys come from raw input with left and right Shift and Ctrl folded, and queued input is read
  in batches with `GetRawInputBuffer` each frame (high polling rate mice). Cursor position, buttons,
  and the wheel stay on window messages, so clicks outside the client area never reach the scene.
  Button and key edges latch within a frame, so a quick click registers both press and release.
  `--no-raw-input` (`WindowDescription::raw_input`) falls back to window messages. A lone Alt release
  does not open the system menu. GameInput (controllers) remains a later option.
- Input load: the sandbox times `poll_events` and counts the raw input records each frame
  (`InputLoad`), shows them in the Statistics panel, and prints the run's average and worst frame.
  Without input the read averages 0.005 ms, the worst frame (window startup) 0.8 ms.
  `scripts/check-input-load.ps1` floods the sandbox with synthetic relative mouse moves (8,000 a second
  by default) and fails above 2 ms for the worst frame's read; it needs the window to hold the focus,
  so it has not run unattended yet.
- Key rebinding (the owner's choices: an action table, a bindings file, and a panel ahead of the
  in-game UI; no controllers yet): `Key` (`platform/keys.hpp`) covers the whole keyboard, each with a
  name (`key_name`, `key_from_name`). `InputBindings` (`platform/input_bindings.hpp`) holds a caller's
  actions with up to two keys each, answers down and pressed, and reads and writes `action = key, key`
  text (a line it cannot read is reported and leaves that action as it was). The sandbox's 22 actions
  (`engine_app/sandbox_actions.hpp`) replace its key reads; Escape, F1, and text-editing keys stay
  fixed. `input_bindings.txt` next to the executable is read at start and written after each change;
  the Controls panel (`--panel controls`) rebinds a slot with the next key pressed (Escape cancels,
  Backspace or Delete clears) and resets to the defaults, the sandbox's keys until the original's
  hotkeys are transcribed from the owner's screenshots.
- Errors: no exceptions anywhere (AGENTS.md, "Error handling"). Callers that continue after a
  recoverable failure log a warning (`log_warning(category, error)`), which reaches stderr, the
  debugger output, and a per-run file in `logs/` next to the executable (`--log-dir`), and fall back
  (the sandbox shows the validation triangle, hot reload keeps the current view, a map skips the
  object). `install_crash_handlers` turns fatal errors, `std::terminate`, and hardware faults into one
  report on stderr, in the log file, and in an error window.

## Sandbox

`EAWEngine sandbox` (options in [tools.md](tools.md)) runs a map or model in a window with a battle.

- Interface: a bar pinned to the top (F1 shows or hides it) with Panels and View menus; panels open
  below the bar and are then freely moved and resized, kept between runs in `sandbox_imgui.ini` next
  to the executable (`D3d12RendererOptions::overlay_settings_file`). Panels (`--panel ...`):
  Statistics (FPS, CPU and GPU frame time from timestamp queries read after the frame's fence, draw
  calls, triangles, instances, video memory against the DXGI budget, texture residency and slots,
  input load, scene, camera, animation, selection, hot-reload status), Spawn, Camera, Graphics, Sound,
  and Controls.
- Graphics panel (`--panel graphics`): v-sync, anti-aliasing, bloom with threshold and strength,
  shadows and their cascade count, fog, water and waves, heat, cloud shadows, sky ambient and
  visibility, silhouettes, soft particles, terrain decals, and the detail-level policy and mesh detail,
  switched while running through `D3d12GraphicsSettings` (`set_graphics_settings` before a frame).
  Shadow maps are created the first time shadows are on. Shadow resolution and particle fidelity stay
  as started.
- Cameras: maps start with the RTS camera; C switches between it, the debug camera, and the free
  flying camera (`--free-camera` places it; `--camera` frames a map view).
- Selection and orders: left-drag box-selects mobile units, a click selects any object (a soldier's
  click or box selects its team once, `entities_in_rectangle`); right-click moves selected units to the
  ground under the cursor, found on the CPU in the same frame (`TerrainHeightField` ray-marched by
  `intersect_terrain`, liquid surfaces included; the z = 0 plane in space), or attacks a hostile under
  it (resolved through a pick against the entity list of the frame that drew it); Ctrl + right-click
  attack-moves, Shift + right-click guards a point or object, Alt + right-click makes a formation move.
  The selection is tinted and outlined by its bounds; selected units' routes are drawn, in 3D through
  the detour in space. Every order goes through `SandboxSession::give_order`.
- Spawning: the spawn panel filters by category (infantry, vehicles, heroes, structures, space units,
  props, other) and faction (XML `Affiliation`), has a search field, lists squads, companies,
  squadrons, and props, marks types not loaded yet, and sets type, count, and owner; N spawns at the
  cursor, Tab/Shift+Tab (Ctrl for ten) cycles types, O cycles the owner. N on an unloaded type builds
  its package on a worker, registers it (`add_meshes`, `ScenePresentation::add_meshes`/
  `add_spawnable`, `UnitAnimator::add_rigs`, `Battle::add_spawnable`), then gives the waiting spawns.
  `--spawn TYPE[:N][@OWNER]` and `--spawn-later` automate it; `--auto-move` waits for the tick after
  its spawn orders.
- Other keys: P pauses the world (no ticks; animation, particles, and texture scroll read a frozen
  presentation clock) while the cameras keep working; B toggles every object's bounds and world axes
  (`--show-bounds`); G draws passability and stamped footprints.
- Replays: `sandbox --replay <file>` takes the log's map (unless `--map`), makes its spawned types
  spawnable, gives each order at its tick, pauses at the log's end, and prints the state hash against
  the recording's; the player's orders are refused meanwhile; with `--capture` it runs 240 ticks a
  frame and captures the end.
- Frame watchdog: a background thread logs a warning (stderr, log file) when one frame step (camera,
  orders, picks, content, simulation and presentation, rendering, events) runs longer than 5 s (60 s on
  WARP or with GPU validation), naming the step and tick, and repeats every 5 s while it lasts.
- The sandbox creates the job system on its frame thread (`sandbox_app.cpp`) and passes it to the
  world and to presentation. Content builds still use `std::async`; a background lane waits for
  content builds split into short jobs (ecs-design.md 7.4).
- `EAWDevLauncher` refreshes its asset and spawn lists a second after the game, installation, or mods
  change; its root field is labelled, and a mod path it cannot convert is logged.

## Tools

The command-line tools and audits are described in [tools.md](tools.md). Implementation notes:

- `EAWAssetInspector`, `EAWDatEditor`, and the Dear ImGui/Direct3D 11 `EAWTools` workbench use the same
  format readers and VFS as the engine.
- `EAWTools` stays on Direct3D 11; its preview is not the production renderer. Its model preview
  selects a damage state and detail level through the engine's rule (`alo_mesh_visibility`,
  `alo_model_variants`), lists the XML types that use the model, and toggles each hardpoint of the
  chosen type (a marker by kind, the `Model_To_Attach` model at its bone, or destroyed: its
  `Damage_Decal` scorch meshes, which `bones_in_subtrees` hides while intact); its map previews draw
  the resolved models.
- Repeated checks are scripts under `scripts/` (`check-battle.ps1`, `check-captures.ps1`,
  `check-input-load.ps1`, `benchmark.ps1`, `benchmark-battle.ps1`, `compare-path-audit.ps1`).
  `check-captures.ps1` includes Mon Calamari 02's water without fog, legacy and enhanced.

## Job system and profiling

- `eaw_jobs` ([ecs-design.md](ecs-design.md), section 7) runs jobs on fibers (`CreateFiberEx`,
  `SwitchToFiber`) over N-1 worker threads plus the creating thread, with counters as the only
  synchronization and no OS mutexes. Jobs go into one bounded lock-free queue per priority
  (simulation, frame, background; Vyukov's ring); `wait` parks the calling fiber on its counter and
  switches the thread to a pool fiber that runs jobs, and the job that brings the counter to zero moves
  the parked fibers to a ready queue (the creating thread's own fiber is pinned to it, for window and
  Direct3D calls). Every switch hands its "after the switch" work (park the previous fiber, or return it
  to the pool) to the fiber switched to, so no fiber is published while it still runs; a counter tracks
  jobs still touching it after their decrement, so it may be destroyed as soon as `wait` returns. Idle
  workers spin briefly, then sleep on an atomic epoch (`std::atomic::wait`); `wake(count)` calls the OS
  only when a worker sleeps and wakes `count` of them, while a ready home fiber (only its own thread may
  resume it) wakes every sleeper. A full queue runs the remaining jobs on the calling fiber; running out
  of fibers or counter waiter slots is a fatal error naming the limit. The build uses `/GT`.
- `parallel_for(jobs, count, grain, function)` cuts chunks from the count and the grain only; it runs a
  single chunk on the caller, otherwise it queues a helper job per worker (at most one per chunk beyond
  the caller's, at most `max_parallel_helpers` 64, on the stack: no allocation per call), and helpers
  and caller take chunks from a shared atomic index.
- Tests (module `jobs`): every job runs once with 0, 1, and 3 workers; a float sum merged in chunk
  order is identical bit for bit with 0, 1, 2, and 7 workers; jobs waiting on their own jobs park and
  resume (also with no worker); a full queue loses nothing; 1,000 Release and 200 Debug runs without a
  failure.
- `EAWBenchmarks` and `scripts/benchmark.ps1` time workloads serially and on the job system
  ([tools.md](tools.md#benchmarks)): float math 6.6 times faster with 7 workers on 8 hardware threads,
  384 A* searches 5.2 times; a job about 43 ns on one thread, 90-110 ns with workers sharing one queue
  (per-worker queues only if a profile asks); `jobs-loops` 67.7 ms, `jobs-tiny` 20.0 ms. The job
  system is not a measurable part of today's battle tick.
- Clients: presentation's pose sampling and the simulation's parallel systems ([Parallel
  systems](#parallel-systems)).
- Profiling: Tracy 0.14.1 (BSD-3-Clause) is pinned by archive hash in `third_party/tracy` and fetched and
  built only with `EAW_PROFILE` (`build.ps1 -Profile`); its client collects on demand and listens on
  localhost only (port 8086). `core/profile.hpp` is the only place that names it (`EAW_PROFILE_ZONE`,
  `_ZONE_TEXT`, `_FRAME`, `_PLOT`, `_FIBER_ENTER`/`_LEAVE`, `_THREAD_NAME`; nothing without the
  option). Marked: the simulation's layers and tick, navigation's route searches, giving way, detours,
  and ground avoidance, every job (the job system names its fibers and enters them on every switch, so
  zones follow a job across threads), the worker threads, and the sandbox frame. The default build
  fetches nothing; the profiling build passes the tests and the reference battle and costs nothing
  measurable without a viewer. Not checked here: a capture with the viewer.

## Engine extensions

None implemented. Explicit asset metadata (declaring the roles legacy mode infers from naming and
placement) is planned in the roadmap's "Engine extensions"; new name-based role rules stay funnelled
to one place per subject so both paths produce the same engine-native categories.
