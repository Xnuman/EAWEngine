# Compatibility research

The bootstrap uses public documentation, black-box behavior, compatibility
corpora built from the user's installed data, and an explicitly isolated
owner-provided Direct3D 9 shader reference under `Shaders/`. It does not use
leaked source code or decompiled code copied from the original executable, and
the engine never modifies or redistributes the installed runtime data.

The shader corpus is evidence for parameter, transform, and render-state
semantics only. It is git-ignored, never distributed, not compiled or shipped, and the new D3D12 shaders are
independently written against engine-native material contracts. Source checkouts
without `Shaders/` must still configure, build, test, and run. No other original
game source, binary, or asset may be added without explicit owner authorization
and provenance review. Because contributors can consult this corpus, project
documentation must not characterize the renderer as strictly clean-room.

Primary starting points:

- AlamoEngine-Tools organization: <https://github.com/AlamoEngine-Tools>
- PetroglyphTools format libraries: <https://github.com/AlamoEngine-Tools/PetroglyphTools>
- Petroglyph game/mod discovery: <https://github.com/AlamoEngine-Tools/PetroglyphGameInfrastructure>
- XML schema project: <https://github.com/AlamoEngine-Tools/eaw-schema>
- Stock-data baseline index: <https://github.com/AlamoEngine-Tools/eaw-baseline>
- Petrolution file-format index: <https://modtools.petrolution.net/docs/Formats>
- MEG v1 format: <https://modtools.petrolution.net/docs/MegFileFormat>
- Shared chunk format: <https://modtools.petrolution.net/docs/ChunkFileFormat>
- Petroglyph Lua object format: <https://modtools.petrolution.net/docs/LuaFileFormat>

Observed local Steam layout on 2026-11-06:

- root application ID: 32470;
- base runtime: `GameData`, expansion runtime: `corruption`;
- both use `Data/megafiles.xml`, loose `Data` subdirectories, main MEG archives,
  patch archives, nested audio MEGs, DAT localization, and 64-bit executables;
- the verified archives begin with the MEG v1 filename/file counts and use
  length-prefixed filenames plus 20-byte file records.

The current compatibility policy mounts manifest archives in their declared
order, then lexically ordered unlisted archives, `Patch.meg`, numerically
ordered patch archives, and finally `64Patch.meg`. Loose files override
archives; FoC overrides inherited EAW data; and mod arguments are ordered with
the first mod at highest priority. A synthetic M1 corpus locks this contract,
including fallback and case/separator normalization. Black-box comparison with
the legacy runtime remains the authority if an observed edge case differs.

Open research questions must be resolved with small black-box tests and recorded
fixtures: Lua VM and coroutine semantics; XML value/reference rules beyond the
public schema; floating-point and tick rules; tactical pathing; random-number
streams; save/replay structure; and network determinism.

The first XML compatibility corpus now parses all 638 resolved stock FoC/EAW
documents. It confirms that Petroglyph data may contain multiple top-level
elements and that three FoC AI template files use `<?xml ...>` without the
standard closing `?`. The reader accepts that observed declaration form while
still rejecting mismatched and unterminated elements. Type/tag value validation
will consume the public `eaw-schema` registry as a separate layer over the
lossless syntax tree and typed-name index.

The schema loader consumes a local checkout rather than downloading definitions
at runtime. It reads the type/tag/enum/hardcoded/metafile/kind YAML subset used
for validation, layers FoC additions over EAW, and follows file-registry
references through the resolved VFS. With the public schema checkout observed
on 2026-11-07, canonical element names and metafile registries authoritatively
type 10,977 of 21,283 named stock definitions. Conservative, unambiguous
structural matching raises validation coverage to 20,881 definitions while
leaving 402 untyped.

Typed XML-object references distinguish 145 missing names from 34 existing
targets that do not satisfy their declared schema type or object kind. Kind
predicates use effective `Behavior` and boolean flag values inherited through
`Variant_Of_Existing_Type`, plus reverse membership fields. The residual cases
remain compatibility-research inputs: public schema declarations, structurally
inferred types, and intentionally permissive shipped data can each require
further evidence before a warning becomes an engine rule.

