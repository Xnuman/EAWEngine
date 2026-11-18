# History

The logbook of the work: what changed, when, and why, newest last. The other documents describe
the current state only; this one keeps the story (owner decisions and reports, superseded
approaches, changed hashes and measurements). Coding agents append a dated entry for every change
(AGENTS.md, Workflow). Entries before 2026-11-16 were collected from the documents when they were
rewritten to describe the current state; the source document is noted in brackets.

## Undated (early work, before the documents kept dates)

- TED environment chunk 8 was first read as a raw integer and gave 1061 (0x0425, the mini-chunk
  header) for every map; it is a mini-chunk. [compatibility-research]
- Repeated `Squadron_Units` fields were first read as "the last one wins", which gave 1,026 squadron
  members on the stock maps instead of 1,474. [compatibility-research]
- Before the model forward axis was established as -Y, moving units faced 90 degrees off their
  travel direction; `EAWAssetInspector inspect` gained bind-pose bone positions and X axes for this
  check. [compatibility-research]
- The SFX language archives (`sfx2d_<language>.meg`) were at first also mounted as unlisted data
  archives with their names at the VFS root; they are now mounted under `DATA/AUDIO/SFX/`, only the
  chosen language's. [compatibility-research]
- Applying unit `Scale_Factor` to models, footprints, and bones changed state hashes; order logs
  recorded before do not verify. [compatibility-research]
- Type-name lists were split on commas and whitespace because `Underworld_Merc_Squad` has no commas;
  now covered by `split_list`. [compatibility-research]
- The first GridPathfinder (A* over TED passability cells per movement-class mask, 8-connected,
  integer costs, no corner cutting, ordered ties, line-of-sight smoothing, regions labeled once per
  mask) expanded about 7,000 nodes in about 1 ms for a cross-map path in Release; a 15-unit order
  across Alderaan cost about 18 ms in one tick. It ignored unit size, other units, formations, and
  runtime walls and shields. [pathfinding-research]
- The research note's staged plan (audit, clearance, JPS, HPA*-style hierarchy, flow fields for
  group orders, ORCA, job scheduling, space) was superseded by the agreed
  docs/pathfinding-design.md; its audit was planned for EAWAssetInspector and became EAWEngine
  path-audit. [pathfinding-research]
- Group movement step 1: units pushed apart by footprint after movement (immobile never pushed, idle
  yielding to moving, nobody pushed onto forbidden ground, pushed units replanned, route priority
  between moving allies so groups queued through a passage, as reported on Bespin and reproduced in
  a test; only units of one owner made way); structures, walls, and obstacle props stamped
  footprints into passability; units turned at their XML rate. Push separation of ground units was
  later removed (pathfinding-design 4.4). [pathfinding-research]
- Group movement step 2: formation slots for group orders; members ahead of their group waited (Age
  of Empires II), a member without progress for three seconds released; ranks from FormationOrder,
  spacing from footprints and FormationSpacing, slowest member's speed. [pathfinding-research]
- Group movement step 3: placed and spawned companies expanded into their units in formation, also
  when the company names a model of its own. [pathfinding-research]
