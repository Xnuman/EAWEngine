# Water rendering research

Research behind the water rendering (roadmap, M2 special materials), gathered 2026-11-13 for the
owner's request: legacy water as the original drew it, and an enhanced mode designed from current
practice in general and in the RTS genre. Summaries are in our words; sources are listed at the end
with whether they are primary (a developer's own talk, paper, blog, or forum post) or secondary.

## What legacy water has to work from

The published legacy shaders contain no water shader: the original drew its water inside the engine.
Legacy water therefore comes from the map data and from observations of the game. A TED map gives
one flat plane per map at a fixed height, its kind (water, lava, ice), a color, and its textures (a
bump map and a surface texture; `scene_terrain.cpp`, `append_water`). What the original did with
them (scroll rates, how the bump map bends the surface, transparency, reflections) is not yet
established and needs comparison captures from the game.

## The RTS camera changes the priorities

At the tactical camera's pitch of about 50 degrees, the view meets the water about 40 degrees off its
normal. With water's base reflectance of about 0.02, Schlick's approximation gives a reflectance of
about 2 %, rising to about 6 % at the top of the view (our calculation, not a source's). Mirror
reflections of the scene therefore add almost nothing at RTS angles; what reads is the light coming
through the surface (color by depth, refraction), the shoreline, and the sun's glint. The free
camera at grazing angles is the exception, where reflection grows quickly; it needs a cheap stand-in
rather than an expensive main path.

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
   direction from the blurred terrain normal. Very high value at RTS distance, low cost. With a fixed
   plane and static terrain, a per-map shore-distance texture can be computed once; units standing
   in water would otherwise ring themselves with foam.
4. Surface detail: two or three normal-map layers scrolling at different scales and directions, as
   Supreme Commander does; optionally flow maps (Valve's Portal 2 flow maps cost two extra samples).
   Stock maps have no flow data; deriving flow from the shore distance is our inference. Fade normal
   strength with distance against shimmer under TAA.
5. Sun glint and a sky reflection weighted by Fresnel. A sharp sun highlight on the bent normals and
   a cube map or sky color; most of the remaining wet look at steep angles. The glint sparkles and
   aliases under TAA: clamp it or widen it with distance, and mind bloom.
6. Caustics (optional): an animated caustic texture projected onto submerged terrain in the terrain
   shader, faded with depth (OpenSAGE does this; players report shallow-water caustics in Age of
   Empires IV). Fits beside the terrain decals.
7. Units in water: wakes and ripples as particles or decals on the plane (the trail and particle
   systems exist); a richer ripple height field simulated around the camera is common in tutorials,
   with no RTS developer source found. Presentation only, so determinism is untouched.
8. Screen-space planar reflections only as an enhanced extra for the free camera: Ghost Recon
   Wildlands mirrors depth across the known plane in a compute pass (0.3-0.4 ms at quarter resolution
   on consoles). At 2-6 % Fresnel it is barely visible at RTS pitch.

## With temporal anti-aliasing (forward renderer)

The water plane is static, so it writes its own motion (camera motion only) and depth. Refracted and
reflected content moves differently from the surface, so one motion vector cannot be right for both
(Marrs et al.); either accept a slight lag in refraction or mark water and tighten the history clamp
there. Animated normals and specular are the main shimmer source: damp high frequencies. Draw order:
opaque scene, scene copy, water, then the transparent passes (as Crest does).

## What to skip

- FFT (Tessendorf) ocean spectra: made for open ocean seen up close; at RTS distance a few normal
  layers look the same, without compute passes and extra TAA care.
- Gerstner displacement and tessellation: invisible from above, and they break the flat plane that
  depth, foam, shore precomputation, and gameplay heights rely on.
- Planar reflection by rendering the scene again: 0 A.D. measured water at about 7 ms at maximum zoom,
  about 5 ms of it reflection and refraction; poor value at 2 % Fresnel. If anything, reflect units
  only, as Supreme Commander does.
- Underwater volumes and subsurface simulation: the camera never goes under water; a depth-tinted
  scattering color covers the look.

## Where evidence is thin

No first-party talk on water was found for Company of Heroes, Total War, StarCraft II, Planetary
Annihilation, Northgard, Anno, Homeworld, They Are Billions, or Warcraft III Reforged. The RTS
evidence is Supreme Commander (a third-party frame analysis and the FAF community), 0 A.D. (its
developers' forum), OpenSAGE (a reimplementation), and an Age of Empires IV mapping guide (read only
through search excerpts). The ranking is a judgment on that evidence plus the Fresnel calculation.

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