The texture corpus observed on 2026-11-07 contains 2,213 resolved DDS files.
The semantic decoder validates all of them: 168 BC1, 181 BC2, 1,656 BC3, 37
24-bit BGR, 169 32-bit BGRA, and two 32-bit BGRX textures, with 17,854 mip
levels in total and a maximum extent of 2048 by 2048. No stock cube or volume
textures are present, but their DDS metadata and subresource layout are handled
for mods. The FoC command-bar mega-texture directory contains 1,107 named atlas
rectangles, 997 with alpha, and resolves to its 2048-square compressed DDS.

Across 1,379 semantic stock ALO models, 9,294 material instances use 35 shader
names and seven texture-parameter names. The compatibility representation keeps
the source shader and all raw parameters, then adds conservative family, blend,
and texture-role hints. This separation allows the future renderer to improve a
shader translation without changing the lossless asset decoder. Reference
profiles derived from the legacy shader corpus now type 7,808 of those 9,294
instances (31 of the 35 names); `alDefault.fx`, `Default.fx`,
`MeshAdditiveReflection.fx`, and `FissureMeshGloss.fx` have no reference source
and remain name-heuristic. The corpus corrected earlier guesses: shields and
nebulae are additive and two-sided, heat meshes target a distortion buffer,
planet cloud maps are layers rather than base/normal maps, and team-color masks
always come from base-texture alpha. Details and remaining uncertainty are in
[Legacy shader mapping](legacy-shader-mapping.md). Fourteen of
1,286 unique model texture references remain absent from the final layered VFS
and are reported with example source models.

The final layered FoC/EAW script corpus contains 370 Lua source files totaling
85,312 lines; current 64-bit patch archives override any older compiled copies
visible at lower layers. Lexical inspection finds 19 dependency spellings, all
resolved to final VFS winners, 1,725 defined function names, and 3,364 called
function names without warnings or parse failures. The resulting conservative
host-call inventory is compatibility evidence rather than a promise that every
name is engine-provided: object methods and functions assigned dynamically can
only be classified once runtime types and execution behavior are modeled.

Mod bytecode inspection follows the published Lua 5.0 structure without
executing code. Standard chunks use the `Lua` signature and version `0x50`;
Petroglyph chunks use `Lup`, version `0x51`, and add a persistent-function
integer after `lineDefined` in every prototype. The parser validates native-size
metadata, strings, constants, nested prototypes, instruction ranges, and the
Petroglyph IDs with explicit resource limits. VM execution, coroutine scheduling,
save-state persistence, and host API behavior remain black-box research areas.


TED placed objects reference their XML object type by CRC-32 (the same
reflected IEEE polynomial as DAT keys) of the ASCII-uppercased type name. The
stock audit observed on 2026-11-07 confirms it: in FoC, 191,048 of 191,083
placed objects across all 269 maps match a named XML definition, and 99.7%
resolve through `Variant_Of_Existing_Type` inheritance to an existing
`Space_Model_Name` or `Land_Model_Name` model (649 unique models). The base EAW
profile matches 137,488 of 137,499 objects. Eleven FoC CRCs (35 objects) match
no definition, and one placed object has a name shared by several element types;
the resolver prefers the definition with a model for the map context, which is
inference. Placed-object rotations are stored in degrees: across the FoC
corpus the third component (heading about the up axis) is non-zero for 165,199
objects and ranges over -611..360, while the first two are non-zero for only
2,067 and 4,488 objects. The engine composes rotations about X, then Y, then Z;
that order and the rotation direction are inference until compared with the
original game. Squadrons, companies, and teams have no model of their own and must
be expanded into their member units. The audit indexes every XML file under
`Data/XML`, not only files registered by the game's object-file manifests.

Space squadrons place no model of their own. Their `Squadron_Units` field lists
member types, and repeated `Squadron_Offsets` fields give each member's offset
in the squadron's local frame. Some stock definitions repeat `Squadron_Units`;
the engine uses the last occurrence, which is the ordinary override rule but is
not yet confirmed against the original game. Across the FoC corpus, 327 placed
squadrons expand into 1,026 member ships. Land teams and companies list members
(`Company_Units`, containers with `ContainerArrangement`) without offsets, so
their layout is runtime formation behavior; the 247 placed instances are counted
and left unexpanded until the simulation owns formations.

