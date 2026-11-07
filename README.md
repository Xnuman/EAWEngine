# EAWEngine

EAWEngine is an independently developed, open compatibility engine for *Star
Wars: Empire at War* and *Forces of Corruption*. It is designed to use the
assets from a game installation owned by the player without modifying that
installation or redistributing its runtime data.

The owner-provided `Shaders/` directory is an isolated development reference
containing legacy Direct3D 9 shader files. It is git-ignored, never
distributed, and never a runtime/build dependency or release input. It only
documents how models were meant to render in Alamo; the engine uses its own
physically based materials and newly written shaders. See [its reference policy](Shaders/README.md). Do not describe the
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
copy of Dear ImGui's official docking branch. Shaders are compiled at build time
with the SDK's `dxc.exe` and `fxc.exe` (override with `EAW_DXC_EXECUTABLE` /
`EAW_FXC_EXECUTABLE`); nothing is compiled at runtime.

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
```

Launch the graphical tools:

```powershell
.\build\vs2022-x64\Debug\EAWTools.exe
```

The GUI auto-detects the Steam installation, browses the winning asset from
every base/patch/mod layer, and edits large DAT files without extracting the
game. The command-line tools remain available for automation.

Verify the installed Forces of Corruption data:

```powershell
.\build\vs2022-x64\Debug\EAWEngine.exe doctor --game foc
```

Open the Direct3D 12 visual sandbox directly or through the separate launcher:

```powershell
.\build\vs2022-x64\Debug\EAWEngine.exe sandbox --game foc
```

```powershell
.\build\vs2022-x64\Debug\EAWLauncher.exe --game foc
```

The sandbox defaults to vsync with two frames of CPU/GPU overlap. Development
switches include `--vsync off`, `--frame-latency 1`, `--warp`, and
`--gpu-validation`. `--frame-limit <count>` supports self-terminating smoke
tests, and `--reload-every <frames>` replaces the scene with a fresh copy every
N frames to exercise resource retirement; the sandbox prints the remaining
texture, slot, and retirement counts on exit. `--pipeline-cache <dir>` stores compiled pipeline states in that
directory so later launches skip pipeline compilation; stale or driver-mismatched
caches are discarded automatically. Use `--model <virtual-alo-path>` to inspect another mounted model,
`--animation <virtual-ala-path>` to select an animation, or
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
.\build\vs2022-x64\Debug\EAWLauncher.exe --game foc `
  --mod "D:\path\to\mod"
```

When multiple `--mod` arguments are present, the first has highest priority,
matching the established Steam launch-option convention.

Inspect the resolved FoC asset set or a specific archive-backed model:

```powershell
.\build\vs2022-x64\Release\EAWAssetInspector.exe summary --game foc
.\build\vs2022-x64\Release\EAWAssetInspector.exe list Data/XML `
  --extension .xml --limit 50
.\build\vs2022-x64\Release\EAWAssetInspector.exe inspect `
  Data/Art/Models/Some_Unit.alo --game foc
.\build\vs2022-x64\Release\EAWAssetInspector.exe xml-audit Data/XML --game foc
.\build\vs2022-x64\Release\EAWAssetInspector.exe lua-audit Data/Scripts --game foc
```

Optionally validate that resolved XML against a local checkout of the public
`eaw-schema` project:

```powershell
git clone https://github.com/AlamoEngine-Tools/eaw-schema.git .\build\eaw-schema
.\build\vs2022-x64\Release\EAWAssetInspector.exe xml-audit Data/XML `
  --game foc --schema-root .\build\eaw-schema
```

Create a sparse localization workspace for a unit and build a complete DAT:

```powershell
.\build\vs2022-x64\Release\EAWDatEditor.exe add-unit `
  .\localization.tsv NEBULA_CLASS_FRIGATE `
  "Nebula-class Frigate" "A fast modern frigate."

.\build\vs2022-x64\Release\EAWDatEditor.exe build `
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

See [Architecture](docs/architecture.md), [Roadmap](docs/roadmap.md),
[Compatibility research](docs/compatibility-research.md), and
[Asset and localization tools](docs/tools.md).

