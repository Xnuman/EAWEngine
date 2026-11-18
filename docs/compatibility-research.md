# Compatibility research

Evidence and findings about the original game's formats and behavior: corpus measurements,
observations of the game, readings of public references, and what remains open. Each finding says
how sure it is. What the engine implements, and how, is in [status.md](status.md); open work is in
[roadmap.md](roadmap.md).

## Sources and how they are used

The research uses, in descending confidence: public documentation, the original Alamo Direct3D 9
shader sources that Petroglyph published with the official Empire at War mod tools
([petroglyphgames.com/eawmodtool](https://www.petroglyphgames.com/eawmodtool/)), black-box
observation of the game, corpora built from the user's installed data, and marked inference. It
does not use leaked source code or code decompiled from the original executable, and the engine
never modifies or redistributes the installed data.

- **The shader sources** are kept locally under `Reference Materials/Shaders/` (git-ignored) as
  evidence for parameter, transform, and render-state semantics. They are never compiled or
  shipped; the D3D12 shaders are written for this engine against engine-native material
  contracts, and a checkout without them must configure, build, test, and run. No other original
  source, binary, or asset may be added without the owner's authorization and a provenance review.
  Because contributors read these sources, the documentation does not call the renderer
  clean-room. The mapping is in [legacy-shader-mapping.md](legacy-shader-mapping.md).
- **The map editor's `Source` folder** (the EAW and FoC XML with developer comments, the enum
  files, and the stock Lua in decompiled form), from a public, unlicensed redistribution, was read
  locally as evidence for the tag survey ([xml-object-tags.md](xml-object-tags.md)), like the
  installed data: none of it is in the repository, and its comments are paraphrased, never quoted.
- **Owner observations** of the retail game (Forces of Corruption) are cited as such; they are
  qualitative unless a measurement is given.
- **EaW-Revised** (GPL-3.0) is read as evidence, never copied or closely paraphrased. Its run-time
  recordings of the publicly available FoC debug build are observations and may be cited (see
  [Recorded rules from EaW-Revised](#recorded-rules-from-eaw-revised)); its findings that rest on
  static reading of disassembled code are left aside.

Third-party references (credited where used below):

- AlamoEngine-Tools organization: <https://github.com/AlamoEngine-Tools>
- PetroglyphTools format libraries: <https://github.com/AlamoEngine-Tools/PetroglyphTools>
- Petroglyph game and mod discovery: <https://github.com/AlamoEngine-Tools/PetroglyphGameInfrastructure>
- XML schema project (MIT): <https://github.com/AlamoEngine-Tools/eaw-schema>
- Stock-data baseline index: <https://github.com/AlamoEngine-Tools/eaw-baseline>
- ModVerify (MIT), its list of assets the executable loads by name (`HardcodedEngineAssets.cs`)
- pg-starwarsgame-lsp (MIT), its projectile preview
- Petrolution file-format index: <https://modtools.petrolution.net/docs/Formats>
- MEG v1 format: <https://modtools.petrolution.net/docs/MegFileFormat>
- Shared chunk format: <https://modtools.petrolution.net/docs/ChunkFileFormat>
- Petroglyph Lua object format: <https://modtools.petrolution.net/docs/LuaFileFormat>
- alo-viewer (MIT, Copyright (c) 2017 GlyphX Tools): particle and bone-billboard field meanings
- GlyphX particle editor (github.com/GlyphXTools/particle-editor, MIT, Copyright (c) 2017 GlyphX
  Tools)
- DrKnickers' fork of the particle editor (github.com/DrKnickers/particle-editor, MIT, 2026; guide
  at drknickers.github.io/particle-editor)
- eaw-fast-consistent-projectiles (GitHub, MIT), a modder's rewrite of the stock projectiles
- rebel5555's projectile tutorial (ModDB, 2009)

## Open research questions

To be settled with small black-box tests and recorded fixtures: Lua VM and coroutine semantics; XML
value and reference rules beyond the public schema; floating-point and tick rules; tactical
pathing; random-number streams; save and replay structure; network determinism. Black-box
comparison with the original stays the authority wherever an observed edge case differs from a
rule below.

## Installation layout and VFS precedence

The Steam installation (application ID 32470) has a base runtime `GameData` and an expansion
runtime `corruption`. Both use `Data/megafiles.xml`, loose `Data` subdirectories, main MEG
archives, patch archives, nested audio MEGs, DAT localization, and 64-bit executables. The
verified archives begin with the MEG v1 filename and file counts and use length-prefixed filenames
plus 20-byte file records.

Precedence, as observed: manifest archives in their declared order, then unlisted archives in
lexical order, `Patch.meg`, numerically ordered patch archives, and `64Patch.meg`. Loose files
override archives; FoC overrides inherited EAW data; mods are ordered with the first at highest
priority. A synthetic corpus locks this contract, including fallback and case and separator
normalization.

The SFX archives are an exception; see [Audio](#audio).

## XML

### XML loading

The game reads its XML through registries. `GameObjectFiles.xml`, `SFXEventFiles.xml`,
`FactionFiles.xml`, `CommandBarComponentFiles.xml`, `CampaignFiles.xml`, `HardPointDataFiles.xml`,
`TargetingPrioritySetFiles.xml`, `TradeRouteFiles.xml`, and `MousePointerFiles.xml` each list the
files of one kind, loaded in list order. Other files are opened by name (`GameConstants.xml`,
`MovementClassTypeDefs.xml`, `SurfaceFX.xml`, and 23 more), some folders are walked whole (`Enum/`,
the AI folders), and campaigns reach their story files by reference.

Source: the public eaw-schema (Alamo Engine Tools, MIT, `eaw/meta/metafiles.yaml`). It also gives
each registry's rule for a name that two of its files define: the first keeps it for game objects,
SFX events, and factions; the last for hardpoints, campaigns, trade routes, command bar components,
and mouse pointers; targeting priority sets merge (read as first-wins here; no stock set is defined
twice). The schema says the rules were measured but not how. They are a reference's claim, to be
confirmed by a black-box test (two registered files defining one unit with different costs).

`XmlAssetDatabase` follows it: registered files first, in list order (a last-wins registry's
reversed, so the first definition a lookup finds is the winner either way), then every other file.
A definition in a file nothing reaches is left out when its element type is one the game loads only
through lists (it occurs in a file reached through a registry or a reference). A type never loaded
through a list stays, since the schema's list of files opened by name may be incomplete.

Stock FoC: no registry entry names a missing file. 208 files are unreached, holding 4,725 story
`Event`s and 88 `Campaign`s of EAW's campaigns (left out: FoC's `CampaignFiles.xml` does not list
them) and 17 black market items and ambient map sounds (kept). No game object is in an unreached
file. 122 game-object names are defined twice.

### Syntax

All 638 resolved stock FoC and EAW XML documents parse. Petroglyph data may contain several
top-level elements, and three FoC AI template files use `<?xml ...>` without the standard closing
`?`. The reader accepts that declaration form and still rejects mismatched and unterminated
elements.

### Schema coverage and typed references

The schema loader reads a local eaw-schema checkout (never downloaded at runtime): the type, tag,
enum, hardcoded, metafile, and kind YAML subset, with FoC additions layered over EAW and file
registries followed through the resolved VFS. Canonical element names and metafile registries
type 10,977 of 21,283 named stock definitions; conservative, unambiguous structural matching raises
validation coverage to 20,881, leaving 402 untyped.

Typed object references distinguish 145 missing names from 34 existing targets that do not satisfy
their declared schema type or object kind. Kind predicates use effective `Behavior` and boolean
flags inherited through `Variant_Of_Existing_Type`, plus reverse membership fields. The residual
cases remain research inputs: schema declarations, structurally inferred types, and permissive
shipped data can each need further evidence before a warning becomes an engine rule.

### List separators

Every list in game data is read with one rule, `split_list` (core/string_utils): commas, `|`, and
ASCII whitespace all separate, and empty items are dropped. Evidence, over the 638 winning FoC XML
files (33,245 comma lists in 410 tags):

- Whitespace inside a comma item occurs in 153 values. In every list field the engine reads it is a
  missing comma, a line break between items, or padding around a pipe, never a name with a space;
  the rest are prose (encyclopedia text) and AI scoring expressions.
- Pipes appear only in masks (`CategoryMask`, `Space_Layer`, category and property fields, the
  movement class definitions) and in AI scoring fields the engine does not read.
- Compared with per-field readers (commas only; pipes only; commas and whitespace), the single rule
  gives the same items for all 8,019 stock values of the fields they parse, with one exception: a
  `Unit_Exclusions` list in `GroundUnitTargetingPriorities.xml` has a line break instead of a comma
  between two type names, which a comma-only reader takes as one name.
- `Underworld_Merc_Squad`'s member list has no commas at all.

Readers of "names, number" groups (`Attack_Priorities`, inaccuracy lines) take a run of names up to
the next number, so a mask written with `|` stays one entry. Mod data may differ; the rule is an
engine choice backed by stock data, not an observation of the original's parser.

### Numbers in XML fields

577 stock values follow a number with text, nearly all the C float suffix (`Select_Box_Z_Adjust`
160 times, `Spin_Away_On_Death_Time` "2.0f", `Spawn_Indigenous_Units_Radius` "150.0f"), a few
hexadecimal (`PixelShaderVersionHEX` "0x0104") or sizes ("1x1"); two values carry a leading `+`.
The engine reads every numeric field like C's `atof`, taking the leading number (`parse_xml_number`,
`TypeFields`), and reports text after the number other than the suffix. That the original reads
them so is an inference from the data working in the game.

## Textures and materials

The stock texture corpus has 2,213 resolved DDS files, all valid: 168 BC1, 181 BC2, 1,656 BC3, 37
24-bit BGR, 169 32-bit BGRA, and two 32-bit BGRX, with 17,854 mip levels in total and a maximum
extent of 2048 by 2048. There are no stock cube or volume textures (their DDS metadata and layout
are handled for mods). The FoC command-bar mega-texture directory holds 1,107 named atlas
rectangles, 997 with alpha, and resolves to its 2048-square compressed DDS.

Across 1,379 stock ALO models, 9,294 material instances use 35 shader names and seven
texture-parameter names. The decoded material keeps the source shader and all raw parameters; the
family, blend, and texture-role hints are a separate layer. Profiles derived from the reference
shaders type 7,808 of the 9,294 instances (31 of the 35 names); `alDefault.fx`, `Default.fx`,
`MeshAdditiveReflection.fx`, and `FissureMeshGloss.fx` have no reference source and remain
name-heuristic. From the reference corpus: shields and nebulae are additive and two-sided, heat
meshes target a distortion buffer, planet cloud maps are layers rather than base or normal maps,
and team-color masks always come from base-texture alpha. Details and remaining uncertainty are in
[legacy-shader-mapping.md](legacy-shader-mapping.md). 14 of 1,286 unique model texture references
are absent from the final layered VFS (reported with example models).

Cube reflection is rare. `MeshBumpReflectColorize` and `RSkinBumpReflectColorize`, the only
reference shaders that sample the engine's sky cube, are used by no stock material;
`MeshAdditiveReflection.fx` (no reference source) by three. The reference math (in the shader
mapping) reflects the view direction about the vertex normal in world space, samples an
engine-supplied cube, and scales by `Specular` and normal-map alpha. No stock cube textures exist,
so the original presumably builds the cube from the sky. Not implemented until a mod needs it.

Faction definitions carry a 0..255 RGBA `Color`. A placed object's owner is a player slot chosen at
runtime, so previews color team-color masks with the type's first `Affiliation` faction; types
without one fall back to a player-slot palette (an engine choice).

## Scripts: corpus and bytecode

The final layered FoC and EAW script corpus has 370 Lua source files, 85,312 lines; the 64-bit
patch archives override older compiled copies at lower layers. Lexical inspection finds 19
dependency spellings, all resolved to VFS winners, 1,725 defined function names, and 3,364 called
function names, without warnings or parse failures. The host-call inventory this gives is evidence,
not a promise that every name is engine-provided: object methods and dynamically assigned
functions can only be classified with runtime types. What the scripts need of the host is in
[Lua host protocol](#lua-host-protocol).

Mod bytecode follows the published Lua 5.0 structure and is inspected without executing it.
Standard chunks use the `Lua` signature and version `0x50`; Petroglyph chunks use `Lup`, version
`0x51`, and add a persistent-function integer after `lineDefined` in every prototype. The parser
validates native-size metadata, strings, constants, nested prototypes, instruction ranges, and the
Petroglyph ids within explicit resource limits. VM execution, coroutine scheduling, save-state
persistence, and host API behavior remain black-box research areas.

## Maps (TED)

The public Petrolution format pages were unavailable (HTTP 404) when the TED fields were
researched, so the findings below are corpus findings, not documented facts.

### Placed objects

A placed object names its XML type by CRC-32 (the reflected IEEE polynomial, as for DAT keys) of the
ASCII-uppercased type name. In FoC, 191,048 of 191,083 placed objects on all 269 maps match a named
XML definition, and 99.7% resolve through `Variant_Of_Existing_Type` to an existing
`Space_Model_Name` or `Land_Model_Name` model (649 unique models). EAW matches 137,488 of 137,499.
Eleven FoC CRCs (35 objects) match no definition, and one placed name is shared by several element
types; the resolver prefers the definition with a model for the map context (inference). The audit
indexes every XML file under `Data/XML`, not only registered ones.

Rotations are in degrees. Across FoC the third component is non-zero for 165,199 objects and ranges
over -611..360; the first two are non-zero for only 2,067 and 4,488. The engine composes rotations
about X, then Y, then Z; that order is inference.

**Handedness and facing.** The world is right-handed with Z up (X east, Y north): through a
left-handed view, maps come out mirrored against the original's editor (owner's screenshots of
`_LAND_PLANET_ANAXES_02`). A placed object's Z rotation is its facing, counter-clockwise from +X,
and models face -Y ([Model forward axis](#model-forward-axis)), so a model turns a quarter before the
placement rotation. Evidence: pieces laid end to end (train tunnels, walls, half-walls) follow
heading + 90 degrees to their nearest neighbour in 697 of 992 measured cases across the stock maps
(Tunnel_A 158 of 158, half-walls 48 of 51; most of the rest are parallel runs side by side), and
Anaxes 02 then matches the editor.

### Owner slots

Placed-object `player` slots (`EAWAssetInspector map-objects` breaks placements down by owner):
slot 3 holds 179,407 stock placements, almost all props, markers, decorations, and multiplayer
structure markers, so it reads as the world's neutral slot. Slots 0, 1, 2, and 6 hold armies,
bases, and squadrons (slot 6 also many props); slots 4, 5, and 7 mostly `Indigenous_Unit` types;
slots 9 to 11 occur on a few maps. Which slot a player takes, and which slots are allied, is decided
at runtime. Until players and factions exist the engine treats slot 3 as neutral and every other
pair of distinct slots as hostile (a provisional rule).

### Squadrons, companies, and containers

Squadrons, companies, and teams have no model of their own on the map and are expanded into their
members.

#### Squadron lists

A space squadron's `Squadron_Units` lists member types, and repeated `Squadron_Offsets` fields give
each member's offset in the squadron's local frame. A repeated `Squadron_Units` continues the list:
`Rebel_X-Wing_Squadron` writes "X-Wing, X-Wing" twice and "X-Wing" once beside five
`Squadron_Offsets`. Read that way, the 327 placed squadrons of the FoC corpus expand into 1,474
member ships. Offsets are in the facing frame: stock V formations put the lead at +X (`30,0,0`) and
the wings behind at -X.

#### Companies

A placed `GroundCompany`, `HeroCompany`, or `SlaveCompany` is its `Company_Units` members, although
most stock companies also name a `Land_Model_Name` (`RI_INFANTRY.ALO` on `Rebel_Infantry_Squad`).
That model appears to be a galactic or GUI representation; tactical companies always appear as
their members. Counted so, the stock maps hold 4,665 company members. `Rebel_Infantry_Squad` lists
15 `Squad_Rebel_Trooper` with `Max_Squad_Size` 5. The data gives no member offsets (members of land
containers are laid out by `ContainerArrangement`, a runtime formation), so the engine arranges
each company in its group-move formation facing the placed heading (an engine choice). 97 placed
`Container` objects have no member list and stay unexpanded. Teams (`Create_Team_Type`) are in
[xml-object-tags.md](xml-object-tags.md).

### Environments

Environment chunk 8 stores the active-environment index as a mini-chunk (type 0x25, four bytes),
not a raw integer; read raw it gives 1061 (0x0425, the mini header) on every map. Decoded so, all
269 FoC and 176 EAW maps name an index inside their environment list (0 to 5).

**Sky domes.** Each environment names primary and secondary sky domes. The names are XML objects
(`SpacePrimarySkydome`, `SpaceSecondarySkydome`, `LandPrimarySkydome`, ...) whose
`Space_Model_Name` or `Land_Model_Name` supplies the model; the definitions mark them `In_Background`
decorations, and primary domes carry the `SKY_DOME` behavior. The engine draws them camera-centered
behind the world, primary first; `Scale_Factor` (1 primary, 25 secondary) and any parallax of the
original are not modeled.

**Lighting.** Every stock environment (624 across both games) stores the same lighting block in
mini-chunks 0x00-0x0d: RGB colors of a key light and two fill lights (0x00-0x02), a specular color
(0x03), an ambient color (0x04), intensities (0x05-0x07), azimuths in radians (0x08-0x0a, 0 to
2 pi), and elevations in radians (0x0b-0x0d, about -pi/2 to pi/2). Keys are typically white and
above the horizon, fills tinted and usually below it, which matches the reference shaders'
engine-supplied key light plus a fill term. The engine takes each direction as pointing from the
surface toward the light and scales colors by intensity; the axis convention, and scaling the
specular color by the key intensity, are inferences.

**Fog** (`map-audit`'s environment field survey): 0x0f is an RGB color, 0x10 a one-byte switch,
0x11 and 0x12 the start and end distances. Of the 135 land environments the stock maps use, 123
switch it on with authored colors and ranges (Utapau tan from 1500 to 5000, Fondor 0 to 3500, the
Sith tombs dark grey from 0 to 1255); the 12 switched off keep the editor's defaults (white, 1000
to 10000); no space environment uses it. The reference shaders make fog linear in view distance
and skip it on additive, shield, heat, sky, and nebula surfaces.

**Wind and clouds** (per-map values from `inspect`). 0x2c reads as the wind speed on a 1-10 scale
(2 on most maps, 7 on the blizzard maps Hoth, Ilum, and Polus, 10 on stormy Kamino and Honoghr; the
reference grass shader calls 10 a strong wind) and 0x2b as its direction in degrees (0 on most,
30-120 elsewhere). 119 of the 135 land environments carry 0x2b-0x2f. 0x2d is the cloud texture's
repeat in world units, 0x2e its drift speed (0-80, 30 on most), 0x2f the cloud texture, and the
drift follows 0x2b: the terrain-information chunk's 0x10 is speed x (cos, sin) of that direction
((30, 0, 0) on Dantooine 02 with 0x2b 0 and 0x2e 30; components up to 21.21 = 30 sin 45 elsewhere),
and its 0x0e and 0x0f repeat the texture and scale. To confirm with the editor's labels; the angle's
zero and sense are not established.

**Weather.** Field 0x29 names a weather scenario; see [Weather](#weather). Other fields are
preserved raw.

### Terrain samples and layers

Terrain samples (terrain chunk child 5, four bytes per vertex, row-major) pack more than elevation.
Bits 0-15 are the signed elevation. Bits 16-23 are a per-vertex terrain layer index: across 894,816
sampled vertices in 188 stock land maps it is always below the map's layer count, and horizontally
adjacent vertices share it 77.5% of the time. Bits 24-31 are a small signed value (mostly -1, 0, or
-2) whose distribution does not change at layer boundaries, so it is not a blend weight; it is
preserved raw. The engine places vertices 20 world units apart (column to +X, row to +Y) with world
height = elevation / 20; under that mapping, placed objects on 135 land maps sit a median 1.2 world
units from the interpolated ground.

Layers carry a tiling scale in mini 0x06 (about 64 world units per repeat in most stock layers), the
color and normal textures (0x0c, 0x13), and fields not interpreted yet (angles in 0x08 and 0x09,
RGB colors in 0x0a and 0x0b, flags, an integer).

**Untextured layers.** 3,369 stock layer entries name no color texture (their mini 0x14 is always
0; textured layers use 1-16). They are painted widely: 40 of 135 stock land maps use them, 39 above
the liquid level. The Bespin maps paint 63% (`_LAND_PLANET_BESPIN_01`) and 79%
(`UM07_CLOUD_CITY_CONFLICT`) of their samples with them, with the liquid below all terrain, and
Kamino paints its ocean floor and platform slopes with one. The engine reads such a layer as
covering nothing: terrain is not drawn where untextured layers dominate, so the sky shows through
(on the 20-unit sample grid). This is inferred from those maps' intended look (a city floating
over clouds, platforms over the sea), not measured.

### Liquid surface

The terrain-information chunk describes one liquid surface per land map. Mini 0x16 is its type
across 188 stock land maps: 0 (41 maps) on dry worlds such as Tatooine and Kessel; 1 (127) on water
worlds such as Kamino, Mon Calamari, and Dagobah; 2 (11) with lava textures on Mustafar, Sullust,
and Shola; 3 (9) with ice textures on Ilum, Polus, and Alzoc III. Mini 0x15 is the surface height in
world units (Cloud City's -150 lies below all its terrain; Kamino's 7 covers about 60% of the map).
Mini 0x1a is an RGB tint, and minis 0x1d-0x1f name textures whose roles depend on the type (for
water 0x1d is a bump map and 0x1e a plain-color fallback; lava and ice store their color texture
first).

Survey of the 92 water maps (`map-audit`): 0x17, 0x18, 0x19, and 0x20 are scalars 0..1 on steps of
1/81 (slider positions, it seems; 0.5 is the untouched value; 0x20 is mostly 0.8-1, 0x17 varies
most, 0.25-1); 0x1b is a second color, white on every map; 0x1f a third texture (lava's
`w_lava00_bc.tga`, empty on water); 0x21 is 0.001-17 (10 on most maps); 0x22 is 0-0.81 (0 on most).
These and the remaining fields (apparently opacity, tiling, scroll speed, cloud texture, scale, and
wind) are preserved raw; their meanings need the editor's labels or captures of the game. The
engine's water tiling and scroll speeds are its own choices.

### Terrain tracks and map decals

Terrain tracks (terrain chunk child 9, chunk 0x100 each; 553 in 31 stock land maps) are textured
ribbons: rivers, shoreline waves, station lights, beams, girders, and scrolling text. Chunk 0x102
lists control points (mini 0x07, world X and Y with Z = 0), 0x104 a width per point (mini 0x08; every
stock track has one width per point), and 0x103 the texture. Chunk 0x105 holds three values, (1, 1,
1) on 391 tracks and (0, 1, 0) on 151, read as opacity at the start, middle, and end. Header minis
0x04 and 0x05 are small values on wave tracks, read as texture scroll per second. The rest (0x00 is
10 on every track, 0x0d ranges 0-181, two flags) is preserved raw.

Map-placed decals (terrain child 6) occur in one stock map, `UM11_RAIDERS_OF_THE_LOST_HOLOCRON.TED`:
65 chunks 0x10 of mini-chunks with no texture name, so they are not drawn. Mini 0x03 is the position
(world X, Y, Z = 0) and 0x05 the size (15 x 15 on 64, 19 x 19 on one). Mini 0x02 is 3 on 64 and 6 on
the larger one: read as an index into a decal-type table the data does not carry, which links it
to no texture. The rest is constant on all 65 (0x00 = 0, 0x01 = 1, 0x04 = 0, 0x06 = 1.0, 0x07 = 0,
0x08 = 1, 0x0a = 0, 0x0b = 1). A screenshot of the mission in the original would show what they
are. Decals made by units and
explosions are in [Terrain decals and dynamic tracks](#terrain-decals-and-dynamic-tracks).

### Passability

The passability raster (root chunk 0x10a, stored X-major) holds one of six values per cell across
all 269 stock maps; each has exactly one bit set, so they behave like an enumeration.
Cross-tabulated with terrain: 0x1 (52% of cells) is flat ground (mean slope 2.5 units per 20-unit
cell); 0x2 (40%) steep, void, or deep ground (mean slope 12.3; 16.6% below the liquid, 11.7%
untextured); 0x20 (3%) lies 95% below the liquid surface and reads as water; 0x100 (4%) is steep and
often untextured; 0x80 and 0x4 are rare and flat. Kamino marks half its flat seabed 0x1, so the
values are authored, not derived from geometry.

The values name the passability types of `Data/XML/MovementClassTypeDefs.xml`, whose entries list,
per movement class, the types it may enter (`Infantry` is `Clear | Shield` in EAW and `Clear |
Shield | InfantryOnly` in FoC; `Hover` adds `Water`; `Flying` and `Space` allow nearly everything).
0x80 and 0x100 occur only in FoC's `maps.meg`, the expansion that added `InfantryOnly` and
`Impassable`, so they are those types; 0x1 is `Clear`, 0x2 `Steep`, 0x20 `Water` (from the geometry
above). 0x4 is read as `Obstacle`, the remaining static base-game type, with medium confidence.
`Shield`, `RebelWall`, and `EmpireWall` are applied at runtime by objects (`OverridePassability`,
`SecondaryOccupationPassability`) and never appear in stock rasters; the engine gives them
provisional bits.

Each ground entity gets the mask of its XML `MovementClass` (`Clear | Shield` when the class is
missing or unknown) and stops before a cell outside it; one standing on such a cell may still leave
(engine choices). No official documentation of the raster was found (the mod-tool pages and
community guides describe painting passability in the editor, not its encoding); comparison with
the original editor remains the check.

## Models (ALO)

### Model forward axis

Alamo models face -Y. Every stock muzzle and fire bone inspected sits at negative Y and points along
-Y in the bind pose: the T2B, AT-ST, and X-wing `Muzzle*` bones, and the Star Destroyer's `FP_*` fire
bones (front hardpoints at y = -280, engines at y = +183). An entity's heading is therefore the
direction of its model's -Y axis. A fire bone's X axis is its firing direction (`FP_F-L_00` points
forward-left, (0.57, -0.82)). `EAWAssetInspector inspect` prints each bone's bind-pose position and
X axis.

### Damage states and detail levels

Models encode alternate appearances in mesh and bone names (corpus findings):

- **`_ALT<n>`** marks a damage state. NB_BASESHIELDGEN carries `base_ALT0`-`ALT3` and
  `NB_dish_ALT0`-`ALT3`, all visible in the bind pose; the original shows one according to health.
  158 of 1,379 stock models use ALT names (index 0 on 557 meshes, 1 on 530, 2 on 418, 3 on 267, 4-12
  rarely), on meshes for buildings and mostly on damage-smoke emitters for vehicles
  (`p_smoke_small_thin_ALT1`); space ships have none. ALT0 is read as the intact state.
- **`Land_Damage_Thresholds`** (222 types: structures, buildables, vehicles, a few space structures)
  gives health fractions, typically `1, 0.66, 0.33` (127 with a final `0`);
  `Land_Damage_Alternates` gives the `_ALT<n>` state for each threshold, and `Land_Damage_SFX` a
  sound on entering each state (`null` for none).
- **`_LOD<n>`** marks a detail level. 145 models use them, most of them infantry and vehicles, many
  skinned; summed triangles by index are 68,881 (LOD0), 129,068 (LOD1), 123,516 (LOD2), and 12,315
  (LOD3, five meshes). LOD0 is the sparsest level in 137 of 145 models and the densest is usually the
  highest index, so higher indices are read as more detailed. 80 types set `LOD_Bias` (0.5-10), and
  the graphics presets (`GRAPHICDETAILS.XML`) set `MeshDetail` 0-1 beside `ParticleDetail` and the
  other detail options.

The engine's default appearance shows ALT0 and each model's highest LOD index, switches levels by
distance, and switches states by health.

### Bone billboards

An ALO bone carries a billboard mode that turns the meshes on it. The MIT-licensed alo-viewer names
the values 0 none, 1 parallel, 2 face, 3 about Z toward the view, 4 about Z toward the light, 5 about
Z toward the wind, 6 sun glow, 7 sun; it treats billboarded meshes as authored facing -Y, turns 1 and
2 with the camera, 3-5 about the vertical axis, and places 6 and 7 at the bone's offset from the
model turned toward the sun, facing the camera.

`texture-audit` counts stock meshes by mode: 0 on 9,013, 1 on 10 (`W_PLANET_FLEET` glows), 2 on 1
(a test model), 4 on 97 (the `SHADOW` cards under trees), 5 on 3 (flags), 6 on 109 (every planet's
glow disc), 7 on 17 (the suns of sky domes). Drawn fixed, a planet's glow (a flat disc 1.4 planets
wide through its center) shows as a bright band across the globe (the owner's report).

The engine follows alo-viewer's reading, except that its turns replace the whole orientation in
world axes rather than applying the camera's rotation in the model's own space (which agrees only
for unrotated models), and "toward the light" uses the environment's key light. Whether mode 4
points the card toward or away from the light, and mode 5 with or against the wind, is alo-viewer's
convention, unchecked against the game. Billboards on attached models (hardpoints) stay fixed.

### Proxies

Model proxies name their effect without directory or extension (`pe_Z95engines` is
`Data/Art/Models/pe_Z95engines.alo`). Station and capital-ship models also carry proxies tagged like
meshes: `p_explosion_med00_ALT1` and `_ALT2` show in damaged states and are skipped on the intact
model; the tag is not part of the file name. Proxies flagged hidden are shown only by gameplay. Some
proxies name files that do not exist (`Lensflare0`); they are reported once per build.

Which proxy is an engine glow is not stated anywhere: the engine's rule (proxies named `pe_*`, the
stock naming, or under an `Engine_Particles` bone) is naming inference.

### Hardpoint models, damage effects, and scorch meshes

- **Attached models.** A hardpoint's `Model_To_Attach` (226 stock uses, such as
  `EV_StarDestroyer_HP00_M-C.alo`) is a separate model placed at its `Attachment_Bone`. The host hull
  has openings where they sit, so without them ships look gutted. The engine merges each into the
  host's mesh at the bone's bind pose, bound rigidly to that bone, and removes it when the hardpoint
  is destroyed (status.md).
- **Damage effects.** Hardpoint XML names a `Damage_Particles` bone (268 stock uses:
  `HP_F-R_EMITDAMAGE`, `HP_E_EMITDAMAGE`, ...), and the models hang their damage proxies under it: the
  Star Destroyer's `p_hp_imperial_damage` smoke sits on children of `HP_F-R_EmitDamage` and its
  siblings, flagged visible like any other proxy. Visibility alone does not separate them, so the
  engine treats proxies at or below a hardpoint's damage bone as that hardpoint's damage effect,
  hidden until it is destroyed. `Engine_Particles` likewise names the engine-glow bone
  (`HP_E_MAINENGINES`, 21 uses).
- **Scorch meshes.** `Damage_Decal` names a bone (`HP_*_BLAST`, 245 stock uses) that carries a small
  alpha-blended mesh of its own: the Star Destroyer has seven (`HP_F-R_Blast` and so on, `MeshAlpha`
  with `W_blast00.tga`), visible in the model like the hull, so drawn as authored they char an intact
  ship. The engine hides the bone and its children until the hardpoint is destroyed, an inference
  from the field name and from intact ships in the original showing no scorch marks.

### Unit Scale_Factor

465 stock unit types set a `Scale_Factor` other than 1 (0.17 to 4.0: X-wings and TIE fighters 0.7,
the Corellian corvette 0.5, the AT-ST 1.6, Star Destroyers 1.0), inherited through
`Variant_Of_Existing_Type`. The engine draws a type's model at that scale and measures what it
derives from the model through the same scale: separation radii and obstacle footprints from model
bounds, hardpoint and fire-bone positions from bones (so weapons fire from the scaled hull), and
attached effects, particle sizes included. An XML `Custom_Soft_Footprint_Radius` stays as written.
That the original scales collision and hardpoints with the model is an inference (it draws the model
scaled, and its bones move with it). Engine glows tuned against the owner's screenshots were
measured on unscaled X-wings and need rechecking at 0.7.

## Movement

### Who walks on the ground

Whether a unit walks on land-map terrain follows its XML `Space_Layer`: `Land` for every ground
vehicle, infantry, and land hero type with a speed; `None` for walkers such as Ewoks and R2-D2;
`Frigate`, `Capital`, or `Corvette` for air units such as snowspeeders and Kamino's air whales
(`Kamino_Aiwha`, which also has `Hover_Offset` 250 and `Layer_Z_Adjust` 100). Types without the field
(birds, space-only types, projectiles) are treated as not walking. Spawned flyers start at the
surface plus `Hover_Offset`. A corpus inference.

### Speed, turning, and acceleration fields

- **`Max_Speed`** is read as world units per 30 Hz logic frame (x30 per second), provisional; the
  particle editor fork's documentation of projectile flight agrees for projectiles
  ([Particle systems](#particle-systems)). GameConstants' `Object_Max_Speed_Multiplier_Land` and
  `_Space` (stock 1.56 and 1.2; developer comments in the source XML say they scale every type's
  maximum speed) apply on top, and `Object_Max_Health_Multiplier_*` (1.0 and 1.5) scale health.
- **`Max_Rate_Of_Turn`** (442 stock uses) is read as degrees per 30 Hz frame, the same convention:
  infantry 16 becomes 480 degrees per second, tanks 12-20, and capital ships 0.4-1.5 become 12-45
  degrees per second. 0.0, the most common value, turns instantly. Provisional on land; in space the
  recordings confirm the per-frame unit ([Recorded rules from
  EaW-Revised](#recorded-rules-from-eaw-revised)).
- **`OverrideAcceleration` and `OverrideDeceleration`** (about 148 stock types: ships .02-.065, tanks
  .2, infantry and heroes) are read as `Max_Speed` units per frame per frame (x900 per second
  squared), a guess: a fraction of top speed per frame fits the values as well. Walkers name none.
  In space the recordings fit the per-frame reading scaled by the space speed multiplier; on land
  nothing is recorded and the engine leaves acceleration and turn rates unscaled.
- **`Min_Speed_Fraction_For_Turn`** (21 types: walkers .1-.4 with `Stopped_Rate_Of_Turn` and
  `Has_Pre_Turn_Anim`, fighters and freighters .6-.7) is read as the speed fraction below which a
  unit turns at its stopped rate (ground) or not at all (space). Space ships that name none turn only
  at full speed (recorded; see straight start below). `Max_Thrust` (70 space types) is not read:
  its meaning is unknown.
- **Facing away.** Units turn at their rate and travel more slowly while facing away from their way
  (an engine choice). GameConstants' `CloseEnoughAngleForMoveStart` says 180 degrees, which would
  never make a ground unit stop to turn; the owner observes that land units do not turn while moving
  off, so the engine uses its own angle (types may name `Close_Enough_Angle_For_Move_Start`, the
  AT-AT 30).
- **Formation fields** sit on unit types, often inherited from shared `Container` base types
  through `Variant_Of_Existing_Type`: `FormationSpacing` mostly 0.5, `FormationOrder` 1-8,
  `FormationRaggedness` 0-2.5, `FormationGrouping` Standard 67 and Solo 13.

### Footprints and obstacles

- **Separation.** A unit's separation footprint is a quarter of the summed X and Y extents of its
  model bounds (an engine choice), or `Custom_Soft_Footprint_Radius` where present (3.5 for stock
  infantry). The original's collision meshes and `UnitCollisionClass` are not interpreted for
  separation yet.
- **Blocking structures** are the `GroundStructure`, `SpecialStructure`, and `SecondaryStructure`
  kinds, plus types whose `UnitCollisionClass` is High, Medium, or Low Obstacle. Build pads and other
  props do not block. A provisional reading. A footprint is the type's XML rectangle when it gives
  one, else the model bounds' rectangle; it is stamped as `Obstacle` into the passability grid.
- **XML footprints.** `Obstacle_Width` and `Obstacle_Height` are a full rectangle placed from its
  corner (`Obstacle_X_Offset`, `Obstacle_Y_Offset`; stock offsets are almost always minus half the
  size). `Custom_Hard_XExtent` and `Custom_Hard_YExtent` are half extents around
  `Custom_Hard_XExtent_Offset` and `_YExtent_Offset`: the T2B tank's 20 by 10 matches its 44 by 22
  chassis. The XML's x runs along the model's -Y (its forward) and its y along the model's +X: the
  off-center boxes of the rock and ruin props match their mesh bounds that way (W_ROCKCLSTR01:
  extents 24, 30 at offset 3, -10 against mesh bounds x -42..22, y -28..22; W_RUINS03 and
  W_TEMPROCKCLSTR03 likewise). Sizes are world units not scaled by `Scale_Factor` (the TIE crawler
  at scale 0.75 and the MPTL at 1.1 match their unscaled meshes; an inference).
  `Obstacle_Proxy_Type` lends another type's footprint (structures defer to their build pad).
- **Walls.** `Empire_Wall` and the matching Rebel wall (the `EB_/RB_BASEWALL-MID` barricades, 82
  placements on Utapau alone) set `OverridePassability` to EmpireWall or RebelWall. The movement
  classes that list those types (Flying, LargeWalker for RebelWall, Space) may cross them. The
  engine stamps a wall's footprint with its own type.

### Pathfinding

No public source documents how the original moves an individual unit. The owner-provided GlyphX
reference code (read-only, not quoted) does not contain the unit-level pathfinder, but shows a
separate AI layer: land task forces plan over a graph of AI hint zones with a threat-weighted cost
and the task force's passability mask, then issue ordinary moves toward each zone's center;
galactic mode has its own pathfinder between planets. The original therefore plans on at least two
levels; its cell-level behavior is unknown. The engine's grid A* is an engine-native choice; what
needs black-box comparison is the behavior players see (which ground a class may cross, how
unreachable orders end, how groups move), not identical routes. Design and research:
[pathfinding-design.md](pathfinding-design.md), [pathfinding-research.md](pathfinding-research.md).

### Group moves of vehicles

Observed by the owner in the retail game (screenshots, 2026-11-17), a group of T2-B tanks ordered to
move:

- Ten T2-Bs ordered as one group form two formations of five at the destination (ten destination
  markers in two blocks).
- A formation of five stands three in front and two behind, the two in the gaps of the three
  (staggered).
- Slots are not bound to tanks: the tanks nearest the destination take the front row, so the group
  spends little time reorganizing on arrival.
- On the way the tanks keep the formation where the ground allows. In a narrow passage the group
  breaks it and forms again after it, the leading tanks slowing down while the last ones catch up.

Inference, unverified: three and two fits a row width of the ceiling of the square root of the count
with each row centered (a shorter row behind falls into the gaps of the one before), but hexagonal
packing (every second row shifted half a pitch) or a fixed shape per count fit too. What splits ten
into two formations of five is unknown; GameConstants `Max_Formation_Area` (14.0) is a candidate by
name only. Observations that would decide: groups of 4, 6, 7, 9, and 15 T2-Bs (6 separates centered
rows from hexagonal packing; 7 and 15 show the split), a mixed group of vehicles and infantry, and
the order of the tanks through a narrow passage.

More observations by the owner (28 screenshots of the slot markers shown when an order is given,
groups of 2 to 13 squads, T2-Bs, and T4-Bs, 2026-11-18):

- Slots are assigned at the destination when the order is given. The facing points from the group
  toward the clicked point, and the slot layout keeps its shape and turns with the facing (seven
  T2-Bs ordered four ways). A slot marker's size follows its unit's footprint; a mixed group
  (eight T2-Bs and five T4-Bs) forms one layout of thirteen slots, not one per type.
- Shapes by count: 2 side by side across the facing; 3 a triangle, two in front and one behind; 4 a
  2 by 2 square; 5 a triangle of three and a pair; 6 two triangles; 7 a triangle and a square; 10
  two blocks of five, each a triangle and a pair. The parts of a layout stand offset diagonally:
  one ahead of the other and to one side.
- Which part leads is not fixed (with seven the smaller part, the triangle, leads the square; with
  five the larger part, the triangle, leads the pair), and units nearer the destination take the front slots; priorities
  (`FormationOrder`) do not strictly order them: infantry is not always placed before tanks.
- Units move toward their slots loosely: movement comes before keeping the formation. No shoving
  was reproduced: units do not move into other units and stop before contact.
- Arrived groups stand ragged, and arrived units keep the heading they arrived with; they do not
  turn to a common facing afterwards.

Inference, unverified, replacing the row rule above: every count seen fits halving. Two to four
units make one cluster (a pair, a triangle, a square); a larger group splits into the floor and
the ceiling of half its count, the halves placed offset diagonally, and each half splits the same
way (5 = 3 + 2, 7 = 3 + 4, 10 = (3 + 2) + (3 + 2), 13 perhaps (3 + 3) + (3 + 4)). The "three in
front and two behind" of five above is the same triangle and pair. Unobserved: how units are
matched to slots (the nearest unit per slot or a cost over the group), which side the rear half
stands on, and what `FormationSpacing` changes (slots of T2-Bs at 1.5 and T4-Bs at 1 both nearly
touch). Groups of 8 and 9, and two groups of different spacing side by side, would test the rule.

The engine on the main branch (removed on the `movement-redesign` branch): final slots go to members by how far forward they stand, as observed
(`take_final_slots`). It lays a group out in rows of the ceiling of the square root of twice the
count, centered (an engine choice: five stand four and one), splits a selection into formations only
by distance (`frame_split_distance`, 400), and keeps the formation through narrow places, sending a
member whose slot is out of straight reach by way of the formation's center.

### Space ships avoid contact

Ships do not push each other in the original, friendly or hostile; they steer to avoid contact, and
units small enough, or at another height, fly above or below other units (owner's observation). The
engine's avoidance rules and numbers (`avoid_*` in `simulation.hpp`) are engine choices; flyers
still separate from squadron mates.

### Space obstacles

Immobile space objects with the `SPACE_OBSTACLE` behavior carry `Space_Obstacle_Radius` (docks 130,
asteroids 30 to 280, wrecks and junk 250 to 800, the Death Star chunks 1,600). Asteroid fields
(`Is_Asteroid_Field`), nebulae (`Is_Nebula`), and ion storms carry one too, but ships enter those:
the stock data has sounds for moving into nebulae and asteroid fields, and the impassable stand-ins
are separate `_NonPassable` types with `Is_Impassable_Asteroid`. The `_NO_Collision` junk variant
drops both the behavior and the radius. The engine keeps ships clear of obstacles by that radius,
and of starbases (which give only `Custom_Soft_Footprint_Radius`, 100 to 550) by their soft radius.
Whether fields block capital ships in the original is unverified.

### Fighter flight

- **What the engine reads.** Types whose `SpaceBehavior` names `FIGHTER_LOCOMOTOR` (the stock
  fighters and bombers in `SpaceUnitsFighters.xml`; the X-wing has `Max_Speed` 4.0, `Min_Speed` 2.5,
  `Max_Rate_Of_Turn` 3.0) fly at no less than `Min_Speed`, on the same provisional scale as
  `Max_Speed`. Without a `Min_Speed`, half the top speed is used.
- **Observed in the original** (the owner's black-box play of Forces of Corruption; qualitative,
  nothing measured):
  - fighters fly in a single plane most of the time;
  - when squadrons attack each other, the fight becomes a dogfight that further squadrons can join:
    fighters chase each other and leave their plane, climbing and diving in loops;
  - a squadron ordered to roughly reverse its direction turns around through a vertical half-loop
    (a "backflip") outside its plane instead of a flat turn; the angle range that triggers it is not
    known;
  - fighters of one squadron push overlapping squadron mates apart.
- **Engine choices, not measured from the original:**
  - fighters never stop: idle or arrived, they circle a point (at their turn rate, a turn circle of
    about 48 units for the X-wing);
  - attack runs: within a third of their weapon range of the target they hold their heading, fly
    past, and turn back once outside;
  - fighters separate only from flyers of their owner, sideways (overlap measured in 3D), and their
    weapons fire all around (fighter weapons come from `Projectile_Types`, which has no cone);
  - fighters pitch at their turn rate, at most 60 degrees, keep the height they spawned at as their
    plane, and return to it within about four turn circles; presentation banks them by their turn
    rate (0.6 radians per radian per second, at most 60 degrees);
  - reversal: a goal more than 135 degrees off the heading and more than four turn circles away
    starts a half-loop (nose up through the vertical at the turn rate, out heading back, then a half
    roll back to upright at twice the turn rate);
  - dogfights: against any flyer (any type with a minimum speed, so fighters, bombers, and mod units
    alike), a flyer steers at the target in 3D, weaving 60 units above and below it over 8 seconds
    on a per-unit phase, within 150 units of its plane, and loops when the target is far behind.
    Other squadrons join through ordinary targeting.
- **Not read yet.** Bombing runs and the original's flight paths and banking, `SpaceBehavior`
  entries other than the locomotor, and fighter launch and docking from carriers.
- **Open measurements:** the vertical extent of dogfights, the reversal angle that triggers a
  half-loop, how fast fighters return to their plane, and whether separation also acts between
  squadrons or against other owners. Video captures of the original would settle these.

## Combat

### Combat fields

From stock XML (semantics provisional where noted):

- **Health and armor.** `Tactical_Health`; `Shield_Points` and `Shield_Refresh_Rate` (points added
  once per GameConstants `ShieldRechargeIntervalInSecs`, recorded in space); `Armor_Type` and
  `Shield_Armor_Type`; `Ranged_Target_Z_Adjust` as the aim height.
- **A unit's weapon.** The first entry of `Projectile_Types` with `Targeting_Max_Attack_Distance`,
  `Projectile_Fire_Recharge_Seconds`, `Projectile_Fire_Pulse_Count`, and
  `Projectile_Fire_Pulse_Delay_Seconds`.
- **The projectile.** `Projectile_Damage`, `Damage_Type`, `Max_Speed` (scaled like unit speeds),
  `Projectile_Max_Flight_Distance`, and the shield and hit-point damage flags.
- **Damage against armor.** `GameConstants.xml` lists `Damage_To_Armor_Mod` entries as `damage
  type, armor type, multiplier` (`Damage_Sniper, Armor_T2B, 0.1`). Shields absorb a shot first
  through the shield armor type and the remainder reaches the hull through the armor type: an engine
  choice, not a measured rule.
- **Targeting.** `CategoryMask`, `Property_Flags`, and `Targeting_Priority_Set` (the named set's
  `Attack_Priorities`, category, property, and `Unit_Exclusions` entries; its
  `Hard_Point_Priorities` are not read). Inaccuracy from `Targeting_Fire_Inaccuracy`, else
  `Fire_Inaccuracy_Distance` ("category, distance" per line), then
  `Targeting_Fire_Inaccuracy_Fixed_Radius`. Ranges are measured in the ground plane.

### Weapon hardpoints and parts

A ship's weapons are its `HardPoints` whose `Type` is a weapon (stock: 174
`HARD_POINT_WEAPON_LASER`, 37 ion cannons, 35 missiles, 26 torpedoes). Each names
`Fire_Projectile_Type`, `Fire_Range_Distance`, `Fire_Pulse_Count`, `Fire_Pulse_Delay_Seconds`, a
recharge range (`Fire_Min/Max_Recharge_Seconds`), a fire cone (`Fire_Cone_Width/Height`, mostly
160-180 degrees), sometimes a `Damage_Type` (which the engine lets override the projectile's) and
its own `Fire_Inaccuracy_Distance`.

What the engine reads: each `HardPoints` entry is a part, its kind from `Type`, its hit points from
`Health` (indestructible when `Is_Destroyable` is No or `Health` is missing), `Is_Targetable`, and
its position at the ship model's `Attachment_Bone` in the bind pose. A weapon hardpoint fires from
`Fire_Bone_A` (and `Fire_Bone_B` in turn) within `Fire_Cone_Width`, centered on that bone's X axis,
and falls silent when its part is destroyed. How damage splits between parts and hull is recorded
([Recorded rules from EaW-Revised](#recorded-rules-from-eaw-revised)).

Engine choices, not measured from the original:

- the recharge is the midpoint of the recharge range (the original presumably draws within it at
  random; a seeded stream is to replace the midpoint);
- each weapon picks the entity's target when that target is in its range and arc, otherwise the
  nearest hostile within them;
- shots aim at the victim's nearest live targetable part;
- losing every shield generator stops shield recharge; losing every engine slows the ship to a
  quarter speed.

Not read yet: `Fire_Cone_Height`, and hardpoint target priorities.

### Muzzles

Weapon positions are not in the unit XML (no tag names a unit's muzzles). The models carry them: of
the 123 models of stock types with `Projectile_Types`, nearly all have `MuzzleA_nn` bones (one in 91,
two in 35, four in 11, up to ten), and the pulse count usually matches or multiplies their number
(T4-B 2 and 2, T2-B 4 and 4, X-wing 4 and 4, TIE Fighter 2 and 2, MPTL 9 and 9, AT-AT 2 and 4).
`MuzzleB_nn` bones belong to hardpoints (the T4-B's three missile tubes are `Fire_Bone_A` of
`HP_T4B_Tank_03`..`05`) or second weapons. The bones are not stored in number order (the T2-B's run
00, 03, 02, 01). The TIE Crawler's three have hidden `MuzzleA_nn_flash` meshes. The engine fires a
unit's shots from the A muzzles in turn, by number (an inference from the naming and the counts).
Bind poses of infantry, heroes, and walking robots are not firing poses (a stormtrooper's muzzle sits
6.9 units to the side at the hip, the Phase III dark trooper's 9 units out, the Droideka's in its
rolled ball).

### Firing while moving and facing

Stock infantry list a walking locomotor (`WALK_LOCOMOTOR`, or `LAND_TEAM_INFANTRY_LOCOMOTOR` for
squad members), `IDLE`, `TARGETING`, and `WEAPON` in `LandBehavior`, while vehicles (T2-B, AT-ST) add
`TURRET`. In play (owner observation, Forces of Corruption) infantry turn to face their target and do
not fire while moving, and some stormtroopers crouch while firing. Related XML:
`Is_Stationary_When_Attacking` (12 uses, 11 Yes: Jedi, Vader, Palpatine, the Elite Guard, which stop
to attack), `Turn_To_Face_Unblockable_Shots` (52, all No), `Must_Face_Target_To_Attack_When_Deployed`
and `Fire_When_Deployed`/`Fire_When_Undeployed` (deploying units), `Crouch_Move_Fire_Angle_Cutoff`
(20, once).

The engine reads the missing `TURRET` as "aims with its body": such ground units face their target
before firing (within 20 degrees) and hold fire while moving; turreted units fire on the move. An
inference from the behavior lists and the observation; the original's exact rules (turn speed while
aiming, crouching, the stationary flag's effect on movement) are not established.

### Impact and death effects

Gameplay effects are not hard-coded per weapon class: the game names them through XML `Particle`
objects (`Particles.xml`, 165 stock objects; see [Which effects the data
names](#which-effects-the-data-names)). Projectiles name one per outcome:
`Projectile_Absorbed_By_Shields_Particle` (88 stock uses), `Projectile_Object_Detonation_Particle`
(91), `Projectile_Object_Armor_Reduced_Detonation_Particle` (56), `Projectile_Lifetime_Detonation_Particle`
(54), and `Projectile_Ground_Detonation_Particle` (70); types name `Death_Explosions` (610) and
hardpoints `Death_Explosion_Particles` (267, all naming `Particle` objects).

The engine plays the shield, hull, armor-reduced, and lifetime effects where a shot ends, and a
type's first death explosion where it dies: one-shot, upright, and scaled by `Scale_Factor`
(positions, speeds, and sizes). A shot whose target died, or that outlived its flight limit, plays
its lifetime effect. When the original counts a hit as "armor reduced" is not documented; the engine
uses a damage-versus-armor modifier below 1 (provisional). Open: whether impacts face the shooter or
the surface normal, and how one of several `Death_Explosions` entries is chosen.

`Death_Explosions` does not always name a `Particle`: 12 of 617 stock entries name a `Projectile`
(`Proj_Construction_Pod_Detonation`, `Proj_Wall_Destruction`, `Proximity_Mine_Blast_Effect`,
`Remote_Bomb_Blast_Effect`). Each has no model, a zero `Projectile_Max_Flight_Distance`, and a
`Projectile_Lifetime_Detonation_Particle`, so the death spawns a shot that detonates on the spot
(an inference from the data). The engine plays that detonation particle (lifetime, else object,
else ground) with the particle's own `Scale_Factor`; whether such an entry's area blast also deals
damage is open. Objects that cannot play (no model, a mesh instead of a particle system, a
projectile without a detonation particle, an undefined name) play nothing and are reported once per
build.

### Death clones

A unit lists `Death_Clone` entries "damage type, clone type" (`Damage_Normal,
AT_ST_Walker_Death_Clone_00`; infantry list several, for force powers, fire, disruptors, infection,
crushing). A clone is a variant of the unit with `Specific_Death_Anim_Index` (the DIE variant),
`Death_Persistence_Duration` (the AT-ST's wreck stays 30 s), `Death_Explosions_End`,
`Death_Clone_Is_Obstacle`, and hard extents; capital ships and stations break apart into a clone with
a model of its own. Every stock user of `Death_SFXEvent_End_Die` also has a `Death_Explosions_End`
and names a large explosion sound, so it is read as the wreck's final explosion (an inference).

The engine uses the normal clone (its `Damage_Normal` entry, else the first) for the wreck's model,
DIE variant, persistence, and end effect and sound, and the `Damage_Crush` clone's clip for deaths by
crushing; other damage-specific clones and wrecks as obstacles are not modeled (roadmap).

## Recorded rules from EaW-Revised

EaW-Revised (GPL-3.0; read, never copied) recorded fixed-force space scenarios in the publicly
available FoC debug build at run time (`tests/fidelity/S-NN-*.json`, 30 logical ticks per second,
three runs agreeing; their `docs/behaviour/space-movement.md`, `space-damage.md`,
`combat-time-to-kill.md`). Under the owner's ruling these observations are evidence; their rules that
rest only on static disassembly are not. Adopted:

- *Shields refresh in steps*: `Shield_Refresh_Rate` once per `ShieldRechargeIntervalInSecs` (stock
  3.0 s): +10 every 90 frames in S-15, S-51, S-52, the Tartan and the Nebulon-B five frames apart. The
  engine draws each unit's phase from its id (how the game chooses it is not observed).
- *The space speed multiplier scales acceleration, deceleration, and turn rate*: S-10 peaks at 3.72 =
  3.1 x 1.2 after 62 frames (0.05 x 1.2 per frame squared), S-12 turns 1.506 = 1.25 x 1.2 degrees per
  frame, S-26 turns in place at 0.84 = 1.4 x 1.2 / 2. Land is not recorded and stays unscaled.
- *Hardpoint damage*: a shot aimed at a destroyable hardpoint damages only it (S-19: 4000 on a
  Nebulon-B laser silenced it and left the hull at 5400); the hull follows the parts at max x min(1,
  S/T + 0.2), S/T the parts' remaining share of their total health (S-45, S-49, S-50), and the entity
  dies with its last destroyable part unless `Should_Be_Destroyed_When_All_Hardpoints_Destroyed` is
  No. Unaimed damage (blasts) still reaches the hull directly, an engine choice; land objects with
  parts follow the rule unobserved.
- *Turning in place*: a ship without a move order turns at its turn rate divided by GameConstants'
  `TurnInPlaceSlowdown` for its `Space_Layer` (corvette 2: S-11 turns 90 degrees in 120 frames, S-26
  0.84 = 1.4 x 1.2 / 2 degrees per frame; frigate 3 and capital 4 are the data's, not observed).
- *Facing an ordered target*: a space ship with an attack order and its target in range turns in
  place toward the target's nearest live hardpoint, exactly onto its bearing (S-26, S-27); idle ships
  keep their heading (S-22 to S-25). Their 10-degree dead band rests on disassembly and is not used.
- *Straight start*: a space ship that names no `Min_Speed_Fraction_For_Turn` turns only at full speed
  and keeps it through the turn (S-12: the corvette flies straight 62 frames from rest and turns once
  it reaches 3.72); the default fraction is 1.
- *Target retention* (the engine's behavior, checked): a held target is kept when a better-ranked one
  appears (S-02: a TIE Defender, which names no `Targeting_Stickiness_Time_Threshold`, stays on its
  X-wing for 5 s after a nearer Y-wing arrives; the engine re-weighs only types that name a
  threshold), and a removed target is replaced at the next shot the fire countdown allows (S-03: each
  weapon takes the nearest hostile in its range and arc when it may fire). A test pins S-02.
- *Layer heights*: a unit spawned in space takes its type's `Layer_Z_Adjust` as its height, the plane
  being 0 (S-10 to S-14, S-32 to S-34: corvette -20, Nebulon-B -90, Acclamator -110, MC80 -290, Tartan
  and station 0). Map placements keep their authored height, and the tag's other users (skydomes at
  -2500, asteroid props at -330) are left to the renderer's layers.

Recorded and not adopted yet (each changes behavior beyond a constant; proposed to the owner):
per-hardpoint shot counts by bearing (S-22 to S-42), as fire-cone regression cases, postponed by the
owner's decision to the simulation rewrite, which brings the staged battles they need (roadmap,
Simulation 1); and hit rates near 40% (corvette on Tartan) and 7% (Tartan on Nebulon-B), far below
the engine's.

## Unit animation

### Clip names and roles

Animations sit beside their model as `<MODEL>_<ROLE>_<NN>.ALA`, NN numbering variants (the Dark
Trooper has ATTACK_00 to _02, IDLE_00 and _01, DIE_00 and _01, and more). Across the 2,408 resolved
stock ALA files the most common roles are IDLE (539), DIE (301), ATTACK (160), MOVE (158), ATTACKIDLE
(108), CINEMATIC (77), BUILD (64), LAND (55), WALKMOVE (53), TALK (43), FLY (40), and TURNL/TURNR (35
each), with crouch, flinch, deploy, take-off, and partial-turn variants. Which clip the original plays
when (and whether XML overrides names) is not established, so the engine's selection rules
(`unit_animation.hpp`, roles in `assets/clip_names.hpp`) are engine choices, except where the clip
joins below give evidence.

- **WALKMOVE and MOVE.** All 30 stock models with WALKMOVE clips also have MOVE clips (infantry and
  heroes: Dark Troopers, stormtroopers, Vader, Mara Jade, Gungans, Noghri, ...), so WALKMOVE reads as
  the walk and MOVE as the run; about 73 more models (mostly vehicles) have MOVE alone. RUN occurs on 4
  models only (two animals and two Force-power variants).
- **Blending.** Whether the original crossfades or cuts between clips is not established; the engine
  blends by choice.
- **Models without clips.** 290 of the land spawnable types' models (mostly buildings and props, with
  the override below counted) and the space ships have no role clips; large ships only have `_D_DIE`
  clips of their destruction models.

### Who chooses animations

The stock Lua scripts have one animation function, `Play_Animation`, with 79 call sites, all on named
story, mission, or cinematic objects (heroes in cutscenes, doors, alarms, a shuttle,
`Intro_Animation`); no script chooses idle, movement, or attack animations or controls blending
(`EAWAssetInspector lua-audit`). Everyday selection is the engine's job; scripts only force a clip on
a scripted object. The calls read `Play_Animation(type, flag[, variant])`: `type` names a clip role in
title case with underscores ("Idle", "Talk", "Attention", "Cinematic", "Talk_Gesture", "Choke_Die"),
matching the file role with the underscores removed (TALKGESTURE), and `variant` (0 or 1 where given)
picks the `_NN` file. The flag is true for held poses ("Attention", "Idle" in a loop) and false for
one-shot gestures ("Talk", "Choke"), so it reads as a loop flag (an inference).

### Animation sources

`Land_Model_Anim_Override_Name` (27 stock uses, with or without `.ALO`) names the model whose clips a
type plays instead of its own; variants inherit it (`Ewok_Hunter` names NI_EWOK.ALO, so
`Ewok_Hunter_B` on NI_EWOK_B.ALO plays NI_EWOK clips). Civilians and slaves use NI_GMale_Urban_A or
RI_INFANTRY, field commanders RI_INFANTRY or EI_TROOPER. The engine matches such clips to the model's
bones by name.

### Clip speeds

`Movement_Animation_Speed` (about 130 stock types) tracks the type's `Max_Speed`: equal on most
(Stormtrooper 0.8/0.8, T2-B 2.3/2.3, Wampa 1.1903/1.1903, Shaak 0.13/0.13, Rancor 0.75/0.75), lower on
others (Rebel trooper 0.65/1, AT-ST 0.8117/1.2, AT-AT 0.1366/0.5, Bantha 0.07/0.35).
`Walk_Animation_Speed` (26 types) is 0.27 to 0.43 against top speeds near 1, about the 0.4 of top
speed the engine walks at, and `Crouch_Animation_Speed` (0.537) sits near the halved crouched speed.

Read as playback multipliers these would play a Shaak's legs at 0.13 times and an AT-AT's at 0.14
times; the owner saw the AT-AT's walk "completely broken" under that reading. They are the unit speed,
in `Max_Speed`'s units, at which the clip plays at its own rate: playback scales by speed over that
reference. The gait measurement below confirms this for `Movement_Animation_Speed`; the original's
bounds on the resulting rate are not known (the engine clamps it, `walk_clip_playback`).

### Animation speeds

`EAWAssetInspector gait` follows the bones of a walk clip that reach its lowest point while they are
down: a planted foot moves backward at the body's ground speed. The AT-AT's MOVE (160 frames) moves its
toes and foot-puff bones at 0.136-0.152 units per 30 Hz frame against `Movement_Animation_Speed`
0.1366; the stormtrooper's feet at 0.52-0.62, 0.78-0.93 with its `Scale_Factor` 1.5, against 0.8; the
Bantha's at 0.046, 0.069 scaled, against 0.07. So `Movement_Animation_Speed` is the ground speed
(`Max_Speed`'s unit) at which the move clip plays at its own rate. 120 stock types give both;
`Max_Speed` over it runs from 0.51 (Vornskr) to 5 (Bantha), 33 within 10% of 1, 23 above 1.5 (the
AT-AT 3.66).

Turn clips do not turn the line between their planted feet measurably (under 0.3 degrees per frame,
both signs), so `Rotation_Animation_Speed` (the AT-AT 0.5 against `Stopped_Rate_Of_Turn` 1) is read by
analogy only, as the turn rate at which turn clips play at their own rate. The corpus fits that
reading as `Movement_Animation_Speed`'s fits `Max_Speed`: of the types whose models have turn clips,
most give it equal to `Max_Rate_Of_Turn` (infantry 20 and 20, the Night Sister and Rancor 1.5 and 1.5,
the Twi'lek 12 and 12), and the walkers well below their turn rates (the AT-ST 2.2 against 7.5, the
SPMAT 1.0 against 3.1, the AT-AA 1.25 against 3.5, the AT-AT 0.5 against 1), so their slow-stepping
turn clips speed up.

### Walker clip joins

`EAWAssetInspector clip-joins` measures the largest bone jump where one clip ends and the next starts.
The AT-AT's clips meet within 0.0003 units at: IDLE's start and MOVESTART's start (IDLE strays at most
0.5 units mid-loop, on a spark proxy), the end of MOVESTART and the start of MOVE, MOVE's loop, the end
of every MOVE_END clip and IDLE's start, and the turns' BEGIN, loop, and END.

Each MOVE_END clip starts where MOVE is at one quarter of its stride: ENDONE at 0/4, ENDTWO at 1/4,
ENDTHREE at 2/4, ENDFOUR at 3/4 (elsewhere the toes jump 10-16 units). Models with three MOVE_END
clips (AT-ST, AT-AA, SPMAT, pod walker, Rancor, Night Sister) start them at thirds of the stride. So a
walker that stops walks on to the next 1/n of its stride, n its MOVE_END count, before its MOVE_END
plays: the original's behavior by this evidence; how it times the walk-on is not known.

### Who uses the walker tags

From `EAWAssetInspector type-tags`:

- `Walk_Transition`, `Has_Pre_Turn_Anim`, and `Locomotor_Has_Animation_Priority` with
  `Attack_Animation_Is_Overlay`: all four on 29 types (AT-AT, AT-ST, pod walker models);
- the first two without the priority on 17 (Rancor 8, SPMAT 5, AT-AA 2, Night Sister 2), none of
  which overrides acceleration, so their speed changes at once;
- `Walk_Transition` alone on 3 (the speeder bike, EV_BIKE);
- `Has_Pre_Turn_Anim` alone on 4 (Juggernaut, Tauntaun, ground beetle; the Tauntaun and Juggernaut
  with small acceleration overrides);
- priority and overlay without transitions on 2 (Destroyer droid).

So 24 types play transitions without animation priority: by the tag's name, their locomotor leads and
the clips play over its motion (inference; how the original shows a Rancor setting off is not
observed).

### The pod walker's raised leg

EV_PODWALKER's walk and turn clips all start and end with its middle leg raised, IDLE with it down:
every join between IDLE and them jumps about 13 units on B_Mid_Foot (the constant offset noted in [The
turn kit](#the-turn-kit), not an artifact). IDLE_LEGLIFT starts where IDLE does (0.00) and ends where
MOVESTART and TURNL_BEGIN start; IDLE_LEGDROP starts where MOVE_ENDONE and TURNL_END end and ends where
IDLE starts (0.0002); only a muzzle bone differs, by 4.2 units. Both are 70 frames. So the pod walker
raises the leg before it sets off or turns and lowers it when it stands (the engine's reading; when the
original lowers it is not observed).

Both of its idles (IDLE_00, 70 frames; IDLE_01, 100) keep the leg down (IDLE_01 starts where IDLE_00
does, 0.00): variants, not a raised idle. The middle foot stands 3.4 units up in IDLE and ATTACK, rises
to 15.4 through IDLE_LEGLIFT, and stays between 14.4 and 18.5 through MOVESTART, MOVE, MOVE_END, and the
turns. Its ATTACK (20 frames) is authored standing: B_Body pitches up to 10.5 degrees while
B_Mid_Shoulder turns up to 13.7 the other way, so the planted foot holds still.

A stock asset defect, kept as it is: EV_PODWALKER_MOVESTART_00.ALA swings the middle leg 12 units back
(B_Mid_Foot and B_Foot_M from y -2.2 to -14.2, rising to 18.5), while MOVE_00 starts with it centred, so
the leg snaps forward as the walk begins (B_Mid_Femur 9.1, B_Mid_Ankle 8.2, the feet 12.1 units; every
other bone joins exactly). The owner checked the retail game: it shows the same snap, so it is the
content, not the engine. No start clip is missing: every stock model has exactly one MOVESTART_00 (a
start always begins from the standing pose), against one MOVE_END clip per part of the stride.

### The turn kit

From `clip-joins` over every stock model with transition clips. Ten models carry the same set:
TURNL/TURNR loops with _BEGIN, _END, _HALF, and _QUARTER (AT-AT, AT-ST, AT-AA, SPMAT, pod walker,
Rancor, Wampa, Tauntaun, Hutt, Night Sister); 25 more (infantry, heroes, small creatures) have only
the two loops. On all ten, joins within 0.5 units (pod walker: a constant 13 units on one bone at every
join, its raised middle leg, see above) show:

- one pose shared by IDLE's first frame, the start of MOVESTART and of each TURNx_BEGIN, and the end of
  every MOVE_END and TURNx_END;
- one pose shared by BEGIN's end, END's start, and the start and end of the loop, HALF, and QUARTER;
- MOVESTART's end meeting MOVE's start (within 1.1).

HALF and QUARTER never meet IDLE (4-21 units): they are not turns of their own but cycles
interchangeable with the loop between BEGIN and END, in any order. A loop cycle meets END only at its
end (1-14 units off at its quarters). HALF and QUARTER are as long as the loop or shorter (Rancor 20
frames against 30, SPMAT 30 against 60, AT-AA's QUARTER 25 against 40), with smaller foot sweeps
(AT-ST planted feet about 62% and 39% of the loop's speed). The body-turn estimate from planted bones
is too noisy on these clips to measure the angle each cycle stands for (left and right sides disagree
by up to four times).

Inference, not evidence: by their names, HALF and QUARTER stand for a half and a quarter of the loop's
angle, so the original likely composes a turn of a given size from whole cycles, as a coin change
(BEGIN, loops, a HALF, a QUARTER, END). The engine composes its turns so (status.md, "Turns composed of
cycles"); how the original picks them is not observed. Leaving IDLE mid-loop costs 0.1-0.3 units on
the walkers and 1.6-3 on the creatures (SPMAT 4, on one bone): the only join in the kit not authored to
meet.

### Crouching

The SPREAD_OUT ability, deferred to the abilities work (roadmap). Infantry and their squads and teams
(Rebel_Trooper, Stormtrooper, Plex_Soldier, pirates, field commanders, Rebel_Infantry_Squad,
Imperial_Stormtrooper_Squad, ...) carry a `Unit_Abilities_Data` entry of `Type` SPREAD_OUT (its sound
events call it "Take Cover"). Rebel_Trooper's has `Mod_Multiplier` entries `SPEED_MULTIPLIER, 0.5f`
and `TAKE_DAMAGE_MULTIPLIER, 0.5f`; GameConstants.xml sets `Spread_Out_Spacing_Coefficient` 1.8 and
the posture blend times `CrouchIdleWalkBlendTime` 0.5, `CrouchMoveBlendTime` 0.75, and
`TeamCrouchMoveBlendTime` 0.5; infantry types set `Crouch_Animation_Speed` (0.537 for Rebel troopers
and stormtroopers, 17 uses). Clips: CROUCHIDLE (23 files), CROUCHMOVE, CROUCHTURNL, and CROUCHTURNR
(13 each), and no crouched attack clips.

Observed in play (owner, Forces of Corruption): the ability toggles a whole squad; it changes idle and
walking animations but not combat animations. Planned reading: a toggle that halves speed and damage
taken, spreads the formation by 1.8, swaps IDLE and MOVE for CROUCHIDLE and CROUCHMOVE (played at
`Crouch_Animation_Speed`) with the XML blend times, and keeps the attack clips. Unknown: how the
multipliers combine with others, how the spacing coefficient applies, and when CROUCHTURN plays
(probably turning in place).

## Particle systems

Particle effects are ALO files whose root chunk is 0x0900 (578 of the 1,957 resolved stock ALO files;
none use the 0x1500 plugin format of later Alamo games). The field meanings follow the MIT-licensed
alo-viewer (Copyright (c) 2017 GlyphX Tools); `src/formats/src/alo_particle_system.cpp` is the
engine's own decoder, checked against the stock corpus with `EAWAssetInspector particle-audit`.

### Format

- **Layout.** A system holds its name (chunk 0x0), a 4-byte chunk 0x1, an emitter list (0x0800 of
  0x0700 emitters), and an optional one-byte "leave particles" chunk 0x2. Each emitter holds its
  property mini-chunks (0x2), texture (0x3), name (0x16), three shape groups (0x29: speed, lifetime,
  position), seven keyframe tracks (0x1: red, green, blue, alpha, scale, texture cell, rotation
  speed), optional child links (0x36: spawn on death, spawn during life), and an optional normal
  texture (0x45).
- **Corrections to alo-viewer.** A shape group (chunk 0x1101) is 64 bytes (type, minimum, maximum,
  cube side, sphere radius and hollow flag, cylinder radius, hollow flag, and height, exact value),
  not 68. Colour track keys hold a 0..255 integer and then the time; other keys hold a float value and
  then the time.
- **Corpus.** All 578 systems decode without warnings: 1,464 emitters, 1 to 15 per system; 492
  systems leave their particles behind. Blend modes are 1 (additive, 900 emitters), 2 (alpha, 543),
  and 11 (bump alpha, 21). 953 emitters are camera-facing billboards, 80 lie in the ground plane
  ("world oriented"), and 65 are velocity-stretched kites; 170 are heat-distortion particles and 5
  weather particles. 685 emit limited bursts, 178 unlimited bursts, and 601 continuously. Child links
  are rare (24 spawn during life, 3 on death). Every emitter writes the properties 0x04 to 0x2B; later
  ids (0x2C to 0x49) are missing from some files, so they keep defaults. The system chunk 0x1 holds 0
  to 13 (350 files hold 0); its meaning is unknown, and it is kept as stored.
- **Not interpreted.** Properties 0x06, 0x11, 0x14, 0x15, 0x2B, 0x3F, 0x44, and 0x49 are kept raw;
  alo-viewer reads and ignores them too.
- **Value ranges.** The random scale, lifetime, and rotation variances (0x12, 0x13, 0x17) lie in 0..1,
  so they are fractions, not percentages. Continuous rates reach 3,500 particles per second, bursts
  2,000 particles, and the shortest burst delay is 0.0003 s, so the engine caps live particles
  explicitly (65,536 by default, drops counted) and bounds emission per update.

### The particle editors

- **The GlyphX particle editor** (MIT, by the authors of alo-viewer) is the authoring tool that writes
  the files the game loads, with its own preview simulation. Where it differs from alo-viewer the
  engine follows it: its file writer stores the lifetime shape group as exactly lifetime x (1 -
  variance)..lifetime, its UI shows the "inward" properties negated so that positive UI values move
  particles toward the emitter, and its rotation and texture rules are simpler than alo-viewer's
  derived ones.
- **DrKnickers' fork** (MIT) keeps the original editor's simulation rules. Its guide describes the
  editor's behavior (its changelog says the guide was checked against the editor's source, not the
  game), and a few changelog entries claim to match the game without saying how: emitters draw in
  their list order, the game steps particle motion frame by frame, atlas indices past the last cell
  wrap, tailed particles ignore the rotation track, and additive particles ignore alpha. The original
  editor agrees on the last point (its fixed-function setup blends additive particles with source and
  destination factors of one).
- **Linked particles.** Both editors carry linked particles ("Link Particles to Instance") with the
  instance's position only, but both play a system as a free-standing object that has a position and
  never an orientation, so that rule describes their previews, not the game. In the engine it visibly
  detaches engine glows from turning fighters (owner's observation, Y-wing squadrons). Linked
  particles therefore follow their emitter's position and rotation under every profile.
- **Look-defining quirks.** Additive blending without alpha and drawing in emitter list order are
  reproduced by the legacy profile (additive adds colour x texture without alpha, the speed-driven
  engine intensity dimming the colour instead; effects draw back to front with their emitters in list
  order) and improved by the enhanced profile (additive scaled by alpha, every particle sorted back to
  front). `particle_rules` in `src/assets` holds the interpretation; `sandbox --particles
  legacy|enhanced` chooses.
- **Projectile flight.** The fork also documents the game's projectile flight
  (`src/simulation/ProjectileShot.h`, `ProjectileFlight.h`, `RocketSpline.h`): `Max_Speed` is world
  units per 30 Hz logic frame (agreeing with the engine's provisional x30), a shot dies after
  `Projectile_Max_Flight_Distance`, and projectile acceleration is zero. By `Projectile_Category`:
  missiles turn toward their target by at most `Max_Rate_Of_Turn` degrees per frame on yaw and pitch
  separately; bombs keep their launch facing and fall faster each frame
  (`Projectile_Bomb_Fall_Accel_Rate`); grenades lob in closed form (speed `Max_Speed`, or 0.8x when
  they may lob slower, gravity `Projectile_Grenade_Gravity` x `Lob_Mod`); rockets follow a spline with
  points every `Projectile_Rocket_Curve_Distance` x 1.1, pushed aside up to
  `Projectile_Rocket_Curve_Offset`. Models are drawn turned 90 degrees about Z after their facing, so
  a model's -Y axis points along the flight, as for units. These are the fork's statements, to be
  checked before the simulation relies on them.

### Playback

The engine's playback (`src/presentation/src/particle_effects.cpp`) follows the particle editor where
it is clear:

- emission starts after the initial delay: bursts of the per-burst count every burst delay, limited by
  the burst count (0 or -1 unlimited), or one particle every 1/rate seconds; skip time pre-simulates;
- particles spawn on the position group and take the speed group as velocity, both in the emitter's
  frame; the stored "inward speed" is added along the spawn offset's direction (positive moves outward;
  the editor shows the value negated, as an inward speed);
- acceleration (optionally in the emitter's frame) plus -gravity along Z, plus the stored "inward
  acceleration" along the same fixed spawn direction (positive pushes outward, likewise);
- lifetime x random(1 - variance, 1) and size x random(1 - variance, 1): variances shorten and shrink,
  never lengthen or grow (alo-viewer lengthens lifetimes);
- the scale track is the sprite's full width, as both references draw it. A radius reading makes
  capital-ship engine glows far too large and bright next to the owner's screenshots of the original
  (with bloom), so the width reading stands;
- colour tracks plus a random addition up to the random colours (grayscale uses the red value for all
  channels);
- rotation speed in revolutions per second, randomly reversed when the flag is set; "random rotation"
  gives a fixed angle: the rotation track's first value (revolutions) x (1 + random -v..v), v the
  rotation variance clamped to 0..1;
- texture cells form a square atlas of floor(sqrt(cells)) per side, indexed by the texture track
  rounded down;
- freeze time, when set and not inside the skipped time, stops the emitter's clock once reached: no
  more emission, and its particles hold still;
- "link to system" moves particles rigidly with the emitter, position and rotation; linked particles
  spawn at the emitter's current transform;
- ground behavior 1 kills particles below the ground, 2 bounces them, 3 stops them;
- blend modes 1 and 4 draw additively (legacy: colour x texture added, alpha ignored; enhanced: scaled
  by alpha), 3, 6, 8, and 9 modulate, the rest alpha-blend (stock data uses only 1, 2, and 11);
- kites are diamonds facing the camera: the quad turned 45 degrees so its top-left texture corner
  trails the velocity on screen, that corner pushed out by max(1, tail size x the velocity's on-screen
  fraction / sqrt 2), the tail size scaled by speed / 1000 for particles linked to a parent;
  world-oriented particles lie in the XY plane;
- particles ignore the map's wind.

Engine choices: sphere spawns use uniform directions (the references sample angles uniformly);
emitters that are only children never emit on their own; smooth interpolation is smoothstep between
keys (the editor's cubic Hermite with zero tangents, the same curve).

### Trails and child emitters

- **Trails.** No stock XML field names a trail, and the classic particle format has no ribbon
  renderer (alo-viewer's chain renderers belong to the plugin format). Engine and missile trails are
  particles that moving emitters leave in the world. The engine emits each particle at its own time
  within a frame, where the emitter was then (an engine choice; alo-viewer turns its high-resolution
  emission off for this format, which leaves clumps behind fast emitters).
- **Child emitters** (the particle editor): a trail child is an emitter instance attached to each
  living parent particle, stopped (its particles living out) when the parent dies; a death child is
  created at the dying particle and stopped at once, so it emits its first burst (or one particle when
  continuous) and nothing more. The engine does the same, with at most 64 trail particles per parent
  per update.

### Emission from meshes

192 emitters emit from a mesh. A proxy's mesh is the mesh rigidly attached to the proxy bone's parent,
as in alo-viewer. Mode 3 emits once per vertex per spawn, 1 at a random vertex, 2 at a random point of
a random triangle; the particle is pushed out along the normal by `emit_from_mesh_offset`, and its
velocity's +Z turns to the normal. Emitters without a mesh (effects played on their own) use their
position shape.

- The Star Destroyer's engine effect (`pe_stardestroyerengines`, every vertex, size 70) sits on
  `engines_big`, a hidden three-vertex triangle whose corners are the three main engines; without the
  mesh it would be one ball at the bone.
- The X-wing's `pe_Z95engines` emits from the four corners of its hidden `engines` quad (one per
  nozzle), but its particles are 1 unit across, red, and live 0.1 s: barely visible as authored.
  Whether the original draws particles with a minimum screen size is not established.

### Bloom and engine intensity

From the owner's screenshots of the original: the original renders with a glow (bloom) option, and
bright hull panels and engine glows bleed light, which is what makes small additive engine particles
such as the X-wing's 1-unit red dots visible. The engine draws bloom of its own (threshold 0.6 on the
brightest channel per texel, soft knee, the block mean mixed with its brightest texel so small glows
survive the downsample, quarter resolution, strength 0.8), chosen by eye, not measured.

In the same screenshots a Star Destroyer's engine glows are small and dim at rest and grow and
brighten as it accelerates. No XML field governs this (the only engine field is `Engine_Particles`),
so engine effects ([Proxies](#proxies)) scale with the unit's speed over its top speed, smoothed over
0.5 s: size from 50% and opacity from 15% at rest. Both mappings are engine choices.

### Which effects the data names

Gameplay effects are data-driven through `Particle` objects (165, `Data/XML/Particles.xml`). Each names
its effect file (`Space_Model_Name`, `Land_Model_Name`, sometimes `Galactic_Model_Name`) and playback
settings: `Scale_Factor` (1.0 for 143, 2.0 and 20.0 for a few), `Particle_Lifetime_Frames` (mostly
30), `Explosion_Jitter_Factor`, `Explosion_Scorch_Mark`, `Particle_Attach_To_Collision`,
`No_Particle_Below_Detail_Level`, `Behavior` (`PARTICLE`, `PARTICLE, EXPLOSION`), and placement flags
(`In_Background`, `In_Midground`). Fields that name Particle objects, by stock use count and owner:

- deaths: `Death_Explosions` (610: props, structures, units), `Death_Explosions_End` (20),
  `Spin_Away_On_Death_Explosion` (22), hardpoints' `Death_Explosion_Particles` (267), and the
  break-off debris' `Debris_Attached_Particle` (149, burning debris);
- projectile impacts: `Projectile_Object_Detonation_Particle` (91),
  `Projectile_Object_Armor_Reduced_Detonation_Particle` (56, "duds"),
  `Projectile_Absorbed_By_Shields_Particle` (88), `Projectile_Ground_Detonation_Particle` (70),
  `Projectile_Lifetime_Detonation_Particle` (54, a shot that runs out);
- hits on units: `Asteroid_Damage_Hit_Particles` (60), `Damage_Hit_Particles` and
  `Shield_Hit_Particles` (cinematic units), `Base_Shield_Penetration_Particle` (34);
- abilities and heroes: `Owner_Particle_Effect`, `Target_Particle_Effect`, `Particle_Effect`, the
  `Projectile_*_Spawn_Effect` fields, and others.

Hardpoints place their damage on model bones: `Damage_Particles` (268, `HP_*_EmitDamage`),
`Damage_Decal` (245, `HP_*_Blast`), and engine hardpoints' `Engine_Particles` (21, `HP_E_MainEngines`)
([Hardpoint models, damage effects, and scorch
meshes](#hardpoint-models-damage-effects-and-scorch-meshes)). Model proxies (engine glows, station
lights, fires) have no XML entry.

### Weather

A land map's active environment names a weather scenario (TED environment field 0x29).
`WeatherScenarios.xml` defines 13: `Weather_Scenario`, attribute `emitter_name` naming a particle
effect, then `Phase` elements in order, each named by a weather kind (CLEAR, ASH, RAIN, SANDSTORM, or
SNOW; other names mean CLEAR, as the file's own header says) with `Emitter_Intensity`,
`Lightning_Intensity`, `Duration`, `Ease_Out_Duration`, and an optional
`Fog_Plane_Interpolation_Alpha`.

Of 114 stock land maps, 49 have an active environment that names one: RAIN_RANDOM 20,
DEFAULT_ALWAYS_CLEAR 9, ASH_RANDOM 5, BLIZZARD_RANDOM 4, RAIN_CONSTANT 4, SANDSTORM_CONSTANT 3,
SANDSTORM_RANDOM 2, SNOW_RANDOM 2 (all environments together name 129).

The scenarios' effects: `p_rain_med00` (one kite emitter, 2000/s, 1 s, falling at 300),
`P_Snowblizzard` (two, 1000/s each, 3 s, drifting sideways at 84 and 177 while falling at 150),
`P_SnowSleet` (2000/s, 3 s), `p_rain_of_ash` (3000/s, 1 s), all with the weather flag, a cube spawn
shape of side 200-250, and weather cube size and distance 500 (rain, ash) or 400 (snow; fade-out
distance 300, 0 for the others); `p_dust_storm` (sandstorms) has no weather flag (1200/s smoke puffs
100-200 across).

How the original places these effects, what the cube size and fade-out distance do, and what the
fog-plane value means are not known. The engine's reading, all inferences or engine choices: the
effect plays once per tile of a world-aligned grid over the ground in view (a tile as wide as its
spawn volume, hung so its particles reach the ground; at most 16 x 16 tiles and 40,000 particles,
thinning the rate), emitting at the phase's intensity times its authored rate; phases play in order
and loop; a phase's ease-out is the last part of its duration, blending toward the next phase;
lightning is a flash of the scene's ambient and key light at a rate proportional to the intensity.
The cube size and distance, fade-out distance, and fog plane are unused.

## Projectile visuals

- **Custom-render bolts.** 39 stock projectile types set `Projectile_Custom_Render`: 1 (26 types)
  draws a bolt from an atlas cell, 2 (12) the same cell tinted by `Projectile_Laser_Color` (ion
  cannons, turbolasers, proton torpedoes, sonic and carbonite guns), 0 (1, an ion laser) draws its
  model. With `Variant_Of_Existing_Type` resolved, 63 FoC projectiles are custom-rendered, and 40 of
  them also name a model (the pg-starwarsgame-lsp preview, MIT). `Projectile_Width` and
  `Projectile_Length` size the bolt (small laser 1 x 6), and `Projectile_Texture_Slot` is
  "column,row"; nothing else in the data sizes them.
- **The atlas.** No XML names it, but ModVerify's list of assets the executable loads by name
  (`HardcodedEngineAssets.cs`, MIT) includes `W_Laser_Pill.tga`, and the public schema types
  `Projectile_Texture_Slot` as a two-number vector and `Projectile_Laser_Color` as RGBA.
  `W_LASER_PILL.dds` (256 x 256) is a 4 x 4 grid of 64-pixel cells holding diagonal glowing pills,
  bottom-left to top-right: (0,0) red, (1,0) orange, (2,0) yellow, (3,0) green, (0,1) blue, (1,1)
  purple. Red lasers use slot 0,0 and green ones 3,0, which fits render 1.
- **Render 2 is a teardrop.** A modder who rewrote the stock projectiles describes small lasers as
  symmetrical bolts "while turbolasers appear teardrop-shaped", and turns any laser into a teardrop by
  setting render 2, slot 2,0, and a `Projectile_Laser_Color` (eaw-fast-consistent-projectiles, MIT). No
  texture holds a teardrop at cell 2,0 (`W_LASER_SMALL`, the bolt models' texture, has red and green
  teardrop flares elsewhere and in fixed colours), so the engine draws it procedurally: a round head at
  the front, a tapering, fading tail, and a white-hot core, in the laser colour.
- **Bolt length.** Render 2's length is `Projectile_Length` widths: in the owner's screenshot of the
  original, a Star Destroyer's green turbolaser shots (5 x 10) are thin streaks of about 25-30 px at
  about 1.3 px per unit, flying away from the camera, which an absolute length of 10 cannot reach and
  50 units foreshortened does; the modder's script also scales turbolaser width and length together.
  Render 1 keeps absolute sizes (X-wing lasers, 1 x 6, look right). The scale, the proportions of head
  and tail, and whether render 1 also measures length in widths for other types remain to be checked;
  sizes come from the XML, so mods that rescale them keep their proportions. Bolts are drawn between
  ticks like entities, head first.
- **A modding tutorial** (rebel5555, ModDB, 2009) gives a render-1 ion bolt 1.7 wide and 55 long with
  `Projectile_Texture_Slot` written as a single "2" and `Scale_Factor` 0.5: render-1 lengths can be
  long streaks in mods, and a single number is a cell index (read as column 2, row 0). Whether
  `Scale_Factor` scales custom-render bolts is open (stock projectiles all use 1.0, so the engine
  ignores it).
- **Model projectiles.** Of the 115 projectile types stock weapons fire, 53 are bolts (custom render 1
  or 2) and 62 draw a model. Many name only one context's model and are fired in both (seven
  concussion missiles name a `Land_Model_Name` and fly in space), so the engine falls back to the other
  context's model, as for effect objects. Some name a particle system as their model
  (`p_turbolaser_burst_med00`, `p_proton_Vengance`, `p_magnepulse_shot`); those play along the flight.
  Missile models carry their trail as a proxy (`W_concussion_missile.alo`: one mesh, one proxy).
  `Scale_Factor` ranges from 0.3 to 5.0 on model projectiles and is applied to them; the model's -Y
  axis points along the flight (the fork's documentation of projectile flight says the same).
- **Open:** 19 land projectiles for melee and environment hits name `W_LASER_INVISIBLE.ALO`, whose
  single additive quad uses the visible `W_Laser_Small.tga` and is not flagged hidden; why the original
  shows nothing for them (if it does not) is unknown, so the engine draws it as authored.

## Terrain decals and dynamic tracks

`TerrainDecalFX.xml` defines 19 `Decal`s (footprints and explosion scorches) by atlas cell, not by
texture: every one picks `UV_Slot` cells of `D_MASTER_DECAL.DDS` (512x512 BGRA8, in both games; read
with `EAWAssetInspector texture-png`). It is a 4x4 grid of 128-texel cells whose alpha is each mark's
shape and whose color is a tangent-space normal for the `DECAL_BUMP_ALPHA` footprints (rows 0-1; the
massive scorch a 2x2 block at 2,1, matching its `Slot_Scale` 2) and black for the `DIFFUSE_ALPHA`
scorches (row 3, plus a burst at 1,2). So scorches darken by alpha times `Intensity` and footprints
dent the lighting. `Default` (cell 0,7, outside the grid) and `Shadow` (cell 1,0, the Rancor's foot)
look unused.

Decals are placed through `SurfaceFX` entries (`Decal_Name` per surface kind):

- **Footprints** trigger from animation foot plants ([Sounds in animations](#sounds-in-animations)).
  Each print is drawn along its foot bone: the AT-ST's cell, long along U (62x39 texels), sits under
  `B_Foot_L/R` whose X axis points forward, and the AT-AA's mirrored left and right cells, long along V
  (91x120), under `B_Toes_*` whose X axis points sideways, so the print's U follows the bone's X. The
  round AT-AT, pod-walker, and Rancor prints do not show it. A reading of the data that the owner's
  AT-ST observation supports.
- **Scorches**: `Explosion_Scorch_Mark` on 36 `Particle` explosion objects
  (`EXPLOSION_SCORCH_MARK_SMALL` and so on, none on water).

Vehicle tracks are a separate system (`DynamicTrackFX.xml`, textured ribbons): 11 tracks with a
texture, width, opacity, segment length, fade distances, and a render mode. The bump ones
(`w_tank_track_01`, `w_footprint00`, R2-D2's) are BC3 tangent-space normals with opaque alpha; the
diffuse ones (`Trk_Dirt`, `Trk_Grass`, 20 wide) are named only by the `DYNAMIC_TRACK_*` SurfaceFX
entries, which no stock unit lists. Units opt in by `SurfaceFX_Name` (stock: `Generic_Footprint` on
infantry, `Generic_Track` on the T4-B, Juggernaut, and Pulse Tank in FoC, `Generic_Track_Half` on the
MPTL and TIE Crawler); the SurfaceFX names the track and the tread bones.

The footprint texture is one stride: a left and a right print, offset along V. The engine draws the
whole texture over each segment (a ribbon's natural mapping); with `segment_length` 6 a stride is 6
units (a trooper is about 14 units tall, so about 77 cm). The `_Concrete` variants' `segment_length`
1 would squash them, but those are at opacity 0.1. A reading, not observed.

## Audio

### Where sounds are

Effects and unit responses are in `Data/Audio/SFX/`: `sfx3d_non_localized.meg`,
`sfx2d_non_localized.meg`, and one `sfx2d_<language>.meg` per language (the case varies:
`sfx2d_english.meg`, `sfx2d_Russian.MEG`). They are not in `megafiles.xml` and store bare file names,
so the engine mounts them under `DATA/AUDIO/SFX/`, only the chosen language's, and archive discovery
skips their folder. Music and dialogue speech are MP3 (under `Data/Audio/Music` and
`Data/Audio/Speech/<language>`); the XML names speech `.wav`, so samples resolve by trying the name
and then `.MP3`, and files are recognized from their first bytes, not their extension.

Every effect sample the stock events name is 16-bit PCM WAV: 9,610 distinct samples, 9,539 resolved
and decoded (3,862 mono 11,025 Hz, 4,754 mono 22,050 Hz, 903 mono 44,100 Hz, 20 stereo 44,100 Hz; 214
minutes), 71 missing from the stock data itself (`sound-audit`).

### Sound events

`SFXEvent` (3,073 stock, 540 positional, 153 looping):

- `Samples`: one chosen per play, at random or with `Play_Sequentially` in turn;
- `Min/Max_Volume` and `Min/Max_Pitch` in percent; `Probability` in percent (may list one value per
  sample; the engine uses the first); `Min/Max_Predelay` in milliseconds; `Play_Count -1` loops;
  `Is_3D`/`Is_2D`;
- `Max_Instances`: the engine replaces the oldest one-shot when a new play reaches the limit, and unit
  loops at the limit wait instead (an inference: the TIE Crawler fires bursts of five shots 0.2 s apart
  with no recharge while its 1 s fire sound, preset `Preset_GUNV`, allows four instances; refusing the
  fifth leaves every fifth shot silent and the sound drifting against the shots);
- `Overlap_Test`: a group name such as `Unit_EHD` shared by a type's responses; the engine lets one
  play of a group sound at a time (an inference);
- `Chained_SFXEvent`, `Pre_Samples`/`Post_Samples` (loop intros and outros, read but not played),
  `Volume_Saturation_Distance`, `Priority`, `Is_Unit_Response_VO`, `Kills_Previous_Object_SFX`, and
  `Use_Preset` naming a preset (`Is_Preset`) whose fields the event overrides.

The first definition of a name wins, as for object types ([XML loading](#xml-loading)).

### Unit sounds and 3D settings

Units name their sounds in XML: `SFXEvent_Fire` (a unit's own weapon), a weapon hardpoint's
`Fire_SFXEvent` (each hardpoint its own; the engine falls back to the type's `SFXEvent_Fire`),
`Death_SFXEvent_Start_Die`, `Death_Explosion_SFXEvent`, `SFXEvent_Engine_Moving_Loop`, and the
responses `SFXEvent_Select`, `SFXEvent_Move`, `SFXEvent_Attack`. The pulses of one tick that share a
shooter and a sound play once (an engine choice, so a battery of identical guns does not stack).

`AUDIO.XML` sets per mode `Audio_<Land|Space>_3D_Saturation_Distance_Mod`, `_Rolloff_Distance_Mod`,
and `_Listener_Z_Pullback_Dist`. The engine's reading: full volume within an event's saturation
distance (200 when it names none) times the factor, then falling as s / (s + r (d - s)) with r = 0.05
times the rolloff factor, heard from the camera's focus raised by the pullback. The curve, the base
rolloff, the default distance, and the limit of 16 unit loops are engine choices to check against the
original.

### Sounds in animations

`DATA/XML/ANIMATIONSFXMAPS.TXT` (1,639 lines on stock FoC, 479 animations) holds `SOUND <animation>
<time> <sound event>` (574 rows: walker hydraulics, death falls, creature sounds) and `SURFACE
<animation> <time> <trigger> <bone>` (578 rows: foot plants, impacts of falling bodies, tracks). A
trigger names a `SurfaceFX` in `SURFACEFX.XML` (the names are listed in
`ENUM/SURFACEFXTRIGGERTYPE.XML`), whose `Surface_Settings` per surface kind (default, dirt, grass,
snow, water, concrete, lava, ...) name a sound, a particle, a decal, and a dynamic track; `NONE`
stands for nothing. The time is read as hundredths of a frame (an inference: a stormtrooper's left
foot lands at 1032-1170 in its 22-frame walk, half way, and in 59 of 60 sampled animations every row
lies within the animation; the exception, `UV_MDU_DEPLOY_00`, has a row at 132% of its length). Only
some models have rows: stormtroopers, heroes, walkers, and creatures, not rebel soldiers.

### Responses to group orders

- **Fleet moves.** `SFXEvent_Fleet_Move` is set on 215 types, infantry and vehicles included, so it is
  not a space-only order response. The owner's guess (unverified): it is the response when a fleet is
  sent to another planet in galactic conquest. The engine does not play it in battles.
- **Assist lines.** When the player orders two or more squads of different units together, the unit
  with the higher priority answers with its own line as usual, and one of the others answers with its
  assist line after a short pause: Luke Skywalker ordered to move with two rebel squads says
  "Understood!", then the troopers "Escorting!" (owner's account). The engine plays a hero's
  `SFXEvent_Group_Move` or `_Group_Attack` (25 types each), else the usual response, then the first
  unit of another type with an `SFXEvent_Assist_Move` or `_Assist_Attack` line 0.2 s after the
  response ends (the pause and the choice of who speaks are engine choices).

### Every sound source in the stock data

Survey of stock FoC XML, maps, and 648 scripts:

- **Events.** 3,073 `SFXEvent`s in the `SFXEVENTS*.XML` files; speech: 2,271 `SpeechEvent`s in
  `SPEECHEVENTS.XML` (advisor and HUD voice, MP3 files, `Text_ID` subtitles); music: 62 `MusicEvent`s
  in `MUSICEVENTS.XML` (MP3 files, `Loop`, `Fade_In_Seconds`, `Fade_Out_Previous_Seconds`).
- **Battle objects** name sounds for: weapons (`SFXEvent_Fire`, hardpoint `Fire_SFXEvent`,
  `SFXEvent_Barrage`), projectiles (`Projectile_SFXEvent_Detonate` 87,
  `Projectile_SFXEvent_Detonate_Reduced_By_Armor` 62), deaths (`Death_SFXEvent_Start_Die`,
  `Death_Explosion_SFXEvent`, `Death_SFXEvent_End_Die` 18, `Spin_Away_On_Death_SFXEvent_Start_Die`
  22), damage (`Land_Damage_SFX` 221, a list by damage level; `SFXEvent_Damaged_By_Asteroid`),
  hardpoints (`SFXEvent_Hardpoint_Destroyed` 362), engines and props (`SFXEvent_Engine_Moving_Loop`,
  `_Idle_Loop`, `SFXEvent_Ambient_Loop` 90 on units, props, structures, and markers such as
  `Audio_Waterfall_Emitter_Marker`), periodic sounds while moving (`SFXEvent_Ambient_Moving` with a
  min/max delay), turrets (`SFXEvent_Turret_Rotating_Loop` 37), abilities (`SFXEvent_Activate` 126,
  `SFXEvent_Target_Ability`, `SFXEvent_Target_Affected`), entering nebulae and asteroid fields, and
  unit responses (`SFXEvent_Select`, `_Move`, `_Attack`, `_Attack_Hardpoint` 426, `_Guard`, `_Stop`,
  `_Fleet_Move`, `_Group_Move`, `_Group_Attack`, `_Assist_Move`, `_Assist_Attack`).
- **The player's side.** HUD and advisor announcements (`SFXEvent_Unit_Lost`,
  `SFXEvent_Unit_Under_Attack`, health warnings, build started, complete, and cancelled, reinforcement
  points, retreat and surrender, battle won or lost, all in `FACTIONS.XML` and the unit types), the GUI
  (`Mouse_Over_SFX` 294, `Click_SFX` 118, command bar), and the galactic map.
- **Ambience.** `WEATHERAUDIO.XML` maps a weather (RAIN, SNOW, SANDSTORM, ASH, CLEAR) and its intensity
  (thresholds 0.01-0.75) to a loop and to intermittent sounds with a delay range, and each environment
  kind (TEMPERATE, ARCTIC, DESERT, FOREST, SWAMP, VOLCANIC, URBAN) to a clear-weather intermittent
  sound. `AMBIENTMAPSOUNDS.XML` is a stub (one test entry, six empty environments). Maps hold no sound
  names. A map's weather scenario is decoded ([Weather](#weather)); where a map's environment kind is
  stored is not found yet (presumably in the TED environment data).
- **Music.** Each faction (`FACTIONS.XML`, `EXPANSION_FACTIONS.XML`) lists ambient and battle music by
  environment kind (`Music_Event_List_Ambient`, `Music_Event_List_Battle`: Space, Temperate, Arctic,
  ...), plus load-screen, super-weapon, win, and lose music; `AUDIO.XML` sets how long a battle stays
  quiet before the music returns to ambient (`Music_Land/Space_Battle_To_Ambient_Peace_Seconds`).
- **Animations.** `ANIMATIONSFXMAPS.TXT` ([Sounds in animations](#sounds-in-animations)).
- **Scripts.** `GameObject.Play_SFX_Event(name)` (83 calls in 24 scripts; all 30 names are defined
  sound events; the engine plays them at the object), `Play_Music(name)` (4: cinematic and cantina
  music), `Stop_All_Music` (25), `Resume_Mode_Based_Music` (14), `Stop_All_Speech` (19),
  `Weather_Audio_Pause(true|false)` (13), `Allow_Localized_SFX(true|false)` (40), `Allow_Ambient_VO`
  (2), `Master_Volume_Restore` (3), `Stop_SFX_Event` (1), and `Play_Bink_Movie` (6, movies with their
  own sound). Story dialogue (`Story_Dialog`, 178 in the campaign XML) plays speech.

## Camera and input

Owner observation, not measured: the tactical camera keeps a fixed pitch and heading and cannot
rotate; the player zooms in and out and pans with the keyboard. As in most RTS games, the left mouse
button selects units, by click or by dragging a selection rectangle, and the right mouse button issues
move and attack commands. Exact angles, zoom limits, pan speeds, and per-map camera settings need
evidence from the shipped data or black-box observation before a legacy camera can follow them.

### Default key bindings

Transcribed from the owner's screenshots of Forces of Corruption's controls menu (Options, five tabs;
copies kept locally under `Reference Materials/Screenshots/Original UI/`, never committed). Every
binding is rebindable there, with Back, Defaults, and Accept. Plain keys are the orders, groups, and
camera; every ability is a chord: Ctrl for land abilities, Shift for space abilities, Alt for heroes.

Units:

| Action | Key | Action | Key |
| --- | --- | --- | --- |
| Select group 1-9, 0 | 1-9, 0 | Guard | G |
| Select all | Ctrl+A | Attack | A |
| Select like | Ctrl+Q | Stop | S |
| Next unit | F | Move | M |
| Previous unit | D | Attack move | T |
| | | Waypoint | W |
| | | Retreat | E |
| | | Reinforce | R |
| | | Place beacon | B |
| | | Super weapon | Ctrl+W |

Land abilities: Deploy/undeploy Ctrl+D, Take cover Ctrl+Z, Jetpack Ctrl+J, Thermal detonator K (the
one unchorded ability), Drop thermal detonator Ctrl+K, Rocket weapon Ctrl+G, Cable attack Ctrl+F,
Sensor ping Ctrl+O, Boost weapon power Ctrl+B, Self destruct Ctrl+X, Deploy stormtroopers Ctrl+H,
Maximum firepower Ctrl+M, Sprint Ctrl+N, System control Ctrl+C, Remote explosive Ctrl+E, Detonate
remote explosive Ctrl+R, Fire cannon Ctrl+U, Point defense laser system Ctrl+L, Stim pack Ctrl+S,
Swap weapons Ctrl+I, Minor drain life Ctrl+T, Proximity mines Ctrl+P.

Space abilities: Boost shield power Shift+O, Tractor beam Shift+T, Gravity well generator Shift+G,
Barrage area Shift+B, Hunt for enemies Shift+H, Boost engine power Shift+E, Lure enemy fighters
Shift+L, Missile jamming field Shift+M, Lock S-foils Shift+W, Ion cannon shot Shift+I, Destroy planet
Shift+D, Sensor jamming Shift+J, Stealth Shift+C, Leech shields Shift+N, Buzz droids Shift+Z,
Enhanced targeting Shift+A, Cluster bomb Shift+K, Point defense laser system Shift+P, Self destruct
Shift+S, Full salvo Shift+F, Deploy squadron Shift+Q.

Interface:

| Action | Key | Action | Key |
| --- | --- | --- | --- |
| Toggle droid advisor | Numpad Subtract | Quick save | F6 |
| Initiate chat | Enter | Quick load | F5 |
| Scroll up, down, left, right | Arrow keys | Mission holocron | Tab |
| Reset camera | End | Taunt one, two, three | Numpad 1, 2, 3 |
| Camera center | Page Up | (Multiplayer) give credits | Shift+Y |
| Camera home | Home | Toggle tactical interface | I |
| Tactical overview | Insert | Toggle player list | P |
| Camera tether | Scroll Lock | | |
| Battlecam | Space | | |
| Unit preview | C | | |
| Screen shot | F12 | | |

Hero: Lucky shot Alt+K, Call for wingmen Alt+W, Force crush Alt+H, Force push Alt+P, Force lightning
Alt+L, Force corrupt Alt+O, Force protect Alt+Z, Capture vehicle Alt+V, Exit vehicle Alt+Y, Proton
beam Alt+X, Seismic charge Alt+M, Weaken enemy Alt+[, Invulnerability Alt+], EMP burst Alt+/,
Control turret Alt+comma, Repair vehicle Alt+period, Flame thrower Alt+J, Redirect all firepower
Alt+;, Saber throw Alt+T, Stealth Alt+C, Jedi mind trick Alt+U, Force sight Alt+S, Force attack
Alt+E, Radioactive contamination Alt+minus, Infection Alt+I, Drain life Alt+D, Summon Rancor Alt+R,
Blast Alt+G, Shield flare Alt+F, Sonic stunner Alt+N, Bribe enemies Alt+B, Full salvo Alt+A, Sensor
jamming Alt+Q.

What the table implies for the engine: bindings are chords (a modifier and a key), and orders are
keys of their own (attack A, attack move T, guard G, move M, stop S, waypoint W), not modifiers held
while right-clicking. There are no WASD camera keys: W, A, S, and D are orders, so panning is the
arrow keys only (and the screen edges, not shown in the menu). An order key arms the next click: the
cursor changes, and the next left-click on the ground or a unit gives the order (the owner's
account of the game; Stop presumably acts at once, unconfirmed).

### Main menu

The owner's screenshot of Forces of Corruption's main menu (build 1.121.859328, shown at the bottom
left): a live space battle as the background (X-wings, a Mon Calamari cruiser and Star Destroyers
with their weapon fire), the title at the top, the LucasArts and Petroglyph logos at the bottom left,
the faction emblem at the right, and the menu at the bottom right under the heading "Main Menu":
Tutorial, Single Player, Multiplayer, Load Game, Options, Exit Game, as wide framed buttons. A UI
reference for the menu's layout, not an asset to reproduce.

### Skirmish battle setup

The owner's screenshot of Forces of Corruption's skirmish setup (`skirmish_setup.png` beside the
others): a game type list (Land, Space, Land Control), the map list with each map's player count in
brackets ("(2) Alderaan at War" to "(4) Yavin Training Grounds", scrolling), a preview of the
selected map with numbered start positions, Official Maps and Custom Maps buttons under it, and
eight player slots, each with four drop-downs: the player (the local player's name, an AI level such
as Easy AI, or Open), the faction (Empire, Rebellion), a team color swatch, and the team (Team 1,
Team 2). Cancel, Advanced..., and Start Game close it. A UI reference for M4's skirmish setup.

## Lua host protocol

What the engine must provide to the stock scripts, from the framework library under
`Data/Scripts/Library` (read as evidence; nothing quoted) and from running every stock script on
recording stubs (`EAWEngine lua-survey`). Which Lua VMs exist, what they cost, and why the host stays
on Lua 5.0.3: [lua-runtime-research.md](lua-runtime-research.md).

- **Language.** Lua 5.0 (the inspector's bytecode decoder reads 5.0 chunks; the scripts use
  `table.getn`, `arg` tables for varargs, and no `%` operator). Stock FoC ships all 370 scripts as
  source; one archive also carries a compiled `PGBase.lua` that loses in VFS precedence to the source in
  `64Patch.meg`.
- **Lifecycle.** The engine calls `Base_Definitions` when a script is created: the framework's version
  resets thread values and events, clears its globals (including `Object` and `PlayerObject`), sets
  `ServiceRate`, and calls the script's own `Definitions`. So the engine binds a game-object script's
  `Object` after creation (an inference). `main` then runs as a thread; when it returns, the script
  ends.
- **Threads.** Script threads are coroutines the engine resumes. The framework's `Sleep` and
  `BlockOnCommand` wait in Lua, calling `PumpEvents`, which yields `true` back to the engine and, when
  resumed, drains `GetEvent` (with `GetEvent.Params`) and calls each event. Yielding `false` ends a
  thread. `_ScriptExit` sets a flag that ends the whole script at the next yield (`ScriptExit` wraps
  it). `GetThreadID` is negative outside threads. `ThreadValue(key)` and `ThreadValue.Set(key, value)`
  are per-thread storage. The framework sets `ServiceRate = 0.2` ("five times a second"); the host
  services each thread that often. `Create_Thread` is also a table: `Create_Thread.Kill(id)` and
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
  string names the task force, followed by composition rules), and often `RequiredCategories`. The AI
  framework's `Base_Definitions` clears a global of each task force's name, so the engine binds the task
  force object under that name and runs `<Name>_Thread` (150 such functions in 134 stock plans). Task
  force events are functions named `<Name>_<Event>` with the task force first among their arguments:
  `Production_Failed` (41 plans), `No_Units_Remaining` (30), `Original_Target_Owner_Changed` (19),
  `Unit_Move_Finished` (13), `Unit_Damaged`, and others; PGEvents.lua defines a `Default_<Event>` for
  most, which the engine presumably calls when a plan defines none (an inference). Plans read
  `PlayerObject` and `Target` (AITarget).
- **Story scripts.** A story script lists its states in `StoryModeEvents` (event name to state
  function); PGStoryMode.lua registers them with the state machine of PGStateMachine.lua, whose `main`
  runs the states. `Story_Event_Trigger(name)` switches to an event's state, and no stock script calls
  it, so the engine fires story events through it (from the campaign's story XML).
- **Engine objects.** Objects are userdata: the framework services `Object` only when `type(Object) ==
  "userdata"`, and `Flush_G` asks userdata whether they are pool-safe. Scripts call their methods with
  a dot, without self (`unit.Move_To(healer, 10)`), so a method is a function bound to its object.
  Objects serve as table keys (`DeathTable[obj]`, `ProxTable[obj]`), so the same object must always be
  the same value. Orders return a block that `BlockOnCommand` polls with `IsFinished()` (the most used
  method, 140 scripts) and reads with `Result()`. `Get_Hull()` is a share of the hull (scripts compare
  it with 0.5 or 0.9), `Get_Type().Get_Name()` (121 uses) names the type, and
  `Find_All_Objects_Of_Type` takes a type name or an object type, and optionally a player. What
  `Result()` returns, and what a method on a dead object does, are not documented: the engine answers
  `Result()` with whether the order ended as ordered, and dead objects with nil (engine choices).
- **Engine names are tables too.** `WeightedTypeList.Create()`, `FogOfWar.Reveal_All`,
  `GameRandom.Get_Float()`: some engine globals are tables of functions, and objects returned by engine
  functions carry methods (`Object.Get_Owner().Get_Faction_Name()`).
- **Object scripts and hints.** A type names its game-object script in XML `Lua_Script` (a module name
  under `Data/Scripts`, 153 stock definitions, 78 types directly). A placed object's hint, which
  `Find_Hint(type, hint)` and `Find_All_Objects_With_Hint(hint)` look up, is a NUL-terminated string in
  mini-chunk 1 of chunk 0 inside the object's 0x482 container in the TED (2,450 stock placed objects,
  1,713 distinct hints; 1,575 of the 1,670 hint names the stock scripts use appear there). A second
  string, in the object's 0x46e container (5,246 objects, 19 distinct non-empty values such as
  `commanderspawn`), is not identified yet.
- **Proximity.** `Event_Object_In_Range(handler, range[, player])` on an object is evaluated when the
  object is serviced (`Service_Wrapper`, which `PumpEvents` and `Process_Proximities` call), with
  handlers `(self, other)` that usually cancel themselves; that every object in range triggers the
  handler on every service is an engine choice.
- **Debug output, the script, modes, categories.** PGDebug.lua formats messages and hands them to three
  engine sinks: `_ScriptMessage` (ScriptMessage, DebugMessage; the most used engine name, 304 scripts),
  `_MessagePopup` (MessageBox), and `_OuputDebug` (OutputDebug, so spelled). The `Script` global stands
  for the running script: messages start with `tostring(Script)`, and
  `Script.Debug_Should_Issue_Event_Alert()` asks whether events should raise popups. Scripts compare
  `Get_Game_Mode()` with "Land", "Space", and "Galactic". A type's categories are its XML
  `CategoryMask`, names separated by `|` (840 stock definitions: Structure, Upgrade, Infantry, Capital,
  LandHero, ...), which `Is_Category("Infantry")` tests.
- **Attack-move and guard.** `Attack_Move(destination[, threat])` (268 stock calls in 79 scripts) takes
  an object (an enemy, a hero, a marker, a task force) or a position, and 70 calls wait on its block
  with `BlockOnCommand`; the second argument comes from the AI (`Get_Self_Threat_Max()`).
  `Guard_Target(anchor)` (174 calls in 71 scripts) takes a position, often the unit's own
  (`unit.Get_Position()`, holding an area), or an object (a structure, a base, a hero, a task force),
  and nothing waits on it. The engine's semantics (engage hostiles within 1.25 x weapon range on the
  way, break off beyond 2 x weapon range, guard radius the weapon range but at least 150 units, a guard
  of an object follows it and ends when it dies) are engine choices to check in play.
- **Scripted conditions.** `Make_Invulnerable(true|false)` (165 stock calls),
  `Set_Cannot_Be_Killed(true|false)` (75), `Suspend_Locomotor(true|false)` (266), `Take_Damage(amount)`
  (almost always a kill: 10000, 9999, 99999), `Teleport_And_Face(marker or position)` and
  `Teleport(target)`. The engine's reading: invulnerable takes no damage at all; cannot be killed stops
  at one hit point (or a hundredth of the health); `Take_Damage` hits the hull past shields and armor;
  a suspended locomotor holds a unit with its orders; teleporting takes an object's heading. All are
  inferences from the names and uses.
- **Random numbers.** `GameRandom(lower, upper)` returns an integer in the inclusive range (scripts
  index lists with it and call `GameRandom(1, 1)`), `GameRandom.Get_Float()` a number in [0, 1), and
  `GameRandom.Free_Random(lower, upper)` the same as `GameRandom`; that it is the unsynchronized variant
  is an inference from its name. The host serves all three from its seeded stream.
- **What scripts call.** Creation and main loops alone (no events, 10 game seconds) reach 46 functions
  and 18 methods. With AI plan threads, story events, task force events, and callbacks (about 2,900
  events over 370 scripts) the survey reaches 167 functions and 129 methods; 317 scripts run without
  errors, and most remaining errors come from story states fired out of their campaign order or from
  stubs where a script expects a number. Most used by script count: `_ScriptMessage`,
  `GetCurrentTime`, `_ScriptExit`, `_MessagePopup`, `Find_Player`, `Find_Object_Type`, the task forces'
  `Produce_Force`, `Find_Hint`, `Get_Game_Mode`, `Find_First_Object`, `Find_All_Objects_Of_Type`, then
  the cinematic and story functions (`Spawn_Unit`, `Lock_Controls`, `Story_Event`,
  `Start_Cinematic_Camera`). The ranked list with argument types comes from `lua-survey --report`.
