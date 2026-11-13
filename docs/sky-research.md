# Sky rendering research

Research behind the sky rendering (roadmap, M2 special materials), gathered 2026-11-13 for the
owner's request: legacy sky domes and nebulae as the original drew them, and an enhanced mode
designed from current practice in general and in the RTS genre. Summaries are in our words; sources
are listed at the end with whether they are primary (a developer's own talk, paper, or post) or
secondary.

## What legacy draws

Land environments name two dome models (a primary and a "horizon" one, such as `DAY_BLUE_SKY` and
`DAY_BLUE_HORIZON_SKY`); the `Skydome` material lights the dome by every map light through its
turned-around normal, times the vertex color, with a scrolling cloud layer over the base by cloud
alpha x vertex alpha. Space maps draw backdrops: star fields, planets (the `Planet` material), and
`Nebula` meshes, added with a vertex wobble and a brightness that rises steeply where the surface
faces along the view (docs/legacy-shader-mapping.md).

## The RTS camera changes the priorities

At the tactical camera's pitch of about 50 degrees the sky is rarely on screen (map edges, the free
camera), but it lights and colors everything the camera does see. The research ranks techniques by
how much they change what is on screen at that pitch, not by how good a sky they draw.

## Techniques, by value for cost at RTS angles

1. Sky-derived ambient. Render the authored dome (or the space backdrop) into a small cube map at map
   load and project it to second-order spherical harmonics for ambient light, blended with the map's
   authored ambient and fills rather than replacing them (Far Cry 3 derived its ambient from the sky
   this way; Final Fantasy XV used the sky through precomputed transfer). Cheap: done once, or when the
   environment changes. Ringing on sharp features wants a window; the sun stays out of the
   projection. Works in today's display-space lighting as a tint; correct energy needs linear lighting.
2. A sky reflection cube map. The same capture, prefiltered by roughness, replaces the water's flat
   "sky color" where planar reflections find nothing, and can give units a specular reflection
   (Supreme Commander reflects a skybox in its water and scales unit reflections by their specular
   maps). One prefilter at load, one cube sample per pixel.
3. Cloud shadows from the authored cloud texture. Project the dome's scrolling cloud layer down along
   the key light onto the ground and units as a multiplier on the sun only. At RTS angles the most
   visible sky effect, since the camera sees the ground: one texture sample in the forward shaders,
   reusing the dome's scroll. The projection will not match the dome's apparent clouds exactly (not
   noticeable from above); the minimum must be clamped for readability; whether a cloud texture's
   alpha means coverage on every map is a guess to tune. Notably, the TED environments already carry
   cloud fields (a cloud texture, a scale, a speed) that look made for exactly this.
4. Fog matched to the dome's horizon. Blend the map's fog color toward the captured horizon band in
   the view direction, hiding the seam between far terrain and sky at map edges and in the free
   camera; a blend weight, since maps authored fog and dome separately.
5. A sun disk and glare on the key light's direction. Trivial, but rarely on screen at RTS pitch; some
   domes paint their own sun (two suns), and in display-space lighting a disk plus bloom looks flat:
   wants HDR first.
6. Space: the backdrop as light. Capture the backdrop (stars, nebulae, planets) for ambient and ship
   reflections as in 1 and 2 (desaturated: additive nebulae tint strongly), and optionally a sparse,
   pixel-stable star layer under the authored nebulae (stars must not shimmer under TAA). Whether
   Homeworld lit ships from its backgrounds is folklore here: not confirmed.
7. A physically based atmosphere (Hillaire 2020, the method behind Unreal's sky atmosphere) as a tint
   layer: its aerial-perspective volume as a principled height fog, a sky for maps without domes.
   Needs physical units, linear lighting, and exposure: after HDR, and only as a blend over the
   authored look.

## What to skip

- Volumetric clouds (Horizon Zero Dawn's Nubis, about 2 ms on PS4): made for first-person horizons;
  from above the shadows matter, not the clouds, and clouds between camera and ground hide the game.
- Bruneton's precomputed scattering: superseded by Hillaire's method.
- Re-capturing the sky every frame: the authored skies are static apart from a cloud scroll.
- Ray-marched volumetric nebulae (Homeworld 3) and HDR skyboxes that replace backdrops: costly, and
  they discard the authored art.
- Astronomical night skies: maps carry no time of day.

## Where evidence is thin

No first-party talk on sky rendering was found for Company of Heroes, Age of Empires IV, StarCraft II,
Planetary Annihilation, Sins of a Solar Empire II, or Homeworld 3. Confirmed: Supreme Commander's
skybox reflections (a frame analysis), Total War's mid-to-far terrain focus, Homeworld 2's
vertex-colored background spheres; a Sins II developer reply suggests its skybox drives ambient. The
RTS ranking is a judgment from the camera's geometry and the general literature. Hillaire's lookup
table sizes and timings were recalled, not read.

## Proposed plan (for the owner)

Legacy (done 2026-11-13): the dome lit as the Skydome material, its clouds, the nebula wobble and
brightness. Enhanced, before HDR: (a) cloud shadows from the TED environment's cloud layer, (b) a
capture of the dome or backdrop to a cube map at load, used for the water's reflection fallback and a
sky-tinted ambient blend, (c) fog toward the horizon color. After HDR: the sun disk, star intensity,
and the atmosphere's aerial perspective as a blend. Every weight is an engine choice.

## Sources

Primary: Hillaire 2020 (https://diglib.eg.org/handle/10.1111/cgf14050; slides
https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf);
Bruneton and Neyret 2008 (https://diglib.eg.org/items/0af15b7c-795a-4cca-97d3-4cc9b99126e4);
Hosek and Wilkie 2012 (https://cgg.mff.cuni.cz/projects/SkylightModelling/HosekWilkie_SkylightModel_SIGGRAPH2012_Preprint.pdf);
Schneider 2015, Horizon Zero Dawn clouds (https://www.advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf);
Nubis Evolved (https://www.guerrilla-games.com/read/nubis-evolved);
Far Cry 3 shading notes (https://blog.selfshadow.com/publications/s2012-shading-course/mcauley/s2012_pbs_farcry3_notes_v2.pdf);
Final Fantasy XV rendering (https://history.siggraph.org/wp-content/uploads/2022/09/2016-Talks-Elcott_Rendering-Techniques-of-Final-Fantasy-XV.pdf);
Frostbite sky and clouds (https://www.ea.com/frostbite/news/physically-based-sky-atmosphere-and-cloud-rendering);
Patry, SIGGRAPH 2021 Advances (https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html);
Animated skybox lighting, SIGGRAPH 2006 (https://advances.realtimerendering.com/s2006/Chapter2-Animated_skybox_rendering_and_lighting_techniques.pdf);
Total War engine anatomy (https://gpuopen.com/learn/anatomy-total-war-engine-part/).

Secondary: Courrèges, Supreme Commander graphics study
(https://www.adriancourreges.com/blog/2015/06/23/supreme-commander-graphics-study/); Homeworld 2
backgrounds (https://simonschreibt.de/gat/homeworld-2-backgrounds); Sins II dark skybox thread
(https://steamcommunity.com/app/1575940/discussions/1/4692280123603294267); Homeworld 3 technology
test (https://www.pcgameshardware.de/Homeworld-3-Spiel-6460/Specials/Technik-Test-1446655/2/); cloud
shadow examples (https://github.com/EntroPi-Games/Unity-Cloud-Shadows/,
https://www.gamedev.net/blogs/entry/2264237-cloud-shadows-on-a-terrain/).