TED environment chunk 8 stores the active-environment index as a mini-chunk
(type 0x25, four bytes), not as a raw integer; reading it raw produced 1061
(0x0425, the mini header) for every map. Decoded correctly, all 269 FoC and 176
EAW maps name an index inside their environment list (0 to 5). Each environment
names primary and secondary sky domes; those names are XML objects
(`SpacePrimarySkydome`, `SpaceSecondarySkydome`, `LandPrimarySkydome`, ...)
whose `Space_Model_Name`/`Land_Model_Name` supplies the model. The definitions
mark them `In_Background` decorations, and primary sky domes carry the
`SKY_DOME` behavior. The engine therefore draws them camera-centered behind the
world, primary first; `Scale_Factor` (1 for primary, 25 for secondary) and any
parallax the original game applied are not yet modeled.

Every stock TED environment (624 across both games) stores the same lighting
block in mini-chunks 0x00-0x0d: RGB colors for a key light and two fill lights
(0x00-0x02), a specular color (0x03), and an ambient color (0x04); intensities
(0x05-0x07); azimuths in radians (0x08-0x0a, 0 to 2*pi); and elevations in
radians (0x0b-0x0d, about -pi/2 to pi/2). Keys are typically white and above the
horizon, while fills are tinted and usually below it. This matches the legacy
shaders' engine-supplied key light plus a fill term derived from lights. The
engine treats each light's direction as pointing from the surface toward the
light and scales colors by intensity; the axis convention, and scaling the
specular color by the key intensity, are inferences. The remaining fields
(apparently fog color and range, wind, and cloud settings) are preserved raw.

Faction definitions carry a 0..255 RGBA `Color`. A placed object's owner is a
player slot chosen at runtime, so previews color team-color masks with the
type's first `Affiliation` faction instead; types without an affiliation fall
back to a player-slot palette.

TED terrain samples (terrain chunk child 5, four bytes per vertex, row-major)
pack more than elevation. Bits 0-15 are the signed elevation. Bits 16-23 are a
per-vertex terrain layer index: across 894,816 sampled vertices in 188 stock land
maps it is always below the map's layer count, and horizontally adjacent
vertices share it 77.5% of the time. Bits 24-31 are a small signed value (mostly
-1, 0, or -2) whose distribution does not change at layer boundaries, so it is
not a blend weight; it is preserved raw. The engine places vertices 20 world
units apart (column to +X, row to +Y) with world height = elevation / 20: under
that mapping, placed objects on 135 land maps sit a median 1.2 world units from
the interpolated ground. Terrain layers carry a tiling scale in mini 0x06 (about
64 world units per repeat in most stock layers), the color and normal textures
(0x0c, 0x13), and further fields not interpreted yet (angles in 0x08/0x09, RGB
colors in 0x0a/0x0b, flags, an integer). The public Petrolution format pages
were unavailable (HTTP 404) when this was researched, so these are corpus
findings rather than documented facts.

The terrain-information chunk describes one liquid surface per land map. Mini
0x16 is its type: across 188 stock land maps, 0 (41 maps) pairs with dry worlds
such as Tatooine and Kessel, 1 (127) with water worlds such as Kamino, Mon
Calamari, and Dagobah, 2 (11) with lava textures on Mustafar, Sullust, and Shola,
and 3 (9) with ice textures on Ilum, Polus, and Alzoc III. Mini 0x15 is the
surface height in world units: Cloud City's -150 lies below all of its terrain,
while Kamino's 7 covers about 60% of the map. Mini 0x1a is an RGB tint, and minis
0x1d-0x1f name textures whose roles depend on the type (for water, 0x1d is a bump
map and 0x1e a plain-color fallback; lava and ice store their color texture
first). Further fields (apparently opacity, tiling, and scroll speed, plus cloud
texture, scale, and wind) are preserved raw; the engine's water tiling and scroll
speeds are its own choices until they are measured.

Terrain tracks (terrain chunk child 9, chunk 0x100 each; 553 in 31 stock land
maps) are textured ribbons: rivers, shoreline waves, station lights, beams,
girders, and scrolling text. Chunk 0x102 lists control points (mini 0x07, world
X/Y with Z = 0), 0x104 a width per point (mini 0x08; every stock track has one
width per point), and 0x103 the texture. Chunk 0x105 holds three values that are
(1, 1, 1) on 391 tracks and (0, 1, 0) on 151, read as opacity at the start,
middle, and end. Header minis 0x04 and 0x05 are small values on wave tracks, read
as texture scroll per second. The remaining header fields (0x00 is 10 on every
track, 0x0d ranges 0-181, two flags) are preserved raw. Terrain decals (child 6)
occur in one stock map: 65 entries with a position, a 15x15 size, and small
integers, but no texture name, so they are not rendered.

