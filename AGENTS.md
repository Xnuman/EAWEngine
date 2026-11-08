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

### M2: visual sandbox — complete (legacy-mode render backlog carried forward)

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
- selection uses a GPU pick target (`R32_UINT`, scene instance index + 1, terrain reserved) written by opaque, alpha-tested, and terrain surfaces; each request gets an id and copies one texel (up to eight per frame, the rest wait, none are dropped) into the frame's readback buffer; results are read after that frame's fence and returned in request order, so they arrive a frame or two later without a CPU wait, and the sandbox maps each id to its purpose (select, move order, spawn). Platform input latches button and key edges within a frame, so a quick click registers both its press and release. Blended effects and backgrounds are not pickable; the selection is tinted and outlined by its bounds;
- debug lines (`set_debug_lines`, engine helpers in `src/engine/debug_draw.*`) draw depth-tested over the scene from a frame-buffered, persistently mapped vertex buffer with a temporary capacity of 32768 lines per frame; lines beyond it are counted in `stats().dropped_debug_lines`. The sandbox outlines the selection and toggles every object's bounds plus world axes with B (`--show-bounds`);
- the sandbox's on-screen statistics panel (hidden at start; F1 or `--overlay` shows it) is a Dear ImGui overlay drawn by the renderer from engine-supplied text (`set_overlay_text`) and mouse input (`set_overlay_input`); it can be moved, resized, and minimized with its "-" button, and while it wants the mouse (`overlay_wants_mouse`) the scene ignores clicks, drags, and the wheel. It shows FPS, CPU and GPU frame time (timestamp queries read after the frame's fence), draw calls, triangles, instances, video memory against the DXGI budget, texture residency and slots, scene, camera, animation, selection, and hot-reload status. Exception to the no-CPU-wait rule: the ImGui DX12 backend waits on the CPU for its font-texture uploads, which happen only when the font atlas changes, never per frame;
- alpha ordering is coarse and material translation is conservative;
- shaders are newly written HLSL under `src/render/shaders` (D3D12, DXC, shader model 6.0, root signature declared in the HLSL) and `src/tools_gui/shaders` (D3D11 tools, FXC, shader model 5.0), compiled at build time into embedded bytecode; there is no runtime shader compilation. Shader reflection metadata is not generated yet, and the pipeline-state cache is opt-in (`--pipeline-cache <dir>`);
- `sandbox --model` lists every `<MODEL>_*.ALA` and Left/Right step through the bind pose and them; models show the intact `_ALT0` damage state and the highest `_LOD<n>` detail level. Model scenes animate bone visibility for rigid attachments through a second, visibility-collapsed palette half (meshes on bones hidden in the bind pose appear when an animation shows the bone); skinned-mesh visibility, damage-state and LOD switching, and gameplay-driven visibility are not modeled; map meshes carry the same visibility half and animate (see M3 item 11);
- the sandbox builds a map or model on a worker thread while the window renders; `load_scene` may replace the scene at any time. Geometry and materials upload at once, while textures are validated up front and stream in under a per-frame byte budget (`--upload-budget`), drawn with role-appropriate fallbacks through a frame-buffered logical-texture residency table. Textures live in a generation-checked handle pool, take texture-table slots from a fence-deferred allocator, and replaced buffers, textures, and bone palettes retire on the direct-queue timeline (`gpu_resource_lifetime.hpp`; `sandbox --reload-every` exercises it). Scene builders read through the core `AssetReader` interface and an engine `AssetCache` (`src/engine/asset_cache.*`) of decoded models, textures, maps, and the XML object catalog; every cached asset records fingerprints of the files it consulted (including failed lookups and listings), and `sandbox --hot-reload` polls them and the MEG archives once a second on a worker, remounts rebuilt archives between builds, and rebuilds the scene from the cache. Decoded textures carry an identity (a fresh serial per decode), and `load_scene` keeps every resident texture whose identity the replacement scene names again, so a reload uploads only changed textures; geometry, instance, and material buffers are still per scene, the renderer holds one scene at a time, and MEG archives added to or removed from a layer need a restart;
- the descriptor and bone capacities are temporary implementation limits, not asset-format limits;
- land terrain renders as a textured heightfield (per-vertex layer index, three-layer blending, layer tiling, normal maps; layers without a color texture are not drawn, an inference from Cloud City and Kamino); water, lava, and ice surfaces render at the map's liquid level with their textures and highlights, resembling the original's low-end fallback because reflection and refraction are not modeled (tiling and scroll are engine choices); tracks render as unlit, alpha-blended ribbons draped on the terrain with start/middle/end opacity and texture scroll; vertex colors reach materials whose legacy profile reads them. Decals (one stock map, no established texture link), fog of war, clouds, and the unexplained terrain layer and track header fields (two angles, two colors, flags) are not rendered or interpreted yet. Land teams/companies are not expanded (their formations are runtime behavior), and transparent draws are not depth-sorted;
- world handedness is unverified: the renderer uses a left-handed look-at over Z-up data, so mirrored text or decals on models would indicate a flipped axis; background parallax and sky-dome scaling are inferred, not measured;
- Alamo models face -Y (muzzle and fire bones, docs/compatibility-research.md): an entity heading is the direction of its model's -Y axis (`model_forward_heading`, `identity_model_heading`);
- `EAWTools` remains on Direct3D 11 and its preview renderer is not the production renderer;
- a complete renderer-agnostic RHI has not yet been extracted;
- simulation, gameplay Lua execution, audio, runtime UI, networking, and save compatibility are future milestones.

