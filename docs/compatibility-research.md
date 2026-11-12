# Compatibility research

The bootstrap uses public documentation, black-box behavior, compatibility
corpora built from the user's installed data, and the original Alamo Direct3D 9
shader sources that Petroglyph published with the official Empire at War mod
tools ([petroglyphgames.com/eawmodtool](https://www.petroglyphgames.com/eawmodtool/)), kept locally under
`Reference Materials/Shaders/`. It does not use leaked source code or decompiled
code copied from the original executable, and the engine never modifies or
redistributes the installed runtime data.

The shader sources are evidence for parameter, transform, and render-state
semantics. They stay out of the repository (git-ignored) and are never compiled
or shipped; the new D3D12 shaders are written for this engine against
engine-native material contracts. Source checkouts without
`Reference Materials/Shaders/` must still configure, build, test, and run. No
other original game source, binary, or asset may be added without explicit owner
authorization and provenance review. Because contributors read these sources,
project documentation does not describe the renderer as clean-room.

The game-object tag survey ([xml-object-tags.md](xml-object-tags.md)) also read the
map editor's `Source` folder (the EAW and FoC XML with developer comments, the enum
files, and the stock Lua in decompiled form) from a public, unlicensed redistribution,
locally and as evidence only, like the installed data: none of it is in the
repository, and its comments are paraphrased, never quoted.

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
the repeats continue the list (see "Squadron lists" below), so the 327 placed
squadrons of the FoC corpus expand into 1,474 member ships. Land teams and companies list members
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
- **Acceleration and turning.** `OverrideAcceleration` and `OverrideDeceleration`
  (about 148 stock types: ships .02-.065, tanks .2, infantry and heroes) are read
  as Max_Speed units per frame per frame (×900 per second squared), a guess:
  a fraction of top speed per frame fits the values as well. Walkers name none.
  `Min_Speed_Fraction_For_Turn` (21 types: walkers .1-.4 with
  `Stopped_Rate_Of_Turn` and `Has_Pre_Turn_Anim`, fighters and freighters
  .6-.7) is read as the speed fraction below which a unit turns at its stopped
  rate (ground) or not at all (space); capital ships name none and take an
  engine default. `Max_Thrust` (70 space types) is not read: its meaning is
  unknown.
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
provisional reading. A footprint is the type's XML rectangle when it gives one
(below), else the model bounds' rectangle. Unit separation uses
`Custom_Soft_Footprint_Radius` where present (3.5 for stock infantry).

XML footprints (2026-11-11). `Obstacle_Width` and `Obstacle_Height` are a full
rectangle placed from its corner (`Obstacle_X_Offset`, `Obstacle_Y_Offset`;
stock offsets are almost always minus half the size). `Custom_Hard_XExtent`
and `Custom_Hard_YExtent` are half extents around `Custom_Hard_XExtent_Offset`
and `_YExtent_Offset`: the T2B tank's 20 by 10 matches its 44 by 22 chassis.
The XML's x runs along the model's -Y (its forward) and its y along the model's
+X: the off-center boxes of the rock and ruin props match their mesh bounds
that way (W_ROCKCLSTR01: extents 24, 30 at offset 3, -10 against mesh bounds
x -42..22, y -28..22; W_RUINS03 and W_TEMPROCKCLSTR03 likewise). Sizes are world
units not scaled by `Scale_Factor` (the TIE crawler at scale 0.75 and the MPTL
at 1.1 match their unscaled meshes; an inference). `Obstacle_Proxy_Type` lends
another type's footprint (structures defer to their build pad).

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
original's exact rules (turn speed while aiming, crouching, the stationary
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

Animation sources and death clones.

- **`Land_Model_Anim_Override_Name`** (27 stock uses, with or without `.ALO`)
  names the model whose clips a type plays instead of its own; variants
  inherit it (`Ewok_Hunter` names NI_EWOK.ALO, so `Ewok_Hunter_B` on
  NI_EWOK_B.ALO plays NI_EWOK clips). Civilians and slaves use
  NI_GMale_Urban_A or RI_INFANTRY, field commanders RI_INFANTRY or EI_TROOPER.
  The engine matches such clips to the model's bones by name.
- **Death clones.** A unit lists `Death_Clone` entries "damage type, clone
  type" (`Damage_Normal, AT_ST_Walker_Death_Clone_00`; infantry list several
  for force powers, fire, disruptors, infection, crushing). A clone is a
  variant of the unit (same model) with `Specific_Death_Anim_Index` (the DIE
  variant), `Death_Persistence_Duration` (the AT-ST's wreck stays 30 s),
  `Death_Explosions_End`, `Death_Clone_Is_Obstacle`, and hard extents. The
  engine leaves clones out of the spawn list and uses the normal clone's DIE
  variant and persistence for corpses; damage-type-specific clones, the
  explosions, and wrecks as obstacles are not modeled yet.
- **Models without clips.** With the override, 290 of the land spawnable
  types' models (mostly buildings and props) and the space ships have no
  role clips; large ships only have `_D_DIE` clips of their destruction
  models.

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

Particle systems. Particle effects are ALO files whose root chunk is 0x0900
(578 of the 1,957 resolved stock ALO files; none use the 0x1500 plugin format
of later Alamo games). The field meanings follow the MIT-licensed alo-viewer
(Copyright (c) 2017 GlyphX Tools); `src/formats/src/alo_particle_system.cpp` is our own
decoder, checked against the stock corpus with `EAWAssetInspector
particle-audit`.

- **Layout.** A system holds its name (chunk 0x0), a 4-byte chunk 0x1, an
  emitter list (0x0800 of 0x0700 emitters), and an optional one-byte
  "leave particles" chunk 0x2. Each emitter holds its property mini-chunks
  (0x2), texture (0x3), name (0x16), three shape groups (0x29: speed,
  lifetime, position), seven keyframe tracks (0x1: red, green, blue, alpha,
  scale, texture cell, rotation speed), optional child links (0x36: spawn on
  death, spawn during life) and an optional normal texture (0x45).
- **Corrections to the reference.** A shape group (chunk 0x1101) is 64 bytes
  (type, minimum, maximum, cube side, sphere radius and hollow flag, cylinder
  radius, hollow flag, and height, exact value), not 68. Colour track keys
  hold a 0..255 integer and then the time; other keys hold a float value and
  then the time.
- **Corpus.** All 578 systems decode without warnings: 1,464 emitters, 1 to
  15 per system, 492 systems leave their particles behind. Blend modes are 1
  (additive, 900 emitters), 2 (alpha, 543), and 11 (bump alpha, 21). 953
  emitters are camera-facing billboards, 80 lie in the ground plane ("world
  oriented"), and 65 are velocity-stretched kites; 170 are heat-distortion
  particles and 5 weather particles. 685 emit limited bursts, 178 unlimited
  bursts, and 601 continuously. Child links are rare (24 spawn during life, 3
  on death). Every emitter writes the properties 0x04 to 0x2B; later ids
  (0x2C to 0x49) are missing from some files, so they keep defaults. The
  system chunk 0x1 holds 0 to 13 (350 files hold 0); its meaning is unknown,
  and it is kept as stored.
- **Not interpreted.** Properties 0x06, 0x11, 0x14, 0x15, 0x2B, 0x3F, 0x44,
  and 0x49 are kept raw; the reference reads and ignores them too.
- **Value ranges** (stock corpus): the random scale, lifetime, and rotation
  variances (0x12, 0x13, 0x17) lie in 0..1, so they are fractions, not
  percentages; continuous rates reach 3,500 particles per second, bursts
  2,000 particles, and the shortest burst delay is 0.0003 s, so the engine
  caps live particles explicitly (65,536 by default, drops counted) and
  bounds emission per update.
- **Second reference: the GlyphX particle editor** (github.com/GlyphXTools/
  particle-editor, MIT, Copyright (c) 2017 GlyphX Tools, the authors of
  alo-viewer). It is the authoring tool that writes the files the game
  loads, with its own preview simulation, and where it differs from
  alo-viewer the engine follows it: its file writer stores the lifetime
  shape group as exactly lifetime x (1 - variance)..lifetime, its UI shows
  the "inward" properties negated so that positive UI values move particles
  toward the emitter, and its rotation and texture rules are simpler than
  alo-viewer's derived ones. Two points where it disagrees with the owner's
  observations of the game are kept as observed: it carries
  linked particles with the emitter's translation only (the engine carries
  rotation too, which removed visible snapping of engine glows on turning
  ships).
- **Third reference: DrKnickers' fork of the particle editor**
  (github.com/DrKnickers/particle-editor, MIT, 2026, with a guide at
  drknickers.github.io/particle-editor). Its simulation keeps the original
  editor's rules; its guide describes the editor's behaviour (its changelog
  says the guide was checked against the editor's source, not the game),
  and a few changelog entries claim to match the game without saying how:
  emitters draw in their list order, the game steps particle motion frame by
  frame, atlas indices past the last cell wrap, tailed particles ignore the
  rotation track, and additive particles ignore alpha. The original editor
  agrees on the last point (its fixed-function setup blends additive
  particles with source and destination factors of one). The guide says
  linked particles ("Link Particles to Instance") follow the instance's
  position only, as the original editor does, but both editors play a
  system as a free-standing object that has a position and never an
  orientation, so that rule describes their previews, not the game; in the
  engine it visibly detaches engine glows from turning fighters (owner's
  observation, Y-wing squadrons, 2026-11-09). Linked particles therefore
  follow their emitter's position and rotation under every profile. The
  other two points are look-defining quirks: the engine's legacy profile
  reproduces them (additive adds colour x texture without alpha, the
  speed-driven engine intensity dimming the colour instead; effects draw
  back to front with their emitters in list order), and the enhanced
  profile improves them (additive scaled by alpha, every particle sorted
  back to front). `particle_rules` in `src/assets` holds the interpretation;
  `sandbox --particles legacy|enhanced` chooses. The fork also documents the game's projectile flight
  (`src/simulation/ProjectileShot.h`, `ProjectileFlight.h`,
  `RocketSpline.h`): `Max_Speed` is world units per 30 Hz logic frame
  (agreeing with the engine's provisional x30), a shot dies after
  `Projectile_Max_Flight_Distance`, projectile acceleration is zero; by
  `Projectile_Category`, missiles turn toward their target by at most
  `Max_Rate_Of_Turn` degrees per frame on yaw and pitch separately, bombs
  keep their launch facing and fall faster each frame
  (`Projectile_Bomb_Fall_Accel_Rate`), grenades lob in closed form (speed
  `Max_Speed` or 0.8x when they may lob slower, gravity
  `Projectile_Grenade_Gravity` x `Lob_Mod`), and rockets follow a spline with
  points every `Projectile_Rocket_Curve_Distance` x 1.1, pushed aside up to
  `Projectile_Rocket_Curve_Offset`. Models are drawn turned 90 degrees about
  Z after their facing, so a model's -Y axis points along the flight, as the
  engine already assumes for units. These are the fork's statements, to be
  checked before the simulation relies on them.
- **Playback** (`src/engine/particle_effects.cpp`), following the particle
  editor where it is clear:
  - emission starts after the initial delay: bursts of the per-burst count
    every burst delay, limited by the burst count (0 or -1 unlimited), or one
    particle every 1/rate seconds; skip time pre-simulates;
  - particles spawn on the position group and take the speed group as
    velocity, both in the emitter's frame; the stored "inward speed" is added
    along the spawn offset's direction (positive moves outward; the editor
    shows the value negated, as an inward speed);
  - acceleration (optionally in the emitter's frame) plus -gravity along Z,
    plus the stored "inward acceleration" along the same fixed spawn
    direction (positive pushes outward, likewise);
  - lifetime x random(1 - variance, 1) and size x random(1 - variance, 1):
    variances shorten and shrink, never lengthen or grow (alo-viewer
    lengthened lifetimes); the scale track is the sprite's full width, as
    both references draw it. A radius reading was tried after the X-wing's
    1-unit engine dots looked too small, but with bloom in place it made
    capital-ship engine glows far too large and bright next to the owner's
    screenshots of the original, so the width reading stands;
  - colour tracks plus a random addition up to the random colours (grayscale
    uses the red value for all channels);
  - rotation speed in revolutions per second, randomly reversed when the
    flag is set; "random rotation" gives a fixed angle: the rotation track's
    first value (revolutions) x (1 + random -v..v), v the rotation variance
    clamped to 0..1;
  - texture cells form a square atlas of floor(sqrt(cells)) per side, indexed
    by the texture track rounded down;
  - freeze time, when set and not inside the skipped time, stops the
    emitter's clock once reached: no more emission, and its particles hold
    still;
  - "link to system" moves particles rigidly with the emitter, position and
    rotation (so engine glows stay on a turning ship); linked particles spawn
    at the emitter's current transform;
  - ground behavior 1 kills particles below the ground, 2 bounces them, 3
    stops them;
  - blend modes 1 and 4 draw additively (legacy: colour x texture added,
    alpha ignored; enhanced: scaled by alpha), 3, 6, 8, and 9 modulate, the
    rest alpha-blend (stock data uses only 1, 2, and 11);
  - kites are diamonds facing the camera: the quad turned 45 degrees so its
    top-left texture corner trails the velocity on screen, that corner pushed
    out by max(1, tail size x the velocity's on-screen fraction / sqrt 2),
    the tail size scaled by speed / 1000 for particles linked to a parent;
    world-oriented particles lie in the XY plane.
- **Trails.** No stock XML field names a trail, and the classic particle
  format has no ribbon renderer (the reference's chain renderers belong to the
  plugin format). Engine and missile trails are particles that moving
  emitters leave in the world; laser bolts are drawn from the projectile's
  `Projectile_Width`, `Projectile_Length`, `Projectile_Texture_Slot`, and
  `Projectile_Laser_Color` (`Projectile_Custom_Render` 1 or 2, 38 stock
  projectiles). The engine emits each particle at its own time within a frame
  where the emitter was then (an engine choice; the reference turns its
  high-resolution emission off for this format, which leaves clumps behind
  fast emitters).
- **Child emitters** (the particle editor): a trail child is an emitter
  instance attached to each living parent particle, stopped (its particles
  living out) when the parent dies; a death child is created at the dying
  particle and stopped at once, so it emits its first burst (or one particle
  when continuous) and nothing more. The engine does the same, with at most
  64 trail particles per parent per update.
- **Engine choices:** sphere spawns use uniform directions (the references
  sample angles uniformly); emitters that are only children never emit on
  their own; smooth interpolation is smoothstep between keys (the editor's
  cubic Hermite with zero tangents, the same curve).
- **Emission from meshes** (192 emitters): a proxy's mesh is the mesh
  rigidly attached to the proxy bone's parent, as in the reference. Mode 3
  emits once per vertex per spawn, 1 at a random vertex, 2 at a random point
  of a random triangle; the particle is pushed out along the normal by
  `emit_from_mesh_offset`, and its velocity's +Z turns to the normal. The
  Star Destroyer's engine effect (`pe_stardestroyerengines`, every vertex,
  size 70) sits on `engines_big`, a hidden three-vertex triangle whose corners
  are the three main engines; without the mesh it was one ball at the bone.
  Emitters without a mesh (effects played on their own) use their position
  shape. The X-wing's `pe_Z95engines` emits from the four corners of its
  hidden `engines` quad (one per nozzle), but its particles are 1 unit
  across, red, and live 0.1 s: barely visible as authored. Whether the
  original draws particles with a minimum screen size is not established.
- **Bloom and engine intensity** (owner's screenshots of the original,
  2026-11-08). The original renders with a glow (bloom) option: bright hull
  panels and engine glows bleed light, which is what makes small additive
  engine particles such as the X-wing's 1-unit red dots visible. The engine
  draws bloom of its own (threshold 0.6 on the brightest channel per texel,
  soft knee, the block mean mixed with its brightest texel so small glows
  survive the downsample,
  quarter resolution, strength 0.8), chosen by eye, not measured. In the same
  screenshots a Star Destroyer's engine glows are small and dim at rest and
  grow and brighten as it accelerates. No XML field governs this (the only
  engine field is `Engine_Particles`), so engine effects (proxies named
  `pe_*`, the stock naming, or under an `Engine_Particles` bone) scale with
  the unit's speed over its top speed, smoothed over 0.5 s: size from 50% and
  opacity from 15% at rest (45% at first, which left stationary ships
  glowing visibly). Both mappings are engine choices.
- **Which effects the data names.** Gameplay effects are fully data-driven
  through `Particle` objects (165, `Data/XML/Particles.xml`). Each names its
  effect file (`Space_Model_Name`, `Land_Model_Name`, sometimes
  `Galactic_Model_Name`) and playback settings: `Scale_Factor` (1.0 for 143,
  2.0 and 20.0 for a few), `Particle_Lifetime_Frames` (mostly 30),
  `Explosion_Jitter_Factor`, `Explosion_Scorch_Mark`,
  `Particle_Attach_To_Collision`, `No_Particle_Below_Detail_Level`,
  `Behavior` (`PARTICLE`, `PARTICLE, EXPLOSION`), and placement flags
  (`In_Background`, `In_Midground`). Fields that name Particle objects, by
  stock use count and owner:
  - deaths: `Death_Explosions` (610: props, structures, units),
    `Death_Explosions_End` (20), `Spin_Away_On_Death_Explosion` (22),
    hardpoints' `Death_Explosion_Particles` (267), and the break-off debris'
    `Debris_Attached_Particle` (149, burning debris);
  - projectile impacts: `Projectile_Object_Detonation_Particle` (91),
    `Projectile_Object_Armor_Reduced_Detonation_Particle` (56, "duds"),
    `Projectile_Absorbed_By_Shields_Particle` (88),
    `Projectile_Ground_Detonation_Particle` (70),
    `Projectile_Lifetime_Detonation_Particle` (54, a shot that runs out);
  - hits on units: `Asteroid_Damage_Hit_Particles` (60), `Damage_Hit_Particles`
    and `Shield_Hit_Particles` (cinematic units), `Base_Shield_Penetration_Particle`
    (34);
  - abilities and heroes: `Owner_Particle_Effect`, `Target_Particle_Effect`,
    `Particle_Effect`, the `Projectile_*_Spawn_Effect` fields, and others.
  Hardpoints place their damage on model bones: `Damage_Particles` (268,
  `HP_*_EmitDamage`), `Damage_Decal` (245, `HP_*_Blast`), and engine
  hardpoints' `Engine_Particles` (21, `HP_E_MainEngines`). Model proxies
  (engine glows, station lights, fires) have no XML entry: which proxy is an
  engine glow is not stated anywhere (the engine's `pe_*` rule is naming
  inference), and nothing in the data scales engine effects with speed (an
  engine choice from the owner's screenshots). Nothing in the data sizes
  custom-render bolts beyond `Projectile_Width` and `Projectile_Length`.
- **Projectile visuals.** 39 stock projectile types set
  `Projectile_Custom_Render`: 1 (26 types) draws a bolt from an atlas cell,
  2 (12) the same cell tinted by `Projectile_Laser_Color` (ion cannons,
  turbolasers, proton torpedoes, sonic and carbonite guns), 0 (1, an ion
  laser) draws its model. `Projectile_Width` and `Projectile_Length` size the
  bolt (small laser 1 x 6), and `Projectile_Texture_Slot` is "column,row".
  No XML names the atlas, but ModVerify's list of assets the game executable
  loads by name (`HardcodedEngineAssets.cs`, MIT) includes `W_Laser_Pill.tga`,
  and the public schema types `Projectile_Texture_Slot` as a two-number
  vector and `Projectile_Laser_Color` as RGBA. With `Variant_Of_Existing_Type`
  resolved, 63 FoC projectiles are custom-rendered, and 40 of them also name a
  model (the pg-starwarsgame-lsp preview, MIT). `W_LASER_PILL.dds` (256 x 256) is a 4 x 4 grid of
  64-pixel cells holding diagonal glowing pills, bottom-left to top-right:
  (0,0) red, (1,0) orange, (2,0) yellow, (3,0) green, (0,1) blue, (1,1)
  purple; red lasers use slot 0,0 and green ones 3,0, which fits render 1.
  Render 2 is a teardrop: a modder who rewrote the stock projectiles
  describes small lasers as symmetrical bolts "while turbolasers appear
  teardrop-shaped", and turns any laser into a teardrop by setting render 2,
  slot 2,0, and a `Projectile_Laser_Color` (eaw-fast-consistent-projectiles
  on GitHub, MIT). No texture holds a teardrop at cell 2,0 (`W_LASER_SMALL`,
  the bolt models' texture, has red and green teardrop flares elsewhere and
  in fixed colours), so the engine draws it procedurally: a round head at
  the front, a tapering, fading tail, and a white-hot core, in the laser
  colour. Its length is `Projectile_Length` widths: in the owner's screenshot
  of the original, a Star Destroyer's green turbolaser shots (5 x 10) are
  thin streaks of about 25-30 px at about 1.3 px per unit, flying away from
  the camera, which an absolute length of 10 cannot reach and 50 units
  foreshortened does; the modder's script also scales turbolaser width and
  length together. Render 1 keeps absolute sizes (X-wing lasers, 1 x 6, look
  right). The scale, proportions of head and tail, and whether render 1 also
  measures length in widths for other types remain to be checked; sizes come
  from the XML, so mods that rescale them keep their proportions. Bolts are
  drawn between ticks like entities, head first. A modding tutorial
  (rebel5555, ModDB, 2009) gives a render-1 ion bolt 1.7 wide and 55 long
  with `Projectile_Texture_Slot` written as a single "2" and `Scale_Factor`
  0.5: render-1 lengths can be long streaks in mods, a single number is a
  cell index (read as column 2, row 0), and whether `Scale_Factor` scales
  custom-render bolts is open (stock projectiles all use 1.0, so the engine
  ignores it for now).
- **Model projectiles.** Of the 115 projectile types stock weapons fire, 53
  are bolts (custom render 1 or 2) and 62 draw a model. Many name only one
  context's model and are fired in both (seven concussion missiles name a
  `Land_Model_Name` and fly in space), so the engine falls back to the other
  context's model, as for effect objects. Some name a particle system as their
  model (`p_turbolaser_burst_med00`, `p_proton_Vengance`, `p_magnepulse_shot`);
  those play along the flight. Missile models carry their trail as a proxy
  (`W_concussion_missile.alo`: one mesh, one proxy). `Scale_Factor` ranges
  from 0.3 to 5.0 on model projectiles and is applied to them; the model's -Y
  axis points along the flight (the fork's documentation of projectile flight
  says the same). Open: 19 land projectiles for melee and environment hits
  name `W_LASER_INVISIBLE.ALO`, whose single additive quad uses the visible
  `W_Laser_Small.tga` and is not flagged hidden; why the original shows nothing
  for them (if it does not) is unknown, so the engine draws it as authored.
- **Unit `Scale_Factor`.** 465 stock unit types set a `Scale_Factor` other
  than 1 (0.17 to 4.0: X-wings and TIE fighters 0.7, the Corellian corvette
  0.5, the AT-ST 1.6, Star Destroyers 1.0), inherited through
  `Variant_Of_Existing_Type`. The engine draws a type's model at that scale
  (placed objects: the TED placement transform times the scale; spawned types:
  their presentation template), and measures what it derives from the model
  through the same scale: separation radii and obstacle footprints from model
  bounds, and hardpoint and fire-bone positions from model bones (so weapons
  fire from the scaled hull). Attached effects (engine glows, fires) take the
  unit's scale too, particle sizes included. An XML
  `Custom_Soft_Footprint_Radius` stays as written. That the original scales
  collision and hardpoints with the model is an inference (it draws the model
  scaled, and its bones move with it). Engine glows tuned earlier against the
  owner's screenshots were measured on unscaled X-wings and need rechecking at
  0.7. State hashes changed with this; order logs recorded before do not
  verify.
- **Impact and death effects.** Gameplay effects are not hard-coded per
  weapon class: the game names them through XML `Particle` objects
  (`Particles.xml`, 165 stock objects), each giving a `Space_Model_Name` and
  `Land_Model_Name` (a particle ALO), a `Scale_Factor`, and lifetime and
  explosion fields. Projectiles name one per outcome:
  `Projectile_Absorbed_By_Shields_Particle` (88 stock uses),
  `Projectile_Object_Detonation_Particle` (91),
  `Projectile_Object_Armor_Reduced_Detonation_Particle` (56),
  `Projectile_Lifetime_Detonation_Particle` (54), and
  `Projectile_Ground_Detonation_Particle` (70); types name
  `Death_Explosions` (610) and hardpoints `Death_Explosion_Particles` (267).
  The engine plays the shield, hull, armor-reduced, and lifetime ones where
  a shot ends, and a type's first death explosion where it dies, one-shot,
  upright, and scaled by `Scale_Factor` (positions, speeds, and sizes). When
  the original counts a hit as "armor reduced" is not documented; the engine
  uses a damage-versus-armor modifier below 1 (provisional). A shot whose
  target died, or that outlived its flight limit, plays its lifetime effect.
  Whether impacts face the shooter or the surface normal, and how several
  `Death_Explosions` entries are chosen, are open. `Death_Explosions` does
  not always name a `Particle` object: 12 of 617 stock entries name a
  `Projectile` (`Proj_Construction_Pod_Detonation`, `Proj_Wall_Destruction`,
  `Proximity_Mine_Blast_Effect`, `Remote_Bomb_Blast_Effect`). Each has no
  model, a zero `Projectile_Max_Flight_Distance`, and a
  `Projectile_Lifetime_Detonation_Particle`, so the death spawns a shot that
  detonates on the spot; the engine plays that detonation particle (lifetime,
  else object, else ground) with the particle's own `Scale_Factor`. That is
  an inference from the data. The projectile's area blast
  (`Projectile_Blast_Area_Range`, `_Damage`) is gameplay the simulation does
  not model yet. All 267 hardpoint `Death_Explosion_Particles` entries name
  `Particle` objects. Objects that cannot play (no model, a mesh instead of a
  particle system, a projectile without a detonation particle, an undefined
  name) are kept, play nothing, and are reported once per build.
- **Not drawn yet:** heat distortion (170 emitters) and weather (5), which
  need passes of their own; wind is ignored; freeze time is kept but unused.
- **Proxies.** Model proxies name their effect without directory or
  extension (`pe_Z95engines` is `Data/Art/Models/pe_Z95engines.alo`). The
  stock station and capital-ship models also carry proxies tagged like meshes:
  `p_explosion_med00_ALT1` and `_ALT2` show in damaged states and are skipped
  on the intact model, and the tag is not part of the file name. Hidden
  proxies wait for gameplay.
- **Hardpoint damage effects.** Hardpoint XML names a `Damage_Particles` bone
  (268 stock uses: `HP_F-R_EMITDAMAGE`, `HP_E_EMITDAMAGE`, ...), and the
  models hang their damage proxies under it: the Star Destroyer's
  `p_hp_imperial_damage` smoke sits on children of `HP_F-R_EmitDamage` and
  its siblings, flagged visible like any other proxy. So visibility alone
  does not separate them; the engine marks proxies at or below a hardpoint's
  damage bone as that hardpoint's damage effect and does not play them on an
  intact ship. Showing them when the hardpoint is damaged or destroyed is part
  of hardpoint presentation. `Engine_Particles` likewise names the engine-glow
  bone (`HP_E_MAINENGINES`, 21 uses).
- **Hardpoint models.** A hardpoint's `Model_To_Attach` (226 stock uses,
  e.g. `EV_StarDestroyer_HP00_M-C.alo`) is a separate model placed at its
  `Attachment_Bone`; the host hull has openings where they sit, so without
  them ships look gutted. The engine merges each into the host's mesh at the
  bone's bind pose, bound rigidly to that bone. Hardpoint models are drawn in
  their own bind pose; their animations (turret rotation) and their removal
  when destroyed come with hardpoint presentation.
- **Hardpoint scorch meshes.** `Damage_Decal` names a bone (`HP_*_BLAST`,
  245 stock uses) that carries a small alpha-blended mesh of its own: the Star
  Destroyer has seven (`HP_F-R_Blast` and so on, `MeshAlpha` with
  `W_blast00.tga`), visible in the model like the hull, so drawn as authored
  they char an intact ship. The engine hides the bone and its children through
  the palette's visibility half until hardpoint damage is presented (an
  inference from the field name and from intact ships in the original showing
  no scorch marks). Some proxies name files that do not exist (`Lensflare0`); they are
  reported once per build.

## Audio

- **Where sounds are.** Effects and unit responses are in `Data/Audio/SFX/`:
  `sfx3d_non_localized.meg`, `sfx2d_non_localized.meg`, and one `sfx2d_<language>.meg` per
  language (the case varies: `sfx2d_english.meg`, `sfx2d_Russian.MEG`). They are not in
  `megafiles.xml` and store bare file names, so the engine mounts them under `DATA/AUDIO/SFX/`,
  only the chosen language's, and archive discovery skips their folder (before this, every
  language's archive was also mounted as an unlisted data archive, with its names at the VFS root).
  Music and dialogue speech are MP3 (under `Data/Audio/Music` and `Data/Audio/Speech/<language>`);
  the XML names speech `.wav`, so samples resolve by trying the name and then `.MP3`, and files are
  recognized from their first bytes, not their extension.
- **Formats.** Every effect sample the stock events name is 16-bit PCM WAV: 9,610 distinct samples,
  9,539 resolved and decoded (3,862 mono 11,025 Hz, 4,754 mono 22,050 Hz, 903 mono 44,100 Hz, 20
  stereo 44,100 Hz; 214 minutes), 71 missing from the stock data itself (`sound-audit`).
- **Sound events** (`SFXEvent`, 3,073 stock, 540 positional, 153 looping): `Samples` (one chosen per
  play, at random or with `Play_Sequentially` in turn), `Min/Max_Volume` and `Min/Max_Pitch` in
  percent, `Probability` in percent (may list one value per sample; the engine uses the first),
  `Min/Max_Predelay` in milliseconds, `Play_Count -1` loops, `Is_3D`/`Is_2D`, `Max_Instances` (the engine replaces the oldest one-shot when a new play
  reaches the limit, an inference: the TIE Crawler fires five 0.2 s pulses without recharge while its
  1 s sample allows four instances, and refusing the fifth left shots silent; unit loops at the limit
  wait instead),
  `Overlap_Test` (a group name such as `Unit_EHD` shared by a type's responses; the engine lets one
  play of a group sound at a time, an inference), `Chained_SFXEvent`, `Pre_Samples`/`Post_Samples`
  (loop intros and outros, not played yet), `Volume_Saturation_Distance`, `Priority`,
  `Is_Unit_Response_VO`, `Kills_Previous_Object_SFX`, and `Use_Preset` naming a preset (`Is_Preset`)
  whose fields the event overrides. The first definition of a name wins, as for object types.
- **Units name their sounds** in XML: `SFXEvent_Fire` (a unit's own weapon), a weapon hardpoint's
  `Fire_SFXEvent` (each hardpoint its own; the engine falls back to the type's `SFXEvent_Fire`),
  `Death_SFXEvent_Start_Die`, `Death_Explosion_SFXEvent`, `SFXEvent_Engine_Moving_Loop`, and the
  responses `SFXEvent_Select`, `SFXEvent_Move`, `SFXEvent_Attack`. The pulses of one tick that share
  a shooter and a sound play once (an engine choice, so a battery of identical guns does not stack).
- **3D settings.** `AUDIO.XML` sets per mode `Audio_<Land|Space>_3D_Saturation_Distance_Mod`,
  `_Rolloff_Distance_Mod`, and `_Listener_Z_Pullback_Dist`. The engine's reading: full volume within
  an event's saturation distance (200 when it names none) times the factor, then falling as
  s / (s + r (d - s)) with r = 0.05 times the rolloff factor, heard from the camera's focus raised by
  the pullback. The curve, the base rolloff, the default distance, and the limit of 16 unit loops
  are engine choices to check against the original.

- **Sounds in animations.** `DATA/XML/ANIMATIONSFXMAPS.TXT` (1,639 lines on stock FoC, 479
  animations) holds `SOUND <animation> <time> <sound event>` (574 rows: walker hydraulics, death
  falls, creature sounds) and `SURFACE <animation> <time> <trigger> <bone>` (578 rows: foot plants,
  impacts of falling bodies, tracks). A trigger names a `SurfaceFX` in `SURFACEFX.XML` (the names are
  listed in `ENUM/SURFACEFXTRIGGERTYPE.XML`), whose `Surface_Settings` per surface kind (default,
  dirt, grass, snow, water, concrete, lava, ...) name a sound, a particle, a decal, and a dynamic
  track; `NONE` stands for nothing. The time is read as hundredths of a frame (an inference: a
  stormtrooper's left foot lands at 1032-1170 in its 22-frame walk, half way, and in 59 of 60
  sampled animations every row lies within the animation; the exception, `UV_MDU_DEPLOY_00`, has
  a row at 132% of its length). Only some models have rows: stormtroopers, heroes, walkers, and
  creatures, not rebel soldiers.
- **Instance limits and fast weapons.** The TIE Crawler fires bursts of five shots 0.2 s apart
  with no recharge, while its fire sound (1 s, preset `Preset_GUNV`) allows four instances; refusing
  the fifth play left every fifth shot silent and the sound drifting against the shots, so the
  engine lets a new one-shot replace the oldest instead (an inference).
- **Every sound source in the stock data** (survey of 2026-11-11, stock FoC XML, maps, and 648
  scripts):
  - Sound events: 3,073 `SFXEvent`s in the `SFXEVENTS*.XML` files; speech: 2,271 `SpeechEvent`s in
    `SPEECHEVENTS.XML` (advisor and HUD voice, MP3 files, `Text_ID` subtitles); music: 62
    `MusicEvent`s in `MUSICEVENTS.XML` (MP3 files, `Loop`, `Fade_In_Seconds`,
    `Fade_Out_Previous_Seconds`).
  - Battle objects name sounds for: weapons (`SFXEvent_Fire`, hardpoint `Fire_SFXEvent`,
    `SFXEvent_Barrage`), projectiles (`Projectile_SFXEvent_Detonate` 87,
    `Projectile_SFXEvent_Detonate_Reduced_By_Armor` 62), deaths (`Death_SFXEvent_Start_Die`,
    `Death_Explosion_SFXEvent`, `Death_SFXEvent_End_Die` 18, `Spin_Away_On_Death_SFXEvent_Start_Die`
    22), damage (`Land_Damage_SFX` 221, a list by damage level; `SFXEvent_Damaged_By_Asteroid`),
    hardpoints (`SFXEvent_Hardpoint_Destroyed` 362), engines and props (`SFXEvent_Engine_Moving_Loop`,
    `_Idle_Loop`, `SFXEvent_Ambient_Loop` 90 on units, props, structures, and markers such as
    `Audio_Waterfall_Emitter_Marker`), periodic sounds while moving (`SFXEvent_Ambient_Moving` with a
    min/max delay), turrets (`SFXEvent_Turret_Rotating_Loop` 37), abilities (`SFXEvent_Activate`
    126, `SFXEvent_Target_Ability`, `SFXEvent_Target_Affected`), entering nebulae and asteroid
    fields, and unit responses (`SFXEvent_Select`, `_Move`, `_Attack`, `_Attack_Hardpoint` 426,
    `_Guard`, `_Stop`, `_Fleet_Move`, `_Group_Move`, `_Group_Attack`, `_Assist_Move`,
    `_Assist_Attack`).
  - The player's side: HUD and advisor announcements (`SFXEvent_Unit_Lost`,
    `SFXEvent_Unit_Under_Attack`, health warnings, build started/complete/cancelled, reinforcement
    points, retreat and surrender, battle won/lost, all in `FACTIONS.XML` and the unit types), the
    GUI (`Mouse_Over_SFX` 294, `Click_SFX` 118, command bar), and the galactic map.
  - Ambience: `WEATHERAUDIO.XML` maps a weather (RAIN, SNOW, SANDSTORM, ASH, CLEAR) and its
    intensity (thresholds 0.01-0.75) to a loop and to intermittent sounds with a delay range, and
    each environment kind (TEMPERATE, ARCTIC, DESERT, FOREST, SWAMP, VOLCANIC, URBAN) to a
    clear-weather intermittent sound. `AMBIENTMAPSOUNDS.XML` is a stub (one test entry, six empty
    environments). Maps hold no sound names; the weather and environment kind of a map are not
    decoded yet (presumably in the TED environment data).
  - Music: each faction (`FACTIONS.XML`, `EXPANSION_FACTIONS.XML`) lists ambient and battle music
    by environment kind (`Music_Event_List_Ambient`, `Music_Event_List_Battle`: Space, Temperate,
    Arctic, ...), plus load-screen, super-weapon, win, and lose music; `AUDIO.XML` sets how long a
    battle stays quiet before the music returns to ambient (`Music_Land/Space_Battle_To_Ambient_Peace_Seconds`).
  - Animations: `ANIMATIONSFXMAPS.TXT` (above).
  - Scripts: `GameObject.Play_SFX_Event(name)` (83 calls in 24 scripts; all 30 names are defined
    sound events; the engine plays them at the object), `Play_Music(name)` (4: cinematic and cantina music), `Stop_All_Music` (25),
    `Resume_Mode_Based_Music` (14), `Stop_All_Speech` (19), `Weather_Audio_Pause(true|false)` (13),
    `Allow_Localized_SFX(true|false)` (40), `Allow_Ambient_VO` (2), `Master_Volume_Restore` (3),
    `Stop_SFX_Event` (1), and `Play_Bink_Movie` (6, movies with their own sound). Story dialogue
    (`Story_Dialog`, 178 in the campaign XML) plays speech.
- **Damage states and detail levels.** 222 object types (structures, buildables, vehicles, a few
  space structures) set `Land_Damage_Thresholds` (health fractions, typically `1, 0.66, 0.33`,
  127 of them with a final `0`), `Land_Damage_Alternates` (the `_ALT<n>` state for each threshold),
  and `Land_Damage_SFX` (a sound on entering each state, `null` for none). 158 models carry `_ALT<n>`
  names (states 0-3, rarely more), on meshes for buildings and mostly on damage-smoke emitters for
  vehicles (`p_smoke_small_thin_ALT1`); space ships have none. 145 models carry `_LOD0`-`_LOD3`,
  most of them infantry and vehicles, many skinned; LOD0 is the sparsest in 137 of them. 80 types
  set `LOD_Bias` (0.5-10), and the graphics presets (`GRAPHICDETAILS.XML`) set `MeshDetail` 0-1
  beside `ParticleDetail` and the other detail options.
- **Handedness and placed-object facing.** The game's world is right-handed with Z up (X east, Y
  north): seen through a left-handed view, maps came out mirrored left to right against the
  original's editor (owner's screenshots of `_LAND_PLANET_ANAXES_02`). A placed object's Z rotation
  is its facing, counter-clockwise from +X, and stock models face -Y (the TIE Crawler's muzzles sit
  at y -4.7), so a model turns a quarter before the placement rotation. Evidence: pieces laid end to
  end (train tunnels, walls, half-walls) follow heading + 90 degrees to their nearest neighbour in
  697 of 992 measured cases across the stock maps (Tunnel_A 158 of 158, half-walls 48 of 51; most
  of the rest are parallel runs side by side), and Anaxes 02 then matches the editor. Squadron
  offsets are in the facing frame: stock V formations put the lead at +X (`30,0,0`) and the wings
  behind at -X.
