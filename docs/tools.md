# Tools

The engine's command-line modes, the launchers, and the asset and localization tools. Debug builds
carry the `_d` suffix (`EAWEngine_d.exe`); the examples use the Release names. Every program only
reads the game installation and mods.

## EAWEngine sandbox

`EAWEngine sandbox` (the default mode) opens the Direct3D 12 sandbox on a model, a map, or the
validation triangle. `EAWEngine --help` lists every option.

```powershell
EAWEngine.exe doctor --game foc                       # discovery, mounts, and an asset probe
EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED `
  --spawn T2B_Tank:6@1 --spawn AT_ST_Walker:6@2       # two opposing groups
EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Star_Destroyer:1@2 --spawn Calamari_Cruiser:1@1   # a capital ship duel
EAWEngine.exe sandbox --game foc --model DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO
```

### Scene and game data

| Option | Effect |
| --- | --- |
| `--game eaw\|foc`, `--game-root <path>` | Game variant and installation (discovered through Steam by default). |
| `--mod <path>` | Add a mod (repeatable; the first has the highest priority). A relative path is looked up as the game's MODPATH, under the runtime folder (`Mods\Name`), then from the working directory. |
| `--map <ted>` | Show a map: placed objects as simulation entities, terrain, liquids, tracks, sky and backdrop. Squadrons and companies appear as their members. |
| `--model <alo>`, `--animation <ala>` | Show one model; without `--animation` its `_IDLE_00.ALA` plays when present. |
| `--validation-triangle` | Skip asset loading. |
| `--spawn <TYPE>[:N][@OWNER]` | Make an XML type spawnable (repeatable); N spawn at the view center for owner slot OWNER (default 1). |
| `--spawn-later <TYPE>[:N][@OWNER]` | Load a type at runtime once the map is shown and spawn it. |
| `--effect <NAME>` | Play a particle effect (for example `P_EXPLOSION_BIG00`) at the map center, repeating. |
| `--language <name>` | Language of the localized sound archive and speech (default `English`; text and speech share it for now). |
| `--volume <0..1>` | Master volume (default 0.2); the sound panel changes it at runtime. |
| `--audio <xaudio2\|fmod\|none>`, `--no-audio` | Audio backend (default `xaudio2`; `fmod` only in builds configured with `-DEAW_AUDIO_FMOD=ON -DFMOD_ROOT=<local SDK>`). A backend that cannot start falls back to silence with a warning; the run summary counts sounds played, skipped by their rules, and missing. Maps play weapon fire, deaths, engine loops, and unit responses to selections and orders. |
| `--particles legacy\|enhanced` | How particle effects are interpreted: legacy (default) reproduces the original's look-defining quirks, enhanced improves them. |
| `--bloom <strength>`, `--no-bloom` | Bloom strength (default 0.8). |
| `--lod <off\|distance\|screen\|hybrid>` | How meshes with detail levels pick one (default `hybrid`: screen size, full detail near, the least far away; `off`: always full detail). |
| `--mesh-detail <0..1>` | The mesh-detail setting (as the graphics presets' `MeshDetail`): 1 keeps detail 1.5 times as far as the base thresholds, 0 half as far (default 1). |
| `--hot-reload` | Rebuild the scene when one of its loose files or MEG archives changes, re-decoding only what changed; the camera is kept. |

Map owner slot 3 is neutral and every other slot hostile, for now.

### Presentation and devices

| Option | Effect |
| --- | --- |
| `--width`, `--height` | Initial client size. |
| `--vsync on\|off`, `--frame-latency 1\|2` | Present mode and queued frames (default vsync, two frames). |
| `--warp`, `--gpu-validation` | Software adapter and Direct3D 12 GPU-based validation. |
| `--upload-budget <MiB>` | Texture bytes streamed per frame (default 64; 0 uploads all at once). |
| `--pipeline-cache <dir>` | Keep compiled pipeline states between runs; stale caches are discarded. |
| `--no-raw-input` | Read mouse and keys from window messages instead of raw input. |
| `--overlay`, `--panel statistics\|spawn\|sound\|camera` | Open the statistics panel or a dev panel at start (`menu` is still accepted; the bar starts shown). |
| `--show-bounds`, `--show-passability` | Start with bounds or passability drawn. |

### Automation and diagnostics

| Option | Effect |
| --- | --- |
| `--frame-limit <n>` | Exit after n frames once the scene is shown. |
| `--auto-move` | Order every mobile unit to the view center. |
| `--no-scripts` | Do not run game-object scripts (XML `Lua_Script`) in battles; also accepted by `battle`, and a recording replays as it was made only with the same setting. |
| `--pick <x> <y>` | Select what is at a client pixel once the scene is shown. |
| `--reload-every <n>` | Replace the scene with a fresh copy every n frames (resource-retirement test); remaining texture, slot, and retirement counts print on exit. |
| `--record <file>` | Write the order log on exit, for headless replay. |
| `--capture`, `--capture-frame`, `--fixed-frame-time`, `--camera` | Reproducible captures; see [Captures and image regression](#captures-and-image-regression). |
| `--log-dir <path>` | Folder for the per-run log (default `logs/` next to the executable; the 20 newest are kept). |
| `--no-error-dialog` | Report fatal errors on stderr and in the log only (also the case when stderr is redirected). |
| `--crash-test assert\|exception\|access-violation` | Fail on purpose to check crash reporting. |

Warnings, such as malformed mod data, go to stderr, the Visual Studio Output window, and the log.
Unexpected failures end the program with one report there and in an error window, whose Retry
breaks into an attached debugger.

### Controls

On maps the sandbox starts in the fixed (RTS) camera; C or the Camera panel switch between fixed, debug (the same camera, drag rotates, right-drag pans), and free (a flying camera: right-drag looks, WASD moves along the view, Q/E down and up, Shift faster, the wheel sets the speed). `--free-camera ex ey ez tx ty tz` starts in the free camera at that eye, looking at that target.

| Input | RTS camera (maps) | Free camera and model view |
| --- | --- | --- |
| WASD or arrows (Shift: faster) | Pan | Pan (maps) |
| Mouse wheel | Zoom within map limits | Zoom |
| Left drag | Box-select mobile units | Rotate and tilt (orbit a model) |
| Left click | Select the object under the cursor | Select the object under the cursor |
| Right click / right drag | Move the selection, or attack the hostile under the cursor | Pan (maps) |
| Ctrl + right click | Attack-move the selection to the point (engage hostiles on the way) | |
| Shift + right click | Guard the point, or the object under the cursor (followed as it moves) | |
| Space | Reset the camera | Reset the camera |

| Key | Action |
| --- | --- |
| F1 | Shows or hides the bar at the top: its Panels menu opens the statistics (graphics numbers), spawn (search, category and faction filters), sound, and camera panels; its View menu toggles bounds and passability. Panels first open side by side under the bar; where they are moved and sized is kept in `sandbox_imgui.ini` next to the executable. |
| N | Spawn the current type at the cursor (a type not loaded yet loads in the background). |
| Tab, Shift+Tab (Ctrl: by ten) | Cycle spawnable types: every unit type with a model for the map. |
| O | Cycle the owner of spawned units. |
| P | Pause or resume the world (simulation, animation, particles); cameras keep working. |
| B | Bounds of every object and the world axes. |
| G | Passability around the camera focus: red steep, blue water, orange impassable, green infantry only, magenta obstacle. |
| Left, Right | Model view: step through the bind pose and every `<MODEL>_*.ALA` beside the model. |
| Escape | Quit. |

Selected units show their remaining route (on space maps in 3D, through the detour a ship takes
around a unit in its way), and damaged or selected units show health (green) and shield (blue)
bars. Panels can be dragged, resized, and minimized; the mouse does not reach the scene
while it is over a panel.

## EAWDevLauncher sandbox scenarios

`EAWDevLauncher.exe` starts `EAWEngine sandbox` with a saved configuration
instead of a hand-typed command line. A scenario names:

- the game (EAW or FoC), an optional installation root, and the mod stack
  (highest priority first);
- the scene: a map from the mounted asset list with debug spawns
  (type, count, and owner slot, as `--spawn TYPE:N@OWNER`), a model with an
  optional animation, or the validation triangle;
- engine options: WARP, GPU validation, v-sync, hot reload, bounds,
  passability, auto move, statistics panel (hidden by default), raw input,
  frame limit, window size, upload budget, and free-form extra arguments.

The **Game root** field names the installation (empty: found through Steam); a folder that is not
one is refused with an error, and a mod folder put there by mistake is named as a mod (mods go in
the list below it, through **Add mod folder...**). The asset and spawn lists refresh by themselves a second after the game, installation, or mod list
changes (the **Refresh asset lists** button still forces one). Each spawn row has a **Pick...** list
of spawnable XML types, built from the
winning XML of the selected game, the inherited base game, and the mod stack:
every named definition with a land or space model (2,710 in stock FoC, 1,046
of them units and structures). It filters by the text typed in the row, by
"units and structures only", and by whether the type has a model for the map's
kind (land or space, from the stock `_SPACE` naming convention). It refreshes
in the background with the asset lists.

The window shows the exact command line (copyable), starts any number of runs
without waiting, and lists each run's exit code. Presets are plain text in
`EAWDevLauncher.scenarios` next to the executable:

```text
[scenario]
name=Bespin skirmish
game=foc
map=DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED
spawn=T2B_Tank:6@1
spawn=AT_ST_Walker:6@2
option=show-passability
vsync=on
```

Unknown keys and options load with warnings in the log, so presets written by a
newer build still open. The scenario model and process start live in the
`eaw_launch` library (`src/launcher`), shared with `EAWLauncher`; the window
shell is `eaw_imgui_app` (`src/imgui_app`), shared with EAWTools.

## Headless battles

`EAWEngine battle` runs a battle on a map without a window, as fast as it can,
and prints the state hash, tick timing, and combat and path statistics. It is
the determinism check and the simulation benchmark.

```powershell
# Two opposing fighter groups at the map center, 3000 ticks, order log written
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --record kuat.log

