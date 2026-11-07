# Architecture

## Product boundary

`EAWLauncher` selects a game variant, installation, mod stack, and engine
settings. It launches `EAWEngine`; it never launches or injects into the legacy
game executable. `EAWEngine` reads the user's existing data in place and owns
the new runtime.

The original files are inputs only:

```text
owned game + mods
        |
        v
layered VFS -> format decoders -> immutable asset database
                                      |
                    +-----------------+------------------+
                    v                                    v
          deterministic simulation              presentation world
                    |                                    |
                    +---------- snapshots --------------+
                                                         |
                                                  render/audio/UI
```

## Module boundaries

1. **Platform**: windowing, input, files, clocks, crash reporting, CPU topology.
2. **VFS**: loose directories, MEG archives, mod priority, asynchronous reads.
3. **Formats**: XML, Lua 5.0 bytecode/source, ALO, ALA, TED, MTD, DAT, DDS,
   shaders, audio, and movies. Decoders produce versioned engine-native data.
4. **Asset runtime**: handles, dependency graph, background streaming, budgets,
   hot reload, and GPU upload queues.
5. **Simulation**: deterministic fixed ticks and data-oriented components.
   Parallel jobs operate on disjoint partitions; commands merge at explicit
   barriers. Render frame rate never changes gameplay results.
6. **Modes**: galactic, land tactical, space tactical, campaign/story, skirmish.
7. **Scripting**: compatible Lua surface plus namespaced new APIs. Scripts may
   schedule work but cannot mutate simulation storage outside command buffers.
8. **Presentation**: renderer, animation, effects, UI, audio, and cinematics.
9. **Tools**: launcher, asset inspector, map/model viewers, converter, profiler,
   compatibility test runner, and mod SDK.

## Rendering direction

The first render target is legacy mode: newly written shaders driven by the
decoded legacy ALO materials and their reference profiles, approximating
original Alamo rendering so the game can run on the new engine and its behavior
can be stabilized. A modern physically based shading pipeline with
engine-native materials follows, using the same profiles as conversion input.
Legacy shaders are mapping evidence only and are never used at runtime.

The renderer uses an RHI with explicit resources, command lists, queues,
barriers, descriptor tables, and pipeline objects. The first production backend
should be Direct3D 12 because the target game and primary audience are on
Windows. Vulkan follows through the same RHI.

The renderer should support bindless resources, GPU-driven culling, indirect
drawing, mesh LODs, texture streaming, modern compressed texture formats,
shadow atlases, clustered lighting, HDR, temporal anti-aliasing, and a scalable
effects system. High-resolution assets are a budget/streaming problem, not a
hard-coded dimension or polygon-count limit.

The Direct3D 12 backend starts with independent direct and copy queue timelines.
Each queue owns a monotonic fence. Swap-chain frame contexts own their command
allocators and may only recycle them after that frame's direct-queue fence has
completed. Uploads run on the copy queue; consumers enqueue a GPU-side wait on
the direct queue instead of stalling the CPU. CPU waits are limited to frame
latency pacing, reuse of a still-in-flight frame context, swap-chain resize, and
shutdown. GPU-backed objects that support submitted work remain retained until
their producing queue's fence completes.

Animated visual scenes preserve model-wide bone indices in immutable vertex
buffers. ALA poses are sampled on the CPU into skin matrices, while every
in-flight frame owns a persistently mapped bone-palette buffer. The CPU updates
that palette only after the frame's direct-queue fence has completed, avoiding
both per-frame resource creation and writes into memory still consumed by the
GPU.

Shaders are engine-written HLSL source files compiled at build time: DXC
(shader model 6.0) for Direct3D 12 and FXC (shader model 5.0) for the Direct3D 11
tool previews, embedded as bytecode in generated headers. The D3D12 root
signature is declared in the HLSL, validated by DXC, and created from the
compiled vertex shader, so there is one definition of the binding layout. An
opt-in `ID3D12PipelineLibrary` cache, keyed by a hash of the embedded bytecode
and a layout version, persists pipeline states between runs; driver or shader
changes discard it and the file is replaced atomically.

The engine owns the camera: controllers in `eaw_engine` turn input into a
`VisualCamera` (eye, target, up, field of view, near/far planes) that the
renderer consumes. An orbit controller frames single models and a tactical
controller pans, rotates, and zooms over a map within its bounds. Background
instances (sky domes and star backdrops) are drawn first, centered on the camera
by transforming them as directions, pinned to the far plane, and without depth
testing, so they never occlude or parallax against the world.

A visual scene groups draws into meshes (one per model) and places them with
instances. Each instance carries a world transform, a tint, and a bone-palette
offset in an immutable structured buffer. Instances are sorted by mesh at load
so every draw of a mesh is issued once with `DrawIndexedInstanced` over a
contiguous instance range; the range start is a root constant because
`SV_InstanceID` restarts at zero for every draw. Map scenes share one
bind-pose palette today; per-instance poses will come from simulation
snapshots.

## Multithreading direction

"Use all cores" is not an architecture by itself. The engine uses a dependency
aware job system and divides work into deterministic phases:

1. collect input and script commands;
2. update spatial queries and perception;
3. update AI planning at controlled cadences;
4. solve movement/path requests in parallel;
5. resolve weapons, projectiles, damage, and abilities;
6. publish an immutable presentation snapshot.

Large unit counts require structure-of-arrays storage, stable entity IDs,
batched queries, hierarchical spatial indices, flow fields or shared paths for
groups, AI level-of-detail, and performance budgets. Adding threads to the
legacy object model would not be sufficient.

## Compatibility policy

Compatibility is measured, not assumed. Every supported format and behavior
gets a corpus test, a decoder test, and—where possible—a golden result captured
from public data or black-box observation. Extensions use new optional tags or
namespaced files so an original mod remains readable without edits.