- **Fleet and group responses.** `SFXEvent_Fleet_Move` is set on 215 types, infantry and vehicles
  included, so it is not a space-only order response. The owner's guess (2026-11-11, unverified): it
  is the response when a fleet is sent to another planet in galactic conquest. The engine does not
  play it in battles. `SFXEvent_Group_Move` and `SFXEvent_Group_Attack` (25 each, heroes) are not
  played either.
- **Muzzles.** Weapon positions are not in the unit XML: the TIE Crawler's model has three
  `MuzzleA_nn` bones, each with a hidden `MuzzleA_nn_flash` mesh, which the original presumably
  fires from and flashes; not used by the engine yet.

## Lua host protocol

What the engine must provide to the stock scripts, from the framework library under
`Data/Scripts/Library` (read as evidence; nothing quoted) and from running every stock script on
recording stubs (`EAWEngine lua-survey`).

- **Language.** Lua 5.0 (the inspector's bytecode decoder reads 5.0 chunks; the scripts use
  `table.getn`, `arg` tables for varargs, and no `%` operator). Stock FoC ships all 370 scripts as
  source; one archive also carries a compiled `PGBase.lua` that loses in VFS precedence to the source
  in `64Patch.meg`.
- **Lifecycle.** The engine calls `Base_Definitions` when a script is created: the framework's
  version resets thread values and events, clears its globals (including `Object` and
  `PlayerObject`), sets `ServiceRate`, and calls the script's own `Definitions`. So the engine binds a
  game-object script's `Object` after creation (an inference). `main` then runs as a thread; when it
  returns, the script ends.