ALO models encode alternate appearances in mesh and bone names. A trailing
`_ALT<n>` marks a damage state: NB_BASESHIELDGEN, for example, carries
`base_ALT0`-`ALT3` and `NB_dish_ALT0`-`ALT3`, all visible in the bind pose, and the
original shows one of them according to health. Across 1,379 stock models, 158
use ALT meshes (index 0 on 557 meshes, 1 on 530, 2 on 418, 3 on 267, 4-12 rarely),
so ALT0 is read as the intact state. A trailing `_LOD<n>` marks a detail level:
145 models use them, and summed triangles by index are 68,881 (LOD0), 129,068
(LOD1), 123,516 (LOD2), and 12,315 (LOD3, five meshes). Per model, LOD0 is the
sparsest level in 137 of 145, and the densest is usually the highest index, so
higher indices are read as more detailed. The engine's default appearance
therefore shows ALT0 and each model's highest LOD index. Switching on damage or
distance is runtime behavior and is not modeled yet. These are corpus findings.

Many terrain layers name no color texture (3,369 stock layer entries; their mini
0x14 is always 0, while textured layers use 1-16). They are painted widely: 40 of
135 stock land maps use them, 39 above the liquid level. The Bespin maps paint
63% (`_LAND_PLANET_BESPIN_01`) and 79% (`UM07_CLOUD_CITY_CONFLICT`) of their
samples with them, with the liquid level below all
terrain, and Kamino paints its ocean floor and platform slopes with one. The
engine reads such a layer as covering nothing: terrain is not drawn where
untextured layers dominate, so the sky and background show through. This is an
inference from those maps' intended look (a city floating over clouds, platforms
over the sea), not a measured rule. The cutout follows the 20-unit sample grid.

Cube reflection is rare in stock content. `MeshBumpReflectColorize` and
`RSkinBumpReflectColorize`, the only reference shaders that sample the engine's
sky cube, are used by no stock material, and `MeshAdditiveReflection.fx`, which
has no reference source, by three. The reference math is recorded in
`docs/legacy-shader-mapping.md`: a world-space reflection of the view direction
about the vertex normal, sampled from an engine-supplied cube and scaled by
`Specular` and normal-map alpha. No stock cube textures exist, so the original
presumably builds the cube from the sky. Implementation is deferred until a mod
needs it.

Camera and input in the original game (owner observation, not yet measured): the
tactical camera keeps a fixed pitch and heading and cannot rotate; the player
zooms in and out and pans with the keyboard. As in most RTS games, the left mouse
button selects units, by click or by dragging a selection rectangle, and the
right mouse button issues move and attack commands. Exact angles, zoom limits,
pan speeds, and per-map camera settings still need evidence from the shipped
data or black-box observation before the legacy camera is implemented.

TED passability (root chunk 0x10a, stored X-major) holds one of six values per
cell across all 269 stock maps; each sample has exactly one bit set, so the
values behave like an enumeration. Cross-tabulated with terrain: 0x1 (52% of
cells) is flat ground (mean slope 2.5 units per 20-unit cell); 0x2 (40%) is
steep, void, or deep ground (mean slope 12.3; 16.6% below the liquid, 11.7%
untextured); 0x20 (3%) lies 95% below the liquid surface and reads as water;
0x100 (4%) is steep and often untextured; 0x80 and 0x4 are rare and flat. Kamino
marks half of its flat seabed 0x1, so the values are authored, not derived from
geometry.

The values name the passability types of `Data/XML/MovementClassTypeDefs.xml`,
whose entries list, per movement class, the types it may enter (for example
`Infantry` is `Clear | Shield` in EAW and `Clear | Shield | InfantryOnly` in
FoC; `Hover` adds `Water`; `Flying` and `Space` allow nearly everything). 0x80
and 0x100 occur only in FoC's `maps.meg`, the expansion that added
`InfantryOnly` and `Impassable`, so they are those types; 0x1 is `Clear`, 0x2
`Steep`, 0x20 `Water` (from the geometry above). 0x4 is read as `Obstacle`, the
remaining static base-game type, with medium confidence. `Shield`, `RebelWall`,
and `EmpireWall` are applied at runtime by objects (`OverridePassability`,
`SecondaryOccupationPassability`) and never appear in stock rasters; the engine
gives them provisional bits. Each ground entity gets the mask of its XML
`MovementClass` (Clear | Shield when the class is missing or unknown) and stops
before a cell outside it; one standing on such a cell may still leave. No
official documentation of the raster was found (the public Petroglyph mod-tool
pages and community guides describe painting passability in the map editor, not
its encoding); comparison with the original map editor remains the check.

