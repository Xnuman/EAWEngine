# EAWEngine

EAWEngine is an independently developed, open compatibility engine for *Star
Wars: Empire at War* and *Forces of Corruption*. It is designed to use the
assets from a game installation owned by the player without modifying that
installation or redistributing its runtime data.

The owner-provided `Reference Materials/Shaders/` directory is an isolated development reference
containing legacy Direct3D 9 shader files. It is git-ignored, never
distributed, and never a runtime/build dependency or release input. It only
documents how models were meant to render in Alamo; the engine uses its own
physically based materials and newly written shaders. See [its reference policy](Reference%20Materials/Shaders/README.md). Do not describe the
renderer as strictly clean-room while that corpus is available to contributors.

This repository is at milestone **M2: visual sandbox**. It currently:

- discovers the Steam installation and both game variants;
- reads legacy Petroglyph `.meg` archives without extracting them;
- creates a case-insensitive layered virtual filesystem;
- supports loose-file and repeatable mod overrides;
- provides separate `EAWLauncher.exe` and `EAWEngine.exe` programs;
- verifies the real installation by resolving and reading a configured asset.
- provides `EAWAssetInspector.exe` for overlay-aware asset inventory and format
  inspection, including a schema-validated XML definition/reference index,
  semantic DDS/MTD textures, normalized legacy materials, ALO models, ALA
  animations, TED maps, and Lua source/bytecode inventories;
- provides `EAWDatEditor.exe` for lossless DAT editing, validation, sparse TSV
  localization workspaces, XML-reference audits, and new-unit string bundles.
- provides `EAWTools.exe`, a native Dear ImGui/DirectX 11 workbench that combines
  the asset inspector and DAT localization studio in a fast modern GUI, with an
  initial textured and animated ALO/ALA inspection viewport.
- provides `EAWDevLauncher.exe`, a development launcher for sandbox scenarios:
  pick the game, installation, and mod stack, a map (with debug spawns and their
  owners) or a model from the mounted asset lists, and engine options; save
  scenarios as presets, start several runs, and see their exit codes.
- provides an initial `EAWEngine.exe` Direct3D 12 visual sandbox with a native
  window/input layer, asynchronous copy-queue uploads, frame-buffered command
  allocators, fence-based lifetime tracking, and a waitable flip-model swap
  chain. It loads stock ALO geometry and base-color DDS mip chains into indexed
  material batches with depth buffering and an orbit camera. Matching ALA idle
  animations are sampled into frame-buffered GPU skin palettes.

It does **not** run the game yet. Production asset rendering, simulation, Lua
execution, audio, runtime UI, networking, and save compatibility remain future
milestones.

## Build

Requirements: Windows, CMake 3.25+, and Visual Studio 2022 with the x64 C++
toolchain and a Windows 10/11 SDK. The first configure fetches a commit-pinned
copy of Dear ImGui's official docking branch and the Lua 5.0.3 release (MIT,
lua.org), pinned by its archive hash. Shaders are compiled at build time
with the SDK's `dxc.exe` and `fxc.exe` (override with `EAW_DXC_EXECUTABLE` /
`EAW_FXC_EXECUTABLE`); nothing is compiled at runtime.

```powershell
.\build.ps1                                     # Debug
.\build.ps1 -Configuration Release
.\build.ps1 -Configuration All -Tests           # both, with EAWEngineTests
.\build.ps1 -Configuration Release -Clean       # from an empty build tree
.\executables\Debug\EAWEngineTests_d.exe
```

`build.ps1` configures `build/` (Visual Studio 2022, x64, C++20) and builds into
`executables/<Config>/`; Debug files carry the `_d` suffix (`EAWEngine_d.exe`).
`-Clean` keeps the fetched third-party sources in `build/_deps`. Where scripts
are blocked, run it as `powershell -ExecutionPolicy Bypass -File build.ps1`.

Launch the graphical tools:

```powershell
.\executables\Debug\EAWTools_d.exe
.\executables\Debug\EAWDevLauncher_d.exe
```

The development launcher stores its presets in `EAWDevLauncher.scenarios` next
to the executable (`executables/<Config>/`) and shows the exact `EAWEngine` command
line it runs, which can be copied for scripts.

The GUI auto-detects the Steam installation, browses the winning asset from
every base/patch/mod layer, and edits large DAT files without extracting the
game. The command-line tools remain available for automation.

Verify the installed Forces of Corruption data:

```powershell
.\executables\Debug\EAWEngine_d.exe doctor --game foc
```

Open the Direct3D 12 visual sandbox directly or through the separate launcher:

```powershell
.\executables\Debug\EAWEngine_d.exe sandbox --game foc
```

```powershell
.\executables\Debug\EAWLauncher_d.exe --game foc
```