- **Threads.** Script threads are coroutines the engine resumes. The framework's `Sleep` and
  `BlockOnCommand` wait in Lua, calling `PumpEvents`, which yields `true` back to the engine and, when
  resumed, drains `GetEvent` (with `GetEvent.Params`) and calls each event. Yielding `false` ends a
  thread. `_ScriptExit` sets a flag that ends the whole script at the next yield (`ScriptExit` wraps
  it). `GetThreadID` is negative outside threads. `ThreadValue(key)` and `ThreadValue.Set(key,
  value)` are per-thread storage. The framework sets `ServiceRate = 0.2` ("five times a second"); the
  host services each thread that often. `Create_Thread` is also a table: `Create_Thread.Kill(id)` and
  `Create_Thread.Is_Thread_Active(id)` act on the ids it returns (stock campaign scripts use both).
- **Events.** What the engine delivers is narrower than it looks. `GetEvent()` returns a queued
  function and `GetEvent.Params()` its argument table; `GetEvent.Reset()` drops the queue. Timers,
  death events, and attacked events are pure Lua: `Register_Timer`, `Register_Death_Event`, and
  `Register_Attacked_Event` fill tables that `Pump_Service` (PGCommands.lua) polls on every
  `PumpEvents`, through `GetCurrentTime`, `TestValid`, and `FindDeadlyEnemy`. Proximity is the
  engine's: `Register_Prox` hands its handler to `Event_Object_In_Range` and services the object with
  `Service_Wrapper`. The host keeps every script function passed to an engine function as a callback
  the engine can queue later.