# Replay a log (from battle or from the sandbox's --record) and check its state hash
EAWEngine.exe battle --game foc --replay kuat.log

# Every mobile unit of a land map ordered to the center, run twice and compared
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED --auto-move --ticks 1500 --check-determinism
```

Exit codes: 0 success, 1 a map that cannot be built or an unreadable log, 4 a
replay whose state hash differs from the recording or a failed determinism
check. Logs are text, one order per line:

```text
EAWEngine battle log 1
map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED
1 spawn Rebel_X-Wing_Squadron 2 1 -939.49023 239.85059 0
40 move 600 200 0 17.1 18.1
95 attack 23.1 17.1
120 stop 17.1
130 stance hold_fire 17.1
end 3000 1aed7f1f9e13345c
```

Each order line starts with the tick it was given at (it applies in the next
step); entity ids are `index.generation`. A replay needs the same game data
and mods as the recording; logs do not record them yet. Headless runs skip
textures and animation clips, and the state hash covers gameplay state only,
so a log recorded in the sandbox replays headless with the same hash.

## Lua survey

`EAWEngine lua-survey` runs every script under `Data/Scripts` in its own sandboxed host on recording
stubs and reports which engine functions and methods they call. It executes the installation's or
mod's scripts, in the engine, never in the inspector or tests.

```powershell
EAWEngine.exe lua-survey --game foc --ticks 300 --report survey
```

Each script is loaded and `Base_Definitions` (else `Definitions`) is called. Then, for `--ticks`
simulation ticks (default 300):

- the engine objects the framework expects are bound as stubs: `Object` for game-object scripts,
  `PlayerObject` for AI, free-store, evaluator, and intervention scripts, `Target` and one object
  per task force for AI plans;
- `main` runs as a thread, and so does each task force's `<Name>_Thread` in an AI plan;
- events are queued as the engine would deliver them: each `StoryModeEvents` entry through
  `Story_Event_Trigger`, one at a time over the first three quarters of the run; each task force
  event (`<Name>_<Event>`, else `Default_<Event>`) at the half; and each function a script handed to
  an engine function (a proximity handler) at three quarters, with stub objects as arguments.

Story events fire in key order, not in their campaign's order, so states that rely on globals an
earlier state set fail; those errors are the survey's, not the host's. The summary lists the
events queued, the most used functions, and the most common errors; `--report <folder>` writes
`api.tsv` (name, function or method, host-implemented or stub, scripts, calls, argument types) and
`scripts.tsv` (per script: loaded, errors, exited, threads left, events queued, the time of its
ticks in all and of its slowest tick, first error). The summary also gives the scripts' average cost
per tick and the slowest single ticks. Compiled scripts are
refused (Lua 5.0 does not verify bytecode), and each call or resume stops after its instruction
budget, so a runaway script cannot hang the survey.

## Check scripts

Repeated checks are scripts under `scripts/` (PowerShell, run from anywhere; they find the
repository from their own location and use the built executables):

```powershell
# The reference battle twice: both runs must agree on the expected state hash
.\scripts\check-battle.ps1                        # -Configuration Debug, -Expected <hash>, -GameRoot <path>