- Group movement step 4: path requests in an ordered queue under a per-tick node budget; a formation
  planned one shared search from its commander (member nearest the centroid) with short legs per
  member, resuming over ticks (Age of Empires II's commander rule). [pathfinding-research]
- Code health: the hardpoint list (`type_hardpoints`) and the scorch-subtree rule
  (`bones_in_subtrees`) became shared helpers. [roadmap]
- M0 and M1 completed; M3 items 1-7 and 11-12 done in order: simulation core, spawning from XML
  (`Max_Speed` first with a provisional x30 scale), renderer object API, first interaction
  (straight-line moves, flyers with instant turning, no collision or formation), terrain following
  and passability, grid A* pathfinding (synchronous in the tick, about 1 ms per cross-map path in
  Release, a 15-unit cross-map order on Alderaan about 18 ms), first combat slice (nearest hostile
  searched every 10 ticks), unit animation on maps (variant and loop phase from the entity id),
  runtime mesh registration. [status]
- group movement step 1: separation footprints, overlapping ground units pushed apart (summed per
  pair, idle units yielding to moving ones), units pushed off their route replanned. Step 2:
  `formation_move` in ranked rows. Step 3: land companies expanded into their members; group moves
  kept together with a slack (a member without progress for 3 s released) and route priority between
  moving units of one owner (as in Age of Empires II). Route planning budgeted at 30,000 nodes a
  tick; ordering every unit on Utapau fell from 40 ms to about 12 ms in one Release tick. [status]
- space combat steps 1-2: several weapons per unit, hardpoints as parts; hardpoint shots damaged the
  part and the hull by a provisional split; fighters' flight model, behavior layer, stances, 3D
  flight, half-loops, dogfights. A repeated `Squadron_Units` raised squadron members on the stock
  maps from 1,026 to 1,474. [status]
- headless battle step 1 (`Battle`) and step 2 (`EAWEngine battle`, order logs, `--record`,
  `--replay`, `--check-determinism`); Debug and Release reached the same hash on Kuat and Utapau
  (Release about 0.05 ms a tick, Utapau's first auto-move tick about 10 ms). Headless builds skipped
  textures and clips (Utapau 0.7 s instead of 2.3); `state_hash` stopped hashing presentation keys
  and snapshot speed, so hash values changed once and older logs no longer verified. [status]
- runtime spawn packages; land models without clips went from 383 to 290 once
  `Land_Model_Anim_Override_Name` was followed. [status]
- the sandbox's `--panel menu` and `--no-overlay` became leftovers once the bar started shown and
  the statistics panel started hidden; both are still accepted for older scripts. [tools]

## 2026-11-06

- The local Steam layout (app 32470, `GameData` and `corruption`, MEG v1 archives) was observed and
  recorded as the basis of discovery. [compatibility-research]

## 2026-11-07

- The public eaw-schema checkout of that day typed 10,977 of 21,283 named stock definitions by
  canonical names and registries; conservative structural matching raised coverage to 20,881.
  [compatibility-research]
- The texture corpus (2,213 DDS files) and the TED placed-object audit (191,048 of 191,083 FoC
  placements matching an XML definition) were recorded. [compatibility-research]
- Shader corpus inventoried (39 readable .fx, 15 .fxh, 2 .fxo; MeshBumpLight.fx found to be
  compiled) for the legacy shader mapping. [legacy-shader-mapping]

## 2026-11-08

- The owner played Forces of Corruption to observe fighters (single plane, dogfights leaving it,
  half-loop reversals, squadron mates pushing apart); the engine then added the same squadron-mate
  separation. [compatibility-research]
- The owner's screenshots of the original showed bloom and speed-dependent engine glows; the engine
  added its own bloom and speed-scaled engine effects. A radius reading of the particle scale track
  was tried after the X-wing's engine dots looked too small, and dropped because it made
  capital-ship glows far too large next to the screenshots. [compatibility-research]
- Engine effects at rest first kept 45% opacity, which left stationary ships glowing visibly;
  lowered to 15%. [compatibility-research]

## 2026-11-09

- The code reorganization began (small libraries, the resource pipeline, `build.ps1`); its results
  are recorded in docs/status.md, "Code organization". [AGENTS]
- The owner observed (Y-wing squadrons) that particles linked by translation only, as both particle
  editors preview them, detach engine glows from turning fighters; linked particles now follow
  position and rotation under every profile. [compatibility-research]
- feature work paused for the reorganization of the code (six steps over 2026-11-09 and 2026-11-10,
  each with unchanged hashes, captures, and audits); the owner's work order of that day (render
  features, capture-and-compare, review of the technical foundation starting with the Lua host,
  combat presentation and the player layer) was later replaced by the roadmap's work order. [status]

## 2026-11-10

- The reorganization was finished and feature work resumed; keeping the code tidy became continuous
  (AGENTS.md, "Code health"). The roadmap's work order was proposed the same day. [AGENTS]
- The owner observed that space ships do not push each other in the original but steer to avoid
  contact; this replaced the engine's earlier assumption that ships separate by pushing.
  [compatibility-research]
- Lua script costs measured with `lua-survey` (Release, AI plans, story events, callbacks): 2.3
  microseconds per script and tick on average, slowest normal tick 0.75 ms; a runaway resume hit the
  5,000,000-instruction budget in about 73 ms. [lua-runtime-research]
- The owner set the work order: the base technical systems and M2's renderer before combat
  presentation and the player layer. [roadmap]
- Work order item 1 done, the first audio stage (Data stage 2, Audio stage 1): the effect archives,
  the WAV reader, the sound-event catalog, XAudio2 (optional FMOD) devices, and positional battle
  sounds from presentation. [roadmap]
- Agreed: `Spawn_Unit` will return provisional objects that the merge maps to the spawned entities,
  so a script can order them at once while they read as not valid for one tick. [roadmap]
- the sound archives were mounted under `DATA/AUDIO/SFX/` per layer with the `--language` archive
  only; before, every language's archive was mounted at the VFS root as an unlisted archive. Audio's
  first stage done (formats, decoding, XAudio2, battle sounds, animation sounds). [status]
- Lua host slices 1-8: Lua 5.0.3 sandbox and framework primitives (the first survey: 362 of 370
  scripts ran, 46 engine functions and 18 methods called); `ScriptValue`, events, and callbacks
  (about 2,900 events, 167 functions, 129 methods, 317 of 370 scripts without errors); engine
  objects and `BattleScripts`; object scripts, hints, and proximity (reference hash and captures
  unchanged; a Kuat battle with the Calamari cruiser gave the same hash with and without scripts);
  message sinks; `Despawn`, stances, `Get_Shield`; standing orders attack-move and guard (reference
  hash changed once to 0xfe56ca19bc7ac97 for the hashed standing order, behavior unchanged);
  scripted conditions (reference hash changed once to 0x84ffaeab29599e7). Agreed that day:
  `Spawn_Unit` waits for the player layer. [status]
- the owner's requests: space ships no longer pushed each other; the first space avoidance steered
  around the single nearest ship ahead (`plan_space_avoidance` detour point). After the owner saw
  ships pass through each other, the second version (ordered course candidates against every
  neighbour, `keep_apart`, destinations moved off clearances, 4.5-radius space formation slots)
  replaced it; measured before: ships down to 0.07 of two radii apart. [status]
- a mod folder entered as the launcher's game root was not an installation, so discovery silently
  fell back to the Steam installation without the mod (the missing mod units);
  `check_installation_root` now refuses such roots. `resolve_mod_path` added; EAWDevLauncher
  refreshes its lists after changes. [status]

## 2026-11-11

- The engine began switching `_LOD<n>` detail levels by distance. [compatibility-research]
- XML footprints (`Obstacle_*`, `Custom_Hard_*Extent`) were researched and adopted for structure
  footprints. [compatibility-research]
- Survey of every sound source in the stock data (XML, maps, 648 scripts). [compatibility-research]
- Maps had been mirrored left to right through a left-handed view; the owner's screenshots of
  `_LAND_PLANET_ANAXES_02` against the original's editor established the right-handed Z-up world and
  the quarter turn of placed models. [compatibility-research]
- The owner guessed (unverified) that `SFXEvent_Fleet_Move` is the galactic fleet-move response; the
  engine stopped playing it in battles. [compatibility-research]
- Ships were made to keep clear of space obstacles by `Space_Obstacle_Radius` and of starbases by
  their soft footprint radius. [compatibility-research]
- The owner inserted, before work order item 2, the game-object tags existing systems should honor,
  worked tag by tag from xml-tag-status.md (gap and partial rows, family by family; rendering rows
  join item 2). [roadmap]
- Detail levels done: renderer-side selection by distance, screen size, or both, with cross-fades.
  [roadmap]
- The owner asked to try a LOD cross-fade driven by the selection's position in a border band around
  each level boundary instead of by time (still open, Rendering stage 3). [roadmap]
- map orientation, from the owner's Anaxes 02 screenshots against the original's editor: the
  renderer viewed the right-handed world through a left-handed look-at, mirroring every scene (the
  tactical pan had been written to match the mirror, the free camera's A/D looked swapped); placed
  objects faced 90 degrees clockwise of their facing (train tubes across their lines) until
  `placed_model_facing`. Reference hash 0x169c3763c48a4288; every capture changed. [status]
- fixes from the game-object tag survey: GameConstants' speed and health multipliers, hardpoint
  death sounds, the `Override_` spelling, projectile cadence fallback, company members moving with
  their team container (`TeamMovement`, later removed with squads), squadron offsets from team
  containers, inaccuracy (land hit rates fell: the reference battle 94 of 415 shots hit against 150
  of 187), one `choose_target` replacing three copies of the nearest-hostile loop. Reference hash
  0xd2ccd9c1fee34a51. [status]
- acceleration and turning (the owner's list: ships went from rest straight into a turn, land units
  turned while moving off): `MovementProfile`, braking, turn speed fractions; capital ships' default
  turn speed fraction 0.25 (until 2026-11-13). Reference hash 0x7e22213aecfe423e. [status]
- sandbox interface (the owner's list): the top bar with Panels and View menus, `sandbox_imgui.ini`,
  a camera panel and the free camera, the spawn panel's filters; the renderer's built-in text
  statistics panel (`set_overlay_text`) went, and `GroundCompany` and `Squadron` became unit kinds
  (they were missing from the list). [status]
- detail levels (render feature 1, first part): every `_ALT`/`_LOD` mesh kept, the `--lod` policy,
  cross-fades. [status]
- EAWTools' model preview gained damage states, detail levels, and hardpoint toggles;
  `type_hardpoints` replaced the separate lookups in `combat_content` and `scene_models` (battle
  hashes unchanged). [status]
- space obstacles (Kuat 55, Hoth 25 counted; space battle hashes changed); `parse_xml_bool` replaced
  three copies. Solo units (reference 0xfd58596858cf2196); `FormationRaggedness` not read then
  (scale unknown). Move-start angle from `Close_Enough_Angle_For_Move_Start`; runtime spawn packages
  got the speed multiplier they had missed. Bank limit. Crushing (the T2B tanks crushed 9 rebel
  infantry, 13 kills against 5; reference 0xaf063cf737e4cfce). Target validity (hash unchanged).
  Corpses and `Remove_Upon_Death`. Engagement ranges. Target stickiness (reference
  0xbad152faff92df9e). Hardpoint destruction effects. Second muzzles (reference 0x6ad3e61ed3aedba3).
  Destruction survivors. Area blasts (reference 0xfc2121a49b4073a3; AT-STs killed infantry groups by
  splash, Hoth 43 kills from 34 hits). Spinning away (reference 0x93ced81ee52c8ea3; its sound came
  later). [status]

## 2026-11-12

- The owner's account of assist lines (Luke "Understood!", troopers "Escorting!") led to the
  engine's assist-line playback. [compatibility-research]
- The owner's recording showed the original's infantry moving as loose teams of about eight, the
  teams in a block; the engine had no squads yet (design concern 7). [pathfinding-design]
- Unit turrets done; walker turn-in-place, begin/end, start and stop transitions done. [roadmap]
- turrets in the simulation (the owner's request: no firing backwards or sideways); a target picked
  at a longer hardpoint range had held the turret while the weapon waited forever for a nearer
  victim; reference 0xae42e24ba70c9280 (the T2-Bs fire from their barrels). Turret aiming in
  presentation the same day. [status]
- walker transition clips as clip roles in presentation (turn loops, BEGIN/END, MOVESTART, MOVE_END
  by stride quarter, thresholds 0.15 rad/s and a tenth of top speed); later replaced by clip-planned
  walks and turns in the simulation. [status]
- crushed deaths; reading the death clones no longer stopped at the `Damage_Normal` entry. Animation
  profiles: `Movement_Animation_Speed` and `Walk_Animation_Speed` were read as multipliers before,
  which played the AT-AT's legs at 0.14 times. [status]
- jetpack loops silenced (the owner's report: Boba Fett played his jetpack sound while walking).
  Spawnable hero companies (the owner's report: the spawn menu's Heroes lacked most named heroes).
  Assist lines (the owner noticed lines lost, then lines talking over the response when overlap
  names differed); group lines. Hardpoint loss reports. Unit voice and ambient sounds. [status]
- projectile flight shapes (`projectile_pose` replaced two copies of the between-ticks placement).
  Projectile flight rules (reference 0x5b9e51ca1e7d7cf3; spin-away explosions had never been
  played). Company teams in formations from the owner's recording (infantry move as loose teams of
  about eight; reference 0xf2e4966b7b4a11a3). Spawn placement (the owner's comparison: sandbox
  spawns stood inside buildings and off the map; the reference battle had spawned its units stuck in
  Utapau's impassable middle; it then fought in the open, 427 shots, 132 hits, 26 kills, 19 crushed;
  reference 0xf1c77ca722f95b91). Turning on the move, from the owner's recording of an AT-AT
  (before, ground units stopped to turn at every corner beyond their angle). Markers no longer
  drawn. Unit muzzles (the owner's request: twin guns must not fire from one barrel; hash
  unchanged). [status]

## 2026-11-13

- EaW-Revised's recorded fixed-force space scenarios (FoC debug build, 30 ticks per second) were
  accepted as evidence under the owner's ruling, and seven rules adopted: stepped shield refresh,
  the space speed multiplier on acceleration and turn rate, hardpoint damage, turning in place,
  facing an ordered target, straight start, and layer heights; target retention was checked as
  already matching. The default `Min_Speed_Fraction_For_Turn` changed from 0.25 (a guess) to 1.
  [compatibility-research]
- Before the hardpoint-damage rule, the engine damaged both the hull and the hit part (an engine
  choice); before stepped refresh it read `Shield_Refresh_Rate` as points per second.
  [compatibility-research]
- XML loading switched to the registries of the public eaw-schema (`XmlAssetDatabase`); the winner
  changed for 6 of the 122 game-object names defined twice (such as `TIE_Defender_Ion_Cannon_Shot`,
  which had resolved to a copy in `Corruption_Special_Units.xml`) and for 2 trade routes; battle
  hashes, captures, and the spawn list were unchanged. [compatibility-research]
- All game-data lists moved onto one reader, `split_list`; the previous per-field readers (commas
  only for weapon, death, projectile, and targeting lists; pipes only for passability; commas and
  whitespace for names, sounds, colors) gave the same items on stock data except one
  `Unit_Exclusions` list in `GroundUnitTargetingPriorities.xml`, where the comma-only reader lost
  both exclusions. [compatibility-research]
- The engine began switching `_ALT<n>` damage states by health (`Land_Damage_Thresholds`).
  [compatibility-research]
- `map-audit`'s environment field survey decoded fog (0x0f-0x12), wind (0x2b, 0x2c), clouds (0x2d,
  0x2e and the terrain-information chunk's 0x0e-0x10), and the water fields' value ranges.
  [compatibility-research]
- Weather scenarios were decoded and drawn; the first reading placed one effect ahead of the camera,
  which left the rest of a zoomed-out view dry, and was replaced by a world-aligned grid of tiles.
  Heat distortion particles were drawn from the same day. [compatibility-research]
- `EAWAssetInspector gait` established that `Movement_Animation_Speed` is the ground speed at which
  the move clip plays at its own rate. [compatibility-research]
- Bone billboard modes were read; before, the engine drew billboarded meshes fixed, and a planet's
  glow disc showed as a bright band across the globe (the owner's report). [compatibility-research]
- Terrain decals (`TerrainDecalFX.xml`, `D_MASTER_DECAL.DDS`) and dynamic tracks were researched;
  one footprint-texture repeat per track width (3 units) had run the prints together, replaced by
  one repeat per `segment_length`. [compatibility-research]
- Destroyed hardpoints began to show their scorch meshes and damage effects and to drop their
  attached models (the owner's report that destroyed hardpoints were not shown).
  [compatibility-research]
- Heat distortion research gathered at the owner's request (modern and RTS practice) to design an
  enhanced heat mode beside legacy. [heat-research]
- Legacy heat's 8-bit neutral drift fixed (128/255 is exactly no shift). [heat-research]
- Enhanced heat plan agreed with the owner (foreground rejection, signed RG16F additive buffer,
  world-unit shift capped in pixels, placement against TAA, shimmer, faded edges) and implemented
  the same day, the TAA item as a reactive mask; shockwave rings left for later as an option.
  [heat-research]
- Legacy-mode planets (VisualLightingModel::planet), Tree wind bend, Grass billboarding and sway,
  MeshShield layers with shield-hit flashes, sky domes and nebulae, and occluded-unit silhouettes
  (mask pass and composite) implemented. [legacy-shader-mapping]
- The heat distortion pass was added; heat particles draw into it, the 3 stock heat meshes stay
  skipped with a warning. [legacy-shader-mapping]
- Damage states done (chosen from health, with their effects, sounds, and rubble); depth-sorted
  transparency done. [roadmap]
- Edge, temporal, and hybrid (TAA then FXAA) anti-aliasing done, with per-object motion vectors for
  models; particles mark a reactive mask that TAA and FXAA respect. [roadmap]
- The owner postponed HDR to the later visual enhancement: it needs linear lighting (the shading
  works in display space like the original's), so it starts the enhanced lighting profile with the
  physically based path. [roadmap]
- Fog done (legacy and enhanced height fog, particles included); volumetric fog for light shafts and
  smoke later, the owner agreeing. [roadmap]
- Decals done: terrain decals in the terrain shader, scorch marks, footprints, DynamicTrackFX
  trails. [roadmap]
- `texture-audit` measured stock use of special materials (9,294 material instances: Planet 296,
  Tree 185, MeshShield 27, Skydome 19, Nebula 15, Grass 14, MeshHeat 3, MeshAdditiveReflection 3) to
  order the special-material work. [roadmap]
- Water: the owner added water (legacy from TED water data, enhanced from water-research.md,
  agreed); done: water drawn again as scenery in its own pass, enhanced depth color, shore fade, two
  normal layers, Fresnel toward a sky color, glint, refraction, foam, planar screen-space
  reflections. [roadmap]
- Wind bend for trees and waving grass (second UV set) done; occluded-unit silhouettes done; heat
  distortion from heat particles and the enhanced heat mode (heat-research.md) done; shields done
  (the material and hit flashes); legacy sky domes and nebulae done. [roadmap]
- Enhanced sky agreed from sky-research.md; done: cloud shadows (a legacy feature), a sky capture
  for the water's reflection fallback, a sky-tinted ambient, fog toward the horizon color. [roadmap]
- Global illumination (the owner: the sky as a major contributor): sky irradiance (captured sky
  projected to spherical harmonics, the enhanced sky ambient) and sky visibility from the
  heightfield done. [roadmap]
- Weather (legacy, from the map's scenario) and soft particles (enhanced) done; weather was later
  tiled over the ground in view after the owner's report. [roadmap]
- Shadows: the owner chose stable cascaded shadow maps from each map's key light, every model
  casting, over the original's stencil shadow volumes and per-model shadow meshes, for quality;
  cascades, the shadow pass, and receiving done. The owner noted backdrops such as planets receive
  none. [roadmap]
- The owner reordered item 2: the job system and an entity-component design join M2 as general
  engine systems, and the battle issues the owner found come first where they need no rework.
  [roadmap]
- Visual fixes (item 2a) done: the line across planets (bone billboards), skydomes too bright (bloom
  ignores sky and fog, sky ambient balanced upward), visibly tiling water (a broad anti-tiling layer
  in both modes). [roadmap]
- Bugs (item 2b) done: walker transitions play at the walk's and turn's rates; the T4-B's ATTACK
  plays with its shots only (the missile launcher is the unimplemented ROCKET_ATTACK ability);
  crushers and what they crush do not push each other. [roadmap]
- Hardpoint damage visuals and breakoff debris done (combat presentation). [roadmap]
- The owner reported that in the sandbox an AT-AT can stay in its idle clip while ordered to move.
  [roadmap]
- The owner decided on a simulation rewrite bringing staged battles (spawn facing, invulnerable
  targets, per-hardpoint shot report) and fire-cone regression cases on them. [roadmap]
- Owner choices: lone non-looping idles rest 2-4 s; most infantry stop to attack (the owner's
  recollection); the engine's own RTS cameras for now (TacticalCameras.xml tags postponed); feel is
  audited later in one pass. [roadmap]
- (to 2026-11-14) Destroyed space hardpoints: scorch marks and damage effects appear, attached
  models go, Death_Breakoff_Prop debris breaks off. [roadmap]
- Sky rendering research gathered at the owner's request (legacy domes and nebulae, enhanced from
  current and RTS practice). [sky-research]
- Legacy sky domes and nebulae implemented; enhanced sky plan agreed with the owner (capture on the
  GPU): cloud shadows, a sky capture for the water's reflection fallback and a sky-tinted ambient,
  fog toward the horizon color; all done the same day, plus sky visibility from the heightfield
  (global-illumination step two). [sky-research]
- review proposals: A (gameplay measures apart from drawing; Utapau headless build 0.45 s against
  0.7 s; hashes unchanged), D (one battle runtime; the sandbox had stepped the world directly and
  spawned no survivors; frame assembly in presentation), C (engine libraries; `eaw_engine` gone), H
  (legacy units in one place), B (typed type fields replacing `FieldReader`, local `parse_float` and
  `split_list`). All hash-, capture-, and audit-neutral. [status]
- overlay textures through the renderer: the ImGui backend no longer signals a fence of its own on
  the direct queue or waits on the CPU (before, an exception: CPU waits for font uploads). DRED
  enabled in every build. [status]
- malformed-data hardening found by comparing with EaW-Revised (commented `megafiles.xml` entries
  had taken a listed archive's place; a MEG header could ask for two million names from a 12-byte
  file; deeper XML nesting overflowed the stack). XML loads through the game's registries. [status]
- recorded rules adopted from EaW-Revised's recordings of the FoC debug build: stepped shield
  refresh (a third of the former per-second rate; Utapau 0x94836fbc0149669a, Kuat
  0xddd57c413c9eb0b6), the space multiplier on acceleration and turns; hardpoint damage and the hull
  cap (Kuat 0xa1ce38ff72d82517); turning in place by TurnInPlaceSlowdown (Utapau 0x4d4fde35264e932a,
  Kuat 0x93e188dbb88b4209, Fondor 0x24674c0449b36a77); facing an ordered target; straight start,
  space turn fraction default 1 from 0.25 (Kuat 0x927dd82aa3a2924); target retention pinned by a
  test; layer heights (Kuat 0x350cd4eb2dea4bb6); decorations out of the simulation (Fondor 02 lost
  532 entities, Utapau 2; Utapau 0xb0762fda26d52b85, Fondor 0x857d79ebf918e980). [status]
- script sandbox hardening after an independent review of the Lua host (`newproxy` let scripts forge
  engine objects that read past a userdata, and `__gc` ran outside the budget). [status]
- crushers walk over what they crush (the owner's report that walkers did not crush enemy infantry):
  separation had held a walker back at standing infantry's edge, and the crushes counted came mostly
  from separation shoving walkers about; pushing the infantry aside was tried and rejected. Utapau
  0x6989134036962a8f. [status]
- attack clips without an aiming pose (the owner's report: the T4-B's gun animation played out of
  range and looped). Walker transitions played at the move clip's rate (the owner's report that
  walkers start, turn, and stop slowly; `gait` measured the AT-AT's feet). [status]
- idles: before, each unit looped one variant chosen by its id. Wreck ends; the five types without a
  death clone had used the 4 s default. Turret motors. Damage states drawn from health (render
  feature 1, second part). [status]
- renderer structure: render contract split by subject, `eaw_render_core`, the frame graph.
  Depth-sorted transparency (147 instances at Kuat, 384 at Utapau). Edge anti-aliasing (FXAA, the
  owner's choice: no MSAA), TAA (a first version that left the jitter out of the reprojection
  blurred every frame, fixed before commit), hybrid AA (the owner's request), motion vectors. Shadow
  cascades, then shadows; backdrops receive no shadows (the owner: no reason for shadows on planets
  for now). Fog; enhanced fog and particle fog, with the owner's question whether the original's fog
  was an optimization. Terrain decals catalog, then drawing (the owner's option A of three; 51
  scorch marks after the Fondor reference battle), footprints (fixed after the owner saw the AT-ST's
  prints turned a quarter when they followed the unit's facing), dynamic tracks (the owner's report:
  repeated once per width the footprint stride came every 3 units; grid cells raised from 32 to 64
  decals). Graphics panel (the owner's request). [status]
- special materials: planets (before, the base texture through the generic lit path), water first
  steps (the owner's plan in water-research.md; a fault found: since presentation moved onto battle
  entities, the water plane and TED tracks were never drawn), water refraction (`check-captures.ps1`
  gained Mon Calamari 02's water, which is how its loss had gone unnoticed), foam (as the 0 A.D.
  developers found), reflections, wind bend, grass (the owner's choice of a second UV set), shield
  material, shield flashes on every absorbed shot, the grass correction (the clump centers the
  215df11 change read from the empty second UV set were zero), sky domes and nebulae (land skies had
  been black: the unlit path multiplied the dome texture by its vertex color), cloud shadows, sky
  capture and sky light (the owner chose capturing on the GPU each frame), sky visibility (a first
  version read the texture's size after its data moved into the streaming queue and never applied;
  caught by captures), occlusion silhouettes, heat distortion, enhanced heat (the owner's choice; a
  fix to the legacy pass in a6a1404 changed the legacy Kuat references slightly), soft particles,
  weather (the owner's report of rain in one small square when zoomed out), anti-aliasing keeps
  particles (the owner's report: the T2B's red bolts nearly vanished under FXAA and TAA; peak 157 to
  180 under TAA), bone billboards (the owner's report of a line across planets), sky brightness (the
  owner's report: on Alderaan 02 bloom clipped the sky band at 251 and the ground 14% above no
  bloom; after, 208 and 2%; the sky ambient had been balanced over all directions, making upward
  ground a tenth brighter), water tiling (the owner's report that water looks too tiled). [status]
- destroyed hardpoints show it (the owner's report that destroyed space hardpoints were not replaced
  by their destroyed versions): scorch meshes had been hidden while intact but never revealed; GPU
  validation clean in a space battle that destroyed 10 hardpoints. [status]
- Water rendering research gathered at the owner's request; plan agreed with the owner: legacy from
  TED water data, enhanced in order depth color, refraction from a scene copy, shoreline fade and
  foam, scrolling normal layers, sun glint, planar screen-space reflections with a sky fallback.
  [water-research]
- Enhanced water implemented (depth color, shore fade, two normal layers, Fresnel, glint,
  refraction, foam, planar screen-space reflections); the owner chose ocean waves for maps such as
  Kamino afterwards (roadmap item h), against the research's "skip Gerstner" advice.
  [water-research]

## 2026-11-14

- `EAWAssetInspector clip-joins` measured walker clip joins (MOVE_END clips start at quarters or
  thirds of the stride), the turn kit shared by ten models, and the pod walker's raised middle leg;
  `type-tags` surveyed who uses the walker tags. [compatibility-research]
- The engine began composing turns in place from BEGIN, whole cycles (loop, HALF, QUARTER), and END;
  the AT-AT's TURN*_HALF and _QUARTER clips, unused until then, are played since.
  [compatibility-research]
- The ECS and job system design was drafted (ecs-design.md), after measuring the flat entity table:
  1,189 entities 0.70 ms average tick, 3,296 entities 4.66 ms average and a 51.5 ms opening tick
  (combat 38 ms, route searches 8 ms); the table then had 67 per-entity arrays. The 2026-11-14
  review's problems 5 (MovementProfile as a bag of modes) and 6 (bookkeeping by hand) were among its
  motivations. [ecs-design]
- The owner's Singe engine source (its ecs and dsa modules) was read as input for the design.
  [ecs-design]
- Research for the ECS and job system design collected by three research passes (ECS storage and
  scheduling; deterministic RTS simulations; game job systems), written for the draft then named
  ECS-DESIGN.md. The job-system pass recommended a thread pool without fibers at first; the owner
  chose fibers on 2026-11-15. [ecs-research]
- Shield hits play only the absorb particle; the shield mesh flash waits for the shield abilities.
  [roadmap]
- Walker locomotion cleanup before the ECS (the owner's review), one commit each: clip naming in one
  place (`assets/clip_names`) and planned walks on their clips' first variants; the 24 stock types
  without animation priority (Rancor, SPMAT, AT-AA, Night Sister, speeder bike, Juggernaut,
  Tauntaun, ground beetle, ...) planned as the AT-AT is by the owner's decision, the old start/stop
  path removed; `UnitAnimator::animate` split into five stages, pose keys unchanged frame for frame
  in two traced scenes; turns report clip and progress and are composed of loop, HALF, and QUARTER
  cycles. [roadmap]
- hardpoint debris (the owner's report that a destroyed hardpoint's destroyed version is not
  spawned; 15 debris types on Kuat with a Star Destroyer). Follow-ups from the owner's checks:
  destroyed hardpoints had collapsed their attachment bone's whole subtree, hiding the Star
  Destroyer's scorch; damage effects had never played; shield hits stopped flashing the shield mesh
  (kept for the shield abilities, the owner's reading). [status]
- walker transitions in presentation got 0.25 s fades and MOVESTART holding the stride; the owner
  found the fades clanky, so they were turned off for a test (`transition_fade_seconds`), later
  removed with presentation's transition guessing. [status]
- walker stops (the owner's video of clanky AT-AT starts and stops): the engine had started the
  MOVE_END of the stride's quarter wherever in it, snapping legs by up to 16 units; a stopping
  walker then walked on to the next quarter. [status]
- animation-led walkers, with the owner's agreement: `Locomotor_Has_Animation_Priority` types sped
  up over MOVESTART and braked over MOVE_ENDONE (the AT-AT had been at full speed on its first tick
  and stopped dead, skating out of every start); then turns wound up and down (`turn_acceleration`,
  `turning_in_place`, `turn_arriving`), a Stop order braking straight ahead; measured on Hoth:
  TURNR_BEGIN at 0.1 s, TURNR_END at 3.16 s, MOVESTART at 3.96 s, MOVE_ENDFOUR at 6.49 s. [status]
- clip-planned turns and overlay attacks (the owner's check: a reset on stop, turn poses repeating):
  the AT-AT's ATTACK had taken the whole body, restarting with every pulse;
  `Attack_Animation_Is_Overlay` played on animated bones only; the turn loop had started mid-loop;
  turns became whole clips (BEGIN, loops, END), replacing the turn acceleration. [status]
- stops by the number of MOVE_END clips (`clip-joins`, proxy bones left out). Walks played as clips
  planned by the simulation (the owner's check: the AT-AT jolted from its stop into idle; a trace
  showed MOVE_ENDFOUR, 160 frames, played at 4.6x and cut at frame 125). [status]
- clip names moved to `assets/clip_names`. Unit animation in stages (the owner's review): `animate`
  from about 580 lines to 146; a probe printed the same 295,630 pose-key lines before and after over
  a mixed Hoth battle and an infantry fight. [status]
- turns composed of cycles (the owner's review, step 4): HALF and QUARTER cycles;
  `turning_in_place`, `turn_arriving`, `AnimationProfile::in_place_turn_rate`, and `turn_end_begun`
  replaced by the snapshot's turn clip. [status]
- every walker with transition clips plays as clips (the owner's decision on cleanup step 2): 24
  more stock types; presentation's own start, stop, and BEGIN/END guessing removed (16 fields,
  `AnimationProfile::pre_turn`, `walk_transition`, `animation_led`, two tuning constants). [status]
- turning back and the raised leg (the owner's check: the AT-AA ignored its turn clips for an order
  given while walking straight; the pod walker's start and stop were broken): a 90-degree turn-back
  latch (`walk_turn_back_angle`), and the pod walker's leg lift and drop (every start and stop had
  jumped 13 units). The same day the turn-back latch was replaced by the turn-in-place angle decided
  on a new way (the owner's check: the AT-AT turned up to 180 degrees without its turn clips where
  30 is right), and overlays became additive (the pod walker's ATTACK pulled the walking legs into
  its standing pose); then overlays left alone the bones a moving clip below moves (the owner's
  check: the pod walker's middle leg was not up for several frames while walking). [status]

## 2026-11-15

- the simulation's fixed tick set to 60 Hz (the owner's decision); the original's logic rate is not
  measured, and XML values per 30 Hz frame are converted to seconds apart from it
  (`assets/legacy_units.hpp`). [architecture]
- The owner checked the retail game: the pod walker's middle-leg snap at the start of its walk shows
  there too, so it is the stock content (EV_PODWALKER_MOVESTART_00.ALA), not the engine.
  [compatibility-research]
- The owner agreed the design (section 9 decisions 1-8): dense core plus sparse pools, own fiber job
  system without OS mutexes, spawn order with one declared hash rebaseline, floats with own
  trigonometry before M6, job system passed explicitly, recipe profiles stored once, Singe's ideas
  not its code, /GT globally. [ecs-design]
- The owner chose fibers and no OS mutexes for the job system; the thread-pool design of the first
  draft was kept as the alternative not taken (7.6). At that time the code base had no thread_local.
  [ecs-design]
- The tick became 60 Hz (16.6 ms), up from the 30 Hz (33 ms) the first measurements were set
  against. [ecs-design]
- Migration step 0 done: per-tick-layer timings in the battle statistics and `--hash-trace` by state
  subject. The timings showed separation took 3.4 of an average tick's 4.5 ms (1.9 of 3.05 ms after
  filtering its candidates before sorting) and that the opening tick is mostly combat. [ecs-design]
- Migration step 1: `eaw_jobs` built; first client presentation's pose sampling. [ecs-design]
- Migration step 2 done: the 48 per-entity arrays became 16 column groups with the same memory
  layout and unchanged hashes; removal through for_each_column, spawn's size check,
  check_entity_table. The owner decided components are column groups, one array per field (decision
  9), kept for now; a struct-per-entity style may replace it later. [ecs-design]
- Migration step 3 began: ComponentPool holds WalkClips and TurnClips (the hash already hashed their
  plans only for such entities). The owner's rule for pooling before step 7: an absent component may
  stand in for its old values only where every non-member had the same constant ones; flight
  manoeuvres, footprints, and crush profiles were pooled on it; minimum speed, cruise height,
  formation, combat, and group cohesion waited. [ecs-design]
- Migration step 4 first slice: the tick became one schedule table of nine systems (commands,
  behavior, navigation, movement, crushing, separation, motion, combat, publish) with declared
  SimData reads and writes; the new DebugValidation build checked writes by copying undeclared data
  before each system and comparing after (about 17 times a Debug tick; the owner's choice to keep it
  out of Debug). The first hand-written declarations missed 15 writes, all found by the check's
  first runs. [ecs-design]
- Typed views (system_view.hpp) introduced; contact (crushing, separation, measured speeds in one
  system) was the first, then movement, navigation, behavior, combat, and publish, each with
  unchanged hashes. Typed views found writes the hand-written declarations missed and
  DebugValidation runs never reached (behavior clears a unit's group speed limit and group when it
  starts an engagement or sends a flyer to loiter; movement: a stopping unit leaves its group).
  Removal was split from combat. Queries as a separate helper were tried and dropped (sorting a
  pool's members cost more than it saved; crushing 0.006 to 0.025 ms a tick). [ecs-design]
- The owner removed DebugValidation's copy check once no system was left for it (typed systems
  checked by the compiler; commands and removal declare all writes); the configuration stays, empty.
  [ecs-design]
- Migration step 5 first part: TypeRecipe and make_spawn in the battle layer, one construction path;
  the owner's rule that a type behaves the same however it appears (runtime-spawned obstacles block
  the ground, immobile space objects keep ships clear, ground following needs a valid ground on both
  paths). [ecs-design]
- Migration step 5 second part (the owner's decision, weighed against waiting for step 6):
  EntityRecipe and RecipeId in the simulation, add_recipe deduplicating recipes, entities pointing
  at their recipe's profiles; hashes unchanged. [ecs-design]
- Migration steps 6 and 7 done together (the owner's choice: one declared hash change): DamageQueue
  and the resolution system, the owner's rule that a shot at a victim killed earlier in the tick
  strikes the body without damage, compacting removal (remove_entities) so dense order is spawn
  order (removal used to swap the last entity into the gap), per-component hashing. Every battle
  hash changed once; the capital ships fired the same 3,789 shots and landed 50 more. [ecs-design]
- The remaining pools followed as declared hash changes of their own (the owner), outcomes
  unchanged: flight (minimum speed and cruise height joining the manoeuvres in FlightColumns),
  formation (order, spacing, moving alone as recipe data; the team as a pool), group cohesion
  (GroupColumns), combat (CombatColumns). Combat's pool lookups first cost about 9% of a 500-unit
  tick (three lookups per candidate); one lookup after the hostility test brought it level (12.0
  against 12.2 ms). [ecs-design]
- The owner asked that a refactoring review of what the migration left (step 9) be agreed with the
  owner before it starts. [ecs-design]
- The owner agreed the pathfinding and movement design (docs/pathfinding-design.md), replacing work
  order 2g's open list, to come before the entity-component migration's step 8. [pathfinding-design]
- Baseline before the redesign (Release, benchmark-battle.ps1, 500 units, 4,400 entities): contact
  (push separation) 6.9 ms average, 34 worst; navigation 3.4 ms (15,000-node budget spent every
  tick); combat 2.2 ms, 106-150 ms worst on the first tick; behavior 0.8 ms; all 11-14 ms, 40-116 of
  600 frames over 16.7 ms. [pathfinding-design]
- Step 1, squads, done: team entities; companies spawn as squads; the sandbox selects them by their
  soldiers. [pathfinding-design]
- Step 2, the pathfinding audit (EAWEngine path-audit, compare-path-audit.ps1), done; baseline of
  the grid A* at four size buckets: 76 maps, 98,800 pairs, 82% reached, 1,385 nodes and 0.25 ms a
  search on average. [pathfinding-design]
- Step 3, first part: contact's grid built by footprint (items in every cell they cover); staggered
  first target searches and retarget beats (a declared hash change). [pathfinding-design]
- Step 4, clearance, done. The owner revised decision D4 after the stock data showed
  UnitCollisionClass does not measure size: each unit plans with its own footprint radius. First
  size buckets were radius 7, 20, 35, and 65; they kept units out of gaps they fit, so the owner set
  buckets every 2 units from 2 to 68. [pathfinding-design]
- Step 5, chunks: one level (16-cell) and two levels (16 under 32) compared on the path audit; then
  refined (the owner's meaning of two levels) built and made the battles' layout (a declared hash
  change: the large Utapau battle 0x37f1b2eb854364ff to 0xc0509bb692e38d6). [pathfinding-design]
- The owner moved ORCA, giving way, and the no-overlap check (step 6) before formation frames:
  overlaps and pushing were what play showed. [pathfinding-design]
- Step 6 done with squads as single agents (with every soldier an agent the large Utapau battle cost
  39 ms a tick; with squads 2 ms); ground separation removed. Each giving-way rule came from a
  deadlock a test showed. The owner's drawing: allies with too narrow a gap part, each to its own
  side. The owner reported that a rule against giving way to smaller units kept companies from
  giving way, and that squads which gave up at once at a blocked way back stayed aside; both were
  changed. [pathfinding-design]
- The owner: a unit with an order reaches its destination or the closest point it can; detours
  around standing units (plan_detours) built, which closed step 6's blocked-unit replans.
  [pathfinding-design]
- Step 7, formation frames with selections split by distance, done. [pathfinding-design]
- After trying frames, the owner made the default group move per-member routes at the slowest
  member's speed, and the frame formation move the special one (Alt + right-click): in play frames
  had companies stand for seconds forming up and waiting, gathering far from some companies, and
  leaving companies far apart. [pathfinding-design]
- Formation fixes after the owner's tests: formations form on the move over the first 150 units; the
  route is planned for the largest vehicle's or soldier's footprint, not a squad's circle (narrow
  ways had looked closed and routes ended short); files take members in the order they stand along
  the heading (by slot, a front member was sent to the back through the others); a final slot on
  ground a member cannot stand on moves to the nearest point it can (it had gone to the frame's
  centre); frame members on the way stopped asking each other to give way (they did, over and over).
  [pathfinding-design]
- The owner reported units stopping while others gave way, and companies sent through a standing
  squad stopping at it; group-mates in their slots, units giving way, and allies standing after a
  move elsewhere stopped counting as a crowd. [pathfinding-design]
- Routes kept off walls by a 5-unit margin: with buckets every 2 units, routes had hugged corners at
  exactly the unit's radius and crowds jammed there. [pathfinding-design]
- Item 2c done: ecs-design.md agreed (dense core table and sparse pools, recipes from XML, explicit
  phase schedule with declared access, fibers for jobs), research in ecs-research.md. [roadmap]
- Item 2d: the job system `eaw_jobs` done with tests across worker counts; its first client,
  presentation's pose sampling. [roadmap]
- Item 2e steps: per-tick-layer timings and sub-hashes and `--hash-trace`; components as column
  groups; pools for walk and turn clips, flight manoeuvres, footprints, crush profiles, then flight,
  formation, group, and combat; the schedule table with the DebugValidation write check and six of
  eight systems on typed views; one construction path with recipes; the damage queue, compacting
  removal, and per-component hashing as the one declared hash change. [roadmap]
- The owner put parallel systems after work order item g (pathfinding). [roadmap]
- Item 2f done: real squads (team entities; companies spawn as squads; the sandbox selects them by
  their soldiers). [roadmap]
- pathfinding-design.md agreed; done: the pathfinding audit, the footprint grid for contact and
  staggered target timers, clearance per unit footprint, chunks (32-cell, 16-cell, then cells,
  chosen over one and two plain levels on the stock maps), ORCA avoidance with allies giving way
  (squads as single agents), formation frames (the owner: avoidance first) with selections split by
  distance; after the owner's tests the formation move became a special move (Alt + right-click),
  the default being each unit on its own route at the slowest speed. [roadmap]
- The owner set the simulation at 60 Hz and the budget of 60 frames per second. [roadmap]
- The route-search budget was halved to 15,000 nodes a tick (the owner): at 500 units the average
  frame fell from 13.6 to 11.0 ms and the frames over budget from 75 to 42 of 600, while 30% fewer
  routes were planned in 10 s; the first tick's target search unchanged. [roadmap]
- job system (work order 2d, step 1): an earlier version lost 4 of 300 test runs to a counter
  destroyed while its last job still read it. First client: pose sampling. [status]
- ECS migration step 0: per-layer timings; in the large Utapau battle separation was 3.4 of 4.5 ms a
  tick (about 50 sorted candidates per query); pre-sort filtering made it 1.9 ms, the tick 3.05 ms;
  the opening tick's 51 ms was mostly combat (38 ms) and route searches (8 ms). Sub-hashes and
  `--hash-trace`. [status]
- ECS step 2: 48 per-entity arrays became 16 column groups (hashes unchanged). Step 3, first slice:
  component pools (walk and turn plans, flight manoeuvres, footprints, crush profiles; hashes
  unchanged). [status]
- ECS step 4: the schedule table and the `DebugValidation` copy check (about 17 times a Debug tick);
  named resources; typed views for contact, movement, navigation, behavior, combat, publish (the
  views found undeclared writes the hand-written declarations missed: separation measuring remaining
  routes again, a stopping unit leaving its group, engagements and loitering flyers clearing group
  fields); `for_each_data` had visited struct members instead of structs, skipping two serial
  counters (fixed); the copy check then removed. `check-battle.ps1 -Extended` added. [status]
- ECS step 5: recipes (`TypeRecipe`, then `EntityRecipe`, the owner's decision); a type behaves the
  same however it appears (the owner's choice); the 3,300-entity battle ticked in 3.2-3.4 ms against
  3.4-3.7 ms. [status]
- ECS steps 6 and 7 (the owner's choice of one declared hash change): damage queue, compacting
  removal, per-component hashing; every expected hash changed (reference 0xbed05879a9bfff2c); the
  reference battle 471 shots and 10 kills against 472 and 10. [status]
- the simulation moved from 30 Hz to 60 Hz (the owner's decision, with the 60 FPS budget); Lua
  `Sleep` and game time would have run at double speed until `BattleRuntime` passed its tick length
  to the script host. The headless default became 6000 ticks; `check-battle.ps1` then ran twice the
  ticks (reference 0x5e486b97af004368; it has since returned to 3000 ticks, 50 s). At 30 and 60 Hz
  the space battles and the large land battle agreed (hit rates 80.4% and 80.6%); the small land
  battle differed (471 shots and 10 kills at 30 Hz, 416 and 14 at 60 Hz), and a 30-120 Hz sweep
  plateaued from about 60 Hz, read as the 30 Hz step's error. The large battle averaged 3.0 ms a
  tick, 6.0 ms per frame. [status]
- battle frame benchmark (the owner's request): `--stress-orders` and `benchmark-battle.ps1`; then
  10 and 50 units passed, 100 went over once, 500 averaged 15.3 ms with 110 of 600 frames over
  (route searches and separation about 6 ms each, the first tick's target search about 125 ms).
  `EntityId`'s `operator<=>` replaced three copies of an id ordering. [status]
- ECS step 3's remaining pools (the owner's choice: each a declared hash change, outcomes
  unchanged): flight (reference 0xd6af1ea5b889a0e3), formation, group cohesion, combat. The route
  budget went from 30,000 to 15,000 nodes a tick (the owner's choice): at 500 units 11.0 ms against
  13.6, 42 frames over against 75. [status]
- squads (pathfinding design step 1; the owner's choices: a team entity, attacks on a team meaning
  any member); `TeamMovement` removed (Stormtrooper_Team .8 against 1). The reference battle fought
  as squads (1,004 shots, 15 kills against 416 and 14); 500 units averaged 16.8 ms (separation 9.3
  ms, 53,683 touching pairs, the crush scan 4.8 ms). [status]
- path audit (design step 2): the grid A* baseline recorded. [status]
- contact's grid by footprint (design step 3): contact 12.8 ms against 14.2 at 500 units; the worst
  tick slower (57-80 ms) because the benchmark spawned armies over each other. Staggered target
  searches (a declared hash change): combat's slowest tick 3.9 ms against 19.9 (large Utapau), 8.3
  against 38 (500 units). [status]
- clearance (design step 4; the owner's decision: each unit's own footprint radius); size buckets 7,
  20, 35, 65; free-ground spawns; at 500 units 15 of 600 frames over against 112, worst 28.9 ms
  against about 110. [status]
- chunks (design step 5): one level of 16-cell squares became the battles' layout (a declared hash
  change), then the refined two-level layout (the owner's meaning of two levels; only the large
  Utapau hash changed, 0x37f1b2eb854364ff to 0xc0509bb692e38d6). The check battles' hashes changed
  once for a new combat field (reference 0x458377bd9e75ba1f). [status]
- avoidance without overlap (design step 6; the owner's rules): ground separation removed, ORCA;
  with every soldier an agent the large battle cost 39 ms, hence squads as agents; large Utapau 7.0
  to 2.0 ms a tick, 500 units 15.0 to 8.0 ms; outcomes changed (large 1,471 kills against 413,
  reference no crushes against 4, AT-AT battle 2,546 shots against 494). [status]
- formation frames (design step 7): large Utapau 8.2 ms a tick; reference 20 kills and 17 crushed
  against 4 and 0; squad soldiers heading for their team cut the large battle's searches from 10,400
  to 2,000; `unit_radius` and `nearest_to_centroid` replaced copies. [status]
- group moves after the owner's tests (companies did not give way and stopped at a standing squad;
  groups stopped short or far apart; companies stood for seconds and gathered far away): the default
  move became `group_move`, the frame Alt + right-click; the space capital-ship battle 4,230 shots
  and 32 kills against 3,798 and 31. [status]
- formation moves fixed after the owner's tests (stopping short, standing for seconds, far gathering
  points, companies apart): forming on the move; arrival fixes; the crowd's reach from four summed
  radii to six. [status]
- giving way and stopping fixed after the owner's tests of the default move (units stopped while
  others had not finished giving way; units that gave way did not return); the stopping could not be
  reproduced in a test. [status]
- destinations placed at the order, size buckets every 2 units from 2 to 68, routes off walls (the
  finer buckets had made routes hug corners: 112 of 150 units of the budget test arrived in time); a
  declared hash change in all five battles. [status]
- detours (the owner's report: an AT-ST stopped for good at a line of packed stormtrooper squads;
  the owner's rule: a unit with an order reaches its destination or the closest point). Giving way
  in any direction, and lanes (the owner: units may step diagonally and go back; lines open); a
  walker had slid around a block of allies 68 units off its line. [status]

## 2026-11-16

- The owner added the rule that docs/history.md is the project's logbook, kept by the coding agents:
  every change appends a dated entry, newest last, and the other documents describe the current
  state only. AGENTS.md was rewritten for clarity with the rule added. [AGENTS]
- Migration step 8 began by measurement on the 500-unit benchmark: route searches 0.16 ms a tick
  against ground avoidance's 4.3 ms (2.5 of it giving way, made cheaper serially first). Ground
  avoidance's ORCA went first on the job system (1.48 to 0.43 ms on 7 workers);
  SimulationWorld::set_job_system added and --check-determinism compares a job-system run with a
  serial one. Route searches followed for the opening spikes (shared NavigationData, eight requests
  at a time; worst search tick 18 to 10-12 ms, hashes unchanged), then combat target choice and
  turrets (combat 1.2 to 0.82 ms, opening spike 6.2 to 2.3 ms). [ecs-design]
- The docs rewrite removed the Singe source's local path from ecs-design.md's sources (a contributor
  path). [ecs-design]
- The owner: a group ordered to a position takes it, not a block 50-100 meters across (a Canderous
  company's FormationSpacing 2.75 put its slots 169 units apart); ground group moves now pack
  members near the point. [pathfinding-design]
- The owner's Kamino recording of jams of moving units: route repairs (a short leg back to a
  waypoint out of sight) and detours around held movers (at most three a route) added.
  [pathfinding-design]
- Asking to give way every tick cost 2.5 ms a tick at 500 units (nearly every ask failing in
  crowds); a failed ask now waits a quarter of a second. [pathfinding-design]
- Item 2e's parallel systems begun: ground avoidance (measurement put it first), route searches in
  batches, combat's target choice and turrets on the job system; measured: stock battles reset
  navigation data twice in 1,800 ticks, 25 ms in all, so rebuilding only touched chunks waits for
  base building. [roadmap]
- Profiling with Tracy done (the owner: Tracy as the main profiler), closing item 2d's profiling
  hooks. [roadmap]
- Item 2h first slice: ocean waves (a water grid of about 12-unit cells, four Gerstner waves fading
  in with depth, crest foam) with tuning sliders in the Graphics panel; waves only on ocean maps
  (the owner: not on lakes, ponds, rivers): Kamino and Manaan by name, with an ocean-maps/on/off
  setting. The owner judges the ocean far from done ("needs a lot of work"); the rest waits for
  Rendering stage 5. [roadmap]
- Key rebinding done ahead of the UI, as the owner chose: the full keyboard (`platform/keys.hpp`),
  an action table with a bindings file (`platform/input_bindings.hpp`), the sandbox's actions, and a
  Controls panel. Controllers (GameInput) wait for the player layer (the owner). [roadmap]
- Input load measured in the sandbox (records and read time a frame, Statistics panel and at exit);
  `scripts/check-input-load.ps1` floods it with a synthetic 8 kHz mouse; the run waits for the owner
  (it needs the focus). [roadmap]
- Combat presentation: a wreck's end sound, death clones drawn with their own models, wrecks play
  their models' proxy effects; `Should_Death_Clone_Play_Idle` found to have no stock effect.
  [roadmap]
- The owner reported the AT-AT animation problem (an AT-AT staying in its idle clip while ordered to
  move) fixed; removed from Simulation stage 1's open list. [roadmap]
- Roadmap rewrite: `Rotation_Animation_Speed` and `Locomotor_Has_Animation_Priority` were still
  listed as open under Simulation stage 1 though xml-tag-status.md marks both done; removed.
  [roadmap]
- giving way cheaper (73 failing asks a tick had cost 2.5 ms; now 0.7 ms, navigation 4.5 to 2.8 ms);
  detours that cannot start end (a unit had planned the same detour every 2 s). A declared hash
  change in all five battles. [status]
- ground avoidance on the job system (ECS step 8): 500 units 6.0 ms a tick (7.5 serially).
  Navigation data shared; route searches in batches (large Utapau worst route-search tick 18 to
  10-12 ms; the stress battle 6.0 against 5.87 ms). Job system: cheaper loops and wakes (a single
  wake reached another worker and the waiting thread slept on: the test suite hung until fixed;
  `jobs-loops` 80.1 to 67.7 ms, `jobs-tiny` 23.5 to 20.0 ms). Combat's target choice and turrets on
  the job system (rebuilding only the chunks a ground change touches was measured and left for base
  building: two resets, 25 ms in the large battle). Hashes unchanged. [status]
- Tracy as the main profiler (the owner's choice; 0.14.1 pinned because 0.15.0 was ten days old).
  [status]
- packed group moves, route repair, and detours around held movers (the owner's Kamino recording: of
  10 Canderous tanks, 7 stood still for good, a group stopped 200-350 units short; the tank's
  `FormationSpacing` 2.75 had made slots 169 units apart); flow priority in avoidance tried and
  dropped; replayed, all 10 arrive (6 exactly, 4 at the crowd's edge, 26 to 51 units off). A
  declared hash change in all five battles. [status]
- the sandbox replays order logs (the owner: to see a recorded battle's end); the Kamino recording
  ended in the same state as headless (0x5e34e8af31380a76). `format_hex` moved to
  `sandbox/text_format`. [status]
- death clones' own models (before, such ships vanished on death) and wrecks' proxy effects; a
  wreck's end sound; the tag status's debris rows, done on 2026-11-13, corrected. [status]
- input load measurement and `check-input-load.ps1`; key rebinding (the owner's choices). [status]
- ocean waves (roadmap item h): first slice, sliders, the owner's first tuning (speed 1.4, crest
  sharpness 0.2, calm below 4, full from 10, crest foam from 0.12), crest foam in patches after the
  owner's screenshot of a regular lattice, the owner's second tuning (the current defaults), and
  waves only on open sea (the owner: Kamino and Manaan). Legacy water unchanged (2 of 1.44 million
  pixels differ at Kamino). [status]
- `parallel_for` took its chunks from a shared index and wakes went only to sleeping workers; A/B on
  one machine with 7 workers in EAWBenchmarks: short loops (`jobs-loops`) 80.1 to 67.7 ms (4.9 to
  5.9 times the serial run), tiny jobs 23.5 to 20.0 ms, long chunks unchanged (211 against 215-220
  ms). [tools]
- Ocean waves first slice: water as a grid of about 12-unit cells, four Gerstner waves fading in
  with depth (first defaults: calm at 6, full at 40), crest foam; legacy water unchanged (Kamino: 2
  of 1.44 million pixels differ), enhanced Kamino differs in 4.8% of the frame. [water-research]
- Graphics-panel sliders for the waves (VisualOceanWaves); first owner tuning became defaults (speed
  1.4, crest sharpness 0.2, calm below 4, full from 10, crest foam from 0.12). [water-research]
- The owner's screenshot showed a regular foam lattice on the deep ocean (crests repeat; breakup
  sampled the tiling normal map); crest foam moved to procedural drifting patches (420-unit patch
  noise, 7-unit breakup). [water-research]
- The owner's second tuning, for bigger waves, became the defaults: height 1.15, length 1.77, speed
  2.04, crest sharpness 0.2, calm below 4, full from 3, crest foam from 0.3. [water-research]
- The owner decided waves run only on open-sea maps (not lakes, ponds, rivers): Kamino and Manaan by
  name (ocean_map), with an ocean-maps/on/off setting; a corpus survey showed a water-share measure
  cannot tell Dagobah's swamp from Kamino. [water-research]
- The owner judged the ocean "needs a lot of work"; the rest deferred to Rendering stage 5.
  [water-research]
- tag status corrected: Permanent, Scale (terrain decals) and Intensity (decal opacity) were
  implemented (src/assets/src/terrain_decals.cpp, src/presentation/src/battle_effects.cpp) but
  marked gap; Behavior/LandBehavior/SpaceBehavior notes updated to the behaviors actually checked
  (TURRET, FIGHTER_LOCOMOTOR, SPACE_OBSTACLE, AMBIENT_SFX). [xml-tag-status]
- xml-object-tags.md: 58 "Not read" remarks on tags the engine now reads (movement, combat, death,
  debris, projectiles, decals, animation) replaced by what the engine does. [xml-tag-status]
- Documentation rewrite (the owner's request): every document now describes the current state
  (status.md 36,000 to 19,300 words, by system instead of a dated log; roadmap.md open work only;
  tools.md, architecture.md, and README.md checked option by option and file by file against the
  code; the research and design docs with their steps marked done, partly done, or open). Their
  history became the entries above. Fixed on the way: 6 stale tag statuses and 58 stale "Not read"
  remarks in the tag tables, the sandbox's --panel and mode help, a stale comment on skipped heat
  and occluded meshes, and a code comment citing the wrong research section. The AT-AT idle-clip
  problem is fixed (the owner's report). [coordinator]
- Ground hits (the owner's choice: misses fly on to the ground): on land a missed shot, or one whose
  target died, flies on past its aim point until it meets the terrain or liquid, its maximum flight,
  the map's edge, or 2,000 units (an engine choice); instant misses trace the line at once. Ground
  impacts play Projectile_Ground_Detonation_Particle and the Projectile_Ground_Detonation_SurfaceFX
  particle and sound for the map's liquid or default (a splash over water alone). Only large
  Utapau's hash changed, 0xc3fc271e6fcc9bd0 to 0xddbc9df8ed7e2bc7 (255 of about 2,800 misses landed
  in 1,800 ticks); the reference, fighters, capital ships, and AT-ATs are unchanged (the new
  projectile state is hashed only for shots flying on). Projectile_Ground_Detonation_Particle had
  been marked done but played only for death explosions naming a projectile; no shot reached the
  ground before.
- The owner's third ocean tuning, for ocean maps, became the defaults: height 1.25 (the mesh bounds'
  maximum), length 2.5, speed 2.7, crest sharpness 0.7, calm below 0, full from 10, crest foam from
  0.54 (before: 1.15, 1.77, 2.04, 0.2, 4, 3, 0.3).
- Death projectiles' detonations: a dying type's Death_Projectiles blast now also reports its
  detonation (ImpactKind::detonated), which plays the projectile's detonation particle and sound
  where the type died (8 stock types: exploding crates, orbital resource containers, destroyable
  docks). Their damage was already done; no hash changed (impacts and the new profile key are not
  hashed).
- Damaged smoke: Damaged_Smoke_Asset_Name (7 stock aircraft) now trails its particle below half
  health (an engine choice). The transform-scale split of attached effects moved into a shared
  helper, without_uniform_scale. No hash changed.
- "Beams for instant-hit weapons" left the open list: no stock weapon fires an instant beam (the
  nine projectiles without a speed are ability blasts and two bombs); every stock beam belongs to an
  ability (superlasers, laser defense, energy weapon, shield leech, tractor beams, drain life).
- "Damage-specific death clones" left combat presentation's open list: besides Damage_Normal and
  Damage_Crush (both done), the stock death-clone damage kinds come from abilities: Damage_Infection
  from IG-88's infection ability (Infection_Projectile), Damage_Remote_Bomb from no weapon (the
  remote bomb deals Damage_Bomb), Force lightning and telekinesis, and Veers' deployed state.
- Spinning away is drawn: a craft that spins away rolls at one and a half turns a second (an engine
  choice) until it dies; before, it flew on unrolled. Presentation only.
- M2 resumed (the owner's choice over the 7 stock hardpoint turrets). The legacy distance fade:
  grass and batched props (Grass, BatchMeshAlpha, BatchMeshGloss) fade out between 5,000 and 7,000
  world units from the camera (guesses; Graphics panel sliders); the opaque BatchMeshGloss by a
  screen-space dither. The pixel shader's dither threshold became a helper shared with the
  detail-level cross-fade.
- The owner sent screenshots of Forces of Corruption's controls menu (five tabs), main menu, and
  skirmish setup, kept locally under Reference Materials/Screenshots/Original UI/. Every default
  binding is transcribed in compatibility-research.md (orders and camera are plain keys, abilities
  Ctrl, Shift, or Alt chords; an order key arms the next click, the owner's account). The owner
  chose to record them only: the sandbox's bindings change with the in-game UI and the player layer.
- The ocean resumed (the owner's call after research into game oceans: out/reports/Ocean waves in
  games.md, local). The owner decided waves stay on ocean maps, lakes and rivers need another
  approach, legacy maps keep the engine's table and a future map editor would mark water kinds by
  brush. The plan, in water-research.md: fading normal detail, FFT cascades, persistent foam, baked
  depth, the rest. Stage 1 (fading normal detail) was built and captured on Kamino; the fine speckle
  gave way to the broad layer's own larger patches, so it lands with stage 2 (the owner's choice).
  Stage 2 began with the spectrum (render/ocean_spectrum, JONSWAP/TMA, Hasselmann spreading, swell;
  default fetch 600,000 units for a peak wavelength near 300 and a significant height near 7, close
  to the Gerstner waves) and Pcg32::gaussian.
- The FFT ocean runs on ocean maps (stage 2 with stage 1): three cascades evolved and transformed on
  the GPU each frame (ocean.hlsl, d3d12_renderer_ocean.cpp), moving the water grid and making its
  normals; the normal maps no longer bend open sea. The spectrum's amplitudes were twice too large
  at first (caught by a new variance test). The vertex shader needed the scene texture table and the
  anisotropic sampler visible to all stages. GPU validation on the RTX 3070 is clean on Kamino and
  Utapau. On Kamino the lattice is gone; the default sea reads calm from above. The Gerstner sliders
  (length, speed, crest sharpness, crest foam) no longer act until the sea-state setting replaces
  them.
- The ocean's sea-state setting replaces the Gerstner sliders (step D): wind speed, fetch, swell,
  choppiness, height scale (now up to 2), calm and full depths, and the foam Jacobian threshold; the
  Gerstner waves and their constants are gone. The owner's tuned Gerstner values (length 2.5, speed
  2.7, crest sharpness 0.7, crest foam from 0.54) no longer apply; the calm and full depths (0, 10)
  carry over. water_wave_reach grew from 12 to 40 units for the FFT's heights.
- The owner noticed the FFT ocean had lost the blue. Measured on Kamino (mean color of open water):
  legacy normal maps 10/15/32, FFT 39/46/66. Neither the reflection nor the glint caused it: the
  body's key light did, since the normal maps' steep normals had kept the body dark and the flat
  wave normal faced the light. The open sea's body now takes the flat normal with a tenth of the
  direct key light (15/17/29) plus three times the wave's change in facing, which brings the swells
  back into view around the deep blue.
- Persistent foam (stage 3): each cascade's foam lives in the packed buffer between frames, halves
  every 2 seconds, and gathers where the cascade's Jacobian falls below the threshold; the drifting
  value-noise patches and the vertex's crest value are gone. A rough test sea (wind 60, choppiness
  1.4) showed a regular field of dark spots from the overview, likely the 263-unit cascade folding
  and repeating; recorded as open.
- The rough sea's dark spots were not folding (they stayed with choppiness 0) but the body's slope
  shading: an added term pushed lee slopes below zero, clipped to black. The shading now scales the
  body's color (1 plus eight times the change in facing, at least 0.35 of it), which keeps rough
  seas free of black and the default sea's swells visible.
- The owner judged the tuned FFT ocean still poor (tiling from the short-fetch sea's 67-unit
  cascade, low-poly up close, flat and grey far away, no screen-space reflections, implausible foam)
  and parked it: waves are off by default, so enhanced water is back to its look before the ocean
  work, and the code stays. The water will be researched again before it resumes.
- Heat meshes draw: the 3 stock MeshHeat uses (the bacta tank's glass, NB_BACTATANK.ALO, found by
  scanning every model) blend their distortion texture into the heat buffer in both heat modes; the
  enhanced heat formula moved into legacy_common.hlsli, shared by particles and meshes. The heat
  pass, now started by heat meshes alone, crashed on a frame without particles (a null particle
  buffer) until it skipped the particles there. The only MeshAdditiveReflection user found is
  W_BESPIN_REFINERY.ALO.
- MeshAdditiveReflection closed as inferred: its 3 uses are the Bespin refinery's lens panes, with
  only a Color and no texture, and the shader has no source; they already draw additive with that
  color, and a reflection would rest on the shader's name alone.
- Placed map decals surveyed: the one stock map is UM11_RAIDERS_OF_THE_LOST_HOLOCRON.TED; its 65
  decals carry a position, a size, and a small type index (3 on 64, 6 on one) but no texture, so
  they stay undrawn until a screenshot of the original shows what they are.
- Stealth meshes are no longer drawn: 9 FoC models carry a visible mesh named stealth (with LOD
  variants on Tyber and Urai Fen); the owner reports the stealth ability shows it in place of the
  model, so it is now kept out of the model's appearance, bounds, and footprint
  (ModelMesh::stealth). Battle hashes unchanged.
- The enhanced water's constants became Graphics-panel sliders (VisualWaterLook: clarity, shore
  fade, foam depth and strength, refraction, reflection distortion, glint strength and sharpness),
  their defaults the old constants; captures of Kamino and Manaan from the previous commit's build
  and this one are identical.
- Reading Shield_Flash_* from GameConstants moved onto the shield abilities: flash_shields is never
  called before them, so the values would reach nothing.
- Turret hardpoints turn (the 7 stock: the Gargantuan battle platform's turrets, a land unit, and
  the level-3 Underworld station's main cannon). Their fire bones live in the attached hardpoint
  model, so before this they fired all around from the attachment bone; now they are looked up
  there, the turret turns within its extent, the weapon fires once its turned cone holds the victim,
  and presentation yaws the attachment bone. Battle hashes unchanged (no reference battle has turret
  hardpoints). The duplicated profile_of lambda went onto the Code health list.
- Turret hardpoints kept turning (the owner's report: the Gargantuan's turrets turned about a degree
  and stopped). A turret whose victim was not the unit's own target found it only on the retarget
  beat and held still between beats, so it advanced one tick's turn per beat; it now remembers the
  victim it turns toward (hardpoint_turret_victim) and its weapon fires at it once the cone holds
  it. A probe of a Gargantuan battle showed the small turrets reaching their 40-degree limits.
  Reference battle hashes unchanged.
- Oriented footprints proposed (pathfinding-design.md 4.7): the owner reported the Gargantuan kept
  out of passages its hull fits, and chose oriented shapes over planning with the half-width or
  keeping circles; the design (capsules along the heading) is agreed, tight turns allowed to overlap
  blocked ground (the owner's choice).
- Oriented footprints, step 1: every unit's recipe and placement carry a FootprintCapsule from its
  model bounds (model_footprint_capsule; a soft radius stays a circle), not read by any system yet.
  Battle hashes unchanged.
- Oriented footprints, step 1 fixed: the previous commit (edd0e1a) changed three land battles'
  hashes though nothing reads the capsules, and was pushed without the battle check gating it (my
  slip). A capsule rebuilt through each placement's turned matrix differed in its last bits with the
  heading, keeping otherwise equal recipes apart, and recipe ids are hashed. A placement that keeps
  its type's size now shares the recipe's capsule (same_scale_tolerance, 1e-4 relative, an engine
  choice); the transform scale became one helper (model_transform_scale) for the three footprint
  functions. Battle hashes back to their expected values.
- Oriented footprints, steps 2 and 3: ground units stand and plan with their capsule's radius (half
  the narrow side), and spawns, teleports, and destinations test the whole spine at the unit's
  heading. A long unit now passes a gap its circle does not fit (a navigation test). The land
  battles' hashes change, as the owner expected (more direct routes): reference 0x6c4a94478db18f43,
  large Utapau 0x2241c93bca2fd517, AT-ATs 0x9b0c6e57b7f4bef7; space battles unchanged (space units
  keep their circle footing). A Gargantuan battle now ends with 43 kills, against 4 before.
- Oriented footprints, step 7 added (the owner): squads become oriented rectangles of their
  formation (width and depth, turned with the squad) for standing, planning, and arriving, the 2D
  rectangles the owner proposed earlier; infantry so far stays circles (a squad's extent).
- Oriented footprints redesigned on rectangles (the owner): the owner pointed out that steps 2-3 had
  in effect built the half-width option for moving units, and that a capsule's radius (half the
  narrow side) fails for wide units. The owner's scheme: oriented rectangles; plan with half the
  side orthogonal to the heading; both sides for placement; squads plan as before and collide and
  place as the rectangle bounding their soldiers in the squad's frame. My suggestions added: the
  collision mesh as the rectangle's source when present, and moving steps testing the rectangle at
  the heading. Section 4.7 rewritten; the capsule slice will be replaced.
- Oriented footprints on rectangles, steps 1 and 2 (the owner's scheme): FootprintRect replaces the
  capsule, from the collision meshes when a model has them, else its drawn bounds; ground units
  stand and plan with the half side orthogonal to the heading, and spawns, teleports, and
  destinations test the whole rectangle (ground_rectangle_fits; obstacle stamping shares its cell
  walk, for_each_cell_in_rectangle). A rectangle test while moving straight, my addition, was tried
  and dropped: the Gargantuan battle's blocked moves rose from 12 to 82. A wide unit now stays out
  of a gap narrower than its width (a navigation test). Hashes: reference 0x8573690d823ab40f, large
  Utapau 0x4641ee051dfb26f6, AT-ATs 0xb9b95ac4f5b1ebf1; space unchanged. The AT-AT battle's path
  nodes rose from 34,244 to 131,625 (its collision rectangle is narrower than its drawn one, 33
  against 44 wide, so it fits more ground); its tick 3 takes 19-21 ms, as it did before this change
  (measured on both builds), now on the roadmap.
- Turret hardpoints fixed after the owner's sandbox report (slow, freezing turrets that did not
  shoot at AT-ATs in their cone and fired without aiming). Three causes, found with probes: the
  Gargantuan has no TURRET behavior, so it counted as a body-aimer and its turret hardpoints held
  fire while it moved and off its heading (they now aim themselves); between retarget beats a turret
  whose victim left its arc dropped it and froze (it now keeps turning toward it); and the small
  turrets' 220-degree data cone on an 80-degree turret let them fire pointing up to 70 degrees away
  (a turret hardpoint now fires only while pointing at its victim, within 5 degrees, and chooses
  victims it can point at; an engine choice). The Gargantuan-AT-AT battle's shots fell from 176 to
  104; reference hashes unchanged.
- The owner's stuck AT-AT (no move from the start, animation frozen) is not a regression: a probe on
  the build before the oriented footprints shows the same walker turning in place for 2.3 s and its
  auto-move ending at tick 150 with the Gargantuan in range, so it never walks; what looks frozen is
  the in-place turn's presentation, left to look into.
- The owner's stuck AT-AT on Utapau (the sandbox's --auto-move, which sends everyone toward the
  camera's focus, Utapau's pit, so each unit goes to the nearest point it can reach): probes in the
  sandbox showed it turning in place with its turn clips, counted as held up (its speed is zero
  while it turns), and the detour code then stopped its move for good (stop_here) at the AT-AT in
  front of it, itself turning in place. A walker turning in place is no longer held up, nor a held
  mover to others. Verified in the sandbox (the AT-AT walks after its turn); a synthetic test of two
  turning walkers did not reproduce the stop without the fix and was dropped. Hashes: large Utapau
  0xb3175f7a915e4fe9, AT-ATs 0xeba641db42fd2ff4; the AT-AT battle's path nodes fell from 131,625 to
  33,379. The headless battle spawns at the map's open middle, the sandbox under the screen center:
  the owner's scenarios reproduce only in the sandbox.
- The Gargantuan's detour on Utapau measured: both planners (refined chunks and plain cells) find
  the same 5,300-unit route, because the gate beside its start reads 30-32 clearance against its
  37.3 footing, though about 90-100 units of clear cells wide: the 20-unit grid's squares understate
  diagonal gaps. Now on the roadmap for the owner's decision.
- Diagonal steps and navigation built at load (the owner: optimize, and build the grid on map load).
  The Gargantuan's detour on Utapau came from the corner rule: the gate's 42-clearance centre line
  runs diagonally between side cells of 32, under its 38, and a diagonal step needed both side cells
  at the full radius. Side cells now need half a cell less (diagonal_side_allowance, an engine
  choice) everywhere the rule lives: the cell search, the line checks, the region and chunk floods,
  chunk links at corners, the refined search's crossings, and ground_stands. Two faults found on the
  way: movement still demanded the full room where routes passed diagonally (1,925 routes and 882
  blocked moves in the AT-AT battle until ground_stands agreed), and the refined search crossed
  between parts only side by side, falling back to whole searches. Navigation data now builds at
  load: each new class's field had reset the data prepared before it, the reserved size was the old
  circle, and the first tick erased reserved fields. Measured against the last commit: the AT-AT
  battle's slowest tick 17 ms to 2.5 ms; the large battle's average 4.90 to 4.82 ms and 99th
  percentile 8.39 to 8.2 ms, nodes 445k to 519k; the Gargantuan's route 5,324 to 1,430 units.
  Hashes: large Utapau 0x862a172bd0c6c388, AT-ATs 0xabcafa1f187abf70; reference and space unchanged.
- The owner confirmed the Gargantuan now takes Utapau's gate; its turrets and its height above the
  ground still look wrong (left for later, on the roadmap).
- Oriented footprints, steps 3 and 4 (rectangles among units). Every spacing test between units now
  uses each unit's extent toward the other (footprints_reach) instead of two radii: placement, ORCA,
  keep_apart, giving way, detours, and the arrival rules; crushing takes what lies within the
  crusher's rectangle; squad soldiers keep their circles until step 5. FootprintRect moved from
  combat.hpp to footprint.hpp. The owner chose to profile the cost (option a): a temporary probe
  showed the same neighbour candidates (48M) and the extra time in keep_apart; after a reboot the
  260-unit battle measured 5.5 ms against 4.8 (14%), but 4.2 ms with avoidance agents forced to
  circles, so the cost is the closer-packed battle, not arithmetic, and the planned data packing was
  dropped; accepted. A navigation test covers crushing by rectangle and side-by-side placement.
  Hashes: reference 0xa6e9ed5caaf4c217, large Utapau 0xc4de3053589e738b, AT-ATs 0x8d8b36566a141c37;
  space unchanged.
- The sandbox's navigation overlay (the owner's request): one switch, G (the action `navigation`,
  `--show-navigation`, the scenario option `show-navigation`), draws every ground unit's footprint
  as the simulation measures it, every moving unit's route, and the whole map's passability; it
  replaces the passability switch, which drew crosses within 40 cells of the camera's focus. The
  passability is drawn as outlines between cells of different classes, so a whole map fits the
  renderer's 32,768 debug lines (Utapau 13,757, Kamino 4,651, Bespin 5,143); a cross on every cell
  would not. The snapshot carries each entity's footprint rectangle (not hashed). Saved scenarios
  with show-passability now warn about an unknown option.
- Planned with the owner, for after the oriented footprints: giving way ahead. When a route is
  planned, the allies standing along it are found through its large chunks and asked early to step
  out of the way, wait until the unit has passed, and return to their places (pathfinding-design.md
  4.4, roadmap).
- Oriented footprints, step 5 (the owner's squads): a squad is the rectangle bounding its soldiers
  in its frame (team_footprint, SquadState::body, refreshed with its shape), for keeping apart from
  other units and for where it may stop; it still plans where its largest soldier fits, and its
  soldiers keep their circles. The 260-unit battle averages 3.8 ms (5.5 with squads as circles).
  Group slots are not yet spaced by their members' sides. Hashes: reference 0x609e1d978e37739c,
  large Utapau 0xfd5d3ff726c03067, AT-ATs 0xd20fddd217b5561b; space unchanged.
- Oriented footprints, step 6: the path audit already plans per footing size bucket (2 to 68), the
  radius units plan with; its header comment named the old buckets (7, 20, 35, 65) and is fixed. The
  navigation overlay draws a squad as its bounding rectangle (the snapshot carries it). All six
  steps of pathfinding-design.md 4.7 are done; slot spacing by members' sides in group formations
  remains.
- Giving way ahead designed with the owner: idle allies along a unit's whole route are asked at once
  whether they could step aside (a dry run of make_way, with the lane chain); if any cannot, the
  unit detours around them at once (the owner's refinement); those that can step aside about 3 s
  before it arrives and return afterwards. Allies firing at an enemy hold their place (the owner's
  choice). The broad phase uses the avoidance grid along the route rather than the chunk graph,
  which knows ground, not units.
- Giving way ahead built (pathfinding-design.md 4.4): when a unit's route is set, the idle allies
  standing along it that could step to a free place (a dry run of make_way, which now takes the
  passer's place and facing and can answer without asking) are listed; each is asked when the unit
  is 3 s of travel away and returns after it has passed. Allies firing hold their place (the owner's
  choice); those that need a lane of allies are still asked on arrival, since asking a packed block
  ahead jammed its lanes. The owner's detour at once around allies that cannot step aside was built
  and measured, then left out: on the large Utapau battle 33 of 39 searches found no way (packed
  crowds close the passage), path expansions rose fivefold (142k to 683k) and blocked moves rose
  from 1,371 to 1,522; the decision is the owner's. Large battle with giving way ahead: 1,371
  blocked moves (1,385 before), 3.97 ms a tick (3.87). Hashes: reference 0x235cc2a1e4f4ec23, large Utapau
  0x7f620bed5af59350, AT-ATs 0xb9f8985ae0fbca7a; the space battles 0x6ceb71e324afdd9d and 0xca4b22c9901874b8 change only because
  the new route state is hashed for every entity.
- The owner decided to keep giving way ahead without a detour planned ahead: units detour when they
  find the way closed (plan_detours). The roadmap item for a detour ahead is removed.
- Route overlay (the owner's request): a route is drawn for every unit that has one, moving or held
  (before, a held unit's route vanished), with every point marked by its kind from the new
  SimulationWorld::route_points (waypoints, avoidance detour point, move target, formation frame
  waypoints, final slot); route() is built on it. Battle hashes unchanged.
- Research of the stock XML for formation, navigation, and movement (the owner's request), recorded
  in xml-object-tags.md: the movement block of GameConstants.xml (about 75 constants, meanings
  inferred from names), the Formation_Priority ladder by role, the faction look of squads in their
  members' formation tags, and the team containers' bodiless movement profiles; three stale passages
  saying the engine skips the team step are corrected. A company that names a Create_Team_Type
  without a Max_Squad_Size (13 stock companies) got one team entity but no team slots for its
  members (assign_teams read the missing size as no teams); it is now one team throughout
  (TeamLayout::container; an engine guess, the original's default unobserved). Battle hashes
  unchanged.
- Detailed route overlay (the owner's request; the owner chose the 16- and 32-cell chunk grids and
  every stage and change): searches fill a PathTrace (the 32-cell and 16-cell chunk routes, the cell
  path before smoothing) while NavigationData traces, and the simulation records every change to a
  ground unit's route (plan, share of a group route, repair leg, detour, formation frame) with its
  stages and the whole route after it, up to 8 per unit and frame, recorded only in the serial code
  that applies results and never hashed. The G overlay draws the chunk grids and the selected ground
  units' records. The debug line helpers moved into one Pen used by the route drawing too. A test
  confirms tracing leaves the state hash unchanged; battle hashes unchanged.
- The owner's observations of group moves in the retail game recorded in compatibility-research.md
  (Group moves of vehicles): ten T2-Bs form two formations of five, each three in front and two
  behind in the gaps; the nearest tanks take the front row; narrow passages break the formation,
  which forms again after with the leaders slowing. The engine differs in its row width (five stand
  four and one) and keeps the formation through narrow places.
- Group movement research (the owner's sources: three papers kept locally and a set of web pages)
  written up as docs/group-movement-research.md: the problems (contention at shared points, route
  churn and oscillation, differences from the original), each source's findings, the
  recommendations, and the order of work agreed with the owner (measure first; then no re-plans for
  ally pushes, side commitment, chosen velocities into ORCA; then the RORCAL symmetry breaker; then
  the formation layer). Found while reading: ORCA is fed realized, clipped velocities instead of
  chosen ones. The state before this work is tagged baseline-2026-11-17.
- Movement measurements (step 1 of the group movement work): the simulation counts route changes by
  kind (Stats::route_changes: plan, shared, repair, detour, frame; never hashed); headless battles
  print a Movement line with reversals (a 10-degree swing undone within a second), stalled
  unit-seconds, and the route changes; EAWEngine movement-scenarios runs five synthetic group moves
  without a game installation. Battle hashes unchanged. The baseline numbers are in
  group-movement-research.md: two groups of five crossing take 31.7 s (about 5 s alone); a T2-B
  probe showed the owner's deadlock, standing still with its heading flipping 10 degrees.
- Branch movement-redesign (the owner's decision: redesign movement as a whole): from the baseline
  tag with the movement measurements, ground movement is stripped to route planning (A* over the 16-
  and 32-cell chunks), clearance, wall margins, and oriented footprints, plus the hard no-overlap
  rule (keep_apart) and crushing. Removed: ground avoidance and the ORCA library, giving way and
  giving way ahead, detours, repair legs, the arrival rules among others, formation frames, land
  group slots and cohesion (a group order sends every unit to the point); space keeps its avoidance,
  formations, and cohesion. Tests of removed behavior are removed (151 pass); two that test kept
  behavior (route tracing over chunks, footprints through a gap) became tests of their own. All five
  battle hashes change (the removed route columns were hashed): reference 0x7b84de93d99224c6, large
  Utapau 0xf4eafde45822c642, Kuat fighters 0x44a0aee571affa51, Kuat capital 0xd3c0c7aae986e0e0,
  AT-ATs 0xa1945a44269bd2d0.