Run a battle without a window (deterministic; prints the state hash, timing,
and combat and path statistics):

```powershell
.\executables\Release\EAWEngine.exe battle --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --ticks 3000 --record kuat.log
.\executables\Release\EAWEngine.exe battle --game foc --replay kuat.log
```

`battle` spawns the `--spawn` groups at the map center (each request 180 units
further along +X) and, with `--auto-move`, orders every mobile unit to the
center. `--ticks` sets the total simulation ticks (default 3000).
`--record <file>` writes the battle's order log (also from the sandbox, on
exit, so an interactive session can be replayed headless), `--replay <file>`
runs a recorded log and checks its final state hash (exit code 4 on a
mismatch), and `--check-determinism` runs the battle twice and compares the
hashes. Logs are text: one order per line with its tick, in replayable terms
(type names, owner slots, points, entity ids).

The sandbox defaults to vsync with two frames of CPU/GPU overlap. Development
switches include `--vsync off`, `--frame-latency 1`, `--warp`, and
`--gpu-validation`. Maps and models build on a worker thread while the window
shows the validation triangle; textures then stream in under
`--upload-budget <MiB>` per frame (default 64, 0 for all at once).
`--frame-limit <count>` supports self-terminating smoke tests and counts frames
from the moment the scene is shown. `--reload-every <frames>` replaces the scene
with a fresh copy every N frames to exercise resource retirement; the sandbox
prints the remaining texture, slot, and retirement counts on exit. With
`--hot-reload`, the sandbox watches the files the scene was built from (loose
files in the game and mod directories and the MEG archives) and rebuilds it,
re-decoding only the changed assets and uploading only the changed textures,
when one of them changes; the camera is kept. Your installation is only read. `--pipeline-cache <dir>` stores compiled pipeline states in that
directory so later launches skip pipeline compilation; stale or driver-mismatched
caches are discarded automatically. Use `--model <virtual-alo-path>` to inspect another mounted model,
A left click without dragging selects the object under the cursor (it is
highlighted, outlined by its bounds, and named in the console; `--pick <x> <y>`
does the same for automation). B toggles every object's bounds and the world
axes (`--show-bounds` starts with them on). G draws the map's passability
types around the camera focus (`--show-passability`): red Steep, blue Water,
orange Impassable, green InfantryOnly, magenta Obstacle. Ground units follow the
terrain and path around cells their movement class may not enter (an
unreachable target ends at the nearest reachable point); selected units show
their remaining route. Flying units keep their altitude. Maps run their placed objects as
simulation entities and start in an RTS camera: WASD/arrows pan, the wheel
zooms, left-drag box-selects mobile units, and right-click orders them to the
clicked point, or to attack the hostile unit under the cursor. Armed units fire
on the nearest hostile in range by themselves; shots are drawn as streaks and
damaged or selected units show health (green) and shield (blue) bars. Debug
spawns take an owner slot: `--spawn T2B_Tank:6@1 --spawn AT_ST_Walker:6@2`
places two opposing groups side by side, and O cycles the owner of units
spawned with N. Company and squadron types spawn as their members
(`--spawn Rebel_Infantry_Squad:2@1`, `--spawn Rebel_X-Wing_Squadron:2@1`), and
placed companies and squadrons on maps appear as their units. Ships fire their
weapon hardpoints; for example `--spawn Star_Destroyer:1@2 --spawn
Calamari_Cruiser:1@1` on a space map starts a capital ship duel. Group orders form ranked rows around the clicked point and move at
the slowest unit's speed; units keep apart by their footprints and turn at
their XML turn rates. Map owner slot 3 is treated as neutral and all other slots as
hostile for now. C switches to the free debug
camera. `--auto-move` orders every mobile unit to the view center for
automated runs. Warnings (for example malformed mod data) go to stderr, the Visual Studio
Output window, and a per-run log file in `logs/` next to the executable
(`--log-dir <path>` changes the folder; the 20 newest files are kept).
Unexpected failures (failed assertions, unrecoverable GPU errors, access
violations) end the program with an error window describing them, and the same
report on stderr and in the log file. Retry breaks into an attached debugger. No window is shown
when stderr is redirected, or with `--no-error-dialog`; `--crash-test
<assert|exception|access-violation>` fails on purpose to check this (the
engine is built without C++ exceptions, so `exception` reports a simulated one).
`--spawn <XML type>[:count]` (repeatable) makes an object type
spawnable on maps: the count spawns at the surface under the view center when
the map appears, N spawns one at the cursor, and Tab cycles through the types,
for example `--spawn T2B_Tank:9 --spawn AT_AT_Walker`. Tab lists every unit type
with a model for the map (Shift+Tab goes back, Ctrl moves ten at a time), and N
on a type that is not loaded yet loads it in the background and spawns it when
ready; `--spawn-later <XML type>[:count][@owner]` does the same from the
command line. F1 opens the dev menu, which shows or hides the statistics panel
(FPS, CPU/GPU frame time, draw calls, triangles, video memory, streaming,
scene, selection, hot reload) and the spawn panel (search the spawnable types,
pick the count and owner, spawn at the view center or with N at the cursor), and
toggles bounds and passability; `--panel menu|statistics|spawn` opens panels at
start, and `--overlay` the statistics panel.
Models' particle proxies (engine glows, lights, fires) play on maps;
`--effect <name>` (repeatable) plays a particle effect such as
`P_EXPLOSION_BIG00` at the map center, restarting it when it ends. Bright
parts of the image bloom (`--bloom <strength>`, default 0.8; `--no-bloom`
turns it off), and engine glows grow and brighten with a unit's speed.
`--particles legacy|enhanced` chooses how particle effects are interpreted:
legacy (the default) reproduces the original's quirks for content made for
it, enhanced improves them. On
maps, P pauses and resumes the world (simulation, animation, particles) while
the cameras keep working, so C's free camera can inspect a frozen scene.
Mouse movement and keys use Windows raw input (unaccelerated mouse movement for
camera rotation); `--no-raw-input` uses window messages instead. The panel
can be dragged and resized, and its "-" button minimizes it to a small tab ("+"
restores it); the mouse does not reach the scene while it is over the panel. `--animation <virtual-ala-path>` selects an animation (Left/Right then step
through the bind pose and every `<MODEL>_*.ALA` beside the model), or
`--validation-triangle` to bypass asset loading. The sandbox automatically tries
the model's `_IDLE_00.ALA` when present. `--map <virtual-ted-path>` instead
loads every placed object of a map as one instanced scene with the map's sky
dome and star backdrop; space squadrons are expanded into their member ships,
and land maps draw their textured terrain heightfield with water, lava, or ice
surfaces and track ribbons such as rivers and shoreline waves (decals are not
drawn yet). Maps use a tactical camera: WASD/arrow keys
or right-drag pan (hold Shift to move faster), left-drag rotates and tilts, the
wheel zooms within map-relative limits, and Space resets the view. Drag with the left mouse button to
orbit, use the wheel to zoom, press Space to reset the camera, and press P to
pause animation.