# The reference scenes on WARP, compared with an earlier set (keep both folders outside the repository)
.\scripts\check-captures.ps1 -Output D:\eaw-captures\before
.\scripts\check-captures.ps1 -Output D:\eaw-captures\after -Reference D:\eaw-captures\before

# Recount docs/xml-tag-status.md after changing rows (rewrites its summary and family counts)
.\scripts\update-tag-status.ps1
```

`check-battle.ps1` holds the expected hash as its default; a change that alters the simulation on
purpose updates it and says why. `check-captures.ps1` renders a Kuat fighter battle (frames 90 and
300), a Utapau land battle (200), a capital ship duel at Kuat (120), the dark trooper model (20),
and the validation scene (5), and writes a diff image next to each capture that changed.

## Captures and image regression

The sandbox can render a scene reproducibly and save it as PNG, and `EAWEngine
compare-images` measures how two captures differ.

```powershell
# The same command renders the same pixels on the same machine and driver.
EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --auto-move `
  --width 960 --height 540 --capture captures\kuat.png --capture-frame 90

# 0 within the limit, 5 over it; --diff marks changed pixels in red.
EAWEngine.exe compare-images reference\kuat.png captures\kuat.png --diff captures\kuat_diff.png
```

- `--capture <file.png>` writes the scene as the scene pass and bloom leave
  it, without debug lines or the overlay, then closes the window.
