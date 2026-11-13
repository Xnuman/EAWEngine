# Heat distortion research

Research behind the heat distortion (roadmap, M2 special materials), gathered 2026-11-13 for the
owner's request: modern approaches in general and in the RTS genre, to design an enhanced heat
mode beside the legacy one. Summaries are in our words; sources are listed at the end with whether
they are primary (a developer's own talk, documentation, or post) or secondary.

## What the engine does now (legacy)

Heat particles (170 stock emitters: engine exhaust, explosion shimmer) blend their bump textures by
opacity into an 8-bit heat buffer cleared to "no shift", depth tested against the opaque scene; the
scene is copied and redrawn shifted by the buffer, 1.5 % of the screen height at full bend (an
engine choice), after the transparent part and before bloom and anti-aliasing. This is the design
most shipped engines use (one accumulated screen buffer applied once: Jericho, Batman: Arkham
Knight, Unreal Engine 3, Dishonored 2, Quantum Break, Doom 2016). The legacy profile keeps it.

## Where better engines differ, by value for cost at RTS distances

1. Rejecting foreground samples. The classic artifact is "leaking": a sample shifted onto something
   in front of the heat drags that object into the distortion (Half-Life 2, Batman, Doom show it;
   Guild Wars documents it). The fix reads the scene depth at the shifted position and falls back to
   the unshifted one where the sample is nearer than the heat, fading the strength as the depth
   difference gets small so no hard edge shows (Godot's refraction, Crytek's GPU Gems 2 chapter with
   an alpha mask; Unreal exposes a refraction depth bias). Here: fighters crossing a capital ship's
   exhaust, units in front of explosions. One depth read; TAA's jitter wants a bias (Unity HDRP
   turns its distortion depth test off under jittered depth). Our water refraction already does this.
2. Signed accumulation. Blending over a neutral 0.5 makes overlapping heat replace rather than add,
   and 8 bits give steps of about 1.3 pixels at 1080p at full strength (stepping in faint shimmer). A
   two-channel 16-bit float buffer cleared to 0 with additive blending fixes both at the same four
   bytes per pixel (Quantum Break uses RG16F; Unreal 5 and Dishonored 2 RGBA16F; Doom RGBA8). The
   8-bit neutral's constant drift was fixed in the legacy shader already.
3. Strength by distance. A fixed screen fraction makes a small hot spot swim as much zoomed out as
   zoomed in; enhanced defines the shift in world units, divides by view depth, and caps the pixel
   shift (standard practice by inference; no single source states it).
4. Placement against temporal anti-aliasing. Heat writes no motion, so TAA's history reprojects the
   distorted image with the wrong motion and averages away the shimmer that makes heat read. Doom
   applies its distortion in the final post pass, after the temporal resolve; the alternative is to
   read motion at the shifted position. With HDR later: in linear space, before tone mapping.
5. Animated shimmer: a second scrolling noise or normal layer over the particle's own, faded by age
   and opacity, kept low: constant shimmer over units blurs their outlines at RTS height (inference).
6. Screen edges: mirror or fade the shift near the borders rather than clamping, which smears the
   edge pixels (Quantum Break mirrors). Supreme Commander hid its refraction's edge bleeding behind a
   thin interface border.
7. Shockwave rings (an expanding ring with a radial normal texture in the same buffer) read well in an
   RTS (Beyond All Reason's 2025 update, scaled by weapon power), but they add visuals the original
   content never had: an engine extension behind metadata or an option, not a default.
8. A half-resolution buffer only if profiling asks for it (Doom and Dishonored 2 use low resolution);
   it needs depth-aware upsampling or units get halos.

## What to skip

- Chromatic aberration in the distortion: three samples, reads as a lens effect, fringes units.
- Blurred ("rough") distortion (HDRP's color pyramid, Doom's blur chain): costly, blurs units.
- Copying the scene before each heat draw (Half-Life 2, Crysis 3): bandwidth for layering that barely
  matters at RTS distance.
- Ray-traced refraction.

## Where evidence is thin

No developer commentary on heat distortion was found for StarCraft II, Supreme Commander, Company of
Heroes, Planetary Annihilation, Sins of a Solar Empire, Homeworld, or Age of Empires IV. The only RTS
source is Beyond All Reason (shockwaves and thruster haze; about 5-10 % of frame rate for its whole
lighting and distortion update; nothing on readability). The readability advice and the distance
scaling are reasoning, not shipped evidence; some details (Flax, Godot, Unreal 4's buffer format) were
read through search excerpts only.

## Plan (agreed with the owner, 2026-11-13; done the same day, item 4 as a TAA reactive mask)

Enhanced heat, opt-in beside the legacy one: (1) foreground rejection with a soft depth fade, (2) a
signed RG16F buffer with additive accumulation, (3) the shift defined in world units and capped in
pixels, (4) applied after temporal anti-aliasing when TAA is on, (5) a faint animated shimmer layer,
(6) faded screen edges. Shockwave rings as an option or extension later. Every constant an engine
choice.

## Sources

Primary: Unreal refraction documentation (https://dev.epicgames.com/documentation/unreal-engine/using-refraction-in-unreal-engine);
Unity HDRP frame settings (https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@15.0/manual/Frame-Settings.html);
Sousa, GPU Gems 2 chapter 19 (https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation);
Godot refraction depth fix (a mirror: https://git.objectionable.solutions/Sara/behaviour-tree-test/commit/8a485ff658a9ad03dfc6bc07d0f350a97a706719);
Beyond All Reason, Lightbringer update (https://www.beyondallreason.info/news/lightbringer-update);
EVE Online Crucible trails (summary only: https://www.engadget.com/2011/11/25/eve-online-shows-off-new-crucible-engine-trails/).

Secondary: Courrèges, Doom 2016 graphics study (https://www.adriancourreges.com/blog/2016/09/09/doom-2016-graphics-study/);
Courrèges, Supreme Commander graphics study (https://www.adriancourreges.com/blog/2015/06/23/supreme-commander-graphics-study/);
Piquet (Froyok), Refracting Pixels (https://www.froyok.fr/blog/2024-12-refraction);
Arm community on Unreal 4's distortion passes (https://community.arm.com/support-forums/f/graphics-gaming-and-vr-forum/51261/pixel-local-storage-cannot-reduce-bandwidth-in-my-app/173613);
Flax Engine forward shader (search excerpt only: https://gitlab.flaxengine.com/Flax/FlaxEngine/raw/branch/1.13/Source/Shaders/Forward.shader);
Guild Wars wiki, post-processing (https://wiki-en.guildwars.com/wiki/Postprocessing_effects).
