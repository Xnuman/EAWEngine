# Asset and localization tools

## EAWTools graphical workbench

`EAWTools.exe` is the primary interactive interface. It is a native C++20
application using Dear ImGui's docking branch with the maintained Win32 and
DirectX 11 backends. Search results and DAT rows use clipped/virtualized tables,
so the interface only draws visible rows even for full 19,000+ entry language
files.

The **Asset Inspector** page can:

- auto-detect or browse to an Empire at War installation;
- switch between Empire at War and Forces of Corruption;
- add ordered loose mod layers and show the final winning virtual asset;
- search and filter by extension;
- inspect DAT, DDS, semantic ALO models, semantic ALA animations, MTD, TED, XML,
  Lua, and generic binary metadata;
- preview ALO geometry in an orbitable viewport, resolve packaged DDS diffuse
  textures through the active mod/VFS layers, play matching ALA animations,
  and optionally display skeleton and collision geometry;
- cap viewport triangle detail for large models. Bind-pose geometry is cached;
  animated skinning is recomputed only while an animation is selected.

The **DAT Localization** page can:

- open a complete game/mod DAT or a sparse, version-control-friendly TSV;
- edit text, group, status, XML context, and translator notes;
- create arbitrary keys or paired unit-name/tooltip keys;
- scan `Data/XML` for missing active `TEXT_*` references;
- filter large tables by text or workflow status and sort Key, Text, Group, or
  Status in either direction by clicking a column header;
- validate duplicate, TODO, CRC, and DAT structure issues;
- build a complete output DAT without changing the baseline file.

The GUI calls the same `eaw_core` APIs as the command-line programs; it does not
spawn or scrape CLI subprocesses. Native Windows file/folder dialogs preserve
Unicode paths.

## EAWAssetInspector

The inspector reads the same layered virtual filesystem as the engine. Results
therefore show the asset that would win after the base archives, patch archives,
loose files, and supplied mods are combined. It never extracts or changes the
game installation. The Forces of Corruption profile mounts the original
`GameData` archives below the expansion's `corruption` archives because many FoC
models intentionally reuse base-game textures and other resources.

The precedence contract is regression-tested across manifest and unlisted
archives, numeric and 64-bit patches, loose files, inherited EAW data, and
multiple ordered mods. Numbered patches use numeric ordering (`Patch10` follows
`Patch2`), and the first supplied mod has highest priority.

```powershell
# Counts, logical sizes, extensions, and winning source mounts
EAWAssetInspector.exe summary --game foc

# XML files visible after all overrides
EAWAssetInspector.exe list Data/XML --extension .xml --limit 100 `
  --mod "D:\path\to\mod"

# Format-specific metadata and a small content inventory
EAWAssetInspector.exe inspect Data/Text/MasterTextFile_English.dat
EAWAssetInspector.exe inspect Data/Art/Models/Some_Unit.alo

# Unique TEXT_* references in one resolved XML file
EAWAssetInspector.exe localization-refs Data/XML/Units.xml

# Audit model references, material mappings, DDS formats, and MTD atlases
EAWAssetInspector.exe texture-audit Data/Art/Models --game foc

# Inventory Lua source/bytecode, dependencies, callbacks, and host API calls
EAWAssetInspector.exe lua-audit Data/Scripts --game foc --limit 100

# Parse and index the complete resolved XML object database
EAWAssetInspector.exe xml-audit Data/XML --game foc

# Add type/tag, scalar, enum, repetition, and object-reference validation
EAWAssetInspector.exe xml-audit Data/XML --game foc `
  --schema-root "D:\path\to\eaw-schema"
```

Current semantic inspectors cover DAT string tables, DDS textures, MTD atlas
directories, ALO model structure, ALA animation structure, TED maps, Petroglyph
XML, and Lua source/bytecode. The XML
reader accepts the game's multi-root fragments and historical declaration
syntax, preserves element/attribute/field order and source lines, and indexes
typed `Name` definitions case-insensitively. `xml-audit` reports definition-type
coverage, duplicate type/name keys, XML file references, missing references,
and parse failures across the final VFS winners. The combined stock FoC/EAW
corpus currently parses all 638 resolved XML files and indexes 21,283 named
definitions across 185 element types.

Schema validation is optional and offline. `--schema-root` accepts either the
root of an `eaw-schema` checkout or one game schema directory. When a repository
root is supplied, FoC validation layers the expansion schema over the inherited
EAW schema. Registry metafiles assign types to referenced files, while a nested
element whose canonical name identifies a more specific registered type takes
precedence. Remaining definitions are typed only when one schema type is the
unique structural match with at least two matching fields and 60 percent field
coverage. Diagnostics cover unknown/non-repeatable fields, Boolean, integer,
float, vector, RGBA and enum shapes, unresolved XML-object names, and references
whose existing target has the wrong declared type or behavior-derived kind.
Variant objects inherit kind-defining fields from their parent unless they
override the field. The external schema remains a work in progress, so
stock-data diagnostics are compatibility evidence to review, not automatically
engine defects.