Whether a unit walks on land-map terrain follows its XML `Space_Layer`: `Land`
for every ground vehicle, infantry, and land hero type with a speed, `None` for
walkers such as Ewoks and R2-D2, and `Frigate`, `Capital`, or `Corvette` for air
units such as snowspeeders and Kamino's air whales (`Kamino_Aiwha`, which also
has `Hover_Offset` 250 and `Layer_Z_Adjust` 100). Types without the field
(birds, space-only types, projectiles) are treated as not walking. Spawned
flyers start at the surface plus `Hover_Offset`. This is a corpus inference.

Pathfinding: no public source documents the algorithm the original uses to move
an individual unit. The owner-provided Petroglyph engine reference (GlyphX
reference code; read-only evidence, not quoted here) does not contain the
unit-level pathfinder, but it shows a separate AI layer: land task forces plan
over a graph of AI hint zones with a threat-weighted cost and the task force's
passability mask, then issue ordinary moves toward each zone's center; galactic
mode has its own pathfinder between planets. The original therefore plans on at
least two levels, but its cell-level behavior is unknown. EAWEngine's grid A* is
an engine-native choice, not a reproduction; behavior that players observe
(which ground a class may cross, how unreachable orders end, how groups move)
is what needs black-box comparison with the original, not identical routes.

TED owner slots (placed-object `player`, audited with `EAWAssetInspector
map-objects`, which now breaks placements down by owner): slot 3 holds 179,407
of the stock placements, almost all props, markers, decorations, and
multiplayer structure markers, so it reads as the world's neutral slot. Slots
0, 1, 2, and 6 hold armies, bases, and squadrons (slot 6 also many props);
slots 4, 5, and 7 hold mostly `Indigenous_Unit` types; slots 9 to 11 occur on a
few maps. Which slot a human or AI player takes, and which slots are allied, is
decided at runtime. Until players and factions exist the engine treats slot 3
as neutral and every other pair of distinct slots as hostile (a provisional
rule).

Combat fields used by the first combat slice (stock XML; semantics provisional
where noted): `Tactical_Health`; `Shield_Points` and `Shield_Refresh_Rate`
(read as points per second, unverified); `Armor_Type` and `Shield_Armor_Type`;
`Ranged_Target_Z_Adjust` as the aim height; the first entry of
`Projectile_Types` with `Targeting_Max_Attack_Distance`,
`Projectile_Fire_Recharge_Seconds`, `Projectile_Fire_Pulse_Count`, and
`Projectile_Fire_Pulse_Delay_Seconds`; and from the projectile type
`Projectile_Damage`, `Damage_Type`, `Max_Speed` (scaled like unit speeds),
`Projectile_Max_Flight_Distance`, and the shield and hit-point damage flags.
`GameConstants.xml` lists `Damage_To_Armor_Mod` entries as
`damage type, armor type, multiplier` (for example `Damage_Sniper,
Armor_T2B, 0.1`). Shields absorb a shot first through the shield armor type and
the remainder reaches the hull through the armor type: an engine choice, not a
measured rule. Ranges are measured in the ground plane. Weapons on hardpoints
(space ships), `Targeting_Fire_Inaccuracy`, turret target priorities, and
energy are not read yet.

Movement fields used by the group-movement work.

- **`Max_Rate_Of_Turn`** (442 stock uses) is read as degrees per 30 Hz frame,
  the same convention as the ×30 `Max_Speed` scale. Infantry 16 becomes 480
  degrees per second, tanks 12-20, and capital ships 0.4-1.5 become 12-45
  degrees per second. 0.0, the most common value, turns instantly.
  Provisional until compared with the original.
- **Footprints.** Units turn at that rate and travel more slowly while facing
  away from their way. Their separation footprint is a quarter of the summed
  X and Y extents of the model bounds (an engine choice). The original's
  collision meshes and `UnitCollisionClass` are not read yet, so only mobile
  units take space; structures and props still overlap units.