- Captures run on a fixed presentation clock (`--fixed-frame-time`, default
  1/30 s): simulation ticks, unit animation, particles, and the camera advance
  by that step per frame, and the clock holds still until every texture is
  resident, so texture streaming never changes the result.
  `--capture-frame <n>` takes the nth presented frame after that (default 1).
- `--camera <x> <y> <distance>` frames a map view explicitly (heading and
  pitch stay the RTS camera's).
- `compare-images` counts a pixel as changed when a colour channel differs by
  more than `--threshold` (default 8) and fails when more than
  `--max-changed` (default 0.001) of the pixels changed. It reads only the
  engine's own uncompressed captures.
- Captures of the stock game are derived from protected data: keep reference
  images locally (outside the repository), never commit them.
- WARP renders identically run to run; hardware GPUs are deterministic for a
  given driver, so references are per machine.
- Against the original, compare features rather than scenes: a screenshot of
  one subject in the original at a similar zoom, a capture of the same subject
  framed with `--camera`, and measured ratios (glow width to hull length, bolt
  length to ship, core colour), recorded in docs/compatibility-research.md.

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
- choose a model's damage state (its `_ALT<n>` meshes, ALT0 intact) and detail
  level (its `_LOD<n>` meshes, the highest by default) with the same rule the
  engine uses (`alo_mesh_visibility`), pick one of the XML object types that use
  the model (types with hardpoints first), and toggle each of its hardpoints:
  shown hardpoints draw a marker coloured by kind (weapons red, shields blue,
  engines orange; labels optional) and their `Model_To_Attach` model at the
  attachment bone, destroyed ones show their `Damage_Decal` scorch meshes (hidden
  while intact, as in battle) and drop the attached model; **Preview** opens
  the model in its own resizable window (as for maps), which stays open while
  other models are selected. Shadow-volume,
  heat-distortion, and occluded-unit geometry is hidden; opaque materials ignore
  texture alpha (which holds gloss or team-color masks), alpha-tested materials
  are cut out, and additive materials blend additively;
- render model and map previews at full detail with a small depth-buffered
  Direct3D 11 renderer (4x MSAA, two-sided, flat-shaded) into an offscreen
  image; grids, markers, and skeletons are drawn over it. Bind-pose geometry is
  uploaded once; animated skinning runs on the CPU and re-uploads each frame.
  This renderer is tool-only; the production renderer is the Direct3D 12 path.

The **DAT Localization** page can:

- open a complete game/mod DAT or a sparse, version-control-friendly TSV;
- edit text, group, status, XML context, and translator notes;
- create arbitrary keys or paired unit-name/tooltip keys;
- scan `Data/XML` for missing active `TEXT_*` references;
- filter large tables by text or workflow status and sort Key, Text, Group, or
  Status in either direction by clicking a column header;
- validate duplicate, TODO, CRC, and DAT structure issues;
- build a complete output DAT without changing the baseline file.

The GUI calls the same core, format, and VFS libraries as the command-line programs; it does not
spawn or scrape CLI subprocesses. Native Windows file/folder dialogs preserve
Unicode paths.

## EAWAssetInspector

The inspector reads the same layered virtual filesystem as the engine. Results
therefore show the asset that would win after the base archives, patch archives,
loose files, and supplied mods are combined. It never changes the game
installation; `extract` copies files out of it into a folder you name. The Forces of Corruption profile mounts the original
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
# A model: skeleton (bone positions and axes), meshes with their bone and each submesh's vertex
# extent in the mesh's own space, materials
EAWAssetInspector.exe inspect Data/Art/Models/Some_Unit.alo

# Unique TEXT_* references in one resolved XML file
EAWAssetInspector.exe localization-refs Data/XML/Units.xml

# Audit model references, material mappings, DDS formats, and MTD atlases
EAWAssetInspector.exe texture-audit Data/Art/Models --game foc

# Decode every particle ALO and summarize blend modes, renderers, and spawning
EAWAssetInspector.exe particle-audit Data/Art/Models --game foc

# Inventory Lua source/bytecode, dependencies, callbacks, and host API calls
EAWAssetInspector.exe lua-audit Data/Scripts --game foc --limit 100

# Parse and index the complete resolved XML object database
EAWAssetInspector.exe xml-audit Data/XML --game foc

# Add type/tag, scalar, enum, repetition, and object-reference validation
EAWAssetInspector.exe xml-audit Data/XML --game foc `
  --schema-root "D:\path\to\eaw-schema"

# The spawnable types the development launcher and the sandbox offer (with mods: --mod)
EAWAssetInspector.exe spawn-types --game foc --mod "D:\path\to\mod" --limit 100

# Resolve and decode every sound event's samples (--language picks the localized archive), and
# check the sounds placed in animations (ANIMATIONSFXMAPS.TXT through SURFACEFX.XML) and every
# object type's sound fields naming an event that is not defined
EAWAssetInspector.exe sound-audit --game foc --language English --limit 50

# Copy the winning file of every virtual path under a prefix out of the archives
EAWAssetInspector.exe extract Data/Scripts --extension .lua --output D:\eaw-extract
```

`extract` writes each resolved file to `<output>/<virtual path>` (upper case, as the archives name
them) for reading with ordinary tools. It refuses an output folder inside the installation or a
mod, keeps files that already exist unless `--overwrite` is given, and skips (with a warning and a
failing exit code) entries whose archive name could not be written where it claims to go: absolute
or parent-traversing names, a colon (a drive or an NTFS stream), or a Windows device name. The
extracted files are the game's protected data: keep them outside the repository, and never quote
them in committed files. `--limit` does not apply.

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
ALOs list their emitters (texture, blend mode, spawning, renderer, shape
groups, child links) and every keyframe track, falling back to the top-level
chunk inventory when they cannot be decoded. TED
inspection reports land/space properties, terrain dimensions and elevation,
texture layers, water maps, tracks, decals, environments, passability, object
CRC counts, AI zones, and embedded map previews. Land TED files also get an
orbitable heightmap preview with adaptive detail, elevation shading, raw
passability-class colors, wireframe, and reference-grid overlays. The GUI
renders space TEDs as an orbitable X/Y placed-object map using decoded
transforms, Z-altitude guides, type colors, player-code outlines, and tactical
bounds. Map previews can be detached into a larger resizable window. The asset
browser also provides an expandable catalog of known game extensions alongside
its custom extension field. With **Objects** enabled, land and space previews
resolve each placed object through its XML type to its model and draw the
model's geometry at the object's position, heading, and scale, textured with
each material's base-color texture. Each unique model is uploaded once and
drawn instanced at full detail. Objects without a drawable model (unresolved types,
squadrons and companies, missing or particle models) keep type-colored markers,
and hovering any object shows its type, element, XML source, and model. Land
transforms use
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

`particle-audit` decodes every ALO whose root chunk marks a particle system
and reports failures, warnings by kind, the property mini-chunks with their
sizes, blend modes, renderer kinds (billboard, ground-plane, kite, heat,
weather), spawning, shape groups, track interpolation, and child links.
`inspect` on a particle ALO lists its emitters and their tracks.

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

`map-audit` also reports the distribution of active-environment indices and
how many maps name an environment index outside their environment list, and
`inspect` on a TED marks the active environment and both sky-dome names, and
lists each environment's key and fill lights, specular, and ambient.

`map-objects` resolves every placed TED object through its type-name CRC to an
XML definition, follows `Variant_Of_Existing_Type` inheritance to the
`Space_Model_Name` or `Land_Model_Name` for the map type, and checks that the
model exists in the layered VFS. It reports coverage, resolved element types,
unknown CRCs, typed objects without a model (such as squadrons and companies,
which spawn member units), missing model files, and variant-chain diagnostics.
It also reports how many space-squadron members were expanded, how many land
teams/companies were left unexpanded, and the placements per TED owner slot by
element type:

```powershell
EAWAssetInspector.exe map-objects Data/Art/Maps --game foc
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
`TEXT_TOOLTIP_MY_NEW_FRIGATE` for `<Encyclopedia_Text>`. The unit id is
upper-cased, other characters become single underscores, and none is kept at
either end (`" my new frigate "` gives `MY_NEW_FRIGATE`), the same rule as the
EAWTools "Add unit" dialog. Additional ability,
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
