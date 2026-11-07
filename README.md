# EAWEngine

EAWEngine is a clean-room, open compatibility engine for *Star Wars: Empire at
War* and *Forces of Corruption*. It is designed to use the assets from a game
installation owned by the player. Game assets are never copied into this
repository or redistributed.

This repository is at milestone **0.3: native asset and localization workbench**. It currently:

- discovers the Steam installation and both game variants;
- reads legacy Petroglyph `.meg` archives without extracting them;
- creates a case-insensitive layered virtual filesystem;
- supports loose-file and repeatable mod overrides;
- provides separate `EAWLauncher.exe` and `EAWEngine.exe` programs;
- verifies the real installation by resolving and reading a configured asset.
- provides `EAWAssetInspector.exe` for overlay-aware asset inventory and format
  inspection, including a schema-validated XML definition/reference index,
  semantic DDS/MTD textures, normalized legacy materials, ALO models, ALA
  animations, and TED maps;
- provides `EAWDatEditor.exe` for lossless DAT editing, validation, sparse TSV
  localization workspaces, XML-reference audits, and new-unit string bundles.
- provides `EAWTools.exe`, a native Dear ImGui/DirectX 11 workbench that combines
  the asset inspector and DAT localization studio in a fast modern GUI, with an
  initial textured and animated ALO/ALA inspection viewport.

It does **not** run the game yet. Rendering, simulation, Lua execution, audio,
UI, networking, and save compatibility remain future milestones.

## Build

Requirements: Windows, CMake 3.25+, and Visual Studio 2022 with the x64 C++
toolchain. The first configure fetches a commit-pinned copy of Dear ImGui's
official docking branch.

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

Or use the separate launcher:

```powershell
.\build\vs2022-x64\Debug\EAWLauncher.exe --game foc
```

Pass a development mod as a read-only overlay:

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

- Clean-room implementation from public format documentation and behavior tests.
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