- **Formation fields** sit on unit types, often inherited from shared
  `Container` base types through `Variant_Of_Existing_Type`:
  - `FormationSpacing`: mostly 0.5.
  - `FormationOrder`: 1-8.
  - `FormationRaggedness`: 0-2.5.
  - `FormationGrouping`: Standard 67, Solo 13.
- **Companies** list their members in `Company_Units`; for example
  `Rebel_Infantry_Squad` lists 15 `Squad_Rebel_Trooper` with
  `Max_Squad_Size` 5.

Companies on maps. A placed `GroundCompany`, `HeroCompany`, or `SlaveCompany`
is its `Company_Units` members even though most stock companies also name a
`Land_Model_Name` (for example `RI_INFANTRY.ALO` on `Rebel_Infantry_Squad`). That
model appears to be a galactic or GUI representation; tactical companies
always appear as their members. Counting that way, the stock maps hold 4,665
company members.

The data gives no member offsets, so the engine arranges each company in its
group-move formation, facing the placed heading (an engine choice). Type-name
lists are split on commas and whitespace, because one stock FoC list
(`Underworld_Merc_Squad`) has no commas. 97 placed `Container` objects have
no member list and stay unexpanded.

Static obstacles. `Empire_Wall` and the matching Rebel wall (the
`EB_/RB_BASEWALL-MID` barricades, 82 placements on Utapau alone) set
`OverridePassability` to EmpireWall or RebelWall. The movement classes that
list those types (Flying, LargeWalker for RebelWall, Space) may cross them. The
engine stamps a wall's footprint with its own type and every other blocking
structure with Obstacle.

Blocking structures are the `GroundStructure`, `SpecialStructure`, and
`SecondaryStructure` kinds, plus types whose `UnitCollisionClass` is High,
Medium, or Low Obstacle. Build pads and other props do not block. This is a
provisional reading, as are the footprints, which are rectangles from the
model bounds until the ALO collision meshes are interpreted. Unit separation
uses `Custom_Soft_Footprint_Radius` where present (3.5 for stock infantry).

Space weapons and squadrons.

- **Weapon hardpoints.** A ship's weapons are its `HardPoints` whose `Type`
  is a weapon (stock: 174 `HARD_POINT_WEAPON_LASER`, 37 ion cannons, 35
  missiles, 26 torpedoes). Each names `Fire_Projectile_Type`,
  `Fire_Range_Distance`, `Fire_Pulse_Count`, `Fire_Pulse_Delay_Seconds`, a
  recharge range (`Fire_Min/Max_Recharge_Seconds`), a fire cone
  (`Fire_Cone_Width/Height`, mostly 160-180 degrees), and sometimes a
  `Damage_Type`, which the engine lets override the projectile's.
- **Recharge.** The engine uses the midpoint of the recharge range. The
  original presumably draws within it at random; a seeded deterministic
  stream will replace the midpoint.
- **Not used yet.** Fire cones (they need the hardpoint bones' orientation),
  `Fire_Inaccuracy_Distance` by target category, and hardpoint `Health`,
  `Is_Targetable`, and `Is_Destroyable`.
- **Squadron lists.** A repeated `Squadron_Units` continues the list:
  `Rebel_X-Wing_Squadron` writes "X-Wing, X-Wing" twice and "X-Wing" once
  beside five `Squadron_Offsets`. The layout used to take only the last
  entry; the stock maps hold 1,474 squadron members, not 1,026.

Model forward axis. Alamo models face -Y. Every stock muzzle and fire bone
inspected sits at negative Y and points along -Y in the bind pose:
- T2B, AT-ST, and X-wing `Muzzle*` bones;
- the Star Destroyer's `FP_*` fire bones (front hardpoints at y = -280,
  engines at y = +183).

The engine therefore defines an entity's heading as the direction of its
model's -Y axis. Before this, moving units faced 90 degrees off their travel
direction. A fire bone's X axis is its firing direction (`FP_F-L_00` points
forward-left, (0.57, -0.82)). `EAWAssetInspector inspect` now prints each
bone's bind-pose position and X axis.

Hardpoints as parts.

- **What the engine reads.** Each `HardPoints` entry is a part: its kind from
  `Type`, its hit points from `Health` (indestructible when `Is_Destroyable`
  is No or `Health` is missing), and `Is_Targetable`. Its position is the
  ship model's `Attachment_Bone` in the bind pose. A weapon hardpoint fires
  from `Fire_Bone_A`, within `Fire_Cone_Width`, centered on that bone's X
  axis.
