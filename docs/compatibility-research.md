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