- **AI plans.** A plan's `Definitions` sets `Category`, `TaskForce` (a list of tables whose first
  string names the task force, followed by composition rules), and often `RequiredCategories`. The
  AI framework's `Base_Definitions` clears a global of each task force's name, so the engine binds the
  task force object under that name and runs `<Name>_Thread` (150 such functions in 134 stock plans).
  Task force events are functions named `<Name>_<Event>` with the task force first among their
  arguments: `Production_Failed` (41 plans), `No_Units_Remaining` (30),
  `Original_Target_Owner_Changed` (19), `Unit_Move_Finished` (13), `Unit_Damaged`, and others;
  PGEvents.lua defines a `Default_<Event>` for most, which the engine presumably calls when a plan
  defines none (an inference). Plans read `PlayerObject` and `Target` (AITarget).
- **Story scripts.** A story script lists its states in `StoryModeEvents` (event name to state
  function); PGStoryMode.lua registers them with the state machine of PGStateMachine.lua, whose
  `main` runs the states. `Story_Event_Trigger(name)` switches to an event's state, and no stock
  script calls it, so the engine fires story events through it (from the campaign's story XML).
- **Engine objects.** Objects are userdata: the framework services `Object` only when
  `type(Object) == "userdata"`, and `Flush_G` asks userdata whether they are pool-safe. Scripts call
  their methods with a dot, without self (`unit.Move_To(healer, 10)`), so a method is a function bound
  to its object. Objects serve as table keys (`DeathTable[obj]`, `ProxTable[obj]`), so the same object
  must always be the same value. Orders return a block that `BlockOnCommand` polls with `IsFinished()`
  (the most used method, 140 scripts) and reads with `Result()`. `Get_Hull()` is a share of the hull
  (scripts compare it with 0.5 or 0.9), `Get_Type().Get_Name()` (121 uses) names the type, and
  `Find_All_Objects_Of_Type` takes a type name or an object type, and optionally a player. What
  `Result()` returns, and what a method on a dead object does, are not documented: the engine answers
  `Result()` with whether the order ended as ordered, and dead objects with nil (engine choices).
- **Object scripts and hints.** A type names its game-object script in XML `Lua_Script` (a module
  name under `Data/Scripts`, 153 stock definitions, 78 types directly). A placed object's hint, which
  `Find_Hint(type, hint)` and `Find_All_Objects_With_Hint(hint)` look up, is a NUL-terminated string
  in mini-chunk 1 of chunk 0 inside the object's 0x482 container in the TED (2,450 stock placed
  objects, 1,713 distinct hints; 1,575 of the 1,670 hint names the stock scripts use appear there).
  A second string, in the object's 0x46e container (5,246 objects, 19 distinct non-empty values
  such as `commanderspawn`), is not identified yet. Proximity is `Event_Object_In_Range(handler,
  range[, player])` on an object, evaluated when the object is serviced (`Service_Wrapper`, which
  `PumpEvents` and `Process_Proximities` call), with handlers `(self, other)` that usually cancel
  themselves; that every object in range triggers the handler on every service is an engine
  choice.