- **Engine choices, not measured from the original:**
  - each weapon picks the entity's target when that target is in its range
    and arc, otherwise the nearest hostile within them;
  - shots aim at the victim's nearest live targetable part;
  - the hull and the hit part both take the damage (the original's split is
    not established);
  - losing every shield generator stops shield recharge;
  - losing every engine slows the ship to a quarter speed.
- **Not used yet.** `Fire_Cone_Height` and the second fire bone
  (`Fire_Bone_B`).

Unit animation clips. Animations sit beside their model as
`<MODEL>_<ROLE>_<NN>.ALA`, NN numbering variants (the Dark Trooper has
ATTACK_00 to _02, IDLE_00 and _01, DIE_00 and _01, and more). Across the 2,408
resolved stock ALA files the most common roles are IDLE (539), DIE (301),
ATTACK (160), MOVE (158), ATTACKIDLE (108), CINEMATIC (77), BUILD (64), LAND
(55), WALKMOVE (53), TALK (43), FLY (40), and TURNL/TURNR (35 each), with
crouch, flinch, deploy, take-off, and partial-turn variants. The engine uses
IDLE, MOVE, WALKMOVE, ATTACK, and ATTACKIDLE so far; which clip the original
plays when (and whether XML overrides names) is not established, so the
selection rules in `unit_animation.hpp` are engine choices.

- **WALKMOVE and MOVE.** All 30 stock models with WALKMOVE clips also have
  MOVE clips (infantry and heroes: Dark Troopers, stormtroopers, Vader, Mara
  Jade, Gungans, Noghri, ...), so WALKMOVE reads as the walk and MOVE as the
  run; about 73 more models (mostly vehicles) have MOVE alone. RUN occurs on
  4 models only (two animals and two Force-power variants).
- **Who chooses animations.** The stock Lua scripts have one animation
  function, `Play_Animation`, with 79 call sites, all on named story,
  mission, or cinematic objects (heroes in cutscenes, doors, alarms, a
  shuttle, `Intro_Animation`); no script chooses idle, movement, or attack
  animations or controls blending (`EAWAssetInspector lua-audit`). Everyday
  selection is therefore the engine's job, and scripts only force a clip on a
  scripted object. All 370 stock scripts are Lua source, so the calls can be
  read: `Play_Animation(type, flag[, variant])`, where `type` names a clip
  role in title case with underscores ("Idle", "Talk", "Attention",
  "Cinematic", "Talk_Gesture", "Choke_Die"), matching the file role with the
  underscores removed (TALKGESTURE), and `variant` (0 or 1 where given) picks
  the `_NN` file. The flag is true for held poses ("Attention", "Idle" in a
  loop) and false for one-shot gestures ("Talk", "Choke"), so it reads as a
  loop flag; that reading is an inference until the original is observed.
- **Blending.** Whether the original crossfades or cuts between clips is not
  established. The engine blends by choice (see `unit_animation.hpp`).

Firing while moving and facing. Stock infantry list a walking locomotor
(`WALK_LOCOMOTOR`, or `LAND_TEAM_INFANTRY_LOCOMOTOR` for squad members),
`IDLE`, `TARGETING`, and `WEAPON` in `LandBehavior`, while vehicles (T2-B,
AT-ST) add `TURRET`. In play (owner observation, Forces of Corruption) infantry
turn to face their target and do not fire while moving, and some stormtroopers
crouch while firing. Related XML: `Is_Stationary_When_Attacking` (12 uses, 11
Yes: Jedi, Vader, Palpatine, the Elite Guard, which stop to attack),
`Turn_To_Face_Unblockable_Shots` (52, all No), `Must_Face_Target_To_Attack_When_Deployed`
and `Fire_When_Deployed`/`Fire_When_Undeployed` (deploying units),
`Crouch_Move_Fire_Angle_Cutoff` (20, once). The engine reads the missing
`TURRET` as "aims with its body": such ground units face their target before
firing (within 20 degrees) and hold fire while moving; turreted units fire on
the move. This is an inference from the behavior lists and the observation; the
the original's exact rules (turn speed while aiming, crouching, the stationary
flag's effect on movement) are not established.

