# EAWEngine

EAWEngine is an independently developed, open compatibility engine and toolset for *Star Wars:
Empire at War* and its expansion *Forces of Corruption*. It runs on the player's own installation:
it reads the game's archives, models, maps, scripts, audio, and localization in place, together
with existing mods, and never modifies, patches, or redistributes them. The aim is a modern engine
(Direct3D 12, a deterministic simulation, room for larger battles and new mechanics) for the content
made for the original game, not a copy of the original's internals.

> **Status:** an engine in development, **not a playable replacement for the game**. The data
> layer is complete; a Direct3D 12 sandbox shows stock maps and models in a legacy mode that lacks
> features content was authored for; a deterministic tactical simulation runs headless and
> sandboxed battles with movement, pathfinding, formations, combat, effects, and a first slice of
> the Lua host. There is no game UI, AI, skirmish, or campaign yet. You need an installed copy of
> the game; the project contains no game data. What each part does and its known limitations are
> in [docs/status.md](docs/status.md); the plan is in [docs/roadmap.md](docs/roadmap.md).

## Requirements

- Windows 10 or 11 with a Direct3D 12 GPU (or the WARP software adapter).
- Visual Studio 2022 with the x64 C++ toolchain and a Windows 10/11 SDK, and CMake 3.30 or later.
- *Star Wars: Empire at War* (Forces of Corruption for most examples), installed, for anything
  beyond the synthetic tests. Steam installations are found automatically; otherwise pass
  `--game-root`.

The first configure fetches pinned dependencies: Dear ImGui (docking branch, by commit) and Lua
5.0.3 (MIT, by archive hash); Tracy 0.14.1 (BSD-3-Clause) only for profiling builds. Shaders are
newly written HLSL compiled at build time with the SDK's `dxc.exe` and `fxc.exe`.

## Build

```powershell
.\build.ps1                                  # Debug
.\build.ps1 -Configuration Release
.\build.ps1 -Configuration All -Tests        # Debug and Release, with the test suite
.\build.ps1 -Configuration Release -Clean    # from an empty build tree
.\build.ps1 -Configuration Release -Profile  # with the Tracy profiler client
```

`-Configuration` takes `Debug`, `Release`, `DebugValidation`, or `All` (Debug and Release).
Programs build into `executables\<Config>\`; Debug files carry the `_d` suffix (`EAWEngine_d.exe`),
DebugValidation files `_dv`. `-Clean` keeps the fetched dependencies in `build\_deps`. Where script
execution is blocked, run `powershell -ExecutionPolicy Bypass -File build.ps1`.

## Basic checks

```powershell
# The test suite (built with -Tests), whole or by module
.\executables\Debug\EAWEngineTests_d.exe
.\executables\Debug\EAWEngineTests_d.exe vfs formats

# Discovery, the mounted layers, and a sample asset read
.\executables\Debug\EAWEngine_d.exe doctor --game foc

# The sandbox under WARP with GPU validation for a few frames
.\executables\Debug\EAWEngine_d.exe sandbox --game foc --warp --gpu-validation --vsync off --frame-limit 8

# A headless battle that prints its state hash and checks determinism
.\executables\Release\EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED `
  --spawn Rebel_Infantry_Squad:2@1 --spawn T2B_Tank:3@2 --auto-move --ticks 3000 --check-determinism

# The reference battles against their expected hashes (-Extended for more)
.\scripts\check-battle.ps1
```

`scripts\check-captures.ps1` compares rendered reference scenes before and after a change; all
scripts and options are in [docs/tools.md](docs/tools.md).

## Quick start

From `executables\Release\`:

```powershell
# Open a land map with two opposing groups (left-drag selects, right-click orders, F1 dev menu)
.\EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED `
  --spawn T2B_Tank:6@1 --spawn AT_ST_Walker:6@2

# The same through the launcher, with a mod on top of the game
.\EAWLauncher.exe --game foc --mod "D:\path\to\mod"

# A headless battle: record its order log, then replay it to the same state hash
.\EAWEngine.exe battle --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --record kuat.log
.\EAWEngine.exe battle --game foc --replay kuat.log
```