ALO reports skeletons, meshes, bounds, materials, texture references, geometry
counts, attachments, proxies, lights, and dazzles. ALA reports format version,
playback timing, bone tracks, packed
translation/rotation/scale blocks, visibility, and stepped channels. Particle
ALO particle files retain a safe top-level chunk fallback. TED
inspection reports land/space properties, terrain dimensions and elevation,
texture layers, water maps, tracks, decals, environments, passability, object
CRC counts, AI zones, and embedded map previews. Land TED files also get an
orbitable heightmap preview with adaptive detail, elevation shading, raw
passability-class colors, wireframe, and reference-grid overlays. The GUI
renders space TEDs as an orbitable X/Y placed-object map using decoded
transforms, Z-altitude guides, type colors, player-code outlines, and tactical
bounds. Map previews can be detached into a larger resizable window. The asset
browser also provides an expandable catalog of known game extensions alongside
its custom extension field. Land previews can overlay the same decoded,
type-colored placed-object markers used by space maps. Land transforms use
positive X/Y editor coordinates at 20 world units per terrain sample, while Z
elevation is converted to the heightmap's 20x stored scale. The
GUI additionally consumes render-ready vertex/index/skin streams, decoded ALA
frames, and every DDS encoding observed in the stock corpus. The shared DDS
decoder also recognizes BC4/BC5 and common DX10 formats for mods. MTD inspection
lists source-image CRCs and atlas rectangles. ALO material inspection preserves
raw parameters and adds engine-neutral shader-family, blend-mode, render-phase,
lighting-model, and texture-role hints. Shaders covered by the legacy reference
corpus use documented reference profiles ([Legacy shader mapping](legacy-shader-mapping.md));
other names are tagged `name-heuristic`. Production rendering of those hints
remains a renderer-layer task.

`texture-audit` parses every resolved DDS and MTD, checks the MTD-to-compressed-
DDS dependency, inventories format and material coverage (including translation
source, render phase, lighting model, and heuristic-only shader names), and
reports missing ALO texture references with an example model. The optional prefix scopes model
inspection; the DDS/MTD corpus always covers the final mounted `Data` VFS.

`lua-audit` is read-only and never executes scripts. For source it ignores
comments and string contents while collecting explicit `require`, `Script.Load`,
`Script.Reload`, and `include` dependencies, named function definitions, and
called names. It resolves modules against the final layered VFS and separates
standard-library and corpus-defined names from candidate engine host calls. For
standard Lua 5.0 and Petroglyph `Lup` bytecode it validates the architecture
header and recursively inventories prototypes, constants, instructions, and
persistent function IDs. This is structural compatibility analysis, not a
decompiler or Lua VM.

`map-audit` parses every resolved TED in one mounted profile and reports corpus
coverage without extracting maps:

```powershell
EAWAssetInspector.exe map-audit Data/Art/Maps --game foc
```

## EAWDatEditor

The DAT editor preserves the Petroglyph binary layout, UTF-16 values, ASCII
keys, CRC32 values, CRC ordering, index ordering, and legacy duplicates. Writes
go to a sibling temporary file and are committed atomically. Existing outputs
are not replaced unless `--force` is passed.

The tool deliberately separates binary deployment files from editable source:

```text
stock/mod MasterTextFile_LANGUAGE.dat
                 +
      sparse localization.tsv
                 |
                 v
new complete MasterTextFile_LANGUAGE.dat
```

TSV workspaces are version-control friendly and support embedded tabs, quotes,
newlines, and full Unicode. Their columns are:

- `Key`: the `TEXT_*` identifier;
- `Text`: localized value;
- `Group`: unit, XML file, campaign, or another mod-defined grouping;
- `Status`: `todo`, `ready`, `new`, `modified`, `existing`, or `deleted`;
- `Context`: XML references or authoring hint;
- `Notes`: reviewer/translator notes.

### New-unit workflow

```powershell
EAWDatEditor.exe add-unit localization.tsv MY_NEW_FRIGATE `
  "My New Frigate" "A fast escort frigate."

EAWDatEditor.exe build MasterTextFile_ENGLISH.dat localization.tsv `
  output\MasterTextFile_ENGLISH.dat
```

This creates `TEXT_UNIT_MY_NEW_FRIGATE` for `<Text_ID>` and
`TEXT_TOOLTIP_MY_NEW_FRIGATE` for `<Encyclopedia_Text>`. Additional ability,
campaign, and UI keys can be ordinary TSV rows in the same group.

### Find missing mod localization

```powershell
EAWDatEditor.exe scan-mod "D:\path\to\mod" `
  "D:\path\to\mod\Data\Text\MasterTextFile_ENGLISH.dat" missing.tsv
```

The scanner ignores XML comments, records up to five source locations per key,
and emits unresolved references as `todo`. A normal build refuses unresolved
TODO rows whose text is empty; `--allow-todo` skips those empty rows for an
intentionally partial build. A TODO row that already contains text is treated as
resolved and included, and editing an empty TODO value in the GUI automatically
changes its status to `ready`.

### Translation validation

```powershell
EAWDatEditor.exe validate MasterTextFile_GERMAN.dat `
  --reference MasterTextFile_ENGLISH.dat
```

Besides structural, CRC, Unicode, and duplicate checks, reference validation
compares `%s`/`%d` placeholders, escaped newlines, bracket/angle/brace tokens,
key coverage, and leading/trailing whitespace. The stock repeated
`TEXT_END_OF_DATA` marker is preserved and reported as a warning, not rejected.

Other commands include `info`, `list`, `get`, `set`, `remove`, `export`, and
`diff`; run `EAWDatEditor.exe --help` for their exact arguments.