Crouching (the SPREAD_OUT ability; deferred to the abilities work in M4).
Infantry and their squads and teams (Rebel_Trooper, Stormtrooper, Plex_Soldier,
pirates, field commanders, Rebel_Infantry_Squad, Imperial_Stormtrooper_Squad,
...) carry a `Unit_Abilities_Data` entry of `Type` SPREAD_OUT (its sound events
call it "Take Cover"). Rebel_Trooper's has `Mod_Multiplier` entries
`SPEED_MULTIPLIER, 0.5f` and `TAKE_DAMAGE_MULTIPLIER, 0.5f`; GameConstants.xml
sets `Spread_Out_Spacing_Coefficient` 1.8 and the posture blend times
`CrouchIdleWalkBlendTime` 0.5, `CrouchMoveBlendTime` 0.75, and
`TeamCrouchMoveBlendTime` 0.5; infantry types set `Crouch_Animation_Speed`
(0.537 for Rebel troopers and stormtroopers, 17 uses). Clips: CROUCHIDLE (23
files), CROUCHMOVE, CROUCHTURNL, and CROUCHTURNR (13 each), and no crouched
attack clips. Observed in play (owner, Forces of Corruption): the ability
toggles a whole squad; it changes idle and walking animations but not combat
animations. Planned reading: a toggle that halves speed and damage taken,
spreads the formation by 1.8, swaps IDLE and MOVE for CROUCHIDLE and CROUCHMOVE
(played at `Crouch_Animation_Speed`) with the XML blend times, and keeps the
attack clips. Unknown: how the multipliers combine with others, how the
spacing coefficient applies, and when CROUCHTURN plays (probably turning in
place).

Fighter flight.

- **What the engine reads.** Types whose `SpaceBehavior` names
  `FIGHTER_LOCOMOTOR` (the stock fighters and bombers in
  `SpaceUnitsFighters.xml`; the X-wing has `Max_Speed` 4.0, `Min_Speed` 2.5,
  `Max_Rate_Of_Turn` 3.0) fly at no less than `Min_Speed`, on the same
  provisional scale as `Max_Speed`. Without a `Min_Speed`, half the top speed
  is used.
- **Engine choices, not measured from the original:**
  - fighters never stop: idle or arrived, they circle a point (at their turn
    rate, a turn circle of about 48 units for the X-wing);
  - attack runs: within a third of their weapon range of the target they hold
    their heading, fly past, and turn back once outside;
  - fighters separate only from flyers of their owner, sideways (overlap is
    measured in 3D), and weapons still fire all around (fighter weapons come
    from `Projectile_Types`, which has no cone);
  - fighters pitch at their turn rate, at most 60 degrees, keep the height
    they spawned at as their plane, and return to it within about four turn
    circles; presentation banks them by their turn rate (0.6 radians per
    radian per second, at most 60 degrees);
  - reversal: a goal more than 135 degrees off the heading and more than four
    turn circles away starts a half-loop (nose up through the vertical at the
    turn rate, out heading back, then a half roll back to upright at twice the
    turn rate);
  - dogfights: against any flyer (any type with a minimum speed, so fighters,
    bombers, and mod units alike), a flyer steers at the target in 3D,
    weaving 60 units above and below it over 8 seconds on a per-unit phase,
    within 150 units of its plane, and loops when the target is far behind.
    Other squadrons join through ordinary targeting.
- **Not used yet.** Bombing runs and the original's flight paths, banking,
  `SpaceBehavior` entries other than the locomotor, and fighter launch and
  docking from carriers.
- **Observed in the original** (black-box play of Forces of Corruption by the
  repository owner, 2026-11-08; qualitative, nothing measured yet):
  - fighters fly in a single plane most of the time;
  - when squadrons attack each other, the fight becomes a dogfight that
    further squadrons can join: fighters chase each other and leave their
    plane, climbing and diving in loops;
  - a squadron ordered to roughly reverse its direction turns around through a
    vertical half-loop (a "backflip") outside its plane instead of a flat turn;
    the angle range that triggers it is not known;
  - fighters of one squadron push overlapping squadron mates apart (the engine
    now does the same).
- **Open measurements:** the vertical extent of dogfights, the reversal angle
  that triggers a half-loop, how fast fighters return to their plane, and
  whether separation also acts between squadrons or against other owners.
  Video captures of the original would settle these.
