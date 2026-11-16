# Water rendering research

Research behind the water rendering (roadmap, M2 special materials): legacy water as the original
drew it, and an enhanced mode designed from current practice in general and in the RTS genre, at
the owner's request. Summaries are in our words; sources are listed at the end with whether they
are primary (a developer's own talk, paper, blog, or forum post) or secondary.

## What legacy water has to work from

The published legacy shaders contain no water shader: the original drew its water inside the
engine. Legacy water therefore comes from the map data and from observations of the game. A TED map
gives one flat plane per map at a fixed height, its kind (water, lava, ice), a color, and its
textures (a bump map and a surface texture; `scene_terrain.cpp`, `append_water`). What the original
did with them (scroll rates, how the bump map bends the surface, transparency, reflections) is not
established and needs comparison captures from the game. The TED water fields 0x17-0x22 (apparently
opacity, tiling, scroll; a guess) are preserved raw but not interpreted; the owner will read their
labels in the map editor.

Until then legacy water is engine choices: the water blended at 75% opacity over its color, the bump
map lighting it with the full key-light highlight, one texture repeat per 200 world units, slow
fixed scroll speeds (`append_water`). In both modes a broad second layer of the bump map and base
color, at an unrelated angle and scale, keeps a grid from lining up across a wide surface
(`WATER_BROAD_*` in `sandbox.hlsl`). Lava and ice are single flat quads; lava is unlit.

## The RTS camera changes the priorities

At the tactical camera's pitch of about 50 degrees, the view meets the water about 40 degrees off
its normal. With water's base reflectance of about 0.02, Schlick's approximation gives a reflectance
of about 2%, rising to about 6% at the top of the view (our calculation, not a source's). Mirror
reflections of the scene therefore add almost nothing at RTS angles; what reads is the light coming
through the surface (color by depth, refraction), the shoreline, and the sun's glint. The free
camera at grazing angles is the exception, where reflection grows quickly; it needs a cheap
stand-in rather than an expensive main path.

## Techniques, by value for cost at RTS angles

1. Color by depth (Beer-Lambert absorption). The depth of what lies under the surface comes from the
   depth buffer, or exactly from the plane height minus the terrain height since the plane is flat;
   the scene behind fades toward the water color exponentially with it. Supreme Commander applies a
   depth ramp to the terrain colors between surface and abyss heights. Highest value: it shows depth
   and keeps units in shallow water readable. Lava wants a short opaque distance; ice a scattering
   tint. Cost: one depth read.
2. Refraction from a copy of the opaque scene. Copy scene color and depth before water draws; sample
   them offset by the surface normal. Offsets can pick up objects in front of the water: detect a
   negative depth difference, fall back to the unoffset sample, and scale the offset by the water
   depth so edges stay soft. Cost: one copy (half resolution is possible) and a few samples.
3. Shoreline: fade in transparency and refraction over the first units of depth, plus scrolling foam
   masked by shallow depth. The 0 A.D. developers found depth alone wrong on shallow maps and used
   the terrain slope too (breakers on steep shores, none on flat shallows), with the shore's
   direction from the blurred terrain normal. Very high value at RTS distance, low cost. With a
   fixed plane and static terrain, a per-map shore-distance texture can be computed once; units
   standing in water would otherwise ring themselves with foam.
4. Surface detail: two or three normal-map layers scrolling at different scales and directions, as
   Supreme Commander does; optionally flow maps (Valve's Portal 2 flow maps cost two extra
   samples). Stock maps have no flow data; deriving flow from the shore distance is our inference.
   Fade normal strength with distance against shimmer under TAA.
5. Sun glint and a sky reflection weighted by Fresnel. A sharp sun highlight on the bent normals and
   a cube map or sky color; most of the remaining wet look at steep angles. The glint sparkles and
   aliases under TAA: clamp it or widen it with distance, and mind bloom.
6. Caustics (optional): an animated caustic texture projected onto submerged terrain in the terrain
   shader, faded with depth (OpenSAGE does this; players report shallow-water caustics in Age of
   Empires IV). Fits beside the terrain decals.
7. Units in water: wakes and ripples as particles or decals on the plane (the trail and particle
   systems exist); a richer ripple height field simulated around the camera is common in tutorials,
   with no RTS developer source found. Presentation only, so determinism is untouched.
8. Screen-space planar reflections, an enhanced extra that matters most for the free camera: Ghost
   Recon Wildlands mirrors depth across the known plane in a compute pass (0.3-0.4 ms at quarter
   resolution on consoles). At 2-6% Fresnel it is barely visible at RTS pitch.

## With temporal anti-aliasing (forward renderer)

The water plane is static, so it writes its own motion (camera motion only) and depth. Refracted and
reflected content moves differently from the surface, so one motion vector cannot be right for both
(Marrs et al.); either accept a slight lag in refraction or mark water and tighten the history clamp
there. Animated normals and specular are the main shimmer source: damp high frequencies. Draw order:
opaque scene, scene copy, water, then the transparent passes (as Crest does).

## What to skip

- FFT (Tessendorf) ocean spectra: made for open ocean seen up close; at RTS distance a few normal
  layers look the same, without compute passes and extra TAA care.
- Gerstner displacement and tessellation on all water: invisible from above, and they break the flat
  plane that depth, foam, shore precomputation, and gameplay heights rely on. The owner chose ocean
  waves for open-sea maps nonetheless; how they keep the flat plane is in
  [Ocean waves](#ocean-waves).
- Planar reflection by rendering the scene again: 0 A.D. measured water at about 7 ms at maximum
  zoom, about 5 ms of it reflection and refraction; poor value at 2% Fresnel. If anything, reflect
  units only, as Supreme Commander does.
- Underwater volumes and subsurface simulation: the camera never goes under water; a depth-tinted
  scattering color covers the look.

## Where evidence is thin

No first-party talk on water was found for Company of Heroes, Total War, StarCraft II, Planetary
Annihilation, Northgard, Anno, Homeworld, They Are Billions, or Warcraft III Reforged. The RTS
evidence is Supreme Commander (a third-party frame analysis and the FAF community), 0 A.D. (its
developers' forum), OpenSAGE (a reimplementation), and an Age of Empires IV mapping guide (read only
through search excerpts). The ranking is a judgment on that evidence plus the Fresnel calculation.

## Enhanced water (the engine's choices)

Agreed with the owner and implemented as `VisualWaterMode::enhanced` (`--water enhanced`, or the
Graphics panel); `water_ps` in `sandbox.hlsl`. All constants are engine choices, tuned by eye.

- Color by depth: the view ray's path through the water, from the opaque depth, hides the bottom by
  Beer-Lambert absorption (`WATER_CLARITY`: 20 units hide 63%).
- Shore: the surface fades in over the first 3 units of depth (`WATER_SHORE_FADE`).
- Normals: the map's bump map plus a second, finer, turned layer drifting another way
  (`WATER_DETAIL_*`), besides the broad layer both modes share.
- Refraction from a copy of the opaque scene, shifted by the bent normal (2% of the screen height,
  growing to full over 4 units of depth); where the shifted pixel shows something above the water,
  the unshifted one is used.
- Shore foam within 6 units of depth, stronger over a steep bottom (a flat bottom gets 35%), in
  rolling lines 2.5 units of depth apart, broken up by the normal layers.
- Fresnel (Schlick, base reflectance 0.02) toward the reflection, and a sharp sun glint (exponent
  256).
- Reflections are screen-space, never a second scene render: planar screen-space reflections
  (`water_reflections.hlsl`, the Ghost Recon Wildlands technique written anew: every opaque pixel
  above the plane is mirrored across it, and among several landing on one pixel the lowest on screen
  wins), with the captured sky ([sky-research.md](sky-research.md)) as the fallback where the screen
  has nothing, or a color from the map's lights without a capture. Legacy mode has no reflections
  (no stock material uses the reflective profiles). General screen-space reflections come with the
  enhanced lighting step, and water then moves onto them.

Open: the water's constants as Graphics-panel sliders (only the waves have them), caustics under
shallow water, wakes from the trail system.

## Ocean waves

On open-sea maps the enhanced water is an ocean (the roadmap's item h). All values are engine
choices.

- The water grid. Water is drawn as a grid of cells of about 12 units, at most 512 a side
  (`water_cell_size`, `water_max_cells` in `scene_terrain.cpp`), whose vertices keep the depth
  beneath them (level less ground height, in the second texture coordinates' x). Lava and ice stay
  one quad. The mesh's bounds grow by `water_wave_reach` (40 units: the default sea's significant
  height near 7, twice that at the largest height scale, with room for higher crests).
- The waves are the FFT ocean's ("The ocean plan" below): its two long cascades move the grid and
  bend its normal (`ocean_fft` in `sandbox.hlsl`).
- The flat plane survives. The waves fade in from none at the calm depth to full at the full depth,
  so shores, shore foam, and units in the shallows see the flat plane, which the simulation keeps
  too. A shelf in the sea floor can show as a sharp line between calm water and waves.
- Crest foam. Foam gathers where a cascade folds and fades by a half-life (stage 3 of "The ocean
  plan"); a fine value noise of about 7-unit cells breaks it up in world space (`ocean_whitecaps`).
- Sliders. With enhanced water the Graphics panel tunes the ocean (`VisualOceanWaves` in
  `d3d12_renderer.hpp`): the sea state (wind speed 32 units a second, fetch 600,000 units, swell 0),
  choppiness (0.8), a height scale (1, at most 2), the calm and full depths (0 and 10, the owner's
  earlier tuning), the Jacobian below which crests foam (0.6), and Defaults; a new sea state uploads
  new starting spectra. A full depth at or below the calm depth makes the waves start at full height
  just past the calm depth.
- Only on ocean maps. Waves run only where the water is open sea, not on lakes, ponds, and rivers
  (the owner's decision). `ocean_map` (`scene_terrain.cpp`, the one place) names the ocean planets
  matched in the map's file name, Kamino and Manaan as the owner chose, and sets
  `VisualLightRig::ocean`; elsewhere the water lies flat without crest foam. A measure cannot decide
  it: by the share of the map under water, the multiplayer Dagobah swamp (57% at least 5 units deep)
  sits beside Kamino (60%, none 20 deep) and Mon Calamari (61%). The waves setting is ocean maps
  (default), on, or off. Test: `test_ocean_maps`.

The owner's verdict: the ocean needs a lot more work (strong tiling and patterns). The plan below
follows from research into how games build oceans.

## The ocean plan

### What causes the tiling

- Four fixed Gerstner waves are a four-line spectrum: their crests interfere in a pattern that
  repeats, which the eye reads as a lattice whatever their tuning. GPU Gems 1 (Finch, 2004) already
  paired four vertex waves with about fifteen more in the normals, with randomized parameters;
  Unreal's water plugin sums sixteen spectrum-sampled waves by default.
- The three normal-map layers (repeats at 1, 2.7, and 0.19 times the base's 200 units) keep their
  full strength at every distance, so from an RTS camera hundreds of repeats show at once.
- The crest foam is computed per frame from the same four waves and has no memory, so it can only
  move the pattern, not break it.

### What shipped oceans do

- Several small FFT cascades instead of one patch or a few waves: War Thunder (2015) four from 5 m
  to 1 km, Atlas (GDC 2019) four bands, Unity HDRP three, Crest moved from Gerstner waves to an
  FFT; the patch sizes share no common factor, so the cascades' repeats never line up. One large
  FFT patch tiles visibly when the view spans a few patches (Tessendorf, "Simulating Ocean Water",
  2002): the RTS camera is the worst case.
- An empirical spectrum: JONSWAP, TMA for finite depth, with Hasselmann's directional spreading and
  a swell term (Horvath, "Empirical directional wave spectra for computer graphics", DigiPro 2015);
  the Phillips spectrum spreads energy too evenly across wavelengths and looks wrong. Atlas uses a
  local wind sea plus a separate swell.
- Normals, slopes, and a foam signal from the same data: the FFT's derivatives, and the Jacobian of
  its horizontal displacement (folding where it drops below a threshold, 0.3-0.5 in War Thunder).
- Foam in a texture that persists: energy injected where the surface folds, faded and blurred each
  update (Crest at 30 Hz, Atlas, Sea of Thieves), used as a mask over a detail texture; distant
  whitecap coverage filtered from the Jacobian's mean and variance (Dupuy and Bruneton 2012).
- Detail fades with distance: each band leaves when its wavelength nears the pixel size, and its
  slopes move into the specular roughness (Bruneton et al. 2010; Toksvig; LEAN mapping); Atlas fades
  a band's displacement from about thirty wave periods away.
- Costs: Atlas's wave simulation 0.5 ms on 2019 GPUs; four 256-square cascades about 0.1 ms on a
  2023 GPU (one 2026 measurement); the cost that matters is the surface's geometry and shading.
- No strategy game publishes its ocean (Supreme Commander's, per an outside frame analysis, is one
  quad with scrolling normal maps), so how well this suits a camera high above is reasoned, not
  observed.

The full report, with sources, is a local research file (`out/reports/Ocean waves in games.md`,
not committed); this section keeps what the engine acts on.

### The stages

Decided by the owner: enhanced waves stay on ocean maps (lakes and rivers need another approach);
legacy maps keep the engine's own table of ocean maps (`ocean_map`), and a future map editor of the
engine's would mark water kinds by brush.

1. Normal detail fades with distance on open sea: each layer fades as its repeat shrinks on screen,
   and the faded slopes widen the glint (`water_layer_presence` in `sandbox.hlsl`). Done with
   stage 2; alone it left the normal maps' broad layer's own pattern showing, so on open sea the
   cascades now make the normal instead of the normal maps.
2. Three FFT cascades replace the four Gerstner waves on ocean maps (status.md, "Ocean waves"):
   done, with the sea-state setting in the Graphics panel. First result on
   Kamino: no repeat or lattice anywhere, soft irregular swells, faint foam where crests fold; at the
   default sea state (peak wavelength about 300, significant height about 7) the slopes are gentle
   and the surface reads calm from above.
3. Foam that builds up and fades, injected from the cascades' Jacobian, in place of the procedural
   crest patches: done (status.md, "Ocean waves").
4. Each map's depth beneath the water and distance to the coast, baked at load, so waves die down
   over shelves by their wavelength instead of by a fixed depth.
5. The rest: the water grid culled by tiles, wave motion in the motion vectors if TAA ghosts, ocean
   maps from metadata for new content, one sea-state control.

## Sources

Primary: Vlachos, Water Flow in Portal 2, SIGGRAPH 2010
(https://www.advances.realtimerendering.com/s2010/Vlachos-Waterflow(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf);
Genin, Screen Space Planar Reflections in Ghost Recon Wildlands
(https://remi-genin.github.io/posts/screen-space-planar-reflections-in-ghost-recon-wildlands);
Wildfire Games forum, Of water and waves (https://wildfiregames.com/forum/topic/16257-of-water-and-waves/page/2);
OpenSAGE, Water rendering (https://opensage.github.io/blog/water-rendering);
Crest technical notes (https://crest.readthedocs.io/en/4.9/user/technical-information.html);
Age of Empires IV, Ultimate Water Guide (https://support.ageofempires.com/hc/en-us/articles/6588308453396-Ultimate-Water-Guide);
Marrs et al., Improving Temporal Antialiasing with Adaptive Ray Tracing
(https://research.nvidia.com/sites/default/files/pubs/2019-03_Improving-Temporal-Antialiasing//Marrs2019_Chapter_TemporalAntialiasingWAdaptiveRays.pdf);
Finch, GPU Gems chapter 1 (https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models).

Secondary: Courrèges, Supreme Commander graphics study
(https://www.adriancourreges.com/blog/2015/06/23/supreme-commander-graphics-study/);
FAF forum, water ramps (https://forum.faforever.xyz/topic/65/about-water-water-ramps);
Catlike Coding, Looking Through Water (https://catlikecoding.com/unity/tutorials/flow/looking-through-water/);
Unreal forums on TAA ghosting with single-layer water
(https://forums.unrealengine.com/t/singlelayerwater-and-temporal-aa-ghost-effect/2677186);
Age of Empires IV forum on caustics (https://forums.ageofempires.com/t/updated-visuals-appreciation/239462).