With several `--mod` arguments, the first has the highest priority, as in the game's Steam launch
options.

## Programs

| Program | Purpose |
| --- | --- |
| `EAWEngine` | The engine: `sandbox`, headless `battle`, `doctor`, `lua-survey`, `path-audit`, `compare-images`. |
| `EAWLauncher` | Chooses the game, installation, and mods, then starts the engine. |
| `EAWDevLauncher` | Edits, saves, and starts sandbox scenarios (map, spawns, model, options) and shows their command lines. |
| `EAWTools` | Graphical workbench: browse every layer's assets, preview models, animations, and maps, edit localization. |
| `EAWAssetInspector` | Command-line inventory, inspection, extraction, and corpus audits (models, textures, maps, particles, sounds, XML, Lua). |
| `EAWDatEditor` | Lossless DAT string-table editing, TSV workspaces, mod text scans, and new-unit text bundles. |
| `EAWBenchmarks` | Workloads timed serially and on the job system, with outputs checked against the serial run. |

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
- **Layered and testable.** Small libraries with inward dependencies, no exceptions, contextual
  errors for malformed data, and automated checks under WARP with GPU validation.

## Documentation

- [AGENTS.md](AGENTS.md): the rules for contributing to this repository (for people and coding
  agents).
- [Architecture](docs/architecture.md): layers and libraries, the resource pipeline, tick layers,
  the content-contract classification, and the source files by subject.
- [Status](docs/status.md): what each system implements, how, and its known limitations.
- [Roadmap](docs/roadmap.md): milestones, system tracks, the work order, verification questions,
  and engine extensions.
- [History](docs/history.md): a dated logbook of the work and the owner's decisions.
- [Tools](docs/tools.md): command lines, sandbox options and controls, scripts, audits, and
  benchmarks.
- [Compatibility research](docs/compatibility-research.md): evidence about the original's formats
  and behavior, with credits.
- [Legacy shader mapping](docs/legacy-shader-mapping.md): how the original's shader families map to
  engine-native materials, with their uncertainty.
- [XML object tags](docs/xml-object-tags.md): the XML tags of game object types and what they mean.
- [XML tag status](docs/xml-tag-status.md): which XML tags the engine reads and the gaps.
- [ECS design](docs/ecs-design.md) and [ECS research](docs/ecs-research.md): the agreed design of
  the simulation's entities, systems, and job system, and the research behind it.
- [Pathfinding design](docs/pathfinding-design.md) and
  [pathfinding research](docs/pathfinding-research.md): route planning, group movement, and
  avoidance, and the research behind them.
- [Lua runtime research](docs/lua-runtime-research.md): the research behind the script host.
- [Sky research](docs/sky-research.md), [water research](docs/water-research.md), and
  [heat research](docs/heat-research.md): the research behind the sky, water, and heat-distortion
  rendering.

## Legal and reference boundaries

EAWEngine is a fan project, not affiliated with or endorsed by Lucasfilm, Disney, Petroglyph, or
any rights holder of *Star Wars: Empire at War*. It contains no game data and requires a
legitimately owned copy of the game. The engine's code and shaders are newly written.

- The original's Direct3D 9 shader sources, which Petroglyph published with the
  [official Empire at War mod tools](https://www.petroglyphgames.com/eawmodtool/), serve as evidence
  of how materials render; they are kept only as local, uncommitted references and are never
  copied, compiled, embedded, or shipped.
- Third-party projects about the game are read as evidence only where their license allows, and
  credited; nothing is copied from copyleft or unlicensed projects, and no finding rests on
  disassembled executables.
- Captures and decoded assets made from the game are derived from protected data and are not to be
  committed or redistributed.