## Immediate roadmap

Work in dependency order unless the user explicitly reprioritizes it.

M2 is complete: a stock space unit and land and space maps load and can be inspected through the D3D12 renderer with legacy-mode materials, environment lighting, terrain, liquids, tracks, streaming, hot reload, selection, debug drawing, and the statistics overlay. Its remaining render items form the legacy-mode render backlog below; they are scheduled when gameplay or evidence needs them, not as an M2 gate.

### M3: tactical simulation slice — active

The first goal is a thin vertical slice: units on a map that can be selected and moved, proving that simulation and rendering stay separate, the tick is deterministic, and commands flow in while snapshots flow out.

1. ~~Simulation core (`src/simulation`, no graphics): stable generation-checked entity ids, structure-of-arrays component storage, a fixed-step scheduler, deterministic command application at tick boundaries, and immutable snapshots for presentation.~~ Done.
2. ~~Spawning from XML: placed TED objects become entities built from their XML object types (owner, position, heading, `Max_Speed` with a provisional ×30 scale) instead of a static scene.~~ Done; reinforcements come later.
3. ~~Renderer object API: the loaded scene's meshes form a mesh library and `set_instances` submits each frame's instances (any count, added or removed freely) through a growable frame-buffered buffer; `ScenePresentation` re-poses instances from interpolated snapshots.~~ Done; animated units on maps are item 11.
4. ~~First interaction: the legacy RTS camera (fixed pitch and heading, zoom, keyboard pan), click and box selection, and right-click move with straight-line movement on the fixed tick.~~ Done: maps start in RTS mode (C toggles the free debug camera); left-drag box-selects mobile units, a click selects any object, and right-click orders selected units to the ground under the cursor, found on the CPU in the same frame (`TerrainHeightField` built from the map, ray-marched by `intersect_terrain`, liquid surfaces included; the z = 0 plane in space), spread on a grid. GPU picks are only used to identify clicked objects, and completed picks are read from any frame context whose fence has finished. Flyers move in a straight 3D line with instant turning (ground units: items 5 and 6); there is no collision or formation yet, and the camera's fixed pitch and heading are provisional. `--spawn <TYPE>[:N]` adds XML types' models to the map's mesh library so the sandbox can spawn them (N at the cursor, Tab cycles); runtime spawning is a deterministic `spawn` simulation command (through `Battle`, see item 10), and types not requested at build time cannot be spawned until meshes can be registered at runtime.
5. ~~Terrain following and passability data.~~ Done provisionally: `GroundMap` (`src/simulation`) carries the heightfield, liquid level, and raw TED passability; units whose XML `Space_Layer` is `Land` or `None` follow the surface on land maps and stop before cells whose passability type their XML `MovementClass` may not enter (`MovementClassTypeDefs.xml`; 0x1 Clear, 0x2 Steep, 0x20 Water, 0x80 InfantryOnly, 0x100 Impassable, 0x4 Obstacle with medium confidence; runtime walls and shields have unverified bits); flyers keep their altitude. Sandbox G draws the types.
6. ~~Pathfinding.~~ Done as a first engine-native slice, not a reproduction of the original's navigation: `GridPathfinder` (`src/simulation/pathfinding.*`) runs A* over the TED passability cells for each ground unit's movement-class mask when its move command is applied at the tick boundary (8-connected, integer costs, no corner cutting, fully ordered ties, so it is deterministic), then drops waypoints a straight line can skip. Connected regions are labeled once per mask, so a goal the class cannot reach moves at once to the nearest cell of the unit's region; a unit standing where it may not be first leaves for the nearest enterable cell. Units follow the waypoints at full speed through corners; the sandbox draws selected units' routes and shows path counts, expanded nodes, and tick cost. Searches run synchronously in the tick (about 1 ms per cross-map path in Release; a 15-unit cross-map order on Alderaan costs about 18 ms), so a per-tick search budget or the job system must take over before large armies. Not yet: unit size and clearance, collision and separation, formations, turn rates, and dynamic obstacles (walls, shields, units). The staged plan and its sources are in [docs/pathfinding-research.md](docs/pathfinding-research.md); A* stays as the reference implementation.
7. ~~First combat slice.~~ Done: the simulation carries engine-native combat data (`src/simulation/include/eaw/combat.hpp`: health, shields with regeneration, armor and shield armor types, aim height, one projectile weapon with range, recharge, pulse bursts, damage type, speed, and flight limit; a `DamageTable` of damage-versus-armor multipliers; a symmetric `Hostility` relation over owner slots) and a deterministic uniform-grid broad phase (`SpatialGrid`). Each tick after movement, shields recharge, armed entities target the nearest damageable hostile in range (ties to the lower id, searched every 10 ticks) or chase an attack-command target, fire bursts, shots home at their speed, shields then armor take damage, and the dead are removed back to front. `combat_content.*` (`src/engine`) translates the XML fields and `GameConstants.xml` damage modifiers; map owner slot 3 is provisionally neutral and all other slots hostile. The sandbox right-click attacks a hostile under the cursor (resolved through a pick against the entity list of the frame that drew it), O cycles the debug-spawn owner, `--spawn TYPE[:N][@OWNER]` places opposing groups, and projectiles and health/shield bars are drawn as debug lines. Not yet: hardpoints (space weapons), several weapons per unit, inaccuracy, turret target priorities, splash damage, death effects and damage states, garrisons, and ability or AI behavior.
8. Group movement, in steps (sources and plan in [docs/pathfinding-research.md](docs/pathfinding-research.md)). Step 1 is done: mobile units have a separation footprint (from model bounds) and an XML turn rate (`Max_Rate_Of_Turn`, provisionally degrees per 30 Hz frame); after movement, overlapping ground units are pushed apart deterministically (summed per pair, then applied; immobile entities never move, idle units yield to moving ones, nobody is pushed onto ground they may not enter), units pushed off their route replan, and units turn at their rate, slowing while facing away. Step 2 is done: `formation_move` commands (the sandbox right-click) lay the selected units out in ranked rows (`formation.hpp`: lower XML `FormationOrder` in front, pitch from footprint and `FormationSpacing`, about twice as wide as deep, centered on the target, slots assigned by where units stand across the facing so paths do not cross) and move them at the slowest member's speed until each arrives. Step 3 is done: land companies (`Company_Units`, GroundCompany/HeroCompany/SlaveCompany) expand into their members even when the company names a model of its own (most stock companies do; 4,665 members across the stock maps), placed at the company's origin and flagged; the map builder arranges each company with the same formation, facing its placed heading, and `--spawn` accepts company types (spawned as their members). Type-name lists split on commas and whitespace (one stock FoC list has no commas). Group moves keep together and queue: members more than a slack ahead of their group's furthest member wait (a member without progress for 3 s is released), and between two moving units of one owner the one with less route left yields much less (route priority, as in Age of Empires II), so a group files through a one-unit-wide passage instead of jamming; only units of one owner make way for each other, so idle enemies and neutrals stand their ground. Immobile obstacles on land maps (structure kinds, and types whose `UnitCollisionClass` names an obstacle) carry a `GroundFootprint` (their model bounds as a rotated rectangle, an engine choice until collision meshes are read) that the simulation stamps into its copy of the ground's passability with their XML `OverridePassability` type (walls: RebelWall, EmpireWall, so classes allowed on walls still cross them) or Obstacle, and removes when the entity is destroyed (cached region labels are dropped); the G overlay shows the stamped grid. Units use XML `Custom_Soft_Footprint_Radius` (infantry 3.5) when present. Route planning is budgeted and shared: path requests (orders, chases, replans) wait in a FIFO queue planned up to 30,000 nodes per tick (at least one request; a unit waiting for its route holds still, a newer request supersedes), and a formation move plans one shared search per movement class from the member nearest the group's centroid, then per member a straight or short (4,096-node) leg onto it and off to its slot, straightened against the ground; a shared request resumes over several ticks when its members exceed the budget. Intermediate waypoints are passed once the next one is in straight reach (a crowd kept units from touching a shared corner waypoint). Ordering every unit on Utapau now costs at most about 12 ms in one Release tick (40 ms before).
9. Space combat, step 1 done: `CombatProfile` holds any number of weapons, each with its own recharge and bursts, firing at the entity's target when within that weapon's range (targeting uses the longest range). `combat_content` reads ship weapons from `HardPoints` (every entry whose `Type` is a weapon: `Fire_Projectile_Type`, `Fire_Range_Distance`, `Fire_Pulse_Count`/`Fire_Pulse_Delay_Seconds`, recharge = midpoint of `Fire_Min/Max_Recharge_Seconds` until a seeded random stream exists, hardpoint `Damage_Type` overriding the projectile's) besides `Projectile_Types`. Separation runs in the plane on space maps. A repeated `Squadron_Units` continues the list (stock Rebel_X-Wing_Squadron: 2 + 2 + 1 X-wings beside five offsets; 1,474 squadron members on the stock maps, 1,026 before), and squadrons spawn like companies (`--spawn Rebel_X-Wing_Squadron`). Step 2 done: every `HardPoints` entry is a part (`HardpointProfile`: kind from `Type`, `Health` unless `Is_Destroyable` No, `Is_Targetable`, offset from the model's `Attachment_Bone` in the entity's forward/left/up frame); weapon hardpoints fire from `Fire_Bone_A` within `Fire_Cone_Width` centered on that bone's X axis, each weapon starting bursts at the entity's target when in its range and arc or else at the nearest hostile within them; shots aim at the victim's nearest live targetable hardpoint and damage it and the hull (provisional split); losing every shield generator stops shield recharge and losing every engine slows to a quarter (provisional); a destroyed weapon hardpoint silences its weapon. Fighter movement done: types whose `SpaceBehavior` names `FIGHTER_LOCOMOTOR` get a minimum flight speed (`Min_Speed` on the `Max_Speed` scale, else half of it) and never stop: they fly along their heading at their turn rate toward their target, destination, or the point they circle when idle or arrived, and make attack runs (inside a third of their weapon range of the target they hold their heading, pass, and turn back outside); flyers do not separate and steer at an attack target themselves instead of chasing it by move orders. Behavior is a layer of its own: each tick, after commands, `run_behavior` turns every unit's order, stance, and target into a chase move request or a flyer's steering intent, before navigation, locomotion, and combat; a `Stance` (aggressive, hold_position, hold_fire; `EntitySpawn::stance`, `set_stance` command) decides whether a unit picks targets and moves for them, while orders always apply. Engine-native; the original's stances are not mapped yet, and the sandbox has no stance control yet. Flight is 3D: flyers pitch (at their turn rate, at most 60 degrees) toward the behavior layer's point, which keeps them at their cruise height (their spawn height) for now, and separate from flyers of their owner sideways (3D overlap, in-plane push); snapshots carry `pitch` and `yaw_rate`, and `ScenePresentation` poses pitch and a bank from the yaw rate (`flight_bank`). A goal more than 135 degrees behind and more than four turn circles away starts a half-loop (behavior asks, the locomotor pitches through the vertical, comes out heading back, and unwinds the half roll, carried as snapshot `roll`); flyers return to their plane within a few turn circles. Against a flyer of any class (fighters, bombers, mod units with the fighter locomotor, by minimum speed, never by name) a flyer dogfights: it steers at the target in 3D, weaving 60 units above and below it on an 8 s period with a per-unit phase, within 150 units of its plane, and loops when the target is far behind; against anything else it keeps attack runs in its plane. All values are engine choices pending measurement. Next: the headless battle command.
10. Headless battle, in steps. Step 1 is done: `Battle` (`src/engine/battle.*`) owns the `SimulationWorld` of the sandbox and of headless runs. It sets the world up from a `BattleMap` (ground, damage table, hostility, then the placed entities in order, then one tick) and takes `BattleCommand`s in replayable terms (spawn TYPE COUNT OWNER at a point, move as a formation, attack, stop, set stance; type names, owner slots, points, entity ids), validating them, translating them into simulation commands for the next tick, and recording each with its tick in a `BattleLog`. Spawning is the simulation's `spawn` command (`SimCommand::spawns`, rejected spawns counted), and command-spawned entities carry `spawnable_visual(type index)` as visual key, by which `ScenePresentation` draws them; the world names entities' types (`SimulationWorld::type_name`), and the state hash includes them. Step 2 is done: `EAWEngine battle` (`battle_runner.*`) builds the map with the types its orders name, spawns the `--spawn` groups at the map center, optionally orders every mobile unit there (`--auto-move`), runs `--ticks` total ticks (default 3000) without a window, and prints the state hash, tick timing, and combat and path statistics; `battle_log.*` writes and reads the order log as text (one order per line with its tick, shortest exact float text that reads back bit-identically, an `end` line with the final tick and state hash), `--record` writes it from battle and sandbox runs, `--replay` runs a log and checks its hash (exit 4 on a mismatch), and `--check-determinism` runs twice. Debug and Release builds reach the same state hash on Kuat and Utapau test battles (Release: about 0.05 ms per tick on average, the first auto-move tick on Utapau about 10 ms). The sandbox's `--auto-move` waits for the tick after its spawn orders. Headless builds skip textures and animation clips (`MapBuildOptions`; Utapau builds in 0.7 s instead of 2.3), and nothing the simulation reads depends on them: the ground's liquid level comes from map data (a liquid type at or above the lowest ground) instead of from the drawn water surface, the headless center is the middle of the ground (or of the placed objects in space), and `state_hash` leaves out presentation keys (`EntitySpawn::visual`, a mesh index that shifted when the water mesh was missing) and derived outputs (snapshot speed); a sandbox session (full build) replays headless with a matching hash. Hash values changed once with this (logs recorded before do not verify). Not yet: logs carry no game or mod identity (a replay must use the same data).
11. ~~Unit animation on maps.~~ Done as a first slice: map meshes are built with the visibility half of the palette and get a `VisualRig` each (model, inverse bind pose, rest palette range, role clips found by the stock naming `<MODEL>_<ROLE>_<NN>.ALA`: IDLE, MOVE, WALKMOVE, ATTACK, ATTACKIDLE; `parse_clip_name`), and the scene palette starts as every mesh's rest pose. `UnitAnimator` (`src/engine/unit_animation.*`, presentation only) picks each drawn entity's role from its snapshot (moving: MOVE, else WALKMOVE, else IDLE; a target while standing: ATTACK, else ATTACKIDLE, else IDLE; otherwise IDLE), its variant and loop phase from its id, restarts the clip when the role changes, and samples poses at pose slots (the clip's frames, at least 30 per second) shared by every entity at the same mesh, clip, and slot, cached across frames; units beyond the palette budget stay at rest. The renderer's bone palette is now set per frame (`set_bone_transforms` before `set_instances`, any size up to `max_bone_transforms()`, 262,144 matrices; frame buffers grow on demand). Simulation state hashes are unchanged. Blending (an engine choice for the engine's own look; the original's blending is not documented): `AlaAnimation::sample_local_pose`, `ala_blend_pose` (normalized-lerp rotations, linear translation and scale), and `ala_compose_pose` blend in bone space (`alo_decompose_transform` splits bind transforms); snapshots carry each entity's `speed` in the last tick (derived, not hashed); `UnitAnimator` smooths that speed (0.15 s) and places it on IDLE - WALKMOVE (40% of top speed) - MOVE (top speed), or IDLE - MOVE by a quarter of top speed without WALKMOVE (a mesh without IDLE blends from its rest pose), with WALKMOVE and MOVE on one shared phase played 0.5x to 1.5x by speed, and fades ATTACK/ATTACKIDLE in and out over 0.2 s; poses are keyed by mesh, up to three (clip, slot, weight in eighths) layers, so steady units still share. Attacks follow shots: snapshots carry each entity's `last_shot_tick` (derived, not hashed), and the animator aims with ATTACKIDLE while a standing unit has a target, plays ATTACK from its start on every shot, and fades back over 0.15 s after it ends (a turreted unit firing on the move plays it over its locomotion; a mesh without ATTACKIDLE loops ATTACK, one without ATTACK only aims). Body aiming (simulation): ground units whose behavior lists (`LandBehavior`, `Behavior`) do not name TURRET (stock infantry) carry `EntitySpawn::aims_with_body`; standing, behavior turns them to face their target (the locomotor turns in place toward the intent's point), and combat fires their weapons only while they stand and face the target within `body_aim_tolerance` (20 degrees, after the stock `Crouch_Move_Fire_Angle_Cutoff`), pausing a burst while they move or turn away; turreted units (T2-B, AT-ST) fire on the move as before, without drawn turret rotation yet. Deaths: snapshots list the entities combat destroyed in their tick (`SimSnapshot::deaths`: id, position, heading, pitch, visual, owner; derived, not hashed; destroy commands are not deaths); the sandbox turns each into a corpse (`ScenePresentation::corpse_instance`, which also forgets the dead entity's template) that `UnitAnimator::add_corpse` keeps when the mesh has a DIE clip: it plays a variant once, holds the last frame 4 s, and is drawn after the living with an invalid entity id (not pickable); at most 4,096 corpses. Not yet: directional deaths (FW_DIE, FL_DIE), turn, flinch, and crouch animations, death effects (explosions, debris, damage states), firing synchronized with shots, acceleration in the simulation (speed jumps, so only the 0.15 s smoothing ramps the blend), XML animation overrides, and Lua `Play_Animation`.
12. Then, in order (details in [docs/roadmap.md](docs/roadmap.md)): runtime mesh registration, and the remaining M3 systems (job system when budgets need it, fog of war, a minimal Lua host surface, a local-player concept).

### Cross-cutting engineering work (planned)

- The sandbox has a frame watchdog: a background thread logs a warning (stderr, log file) when one frame step (camera, orders, picks, content, a simulation tick, presentation, unit animation, rendering, events) runs longer than 5 s (60 s on WARP or with GPU validation), naming the step and the simulation tick, and repeats every 5 s while it lasts, so a frozen window leaves a diagnosis.
- Raw input is done: `src/platform` registers the mouse and keyboard for `WM_INPUT`. `InputState::raw_mouse_delta_x/y` carry unaccelerated device movement (absolute devices such as tablets and remote desktop are converted to deltas), and keys come from raw input with left/right Shift and Ctrl folded. Cursor position, buttons, and wheel stay on window messages so clicks outside the client area never reach the scene. Queued raw input is read in batches with `GetRawInputBuffer` each frame (high polling rate mice), and the overlay shows raw events per frame. `--no-raw-input` (`WindowDescription::raw_input`) falls back to window messages. GameInput (controllers) remains a later option.
- No exceptions: every target compiles with exceptions disabled (`/EHs-c-`, `_HAS_EXCEPTIONS=0`), and `src`/`tests` contain no `throw`, `try`, or `catch`. Recoverable failures (malformed game or mod data, missing assets, a map or model that cannot be built, invalid command lines or user input, a window or GPU device that cannot be created at startup) return `eaw::Status` or `eaw::Result<T>` (`eaw/status.hpp`) with a contextual `eaw::Error` message; propagate with `EAW_TRY` / `EAW_TRY_ASSIGN`. Callers that continue report the failure as a warning through the log (`eaw/log.hpp`: `log_warning(category, error)`), which reaches stderr, the debugger output window, and a per-run file in `logs/` next to the executable (`--log-dir`), and fall back (the sandbox shows the validation triangle, hot reload keeps the current view, a map skips the object). Programming errors and violated API preconditions are `EAW_ASSERT_MSG`; unrecoverable runtime failures (a failed D3D12 call after startup, device removal, failed fence waits) are `eaw::fatal_error` with their diagnostics; allocation failure is fatal. `install_crash_handlers` turns these, `std::terminate`, and hardware faults into one report on stderr, in the log file, and in an error window. Use non-throwing standard-library forms for data and user input (`std::from_chars`, `std::error_code` filesystem overloads, checked indexing).

### Legacy-mode render backlog

- Cube reflections (no stock material uses them), planet atmosphere/clouds/city lights, shield distortion layers, nebula wobble, wind bend, heat distortion, occluded-unit silhouettes, shadows, and fog.
- Effects: particles (particle ALOs are skipped today, so engine glows, explosions, smoke, and muzzle flashes are missing), trails attached to moving objects (engine and projectile trails; TED ground tracks already render), runtime decals (scorch marks) besides the unlinked map decals, and real projectile visuals (shots are debug-line streaks).
- Decals (no texture link established), depth-sorted transparency, and land teams/companies (expanded by runtime formations in M3).
- Damage-state (`_ALT<n>`) and LOD switching, skinned-mesh visibility, gameplay-driven visibility.
- Per-asset geometry handles and several resident scenes (covered by the M3 object API), and extracting a renderer-agnostic RHI.
- Verifying world handedness against the original.
- The PBS material model and its documented conversion from legacy profiles once legacy mode is stable.

Later milestones remain:

- **M4:** complete land/space skirmish systems, pathfinding, reinforcements, abilities, tactical AI, UI, audio, and deterministic replays.
- **M5:** galactic mode, campaigns, economy, heroes, story events, campaign AI, persistence, and versioned saves.
- **M6:** broad XML/Lua compatibility, Steam mod discovery, multiplayer/replays, authoring tools, profiling, a documented namespaced mechanics SDK, and modern model/animation formats (FBX, glTF 2.0; blending, retargeting, blend shapes) as optional engine-native assets beside ALO/ALA.

The authoritative milestone description is [docs/roadmap.md](docs/roadmap.md). Update it and this summary together when scope or status changes.

## Repository map and dependency direction

- `src/core`: VFS, formats, compatibility data, and reusable asset logic. It must not depend on graphics or Win32 presentation code.
- `src/platform`: operating-system window, input, clock, and related platform services.
- `src/render`: rendering interfaces, visual-scene data, and graphics backends. It must consume engine-native visual data, not parse MEG/ALO/ALA/TED files or know VFS precedence. The D3D12 backend keeps its public interface in `d3d12_renderer.hpp`; the implementation class lives in `d3d12_renderer_impl.hpp` with its member functions defined by subject in `d3d12_renderer_device.cpp` (startup, swap chain, buffers, waits, retirement), `_pipelines.cpp` (pipeline states and cache), `_resources.cpp` (uploads, scene loading, texture streaming), `_frame.cpp` (per-frame state and `render()`), and `_overlay.cpp`; `d3d12_support.*` holds the HRESULT, queue, handle, and description helpers.
- `src/simulation`: the deterministic tactical simulation (entity ids, structure-of-arrays components, fixed-step scheduling, commands, snapshots). It must not depend on graphics, platform, or file access. `SimulationWorld` is split by tick layer ([docs/architecture.md](docs/architecture.md#simulation-tick-layers)): `simulation.cpp` (world core), `simulation_behavior.cpp` (commands, stances, and what each unit wants), `simulation_navigation.cpp` (routes and group cohesion), `simulation_locomotion.cpp` (physical movement and separation), and `simulation_combat.cpp` (targeting, weapons, damage), sharing `simulation_internal.hpp`. Decisions (chasing, attack runs, engaging on one's own) belong in behavior; locomotors and combat never decide where a unit goes.
- `src/engine`: runtime orchestration and the bridge from decoded assets to visual/runtime representations. `battle.*` holds battles above the simulation (setup from a built map, replayable orders, the order log), shared by the sandbox and headless runs. The sandbox is split by responsibility: `sandbox_options` (command line, unit-tested), `sandbox_content` (background scene builds and hot-reload checks), `sandbox_debug_lines` and `sandbox_overlay` (what is drawn and shown, pure functions of explicit view structs), and `sandbox_app` (the window and renderer session: one method per frame step, compiled into the executable); `main.cpp` only handles startup, discovery, and the doctor probe. The scene builders behind `visual_scene_builder.hpp` are split the same way: `scene_materials` (textures, materials, lighting), `scene_models` (models, palettes, animations), `scene_terrain` (terrain, liquids, tracks), `object_type_properties` (movement and speed fields of XML types), and `visual_map_builder` (the map build), sharing `scene_builder_internal.hpp`.
- `src/launcher`: configuration and process launch only: `EAWLauncher` and the `eaw_launch` library (sandbox scenarios and presets, `ChildProcess`).
- `src/dev_launcher`: `EAWDevLauncher`, the development GUI that edits and starts sandbox scenarios through `eaw_launch`.
- `src/imgui_app`: `eaw_imgui_app`, the shared Win32 + Direct3D 11 + Dear ImGui window shell and file dialogs of the desktop tools.
- `src/asset_inspector` and `src/dat_editor`: command-line tools built on `eaw_core`. The inspector has one source file per command group (`inspect_asset`, `audit_textures`, `audit_lua`, `audit_maps`, `audit_xml`) plus `inspector_options` and `inspector_common`, declared in `inspector.hpp`.
- `src/tools_gui`: the Dear ImGui workbench. Tool UI may present core data but must not duplicate parsers. Each panel has its own file (`gui_asset_workspace`, `gui_map_view`, `gui_model_view`, `gui_localization`); `GuiApp::State` is declared in `gui_internal.hpp` with its methods in `gui_state.cpp`, and shared helpers live in `gui_support.cpp`.
- `tests`: automated decoder, VFS, compatibility, and runtime-contract tests, one module per file (`test_<module>.cpp`, listed in `EAW_TEST_MODULES` in `CMakeLists.txt`, one ctest entry each); shared requirement helpers and synthetic format fixtures live in `test_support.*`. `EAWEngineTests <module or test>...` runs only the named modules or tests (`EAWEngineTests test_fighter_flight`), and debug-CRT assertions report on stderr instead of a dialog.
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

- C++20; Windows builds use `/W4 /permissive- /utf-8` with exceptions disabled (`/EHs-c-`, `_HAS_EXCEPTIONS=0`, set once for every target in `CMakeLists.txt`). New warnings are defects.
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