- **Debug output, the script, modes, categories.** PGDebug.lua formats messages and hands them
  to three engine sinks: `_ScriptMessage` (ScriptMessage, DebugMessage; the most used engine name,
  304 scripts), `_MessagePopup` (MessageBox), and `_OuputDebug` (OutputDebug, so spelled). The
  `Script` global stands for the running script: messages start with `tostring(Script)`, and
  `Script.Debug_Should_Issue_Event_Alert()` asks whether events should raise popups. Scripts compare
  `Get_Game_Mode()` with "Land", "Space", and "Galactic". A type's categories are its XML
  `CategoryMask`, names separated by `|` (840 stock definitions: Structure, Upgrade, Infantry,
  Capital, LandHero, ...), which `Is_Category("Infantry")` tests.
- **Attack-move and guard.** `Attack_Move(destination[, threat])` (268 stock calls in 79 scripts)
  takes an object (an enemy, a hero, a marker, a task force) or a position, and 70 calls wait on its
  block with `BlockOnCommand`; the second argument comes from the AI (`Get_Self_Threat_Max()`).
  `Guard_Target(anchor)` (174 calls in 71 scripts) takes a position, often the unit's own
  (`unit.Get_Position()`, holding an area), or an object (a structure, a base, a hero, a task force),
  and nothing waits on it. The engine's semantics (engage hostiles within 1.25 x weapon range on the
  way, break off beyond 2 x weapon range, guard radius the weapon range but at least 150 units, a
  guard of an object follows it and ends when it dies) are engine choices to check in play.
