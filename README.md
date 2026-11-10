# EAWEngine

EAWEngine is an independently developed, open compatibility engine for *Star Wars: Empire at War*
and its expansion *Forces of Corruption*. It runs on the player's own installation: it reads the
game's archives, models, maps, scripts, and localization in place, together with existing mods, and
never modifies, patches, or redistributes them. The aim is a modern engine (Direct3D 12, a
deterministic simulation, room for larger battles and new mechanics) for the content made for the
original game.

> **Status:** an engine in development, not yet a playable replacement for the game. The data layer
> and a Direct3D 12 sandbox work; the tactical simulation is being built. You need an installed copy
> of the game; the project contains no game data.

## What works today

- **Game data.** Steam installation discovery for both games; a layered virtual file system over
  MEG archives, patches, loose files, and mod stacks in the original's precedence; lossless readers
  for ALO models and particle systems, ALA animations, TED maps, DDS and MTD textures, XML, DAT
  string tables, and Lua sources and bytecode, checked against the whole stock corpus.
- **Visual sandbox.** Direct3D 12 rendering of stock models and maps in a legacy mode that
  approximates the original's look: skinned animation, terrain with water, lava, and ice, sky
  backdrops, particle effects, bloom, texture streaming, and hot reload of edited files.
- **Tactical simulation.** A deterministic fixed-tick simulation: units spawned from the game's XML,
  pathfinding and formations, flight, weapons, shields and damage, RTS selection and orders, and
  headless battles whose order logs replay to the same state hash.
- **Scripts.** A sandboxed Lua 5.0 host that runs the stock scripts against recording stubs to
  measure which engine functions they use.
- **Tools.** An asset inspector and audits, a DAT localization editor, a Dear ImGui workbench with
  model and map previews, and a development launcher for sandbox scenarios.

## Requirements

- Windows 10 or 11 with a Direct3D 12 GPU (or the WARP software adapter).
- Visual Studio 2022 with the x64 C++ toolchain and a Windows 10/11 SDK, and CMake 3.25 or later.
- *Star Wars: Empire at War* (Forces of Corruption for most examples), installed, for anything beyond
  the synthetic tests. Steam installations are found automatically; otherwise pass `--game-root`.

The first configure fetches two pinned dependencies: Dear ImGui (docking branch, by commit) and Lua
5.0.3 (MIT, by archive hash). Shaders are newly written HLSL compiled at build time with the SDK's
`dxc.exe` and `fxc.exe`.

## Build

```powershell
.\build.ps1                                  # Debug
.\build.ps1 -Configuration Release
.\build.ps1 -Configuration All -Tests        # Debug and Release, with the test suite
.\build.ps1 -Configuration Release -Clean    # from an empty build tree
```

Programs build into `executables\<Config>\`; Debug files carry the `_d` suffix (`EAWEngine_d.exe`).
`-Clean` keeps the fetched dependencies in `build\_deps`. Where script execution is blocked, run
`powershell -ExecutionPolicy Bypass -File build.ps1`. With `-Tests`, run the suite with
`.\executables\Debug\EAWEngineTests_d.exe` (or name modules: `EAWEngineTests_d.exe vfs formats`).

## Quick start

From `executables\Release\`:

```powershell
# Check discovery, the mounted layers, and a sample asset read
.\EAWEngine.exe doctor --game foc

# Open a land map with two opposing groups (left-drag selects, right-click orders, F1 dev menu)
.\EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED `
  --spawn T2B_Tank:6@1 --spawn AT_ST_Walker:6@2

# The same through the launcher, with a mod on top of the game
.\EAWLauncher.exe --game foc --mod "D:\path\to\mod"

# A headless battle: prints the state hash; record it, then replay and verify it
.\EAWEngine.exe battle --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --record kuat.log
.\EAWEngine.exe battle --game foc --replay kuat.log
```

With several `--mod` arguments, the first has the highest priority, as in the game's Steam launch
options.

## Programs

| Program | Purpose |
| --- | --- |
| `EAWEngine` | The engine: `sandbox`, headless `battle`, `doctor`, `lua-survey`, `compare-images`. |
| `EAWLauncher` | Chooses the game, installation, and mods, then starts the engine. |
| `EAWDevLauncher` | Edits, saves, and starts sandbox scenarios (map, spawns, model, options) and shows their command lines. |
| `EAWTools` | Graphical workbench: browse every layer's assets, preview models, animations, and maps, edit localization. |
| `EAWAssetInspector` | Command-line inventory, inspection, and corpus audits (models, textures, maps, particles, XML, Lua). |
| `EAWDatEditor` | Lossless DAT string-table editing, TSV workspaces, mod text scans, and new-unit text bundles. |

Options, controls, and workflows are in [docs/tools.md](docs/tools.md).

## Design principles

- **The installation stays untouched.** Game and mod directories are read-only inputs; the engine
  never injects into or patches the original executable.
- **Existing mods are the baseline.** Formats are decoded losslessly and interpreted separately, so
  unknown data survives; new mechanics and formats arrive as optional extensions that old content
  never needs.
- **Content contract, not internals.** What the data means is followed with evidence; how the old
  engine happened to compute it is modernized; quirks that content was tuned against are kept as
  named rules of a legacy profile, beside an enhanced one.
- **Determinism.** Gameplay runs on a fixed tick, apart from the frame rate; presentation never
  feeds back into the simulation, and replays reproduce the state hash.
- **Layered and testable.** Small libraries with inward dependencies (formats, VFS, assets,
  simulation, engine, renderer), no exceptions, contextual errors for malformed data, and automated
  checks under WARP with GPU validation.

## Documentation

- [Architecture](docs/architecture.md): layers, data flow, simulation, renderer, and policies.
- [Status](docs/status.md) and [roadmap](docs/roadmap.md): what is implemented and what comes next.
- [Tools](docs/tools.md): command lines, sandbox options and controls, audits, localization.
- [Compatibility research](docs/compatibility-research.md): evidence about the original's formats
  and behavior; [legacy shader mapping](docs/legacy-shader-mapping.md),
  [Lua runtime](docs/lua-runtime-research.md), and [pathfinding](docs/pathfinding-research.md)
  research.
- [AGENTS.md](AGENTS.md): the rules for contributing to this repository.

## Legal

EAWEngine is a fan project, not affiliated with or endorsed by Lucasfilm, Disney, Petroglyph, or
any rights holder of *Star Wars: Empire at War*. It contains no game data and requires a legitimately
owned copy of the game. The engine's code and shaders are newly written. The original's Direct3D 9
shader sources, which Petroglyph published with the
[official Empire at War mod tools](https://www.petroglyphgames.com/eawmodtool/), serve as evidence of
how materials render; the engine never compiles or ships them. Captures and decoded assets made
from the game are derived from protected data and are not to be committed or redistributed.
