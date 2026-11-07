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