- **Assist lines.** When the player orders two or more squads of different units together, the
  unit with the higher priority answers with its own line as usual, and one of the others answers
  with its assist line after a short pause: Luke Skywalker ordered to move with two rebel
  squads says "Understood!", then the troopers "Escorting!" (owner's account, 2026-11-12). The
  engine plays `SFXEvent_Group_*` or the usual response first (a hero's group line wins), then the
  first unit of another type with an `SFXEvent_Assist_Move` or `_Assist_Attack` line 0.8 s later
  (the pause and the choice of who speaks are engine choices).
- **Space ships avoid contact.** Ships do not push each other in the original, friendly or
  hostile; they steer to avoid contact, and units that are small enough, or at another height,
  fly above or below other units (owner's observation, 2026-11-10; it replaces the engine's earlier
  assumption that ships separate by pushing). The engine's avoidance rules and numbers
  (`avoid_*` in `simulation.hpp`) are engine choices; flyers still separate from squadron mates.
- **Space obstacles.** Immobile space objects with the `SPACE_OBSTACLE` behavior carry
  `Space_Obstacle_Radius` (docks 130, asteroids 30 to 280, wrecks and junk 250 to 800, the Death
  Star chunks 1,600). Asteroid fields (`Is_Asteroid_Field`), nebulae (`Is_Nebula`), and ion storms
  carry one too, but ships enter those (the stock data has sounds for moving into nebulae and
  asteroid fields, and the impassable stand-ins are separate `_NonPassable` types with
  `Is_Impassable_Asteroid`); the `_NO_Collision` junk variant drops both the behavior and the
  radius. The engine keeps ships clear of the others by that radius, and of starbases (which give
  only `Custom_Soft_Footprint_Radius`, 100 to 550) by their soft radius (2026-11-11; whether
  fields block capital ships in the original is unverified).
- **Scripted conditions.** `Make_Invulnerable(true|false)` (165 stock calls),
  `Set_Cannot_Be_Killed(true|false)` (75), `Suspend_Locomotor(true|false)` (266),
  `Take_Damage(amount)` (almost always a kill: 10000, 9999, 99999), `Teleport_And_Face(marker or
  position)` and `Teleport(target)`. The engine's reading: invulnerable takes no damage at all;
  cannot be killed stops at one hit point (or a hundredth of the health); `Take_Damage` hits the
  hull past shields and armor; a suspended locomotor holds a unit with its orders; teleporting takes
  an object's heading. All are inferences from the names and uses.
- **Random numbers.** `GameRandom(lower, upper)` returns an integer in the inclusive range (scripts
  index lists with it and call `GameRandom(1, 1)`), `GameRandom.Get_Float()` a number in [0, 1), and
  `GameRandom.Free_Random(lower, upper)` the same as `GameRandom`; that it is the unsynchronized
  variant is an inference from its name. The host serves all three from its seeded stream.
- **Engine names are tables too.** `WeightedTypeList.Create()`, `FogOfWar.Reveal_All`,
  `GameRandom.Get_Float()`: some engine globals are tables of functions, and objects returned by engine
  functions carry methods (`Object.Get_Owner().Get_Faction_Name()`).
- **What scripts call.** The first survey (creation and main loops, no events, 10 game seconds)
  reached 46 functions and 18 methods. With AI plan threads, story events, task force events, and
  callbacks (about 2,900 events over 370 scripts) it reaches 167 functions and 129 methods; 317
  scripts run without errors, and most remaining errors come from story states fired out of their
  campaign order or from stubs where a script expects a number. Most used by script count:
  `_ScriptMessage`, `GetCurrentTime`, `_ScriptExit`, `_MessagePopup`, `Find_Player`,
  `Find_Object_Type`, the task forces' `Produce_Force`, `Find_Hint`, `Get_Game_Mode`,
  `Find_First_Object`, `Find_All_Objects_Of_Type`, then the cinematic and story functions
  (`Spawn_Unit`, `Lock_Controls`, `Story_Event`, `Start_Cinematic_Camera`). The ranked list with
  argument types comes from `lua-survey --report`.
- **Choice of runtime.** Which Lua VMs exist, what they cost, and why the host stays on Lua 5.0.3
  for the original's scripts: [lua-runtime-research.md](lua-runtime-research.md).