Use the launcher with a mod overlay:

```powershell
.\executables\Debug\EAWLauncher_d.exe --game foc `
  --mod "D:\path\to\mod"
```

When multiple `--mod` arguments are present, the first has highest priority,
matching the established Steam launch-option convention.

Inspect the resolved FoC asset set or a specific archive-backed model:

```powershell
.\executables\Release\EAWAssetInspector.exe summary --game foc
.\executables\Release\EAWAssetInspector.exe list Data/XML `
  --extension .xml --limit 50
.\executables\Release\EAWAssetInspector.exe inspect `
  Data/Art/Models/Some_Unit.alo --game foc
.\executables\Release\EAWAssetInspector.exe xml-audit Data/XML --game foc
.\executables\Release\EAWAssetInspector.exe lua-audit Data/Scripts --game foc
```

Optionally validate that resolved XML against a local checkout of the public
`eaw-schema` project:

```powershell
git clone https://github.com/AlamoEngine-Tools/eaw-schema.git .\build\eaw-schema
.\executables\Release\EAWAssetInspector.exe xml-audit Data/XML `
  --game foc --schema-root .\build\eaw-schema
```

Create a sparse localization workspace for a unit and build a complete DAT:

```powershell
.\executables\Release\EAWDatEditor.exe add-unit `
  .\localization.tsv NEBULA_CLASS_FRIGATE `
  "Nebula-class Frigate" "A fast modern frigate."

.\executables\Release\EAWDatEditor.exe build `
  "D:\path\to\MasterTextFile_ENGLISH.dat" `
  .\localization.tsv .\MasterTextFile_ENGLISH.dat
```

See [Asset and localization tools](docs/tools.md) for the complete workflow.

## Design principles

- Independent implementation driven by public documentation, measured behavior,
  compatibility corpora, and the explicitly isolated legacy shader reference.
- Original installation and mod directories are read-only inputs.
- Data compatibility is separated from the renderer and simulation.
- Deterministic fixed-step simulation; presentation may run independently.
- Data-oriented parallel jobs for scale, with deterministic synchronization
  points for gameplay and multiplayer.
- A rendering hardware interface, initially implemented with Direct3D 12 and
  later with Vulkan, rather than graphics API calls in gameplay systems.

See [Architecture](docs/architecture.md), [Implementation status](docs/status.md), [Roadmap](docs/roadmap.md),
[Compatibility research](docs/compatibility-research.md), and
[Asset and localization tools](docs/tools.md).

