# Compatibility research

The bootstrap uses public documentation and the user's installed data. It does
not use leaked source code, decompiled code copied from the original binary, or
redistributed game assets.

Primary starting points:

- AlamoEngine-Tools organization: <https://github.com/AlamoEngine-Tools>
- PetroglyphTools format libraries: <https://github.com/AlamoEngine-Tools/PetroglyphTools>
- Petroglyph game/mod discovery: <https://github.com/AlamoEngine-Tools/PetroglyphGameInfrastructure>
- XML schema project: <https://github.com/AlamoEngine-Tools/eaw-schema>
- Stock-data baseline index: <https://github.com/AlamoEngine-Tools/eaw-baseline>
- Petrolution file-format index: <https://modtools.petrolution.net/docs/Formats>
- MEG v1 format: <https://modtools.petrolution.net/docs/MegFileFormat>
- Shared chunk format: <https://modtools.petrolution.net/docs/ChunkFileFormat>

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

