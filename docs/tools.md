# Tools

The engine's command-line modes, the launchers, the check scripts, and the asset and localization
tools. Debug builds carry the `_d` suffix (`EAWEngine_d.exe`); the examples use the Release names.
Every program only reads the game installation and mods.

## EAWEngine modes

`EAWEngine [mode] [options]`; `EAWEngine --help` lists every option.

| Mode | What it does |
| --- | --- |
| `sandbox` (default) | Opens the Direct3D 12 sandbox on a map, a model, or the validation triangle. |
| `battle` | Runs a battle on `--map` (or `--replay`) without a window ([Headless battles](#headless-battles)). |
| `doctor` (alias `bootstrap`) | Checks discovery and mounts and reads one probe asset. |
| `path-audit` | The pathfinding baseline ([Path audit](#path-audit)). |
| `lua-survey` | Runs every script on recording stubs ([Lua survey](#lua-survey)). |
| `compare-images` | Compares two captures ([Captures and image regression](#captures-and-image-regression)). |
| `movement-scenarios` | Synthetic group moves measured for route churn and stalls ([Movement measurements](#movement-measurements)). |

Every mode but `compare-images` and `movement-scenarios` first mounts the game and reads the probe asset (`--probe <path>`,
default `Data/XML/GameObjectFiles.xml`) and prints the installation, runtime root, hardware threads,
mounts, and indexed MEG entries. Exit codes of that start: 2 no installation found or an
installation without both game data sets, 3 the probe missing or unreadable.

```powershell
EAWEngine.exe doctor --game foc                       # discovery, mounts, and an asset probe
EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED `
  --spawn T2B_Tank:6@1 --spawn AT_ST_Walker:6@2       # two opposing groups
EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Star_Destroyer:1@2 --spawn Calamari_Cruiser:1@1   # a capital ship duel
EAWEngine.exe sandbox --game foc --model DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO
```

## EAWEngine sandbox

### Scene and game data

| Option | Effect |
| --- | --- |
| `--game eaw\|foc`, `--game-root <path>` | Game variant and installation (discovered through Steam by default). |
| `--mod <path>` | Add a mod (repeatable; the first has the highest priority). A relative path is looked up as the game's MODPATH, under the runtime folder (`Mods\Name`), then from the working directory. |
| `--probe <path>` | The virtual asset read at start (default `Data/XML/GameObjectFiles.xml`). |
| `--map <ted>` | Show a map: placed objects as simulation entities, terrain, liquids, tracks, sky and backdrop. Squadrons and companies appear as their members. |
| `--model <alo>`, `--animation <ala>` | Show one model; without `--animation` its `_IDLE_00.ALA` plays when present. |
| `--validation-triangle` | Skip asset loading. |
| `--spawn <TYPE>[:N][@OWNER]` | Make an XML type spawnable (repeatable); N (at most 1000) spawn at the view center for owner slot OWNER (default 1), each request its own group. |
| `--spawn-later <TYPE>[:N][@OWNER]` | Load a type at runtime once the map is shown and spawn it. |
| `--effect <NAME>` | Play a particle effect (for example `P_EXPLOSION_BIG00`) at the map center, repeating (repeatable). |
| `--language <name>` | Language of the localized text and speech archives (default `English`; text and speech share it for now). |
| `--volume <0..1>` | Master volume (default 0.2, an engine choice); the sound panel changes it at runtime. |
| `--audio <xaudio2\|fmod\|none>`, `--no-audio` | Audio backend (default `xaudio2`; `fmod` only in builds configured with `-DEAW_AUDIO_FMOD=ON -DFMOD_ROOT=<local SDK>`). A backend that cannot start falls back to silence with a warning; the run summary counts sounds played, skipped by their rules, and missing. Maps play weapon fire, deaths, engine loops, and unit responses to selections and orders. |
| `--particles legacy\|enhanced` | How particle effects are interpreted: legacy (default) reproduces the original's look-defining quirks, enhanced improves them. |
| `--bloom <0..10>`, `--no-bloom` | Bloom strength (default 0.8; 0 disables it). |
| `--water <mode>` | The map's water: `legacy` (the surface blended at its material's opacity, the default) or `enhanced` (color and opacity from the depth beneath, soft shores, two normal layers, Fresnel, sun glint, ocean waves in deep water with foam on their crests; [water-research.md](water-research.md)). Also in the Graphics panel, which with enhanced water shows the water's sliders (clarity, shore fade, foam depth and strength, refraction, reflection distortion, glint strength and sharpness, and Defaults) and the ocean's: the sea state (wind speed, fetch, swell), choppiness, wave height (up to twice the spectrum's), the depth below which water is calm and the depth from which waves are full, the Jacobian below which crests foam, and Defaults. |
| `--heat <mode>` | Heat distortion: `legacy` (the default) or `enhanced` (signed 16-bit accumulation, shift sized in world units, shimmer, foreground rejection, faded edges, TAA reactive weight; [heat-research.md](heat-research.md)). Also in the Graphics panel. |
| `--soft-particles` | Soft particles (enhanced, off by default): depth-tested particles fade out over 20 world units as the scene behind them comes near, instead of cutting a hard line into the ground. Also in the Graphics panel. |
| `--sky-ambient`, `--sky-visibility` | Enhanced sky lighting (off by default): the ambient term from the captured sky, and terrain and units losing sky light under slopes and cliffs ([sky-research.md](sky-research.md)). Also in the Graphics panel. |
| `--fog <mode>` | The map environment's fog: `legacy` (the original's linear fog, the default; also `on`), `enhanced` (exponential height fog brightening toward the sun), or `off`. |
| `--shadows <on\|off>` | Key-light shadows (cascaded shadow maps, on by default). |
| `--aa <mode>`, `--no-aa` | Anti-aliasing: `fxaa` (an edge pass over the finished frame, the default), `taa` (temporal), `hybrid` (TAA, then the edge pass over its result), or `off` (`--no-aa`). |
| `--lod <off\|distance\|screen\|hybrid>` | How meshes with detail levels pick one (default `hybrid`: screen size, full detail near, the least far away; `off`: always full detail). |
| `--mesh-detail <0..1>` | The mesh-detail setting (as the graphics presets' `MeshDetail`): 1 keeps detail 1.5 times as far as the base thresholds, 0 half as far (default 1). |
| `--hot-reload` | Rebuild the scene when one of its loose files or MEG archives changes, re-decoding only what changed; the camera is kept. |

Map owner slot 3 is neutral and every other slot hostile, for now (`neutral_map_owner`).

### Presentation and devices

| Option | Effect |
| --- | --- |
| `--width`, `--height` | Initial client size (default 1600 by 900). |
| `--vsync on\|off`, `--frame-latency 1\|2` | Present mode and queued frames (default vsync, two frames). |
| `--warp`, `--gpu-validation` | Software adapter and Direct3D 12 GPU-based validation. |
| `--upload-budget <MiB>` | Texture bytes streamed per frame (default 64; 0 uploads all at once). |
| `--pipeline-cache <dir>` | Keep compiled pipeline states between runs; stale caches are discarded. |
| `--no-raw-input` | Read mouse and keys from window messages instead of raw input. |
| `--overlay` | Open the statistics panel at start (hidden by default; `--no-overlay` is still accepted). |
| `--panel statistics\|spawn\|sound\|camera\|graphics\|controls` | Open a dev panel at start (repeatable; `menu` is still accepted, the bar starts shown). |
| `--show-bounds`, `--show-navigation` | Start with bounds, or the navigation overlay, drawn. |
| `--free-camera <ex> <ey> <ez> <tx> <ty> <tz>` | Start in the free camera at that eye, looking at that target. |

### Automation and diagnostics

| Option | Effect |
| --- | --- |
| `--frame-limit <n>` | Exit after n frames once the scene is shown. |
| `--auto-move` | Order every mobile unit to the view center. |
| `--no-scripts` | Do not run game-object scripts (XML `Lua_Script`) in battles; also accepted by `battle`, and a recording replays as it was made only with the same setting. |
| `--pick <x> <y>` | Select what is at a client pixel once the scene is shown. |
| `--reload-every <n>` | Replace the scene with a fresh copy every n frames (resource-retirement test); remaining texture, slot, and retirement counts print on exit. |
| `--record <file>` | Write the order log on exit, for headless replay. |
| `--replay <file>` | Play a recorded order log (its map unless `--map` is given) on the battle's clock, pause at its end (it does not run on), and print the state hash against the recording's; it ends in the same state as the headless replay. The player gives no orders and spawns nothing meanwhile (cameras, selection, and pausing work); `--spawn`, `--spawn-later`, and `--auto-move` are refused with it. With `--capture`, it runs there fast (240 ticks a frame) and captures the end, e.g. `sandbox --game foc --replay stuck.log --capture end.png --camera 2888 2154 900`. |
| `--capture`, `--capture-frame`, `--fixed-frame-time`, `--camera` | Reproducible captures; see [Captures and image regression](#captures-and-image-regression). |
| `--log-dir <path>` | Folder for the per-run log (default `logs/` next to the executable; the 20 newest are kept). |
| `--no-error-dialog` | Report fatal errors on stderr and in the log only (also the case when stderr is redirected). |
| `--crash-test assert\|exception\|access-violation` | Fail on purpose to check crash reporting. |

Warnings, such as malformed mod data, go to stderr, the Visual Studio Output window, and the log.
Unexpected failures end the program with one report there and in an error window, whose Retry
breaks into an attached debugger.

### Controls

On maps the sandbox starts in the fixed (RTS) camera; the camera-mode key (C) or the Camera panel
switch between fixed, debug (the same camera: drag rotates, right-drag pans), and free (a flying
camera: right-drag looks, the pan keys move along the view, Q/E down and up, Shift faster, the
wheel sets the speed). A model view switches between orbiting the model and the free camera.

Mouse input:

| Input | RTS camera (maps) | Free camera and model view |
| --- | --- | --- |
| Mouse wheel | Zoom within map limits | Zoom (free camera: flying speed) |
| Left drag | Box-select mobile units | Rotate and tilt (orbit a model) |
| Left click | Select the object under the cursor | Select the object under the cursor |
| Right click / right drag | Move the selection (each unit on its own route to its slot, at the slowest one's speed), or attack the hostile under the cursor | Pan (maps) |
| Alt + right click | Move the selection as one formation that keeps its shape on the way | |
| Ctrl + right click | Attack-move the selection to the point (engage hostiles on the way) | |
| Shift + right click | Guard the point, or the object under the cursor (followed as it moves) | |

Key actions (`engine_app/sandbox_actions.cpp`), with their default keys:

| Action (`input_bindings.txt` name) | Default keys | Effect |
| --- | --- | --- |
| `camera_forward`, `camera_back`, `camera_left`, `camera_right` | W/Up, S/Down, A/Left, D/Right | Pan (free camera: fly). |
| `camera_fast` | Shift | Camera faster while held. |
| `camera_rise`, `camera_sink` | E, Q | Free camera up and down. |
| `camera_reset` | Space | Camera back to its start. |
| `camera_mode` | C | Next camera mode. |
| `order_attack_move`, `order_guard`, `order_formation` | Ctrl, Shift, Alt | Right click attack-moves, guards, or moves as a formation while held. |
| `spawn` | N | Spawn the current type at the cursor (a type not loaded yet loads in the background). |
| `spawn_owner` | O | Cycle the owner of spawned units. |
| `spawn_next_type` | Tab | Cycle spawnable types: every unit type with a model for the map. |
| `spawn_type_back`, `spawn_type_ten` | Shift, Ctrl | While held, the next type goes back, or steps by ten. |
| `pause` | P | Pause or resume the world (simulation, animation, particles; models: the animation); cameras keep working. |
| `bounds` | B | Bounds of every object and the world axes. |
| `navigation` | G | Navigation: every ground unit's footprint as the simulation measures it (its rectangle with a line to its front, or its circle), every unit's route, the route planner's chunk grids (16-cell squares faint, 32-cell squares bright), the recorded route changes of the selected ground units (below), and the whole map's passability as outlines between cells of different classes (red steep, blue water, orange impassable, green infantry only, magenta obstacle). Selected units' routes show without it. |
| `animation_next`, `animation_previous` | Right, Left | Model view: step through the bind pose and every `<MODEL>_*.ALA` beside the model. |

Escape quits and F1 shows or hides the bar at the top; these two are fixed. Every other action is
rebound in the Controls panel: click a key, press the new one (Escape cancels, Backspace clears);
up to two keys an action. Bindings are saved to `input_bindings.txt` next to the executable,
`action = key, key` a line, and read at start; each line that cannot be read is reported and left
out.

The bar's Panels menu opens the statistics (graphics numbers), spawn (search, category and faction
filters), sound, camera, graphics, and controls panels; its View menu toggles bounds and
passability. The graphics panel switches v-sync, anti-aliasing, bloom and its threshold and
strength, shadows and their cascades, fog, terrain decals, and detail levels while the sandbox runs;
shadow resolution and particle fidelity stay as started. Panels first open side by side under the
bar; where they are moved and sized is kept in `sandbox_imgui.ini` next to the executable. Panels
can be dragged, resized, and minimized; the mouse does not reach the scene while it is over a panel.

Selected units show their remaining route, moving or held (on space maps in 3D), with every
point marked by what it is (`SimulationWorld::route_points`):
- the unit's own legs in cyan, each waypoint a cross;
- an avoidance detour point (a ship steering around a unit in its way) an orange diagonal cross;
- the move target a yellow square.

While the navigation overlay is on, the simulation records every change to a ground unit's route
(`SimulationWorld::set_route_tracing`, `route_traces`; up to 8 per unit, not part of the state
hash), and the overlay draws those of the selected ground units:
- each recorded route, oldest faint and newest bright, colored by what set it: a plan of its own
  cyan, its share of a group's shared route teal, a way around a unit in its way orange;
- for the newest record, the stages of the search behind it: the 32-cell
  chunk route (violet, large squares at its parts' representative cells), the 16-cell chunk route
  (pink, small squares), and the cell path before smoothing (grey, crosses).

Records start when the overlay is turned on and are forgotten when it is turned off.

Damaged or selected units show health (green) and shield (blue) bars.

## EAWLauncher

`EAWLauncher.exe [options]` starts `EAWEngine sandbox` (which must be next to it) with the same
options, adding `--game-root` from discovery when none is given, and returns the engine's exit
code. An explicit `--game-root` that is not an installation, or no installation found, exits with 2.

## EAWDevLauncher sandbox scenarios

`EAWDevLauncher.exe` starts `EAWEngine sandbox` with a saved configuration instead of a hand-typed
command line. A scenario names:

- the game (EAW or FoC), an optional installation root, and the mod stack (highest priority
  first);
- the scene: a map from the mounted asset list with debug spawns (type, count, and owner slot, as
  `--spawn TYPE:N@OWNER`), a model with an optional animation, or the validation triangle;
- engine options: WARP, GPU validation, v-sync, hot reload, bounds, passability, auto move,
  statistics panel (hidden by default), raw input, frame limit, window size, upload budget, and
  free-form extra arguments.

The **Game root** field names the installation (empty: found through Steam); a folder that is not
one is refused with an error, and a mod folder put there by mistake is named as a mod (mods go in
the list below it, through **Add mod folder...**). The asset and spawn lists refresh by themselves a
second after the game, installation, or mod list changes (**Refresh asset lists** forces one). Each
spawn row has a **Pick...** list of spawnable XML types, built from the winning XML of the selected
game, the inherited base game, and the mod stack: every named definition with a land or space model
(2,710 in stock FoC, 1,046 of them units and structures). It filters by the text typed in the row,
by "units and structures only", and by whether the type has a model for the map's kind (land or
space, from the stock `_SPACE` naming convention), and refreshes in the background with the asset
lists.

The window shows the exact command line (copyable), starts any number of runs without waiting, and
lists each run's exit code. Presets are plain text in `EAWDevLauncher.scenarios` next to the
executable:

```text
[scenario]
name=Bespin skirmish
game=foc
map=DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED
spawn=T2B_Tank:6@1
spawn=AT_ST_Walker:6@2
option=show-navigation
vsync=on
```

Unknown keys and options load with warnings in the log, so presets written by a newer build still
open. The scenario model and process start live in the `eaw_launcher` library (`src/launcher`),
shared with `EAWLauncher`; the window shell is `eaw_imgui_app` (`src/imgui_app`), shared with
EAWTools.

## Headless battles

`EAWEngine battle` runs a battle on a map without a window, as fast as it can, and prints the state
hash, tick timing (per tick layer too: average and slowest milliseconds of commands, behavior,
navigation, movement, contact, combat, resolution, and publishing), the frame budget, and combat and
path statistics. The frame budget counts the ticks after the spawns that took longer than a 60 FPS
frame (16.7 ms), with the average, 99th percentile, and worst; headless, a frame is the tick a 60 Hz
frame runs, without rendering. It is the determinism check and the simulation benchmark.

| Option | Effect |
| --- | --- |
| `--map <ted>`, `--spawn`, `--auto-move`, `--no-scripts`, `--game`, `--mod` | As in the sandbox; `--map` or `--replay` is required. |
| `--ticks <n>` | Ticks to run (1 to 100,000,000; default 6000, or the replayed log's end). |
| `--tick-rate <hz>` | Ticks per second (1 to 1000, default 60; a replay runs at its recording's rate). |
| `--workers <n>` | Job system workers (0 to 256; default the hardware threads less one; 0 runs the jobs on the battle's own thread). |
| `--record <file>`, `--replay <file>` | Write the order log at the end; replay one (its map unless `--map` is given) and check its state hash. |
| `--check-determinism` | Run a second time without the job system and require identical state hashes. |
| `--hash-trace` | With `--check-determinism`: hash the state by subject every tick and name where the runs differ. |
| `--stress-orders` | Keep the spawned units busy instead of `--auto-move` (battle mode only; not with `--replay`). |

`--stress-orders`: every second each group of up to eight units of one owner gets a new order (move,
attack-move, attack, guard, in turn) to a point within 600 units of the center drawn from a
fixed-seed stream (on open ground on land) or to a hostile target, the groups staggered over the
ticks; the spawned units cannot be killed, so the load stays that of every unit. The orders are
recorded like any other, so `--check-determinism` and `--record` work.

```powershell
# Two opposing fighter groups at the map center, 6000 ticks (100 s at 60 Hz), order log written
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --record kuat.log

# Replay a log (from battle or from the sandbox's --record) and check its state hash
EAWEngine.exe battle --game foc --replay kuat.log

# Every mobile unit of a land map ordered to the center, run twice and compared (the first run on
# the job system, the second serially: the parallel systems must not change the result)
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED --auto-move --ticks 3000 --check-determinism

# The same with two job system workers
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED --auto-move --ticks 3000 --workers 2

# The same battle's 50 seconds at 30 Hz: whether a result depends on the tick rate
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED --auto-move --tick-rate 30 --ticks 1500

# The same, and if the runs differ, the first tick and the parts of the state where they do
EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED --auto-move --ticks 3000 --check-determinism --hash-trace
```

`--hash-trace` hashes the state by subject after every tick of both runs (bodies, routes, combat,
clips, flight, formations, orders, projectiles, random: `StateSubject`, the same fields as the
state hash, whose value it does not change) and, when the runs differ, prints the first tick after
which they do and the subjects that differ there. It costs about one state hash a tick.

Exit codes: 0 success, 1 a map that cannot be built or an unreadable log, 4 a replay whose state
hash differs from the recording or a failed determinism check. Logs are text, one order per line:

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

Each order line starts with the tick it was given at (it applies in the next step); entity ids are
`index.generation`. A replay needs the same game data and mods as the recording; logs do not record
them yet. Headless runs skip textures and animation clips, and the state hash covers gameplay state
only, so a log recorded in the sandbox replays headless with the same hash.

## Movement measurements

How smoothly ground units move is measured in two places ([group-movement-research.md](group-movement-research.md)):

- Every headless battle prints a `Movement:` line after `Paths:`: the **reversals** (a turn of at
  least 10 degrees one way undone within a second, the visible turning left and right; steering
  wobble below half a thousandth of a radian a tick is ignored), the **stalled** unit-seconds (ordered
  to move but under a twentieth of top speed) out of all unit-seconds moving, the **frozen** units
  (ordered to move but not moved at all for 5 seconds, each episode once), and the **route
  changes** by what made them: a plan (an order or a re-plan), a share of a group's shared route
  (space), or a way around a unit that blocked it; then the moves **stopped at units** (ended at a
  unit of the same order that arrived, or with no way around one of another order). Squad soldiers
  are left
  out (they follow their team). The thresholds are engine choices for the measure
  (`sandbox/movement_metrics.hpp`); route changes are counted by the simulation
  (`SimulationWorld::Stats::route_changes`), never hashed.
- `EAWEngine movement-scenarios` needs no game installation. On generated flat ground (80 x 80
  cells, 10 units apart) it runs, with tanks roughly like a T2-B (an approximation, not converted XML
  values), ten tanks through a one-tank gap in a wall as a formation and as a group move, a head-on
  pair, three tanks converging on one point, and two groups of five crossing. Each runs until every
  tank stopped, or 60 seconds, and prints its time, the measurements above, blocked moves, and the
  state hash. The runs repeat exactly, so a movement change is compared line by line with the
  baseline in [group-movement-research.md](group-movement-research.md#baseline).

```powershell
EAWEngine.exe movement-scenarios
```

## Path audit

`EAWEngine path-audit` is the pathfinding baseline ([pathfinding-design.md](pathfinding-design.md),
section 5, step 2). On `--map`, or every map under `Data/Art/Maps` whose name starts with `_LAND`,
it builds the battle (so placed objects' footprints are stamped). For each movement class of
`MovementClassTypeDefs.xml` (each distinct mask once) and each footprint size bucket (radius every 2
units, 2 to 68; classes appear as `class@radius`) it plans `--pairs` (1 to 1,000,000, default 100)
seeded random pairs of start and goal cells where a unit of that size fits. It prints, per map and
class, how many reached their goal, the nodes expanded (mean, 95th percentile, most), the time, and
the mean path length, then the totals; `--report <folder>` writes `path-audit.tsv`, one row per
pair.

The pairs depend only on the map, the class, and the fixed seed, so two runs plan the same ones and
`scripts/compare-path-audit.ps1 -Reference <folder> -Candidate <folder>` compares them pair by pair:
reach changes, the candidate's nodes and time against the reference's, and path lengths for pairs
both reached.

`--layout none|chunks|chunks2|refined` sets the chunk layout searches use: none (the default), one
level of 16-cell squares, two levels, or refined, the battles' layout (32-cell squares, then 16-cell
ones, then cells part by part). The `first_stretch` column holds the nodes spent up to the refined
layout's first two parts, what a lazily refined route would need before a unit moves. Node counts
repeat exactly; times vary between runs by up to about 40% for a class, so compare them over
repeated runs.

```powershell
EAWEngine.exe path-audit --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED
EAWEngine.exe path-audit --game foc --report D:\eaw-audit\before
.\scripts\compare-path-audit.ps1 -Reference D:\eaw-audit\before -Candidate D:\eaw-audit\after
```

The baseline (2026-11-15, the grid A*): 76 stock land maps, 13 movement classes, 98,800 pairs, 82%
reached (the rest partial: their goal lies in a region the start cannot reach); nodes expanded mean
1,385, 95th percentile 6,722; 0.25 ms a search on average, 1.1 ms at the 95th percentile; the whole
run takes about 80 s. With a size bucket every 2 units the run plans 3,359,200 pairs: with the
refined layout and the graded wall cost (2026-11-19) 81% reached, nodes expanded mean 447, 95th
percentile 1,409, 0.24 ms a search (0.52 ms at the 95th percentile), and the whole run takes about 20
minutes; a single map (44,200 pairs) under a minute.

## Lua survey

`EAWEngine lua-survey` runs every script under `Data/Scripts` in its own sandboxed host on recording
stubs and reports which engine functions and methods they call. It executes the installation's or
mod's scripts, in the engine, never in the inspector or tests.

```powershell
EAWEngine.exe lua-survey --game foc --ticks 600 --report survey
```

Each script is loaded and `Base_Definitions` (else `Definitions`) is called. Then, for `--ticks`
simulation ticks (default 600, 10 seconds at 60 Hz):

- the engine objects the framework expects are bound as stubs: `Object` for game-object scripts,
  `PlayerObject` for AI, free-store, evaluator, and intervention scripts, `Target` and one object
  per task force for AI plans;
- `main` runs as a thread, and so does each task force's `<Name>_Thread` in an AI plan;
- events are queued as the engine would deliver them: each `StoryModeEvents` entry through
  `Story_Event_Trigger`, one at a time over the first three quarters of the run; each task force
  event (`<Name>_<Event>`, else `Default_<Event>`) at the half; and each function a script handed
  to an engine function (a proximity handler) at three quarters, with stub objects as arguments.

Story events fire in key order, not in their campaign's order, so states that rely on globals an
earlier state set fail; those errors are the survey's, not the host's.

The summary lists the events queued, the most used functions, the scripts' average cost per tick
and the slowest single ticks, and the most common errors. `--report <folder>` writes `api.tsv`
(name, function or method, host-implemented or stub, scripts, calls, argument types) and
`scripts.tsv` (per script: loaded, errors, exited, threads left, events queued, the time of its
ticks in all and of its slowest tick, first error). Compiled scripts are refused (Lua 5.0 does not
verify bytecode), and each call or resume stops after its instruction budget, so a runaway script
cannot hang the survey.

## Check scripts

Repeated checks are PowerShell scripts under `scripts/`. They run from anywhere, find the repository
from their own location, and use the built executables (Release unless `-Configuration` says
otherwise).

```powershell
# The reference battle twice: both runs must agree on the expected state hash
.\scripts\check-battle.ps1                        # -Configuration Debug|Release|DebugValidation, -Expected <hash>, -GameRoot <path>
.\scripts\check-battle.ps1 -Extended              # and four more battles: large Utapau, Kuat fighters, Kuat capital ships, Utapau AT-ATs

# The reference scenes on WARP, compared with an earlier set (keep both folders outside the repository)
.\scripts\check-captures.ps1 -Output D:\eaw-captures\before
.\scripts\check-captures.ps1 -Output D:\eaw-captures\after -Reference D:\eaw-captures\before
                                                  # -Configuration Debug|Release, -GameRoot, -Hardware, -ExtraArguments

# Recount docs/xml-tag-status.md after changing rows (rewrites its summary and family counts; -Path <file>)
.\scripts\update-tag-status.ps1

# Benchmarks (EAWBenchmarks) compared with an earlier run (keep the result files outside the repository)
.\scripts\benchmark.ps1 -Output D:\eaw-bench\before.tsv
.\scripts\benchmark.ps1 -Output D:\eaw-bench\after.tsv -Reference D:\eaw-bench\before.tsv
                                                  # -Suites <names>, -Workers 0,3,7, -Repeat 7, -Configuration

# Battle frames against the 60 FPS budget under constant new orders, at 10, 50, 100, and 500 units
.\scripts\benchmark-battle.ps1                    # -Counts 100,500, -Seconds 10, -Map <path>, -Configuration, -GameRoot <path>

# The sandbox under a synthetic 8 kHz mouse: reading input must stay cheap (hands off the computer while it runs)
.\scripts\check-input-load.ps1                    # -Rate 8000, -Seconds 10, -MaxReadMilliseconds 2, -Map <path>, -Configuration, -GameRoot <path>

# Two path audits pair by pair
.\scripts\compare-path-audit.ps1 -Reference <folder> -Candidate <folder>
```

### check-battle.ps1

The reference battle is Utapau, two opposing groups (`Rebel_Infantry_Squad:2@1`, `T2B_Tank:3@2`)
ordered to the center for 3000 ticks, run twice. The script holds the expected hash as its default
(`-Expected`); a change that alters the simulation on purpose updates it and says why. `-Extended`
adds, each run twice as well: a large Utapau battle (squads, tanks, and walkers, 1800 ticks),
fighters at Kuat, capital ships at Kuat, and AT-ATs at Utapau.

### check-captures.ps1

It renders a Kuat fighter battle (frames 90 and 300), a Utapau land battle (200), a capital ship
duel at Kuat (120), the dark trooper model (20), the validation scene (5), and Mon Calamari 02's
water without fog, legacy and enhanced (60), and writes a diff image next to each capture that
changed. The scenes render on WARP; `-Hardware` renders on the GPU instead (faster and
deterministic for one driver, so its references are per machine and never compared with WARP ones).
`-ExtraArguments` passes options to every scene, so a change behind an option can be checked with
the option off against the earlier references (`-ExtraArguments '--no-aa'`; several options in one
space-separated string, `'--aa taa'`).

### benchmark-battle.ps1

Not for every change: run it when a change risks route planning or simulation performance
(pathfinding, movement, group cohesion, targeting). Per unit count it builds the map (Utapau by
default), spawns half the units a side (each side half single vehicles, half infantry squads; a
squad counts as one unit), and runs `-Seconds` of `EAWEngine battle --stress-orders` (the ticks that
apply the spawns are not measured). It prints the frames over budget, the average, 99th percentile,
and worst frame, the entities, and the route searches, and fails when any frame went over 16.7 ms.

On the development machine (2026-11-16, 8 hardware threads): 100 units average 1.3 ms (worst
7.3 ms); 500 units (5,000 entities) average 6.0 ms with no frame over (worst 13.9 ms), with ground
avoidance's ORCA on the job system (7.5 ms serially). Timings vary by about 1 ms between runs on
that machine.

### check-input-load.ps1

It stands in for a high polling rate mouse: it starts the sandbox, waits until its window has the
focus (Windows gives it the focus only when nothing else holds it, so start the script and leave the
computer alone), and sends relative mouse moves through `SendInput`, one pixel right and one left
alternately so the cursor stays put. The sandbox prints its input load at exit (`Input raw | 12.0
records a frame (max 40) | read 0.020 ms (max 0.100)`: raw input records and the time `poll_events`
took, average and worst frame; the Statistics panel shows the same line). The script fails when no
records arrived or the worst frame's read exceeds `-MaxReadMilliseconds` (2 ms, an engine choice:
an eighth of a 60 FPS frame; the window's startup frames take up to about 0.8 ms without any
input).

## Benchmarks

`EAWBenchmarks` times workloads serially (the way the engine runs them without the job system) and
on the job system with several worker counts, and fingerprints each run's output: a job-system run
whose output differs from the serial run's fails the benchmark, whatever its speed. Timings are only
representative in Release, and a busy machine moves them by 10% or more, so compare medians of
seven runs or more.

```powershell
EAWBenchmarks.exe --list                    # the suites
EAWBenchmarks.exe                           # every suite; worker counts 0, 1, 2, 4, and all
EAWBenchmarks.exe paths --workers 0,3,7 --repeat 9 --output D:\eaw-bench\paths.tsv
```

Options: suite names to run (default all), `--workers <n,n,...>` (default 0, 1, 2, 4, and the
hardware threads less one), `--repeat <n>` (timed runs after one warm-up, default 5; the median is
reported), `--output <file.tsv>`, and `--list`.

Suites:

- `jobs-tiny`: 200,000 one-number jobs, the scheduling cost of a job;
- `jobs-math`: 2,000,000 items of float math in chunks, scaling;
- `jobs-loops`: 1,000 `parallel_for` calls of 512 items in chunks of 4, the fixed cost of a short
  loop, as a simulation tick runs them;
- `jobs-nested`: jobs waiting on their own jobs, parking and resuming fibers;
- `paths`: 384 A* searches on a synthetic 512 by 512 ground, the route searches the simulation runs
  on the job system;
- `poses`: one frame of `UnitAnimator::animate` with 256 new 40-bone poses and an empty pose cache,
  presentation's pose sampling.

`scripts/benchmark.ps1` runs it and, with `-Reference`, prints each configuration's median against
an earlier run's.

Measurements on the development machine (8 hardware threads). On 2026-11-15: float math 6.6 times
faster with 7 workers, path searches 5.2 times; a job costs about 43 ns on one thread and 90-110 ns
with workers sharing one queue, so a job should carry several microseconds of work. On 2026-11-16
with 7 workers: short loops 67.7 ms (5.9 times the serial run), tiny jobs 20.0 ms, long chunks 211
ms.

A suite's work runs in functions the compiler may not inline (`churn_range`): inlined, the serial
loop and the chunks got different code from one source, and a change to `parallel_for` alone made
the serial run 23% slower.

## Profiling

Tracy (BSD-3-Clause, 0.14.1, `third_party/tracy`) is the engine's profiler. A profiling build
fetches and compiles its client; the default build has neither, and every mark compiles to nothing:

```powershell
.\build.ps1 -Configuration Release -Profile      # EAW_PROFILE=ON; a later build without -Profile turns it off
.\executables\Release\EAWEngine.exe battle --game foc --map DATA/ART/MAPS/_LAND_PLANET_UTAPAU_01.TED --spawn T2B_Tank:125@1 --spawn AT_ST_Walker:125@2 --stress-orders --ticks 6000
```

Then connect the Tracy viewer of the same release (0.14.1, from the project's GitHub releases, not
built here) to `localhost`. The client collects only while a viewer is connected (on demand) and
listens on this machine only (port 8086). Without a viewer a profiling build runs as fast as the
default one (the 500-unit benchmark: 5.9 ms a tick against 6.0).

What is marked (`core/profile.hpp`, the only place that names Tracy): each simulation layer as a
zone named like the battle's layer report, the tick as the frame set "simulation tick"; inside
navigation the route searches, giving way, detours, and ground avoidance; every job, with the job
system's fibers named ("job fiber N") so zones follow a job that waits and resumes on another
thread; the worker threads ("job worker N"); and in the sandbox the simulation, particles, and
render parts of a frame and the frame set "frame". Add marks with `EAW_PROFILE_ZONE("name")` (a
string literal) or `EAW_PROFILE_ZONE_TEXT(text, size)` for a name from a table.

## Captures and image regression

The sandbox can render a scene reproducibly and save it as PNG, and `EAWEngine compare-images`
measures how two captures differ.

```powershell
# The same command renders the same pixels on the same machine and driver.
EAWEngine.exe sandbox --game foc --map DATA/ART/MAPS/RM01_SPACE_PLANET_KUAT.TED `
  --spawn Rebel_X-Wing_Squadron:2@1 --spawn TIE_Fighter_Squadron:2@2 --auto-move `
  --width 960 --height 540 --capture captures\kuat.png --capture-frame 90

# --diff writes the first image dimmed, with changed pixels in red.
EAWEngine.exe compare-images reference\kuat.png captures\kuat.png --diff captures\kuat_diff.png
```

- `--capture <file.png>` writes the scene as the scene pass and bloom leave it, without debug lines
  or the overlay, then closes the window.
- Captures run on a fixed presentation clock (`--fixed-frame-time <s>`, above 0 and at most 1,
  default 1/30 s): simulation ticks, unit animation, particles, and the camera advance by that step
  per frame, and the clock holds still until every texture is resident, so texture streaming never
  changes the result. `--capture-frame <n>` takes the nth presented frame after that (default 1).
- `--camera <x> <y> <distance>` frames a map view explicitly (heading and pitch stay the RTS
  camera's).
- `compare-images <expected.png> <actual.png>` counts a pixel as changed when a colour channel
  differs by more than `--threshold` (0 to 255, default 8) and fails when more than `--max-changed`
  (a fraction, default 0.001) of the pixels changed. Exit codes: 0 within the limit, 5 over it, 1 an
  image could not be read, 2 a bad command line. It reads only the engine's own uncompressed
  captures.
- Captures of the stock game are derived from protected data: keep reference images locally
  (outside the repository), never commit them.
- WARP renders identically run to run; hardware GPUs are deterministic for a given driver, so
  references are per machine.
- Against the original, compare features rather than scenes: a screenshot of one subject in the
  original at a similar zoom, a capture of the same subject framed with `--camera`, and measured
  ratios (glow width to hull length, bolt length to ship, core colour), recorded in
  [compatibility-research.md](compatibility-research.md).

## EAWTools graphical workbench

`EAWTools.exe` is the primary interactive interface: a native C++20 application using Dear ImGui's
docking branch with the maintained Win32 and DirectX 11 backends. Search results and DAT rows use
clipped (virtualized) tables, so the interface draws only visible rows even for full 19,000+ entry
language files. The GUI calls the same core, format, and VFS libraries as the command-line programs;
it does not spawn or scrape CLI subprocesses. Native Windows file and folder dialogs preserve
Unicode paths.

### Asset Inspector page

- auto-detect or browse to an Empire at War installation, and switch between Empire at War and
  Forces of Corruption;
- add ordered loose mod layers and show the final winning virtual asset;
- search and filter by extension (an expandable catalog of known game extensions alongside a custom
  extension field);
- inspect DAT, DDS, semantic ALO models, semantic ALA animations, MTD, TED, XML, Lua, and generic
  binary metadata;
- preview ALO geometry in an orbitable viewport, resolve packaged DDS diffuse textures through the
  active mod and VFS layers, play matching ALA animations, and optionally display skeleton and
  collision geometry;
- choose a model's damage state (its `_ALT<n>` meshes, ALT0 intact) and detail level (its `_LOD<n>`
  meshes, the highest by default) with the same rule the engine uses (`alo_mesh_visibility`), pick
  one of the XML object types that use the model (types with hardpoints first), and toggle each of
  its hardpoints: shown hardpoints draw a marker coloured by kind (weapons red, shields blue, engines
  orange; labels optional) and their `Model_To_Attach` model at the attachment bone, destroyed ones
  show their `Damage_Decal` scorch meshes (hidden while intact, as in battle) and drop the attached
  model;
- **Preview** opens the model in its own resizable window (as for maps), which stays open while
  other models are selected.

Model previews hide shadow-volume, heat-distortion, and occluded-unit geometry; opaque materials
ignore texture alpha (which holds gloss or team-color masks), alpha-tested materials are cut out,
and additive materials blend additively. Model and map previews render at full detail with a small
depth-buffered Direct3D 11 renderer (4x MSAA, two-sided, flat-shaded) into an offscreen image;
grids, markers, and skeletons are drawn over it. Bind-pose geometry is uploaded once; animated
skinning runs on the CPU and re-uploads each frame. This renderer is tool-only; the production
renderer is the Direct3D 12 path. The GUI consumes render-ready vertex, index, and skin streams,
decoded ALA frames, and every DDS encoding observed in the stock corpus.

Land TED files get an orbitable heightmap preview with adaptive detail, elevation shading, raw
passability-class colors, wireframe, and reference-grid overlays. Space TEDs render as an orbitable
X/Y placed-object map using decoded transforms, Z-altitude guides, type colors, player-code
outlines, and tactical bounds. Map previews can be detached into a larger resizable window. With
**Objects** enabled, land and space previews resolve each placed object through its XML type to its
model and draw the model's geometry at the object's position, heading, and scale, textured with each
material's base-color texture; each unique model is uploaded once and drawn instanced at full
detail. Objects without a drawable model (unresolved types, squadrons and companies, missing or
particle models) keep type-colored markers, and hovering any object shows its type, element, XML
source, and model. Land transforms use positive X/Y editor coordinates at 20 world units per terrain
sample, while Z elevation is converted to the heightmap's 20x stored scale.

### DAT Localization page

- open a complete game or mod DAT, or a sparse, version-control-friendly TSV;
- edit text, group, status, XML context, and translator notes;
- create arbitrary keys or paired unit-name and tooltip keys;
- scan `Data/XML` for missing active `TEXT_*` references;
- filter large tables by text or workflow status and sort Key, Text, Group, or Status in either
  direction by clicking a column header;
- validate duplicate, TODO, CRC, and DAT structure issues;
- build a complete output DAT without changing the baseline file.

## EAWAssetInspector

The inspector reads the same layered virtual file system as the engine, so results show the asset
that would win after the base archives, patch archives, loose files, and supplied mods are combined.
It never changes the game installation; `extract` copies files out of it into a folder you name. The
Forces of Corruption profile mounts the original `GameData` archives below the expansion's
`corruption` archives because many FoC models intentionally reuse base-game textures and other
resources.

The precedence contract is regression-tested across manifest and unlisted archives, numeric and
64-bit patches, loose files, inherited EAW data, and multiple ordered mods. Numbered patches use
numeric ordering (`Patch10` follows `Patch2`), and the first supplied mod has highest priority.

### Commands and options

| Command | Operand |
| --- | --- |
| `summary` | Counts, logical sizes, extensions, and winning source mounts under `--prefix`. |
| `list [prefix]` | The winning files under the prefix. |
| `inspect <virtual-path>` | Format-specific metadata. |
| `gait <clip.ala>` | A walk clip's foot-planted ground speed, a turn clip's turn rate. |
| `clip-joins <model.alo>` | The largest bone jump where the engine joins the model's clips. |
| `type-tags <Tag>[,<Tag>...]` | Which XML types set the tags, and to what. |
| `map-audit [prefix]`, `map-objects [prefix]` | Map corpus audits. |
| `texture-audit [prefix]`, `particle-audit [prefix]`, `lua-audit [prefix]`, `xml-audit [prefix]` | Corpus audits. |
| `localization-refs <xml-path>` | Unique `TEXT_*` references in one resolved XML file. |
| `extract [prefix] --output <folder>` | Copy the winning files out of the archives. |
| `spawn-types` | The spawnable types the development launcher and the sandbox offer. |
| `sound-audit` | Resolve and decode every sound event's samples. |
| `texture-png <texture-path> --output <file.png>` | One texture's top level as a PNG. |

| Option | Effect |
| --- | --- |
| `--game-root <path>`, `--game eaw\|foc` | Installation (auto-detected) and profile (default `foc`). |
| `--mod <path>` | Mod root; repeatable, the first has priority. |
| `--language <name>` | Language of the text and speech archives (default `English`). |
| `--schema-root <path>` | Optional `eaw-schema` checkout for XML validation. |
| `--prefix <path>` | Summary and list prefix (default `Data`). |
| `--extension <.ext>` | Filter by extension (the dot is optional). |
| `--limit <count>` | Most list or sample rows (not used by `extract`). |
| `--output <path>` | `extract`: the folder; `texture-png`: the PNG file (outside the game and mods). |
| `--overwrite` | `extract`: replace files that already exist. |

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

# The ground speed at which a walk clip plays without its feet sliding (its planted bones' speed),
# and how fast a turn clip turns the body; the model is found from the clip's name
EAWAssetInspector.exe gait Data/Art/Models/EV_AT-AT_MOVE_00.ala

# Which XML types set the given tags, and to what (resolved through Variant_Of_Existing_Type; one
# tab-separated row per type with its element and land model; "-" where a tag is absent)
EAWAssetInspector.exe type-tags Walk_Transition,Has_Pre_Turn_Anim,Locomotor_Has_Animation_Priority --game foc

# Whether a model's clips meet where the engine joins them (idle, start, walk, the stops by part of
# the stride, turns with their HALF and QUARTER cycles, a raised leg's IDLE_LEGLIFT and
# IDLE_LEGDROP, a second idle, and how far ATTACK strays from its first frame): the largest bone
# jump at each join
EAWAssetInspector.exe clip-joins Data/Art/Models/EV_AT-AT.alo

# A particle system: each emitter's texture, blend, rate, kind, gravity (weather emitters also
# their cube size, fade-out, and distance, speed, and spawn cube), and tracks
EAWAssetInspector.exe inspect Data/Art/Models/p_rain_med00.alo

# Unique TEXT_* references in one resolved XML file
EAWAssetInspector.exe localization-refs Data/XML/Units.xml

# Audit model references, material mappings, DDS formats, and MTD atlases
EAWAssetInspector.exe texture-audit Data/Art/Models --game foc

# Decode every particle ALO and summarize blend modes, renderers, and spawning
EAWAssetInspector.exe particle-audit Data/Art/Models --game foc

# Inventory Lua source and bytecode, dependencies, callbacks, and host API calls
EAWAssetInspector.exe lua-audit Data/Scripts --game foc --limit 100

# Parse and index the complete resolved XML object database
EAWAssetInspector.exe xml-audit Data/XML --game foc

# Add type and tag, scalar, enum, repetition, and object-reference validation
EAWAssetInspector.exe xml-audit Data/XML --game foc `
  --schema-root "D:\path\to\eaw-schema"

# Map corpus coverage, and placed objects resolved to their models
EAWAssetInspector.exe map-audit Data/Art/Maps --game foc
EAWAssetInspector.exe map-objects Data/Art/Maps --game foc

# The spawnable types the development launcher and the sandbox offer (with mods: --mod)
EAWAssetInspector.exe spawn-types --game foc --mod "D:\path\to\mod" --limit 100

# Resolve and decode every sound event's samples (--language picks the localized archive), and
# check the sounds placed in animations (ANIMATIONSFXMAPS.TXT through SURFACEFX.XML) and every
# object type's sound fields naming an event that is not defined
EAWAssetInspector.exe sound-audit --game foc --language English --limit 50

# Copy the winning file of every virtual path under a prefix out of the archives
EAWAssetInspector.exe extract Data/Scripts --extension .lua --output D:\eaw-extract
```

### extract and texture-png

`extract` writes each resolved file to `<output>/<virtual path>` (upper case, as the archives name
them) for reading with ordinary tools. It refuses an output folder inside the installation or a
mod, keeps files that already exist unless `--overwrite` is given, and skips (with a warning and a
failing exit code) entries whose archive name could not be written where it claims to go: absolute
or parent-traversing names, a colon (a drive or an NTFS stream), or a Windows device name. The
extracted files are the game's protected data: keep them outside the repository, and never quote
them in committed files. `--limit` does not apply.

`texture-png <texture path> --output <file.png>` writes the top level of one texture as a PNG, to
look at a texture's layout or channels with ordinary tools (the decal atlas
`DATA/ART/TEXTURES/D_MASTER_DECAL.DDS` was read this way). Uncompressed formats only; the same
read-only rule applies, and the image is the game's protected data.

### What inspect reports

The semantic inspectors cover DAT string tables, DDS textures, MTD atlas directories, ALO model
structure, ALA animation structure, TED maps, Petroglyph XML, and Lua source and bytecode.

- ALO: skeletons, meshes, bounds, materials, texture references, geometry counts, attachments,
  proxies, lights, and dazzles. Material inspection preserves raw parameters and adds
  engine-neutral shader-family, blend-mode, render-phase, lighting-model, and texture-role hints.
  Shaders covered by the legacy reference corpus use documented reference profiles
  ([Legacy shader mapping](legacy-shader-mapping.md)); other names are tagged `name-heuristic`.
- Particle ALOs: their emitters (texture, blend mode, spawning, renderer, shape groups, child links)
  and every keyframe track, falling back to the top-level chunk inventory when they cannot be
  decoded.
- ALA: format version, playback timing, bone tracks, packed translation, rotation, and scale blocks,
  visibility, and stepped channels.
- TED: land or space properties, terrain dimensions and elevation, texture layers, water maps,
  tracks, decals, environments, passability, object CRC counts, AI zones, and embedded map previews.
  It marks the active environment and both sky-dome names, lists each environment's key and fill
  lights, specular, and ambient, and prints the liquid (type, level, color, textures) and every
  terrain-information field as floats or text.
- MTD: source-image CRCs and atlas rectangles.
- DDS: every encoding observed in the stock corpus; the shared decoder also recognizes BC4/BC5 and
  common DX10 formats for mods.
- XML: the reader accepts the game's multi-root fragments and historical declaration syntax,
  preserves element, attribute, and field order and source lines, and indexes typed `Name`
  definitions case-insensitively.

### Audits

`xml-audit` reports registry entries naming no file, the files no registry or reference reaches (and
the definitions left out of them), definition-type coverage, duplicate type and name keys, XML file
references, missing references, and parse failures across the final VFS winners. The combined stock
FoC and EAW corpus parses all 638 resolved XML files and indexes 21,283 named definitions across 185
element types.

Schema validation is optional and offline. `--schema-root` accepts either the root of an
`eaw-schema` checkout or one game schema directory. When a repository root is supplied, FoC
validation layers the expansion schema over the inherited EAW schema. Registry metafiles assign
types to referenced files, while a nested element whose canonical name identifies a more specific
registered type takes precedence. Remaining definitions are typed only when one schema type is the
unique structural match with at least two matching fields and 60 percent field coverage.
Diagnostics cover unknown and non-repeatable fields, Boolean, integer, float, vector, RGBA and enum
shapes, unresolved XML-object names, and references whose existing target has the wrong declared
type or behavior-derived kind. Variant objects inherit kind-defining fields from their parent unless
they override the field. The external schema remains a work in progress, so stock-data diagnostics
are compatibility evidence to review, not automatically engine defects.

`texture-audit` parses every resolved DDS and MTD, checks the MTD-to-compressed-DDS dependency,
inventories format and material coverage (including translation source, render phase, lighting
model, and heuristic-only shader names), and reports missing ALO texture references with an example
model. The optional prefix scopes model inspection; the DDS and MTD corpus always covers the final
mounted `Data` VFS.

`particle-audit` decodes every ALO whose root chunk marks a particle system and reports failures,
warnings by kind, the property mini-chunks with their sizes, blend modes, renderer kinds
(billboard, ground-plane, kite, heat, weather), spawning, shape groups, track interpolation, and
child links.

`lua-audit` is read-only and never executes scripts. For source it ignores comments and string
contents while collecting explicit `require`, `Script.Load`, `Script.Reload`, and `include`
dependencies, named function definitions, and called names. It resolves modules against the final
layered VFS and separates standard-library and corpus-defined names from candidate engine host
calls. For standard Lua 5.0 and Petroglyph `Lup` bytecode it validates the architecture header and
recursively inventories prototypes, constants, instructions, and persistent function IDs. This is
structural compatibility analysis, not a decompiler or Lua VM.

`map-audit` parses every resolved TED in one mounted profile and reports corpus coverage without
extracting maps. It also reports the distribution of active-environment indices and how many maps
name an environment index outside their environment list; every field of the active environments by
mini-chunk type for land and space maps (count, size, and each 4-byte word's range as a float and as
an integer, or a few text values: how the fog fields were identified); and the same for the land
maps' terrain-information fields grouped by liquid type (to identify the water, lava, and ice
fields).

`map-objects` resolves every placed TED object through its type-name CRC to an XML definition,
follows `Variant_Of_Existing_Type` inheritance to the `Space_Model_Name` or `Land_Model_Name` for
the map type, and checks that the model exists in the layered VFS. It reports coverage, resolved
element types, unknown CRCs, typed objects without a model (such as squadrons and companies, which
spawn member units), missing model files, and variant-chain diagnostics. It also reports how many
space-squadron members were expanded, how many land teams and companies were left unexpanded, and
the placements per TED owner slot by element type.

## EAWDatEditor

The DAT editor preserves the Petroglyph binary layout, UTF-16 values, ASCII keys, CRC32 values, CRC
ordering, index ordering, and legacy duplicates. Writes go to a sibling temporary file and are
committed atomically. Existing outputs are not replaced unless `--force` is passed.

```text
EAWDatEditor info <file.dat>
EAWDatEditor list <file.dat> [--filter text] [--limit count]
EAWDatEditor get <file.dat> <key>
EAWDatEditor set <input.dat> <output.dat> <key> <text> [--force]
EAWDatEditor remove <input.dat> <output.dat> <key> [--force]
EAWDatEditor validate <file.dat> [--reference original.dat]
EAWDatEditor export <file.dat> <workspace.tsv> [--force]
EAWDatEditor diff <base.dat> <edited.dat> <workspace.tsv> [--force]
EAWDatEditor build <base.dat> <workspace.tsv> <output.dat> [--allow-todo] [--force]
EAWDatEditor scan-mod <mod-root> <base.dat> <workspace.tsv> [--force]
EAWDatEditor add-unit <workspace.tsv> <unit-id> <name> <description> [--force]
```

The tool separates binary deployment files from editable source:

```text
stock/mod MasterTextFile_LANGUAGE.dat
                 +
      sparse localization.tsv
                 |
                 v
new complete MasterTextFile_LANGUAGE.dat
```

TSV workspaces are version-control friendly and support embedded tabs, quotes, newlines, and full
Unicode. Their columns are:

- `Key`: the `TEXT_*` identifier;
- `Text`: localized value;
- `Group`: unit, XML file, campaign, or another mod-defined grouping;
- `Status`: `todo`, `ready`, `new`, `modified`, `existing`, or `deleted` (removes the key);
- `Context`: XML references or authoring hint;
- `Notes`: reviewer and translator notes.

### New-unit workflow

```powershell
EAWDatEditor.exe add-unit localization.tsv MY_NEW_FRIGATE `
  "My New Frigate" "A fast escort frigate."

EAWDatEditor.exe build MasterTextFile_ENGLISH.dat localization.tsv `
  output\MasterTextFile_ENGLISH.dat
```

This creates `TEXT_UNIT_MY_NEW_FRIGATE` for `<Text_ID>` and `TEXT_TOOLTIP_MY_NEW_FRIGATE` for
`<Encyclopedia_Text>`. The unit id is upper-cased, other characters become single underscores, and
none is kept at either end (`" my new frigate "` gives `MY_NEW_FRIGATE`), the same rule as the
EAWTools "Add unit" dialog. Additional ability, campaign, and UI keys can be ordinary TSV rows in
the same group.

### Find missing mod localization

```powershell
EAWDatEditor.exe scan-mod "D:\path\to\mod" `
  "D:\path\to\mod\Data\Text\MasterTextFile_ENGLISH.dat" missing.tsv
```

The scanner ignores XML comments, records up to five source locations per key, and emits unresolved
references as `todo`. A normal build refuses unresolved TODO rows whose text is empty;
`--allow-todo` skips those empty rows for an intentionally partial build. A TODO row that already
contains text is treated as resolved and included, and editing an empty TODO value in the GUI
changes its status to `ready`.

### Translation validation

```powershell
EAWDatEditor.exe validate MasterTextFile_GERMAN.dat `
  --reference MasterTextFile_ENGLISH.dat
```

Besides structural, CRC, Unicode, and duplicate checks, reference validation compares `%s`/`%d`
placeholders, escaped newlines, bracket, angle, and brace tokens, key coverage, and leading and
trailing whitespace. The stock repeated `TEXT_END_OF_DATA` marker is preserved and reported as a
warning, not rejected.
