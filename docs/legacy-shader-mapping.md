# Legacy shader mapping

This document records how the original Alamo Direct3D 9 shader sources map onto
engine-native material data. Petroglyph published them with the official Empire
at War mod tools ([petroglyphgames.com/eawmodtool](https://www.petroglyphgames.com/eawmodtool/)); contributors keep a
local copy under `Reference Materials/Shaders/`, which is git-ignored.

The profiles describe how a model was meant to render in Alamo. They drive the
first render target, legacy mode, which approximates the original appearance
with newly written shaders. A later physically based path will use engine-native
materials and convert these profiles into them. It is the evidence behind the
reference-profile table in `src/assets/src/legacy_material.cpp`; keep the two in sync.

The corpus is a reference. Nothing here is compiled, embedded, or shipped, and
the entries below describe observed behavior in engine terms rather than
reproducing legacy code. See [Compatibility research](compatibility-research.md)
for the boundary.

## Corpus inventory (observed 2026-11-07)

- 39 readable effect sources (`.fx`), 15 shared headers (`.fxh`), and two
  compiled effects (`.fxo`).
- `MeshBumpLight.fx` is not source: it begins with a compiled-effect header and
  is treated as opaque binary evidence, like the `.fxo` files. Neither is
  executed or disassembled.
- `AlamoEngine.fx` only declares engine-global parameters (matrices, lights,
  fog, fog-of-war, wind, time) and is not a material.
- `AdditiveBloom.fxh` is not included by any effect in the corpus.
- Every source file carries a "confidential, do not distribute" header. The
  directory is local-only reference material: it is git-ignored and must never
  be committed, packaged, or published.

Every effect declares metadata the legacy engine consumed: a render phase
(`Opaque`, `Transparent`, `TerrainMesh`, `Heat`, `Shadow`, `Occluded`), a
vertex-processing path (`Mesh`, `RSkin`, `ShadowVolume`, `Grass`), a vertex
layout, tangent-space and shadow-volume flags, and sometimes a depth-sort flag.
Techniques are ordered by hardware tier (DX9, DX8, fixed function, plus a 3ds Max
viewport preview). Only the highest programmable tier is mapping evidence;
fixed-function and Max techniques are ignored.

## Stock usage

Across the 1,379 semantic stock FoC/EAW models (9,294 material instances), 31
of the 35 shader names have reference profiles. These cover 7,808 instances (84%).
The remaining 1,486 instances use names outside the corpus and keep the
heuristic fallback, reported as `name-heuristic`:

| Shader | Uses | Status |
| --- | ---: | --- |
| `alDefault.fx` | 1,461 | No source; treated as `default-surface`, opaque. |
| `Default.fx` | 21 | No source; treated as `default-surface`, opaque. |
| `MeshAdditiveReflection.fx` | 3 | No source; name implies additive. |
| `FissureMeshGloss.fx` | 1 | No source; name implies vertex-lit gloss. |

Run `EAWAssetInspector texture-audit Data/Art/Models --game foc` to reproduce
the source, phase, lighting, and heuristic-only counts.

## Shared observations

These apply to all lit families unless an entry says otherwise.

- **Lighting.** Ambient/fill light is a second-order spherical-harmonic
  irradiance term evaluated per vertex. On top of that sits one directional
  key light. Normal-mapped families evaluate the key light per pixel in tangent
  space and use only the fill term per vertex. Vertex-lit families use the
  combined term per vertex.
- **Modulation.** Diffuse is texture × lighting × 2 (a legacy "2x modulate"
  convention), computed on gamma-space texels. Specular is added after the
  diffuse term.
- **Specular exponent.** The key-light highlight uses a fixed exponent of 16.
  The `Shininess` material parameter is declared everywhere but never read.
- **Material colors.** `Diffuse`, `Specular`, and `Emissive` scale the
  corresponding terms; emissive is added to vertex diffuse before texturing.
- **Engine fade.** The engine supplies a light-scale color whose RGB dims or
  highlights and whose alpha fades the object. Most opaque families enable
  `SRCALPHA/INVSRCALPHA` blending only while that alpha is below 1
  (`fade_blend`). Additive families premultiply their color by it instead.
- **Colorization.** Wherever implemented, the team-color mask is the **base
  texture alpha**. The surface color is a lerp from base to base × colorization
  color. The `GlossColorize` header comment says the mask comes from a separate
  texture, but the code uses base alpha; the code is taken as evidence.
- **Fog.** Fog is linear in view-space distance. Additive, shield, heat, sky,
  and nebula effects disable it.
- **Rigid skinning (`RSkin*`).** These variants use one bone per vertex, with
  the palette index carried in the normal's fourth component. Positions and
  normals are transformed into world space before the shared shading. Texture
  coordinates are stored as integers scaled by 4096. The 24-matrix palette is
  a Direct3D 9 constant-register limit, not an asset-format limit.
- **Samplers.** Trilinear filtering with wrap addressing throughout. The cube
  reflection and distortion samplers use defaults.
- **Fog of war and cloud shadows.** Batch, grass, and terrain families
  multiply the result by a fog-of-war texture, and terrain also by a
  cloud-shadow texture. Both are sampled with world-space planar projections
  supplied by the engine.

## Family mapping

`opaque` means depth write on, depth test less-equal, back-face culling (the
Direct3D 9 default) unless stated. Confidence is **high** when behavior is read
directly from the source's programmable technique, and **medium** when it relies
on interpretation of engine-supplied inputs.

### Vertex-lit and normal-mapped surfaces (family `mesh`)

| Legacy | Engine features | Confidence and differences |
| --- | --- | --- |
| `MeshGloss`, `RSkinGloss` | opaque, fade blend, vertex-lit; gloss = base alpha | High. |
| `MeshGlossColorize`, `RSkinGlossColorize` | as gloss; colorize (base alpha); gloss = `GlossTexture` red | High. |
| `MeshBumpColorize`, `RSkinBumpColorize` | opaque, normal-mapped, tangent frame; colorize; gloss = normal-map alpha; constant `UVOffset` | High. DX9 tier has a per-pixel highlight; DX8 tier uses vertex specular. |
| `MeshBumpReflectColorize`, `RSkinBumpReflectColorize` | as bump colorize, plus engine sky-cube reflection scaled by `Specular` and normal-map alpha | Medium: the cube comes from the engine environment, not the material. |
| `BatchMeshGloss` | as gloss; fog of war; distance fade | High. Blending is always enabled, but output alpha is only the fade, so it is opaque unless faded. |
| `FSAMeshGlow` | opaque, vertex-lit; base alpha masks an **unlit** `Specular` color (self-illumination) | High for the math; the "glow" name suggests a bloom role not visible in the source. |

### Alpha-blended surfaces

| Legacy | Engine features | Confidence and differences |
| --- | --- | --- |
| `MeshAlpha`, `RSkinAlpha` | alpha blend, no depth write; opacity = base alpha × material/fade alpha; unmasked vertex specular | High. Only `MeshAlpha` declares depth sorting. |
| `MeshAlphaScroll` | as alpha; `UVScrollRate` × time | High. |
| `BatchMeshAlpha` | as alpha; fog of war; distance fade | High. |
| `MeshAlphaGloss`, `RSkinAlphaGloss` | as alpha; gloss = `GlossTexture` red | High. `Colorization` is declared but unused. |

### Additive effects

| Legacy | Engine features | Confidence and differences |
| --- | --- | --- |
| `MeshAdditive`, `RSkinAdditive` | `ONE/ONE`, no depth write, unlit; `Color` × fade; `UVScrollRate` × time | High. |
| `MeshAdditiveVColor`, `RSkinAdditiveVColor` | as additive, but vertex color replaces `Color` | High. |
| `MeshAdditiveOffset` | as additive; constant `UVOffset` instead of scrolling | High. |

### Special families

| Legacy | Family | Engine features | Confidence and differences |
| --- | --- | --- | --- |
| `MeshShield` | `shield` | `ONE/ONE`, no depth write, **no culling**, depth sorted, unlit | High for state. Result = base (UVs distorted by the distortion map, fixed scale 0.25) × `WaveTexture` × vertex color × `Color`. Vertex color is scaled by saturate(object normal z + `EdgeBrightness`). Each layer has its own UV scale and a V-only scroll. Medium: the `Wave*` and `Distort*` scale/scroll parameters drive each other's texture coordinates, so the observed pairing is preserved rather than the names. |
| `MeshHeat`, `RSkinHeat` | `heat` | Alpha-blends the distortion texture into a full-screen distortion buffer; alpha = vertex alpha; `UVScrollRate` × time | High. `RSkinHeat` declares the Transparent phase rather than Heat; recorded as observed. The sandbox skips heat with a warning until a distortion pass exists. |
| `MeshOccludedUnit`, `RSkinOccludedUnit` | `occluded` | Solid `Color`, alpha blend, depth test **greater**, no depth write, stencil marks drawn pixels | High. Used to show units behind buildings; selected by the engine, not by stock ALO materials. Skipped by the sandbox. |
| `MeshShadowVolume`, `RSkinShadowVolume` | `shadow` | Stencil-only; extrudes back-facing vertices away from the key light; depth-fail and depth-pass variants with two-sided stencil | High. Needs tool-preprocessed volume geometry. A modern shadow technique is expected to replace it; the profile only identifies and excludes it. |
| `BlobStencilMasked` | `selection` | Two passes: alpha-tested (>128/255) depth/stencil mask, then a blended pass masked by stencil | High. Not referenced by stock ALOs. |

### Environment and world

| Legacy | Family | Engine features | Confidence and differences |
| --- | --- | --- | --- |
| `Planet` | `planet` | opaque, fade blend, tangent frame, `planet-atmosphere` lighting | Medium. A rim term fades toward `Atmosphere` with exponent `AtmospherePower`. The cloud layer is lit by its own normal map and lerped over the surface, scrolling along U at `CloudScrollRate`. Specular is masked by normal-map alpha. City lights (`CityColor`) are masked by base alpha and shown on the night side. The header comment swaps those two alpha roles; the code is taken as evidence. |
| `Skydome` | `sky` | No depth write, no blending, `sky-dome` lighting (inward normals), vertex color | High. The cloud layer is lerped by cloud alpha × vertex alpha, scaled by `CloudScale`, and scrolled along U. |
| `Nebula` | `nebula` | `ONE/ONE`, no culling, no depth write, sorted, unlit; vertex color; scroll at 3× `UVScrollRate` | Medium. Vertices wobble sinusoidally in object space (`DistortionScale`, `SFreq`, `TFreq`), and brightness rises steeply as the view-space normal faces the camera. |
| `TerrainMeshBump` | `terrain` | TerrainMesh phase, opaque, normal-mapped; gloss = base alpha; fog of war and cloud shadow | High. Where the key light faces away, a quarter of the absolute N·L still lights the surface (a wrap term). |
| `TerrainMeshGloss` | `terrain` | TerrainMesh phase, opaque, vertex-lit; gloss = base alpha; fog of war and cloud shadow | High. |
| `Tree` | `foliage` | Alpha test >128/255 with depth write, fade blend; base alpha is opacity and gloss; wind bend | High. Bend offset ∝ height² × engine bend vector × `BendScale`. Declares tangent space, but its normal-map path is disabled. |
| `Grass` | `foliage` | Alpha blend plus alpha test >8/255, no depth write; billboards rotated about clump centers; wind sway; fog of war; distance fade | High for state; medium for wind constants. Diffuse lerps between `Diffuse` and `Diffuse1` with the sway phase. |

### Utility

| Legacy | Family | Notes |
| --- | --- | --- |
| `MeshCollision` | `collision` | Solid `Color` × fade; blending disabled despite its comment. Never drawn by the sandbox. |
| `MeshSolidColor` | `solid-color` | Solid `Color`, no depth write; used for cinematic color keys. |
| `MeshLightVisualize` | `debug` | Displays the spherical-harmonic lighting term. |

## Texture parameters

| Parameter | Semantic | Notes |
| --- | --- | --- |
| `BaseTexture` | `base-color` | Alpha meaning depends on the family: opacity, gloss, colorize mask, city-light mask, or self-illumination mask. |
| `NormalTexture` | `normal` | Tangent-space; alpha is gloss in bump and planet families. |
| `GlossTexture` | `specular` | Red channel only. |
| `CloudTexture` | `cloud` | Planet and sky layer, not a second base map. |
| `CloudNormalTexture` | `cloud-normal` | Planet cloud lighting. |
| `DistortionTexture` | `distortion` | Heat target or shield UV perturbation. |
| `WaveTexture` | `modulation` | Shield color mask. |

## Direct3D 9 assumptions to re-evaluate

Do not carry these into the modern renderer by default:

- Gamma-space lighting and the 2x modulate convention. A modern renderer needs
  an explicit, documented color-space decision and a calibrated approximation.
- Packed tangent-space vectors interpolated in [0, 1] and left unnormalized
  per pixel.
- Fixed-function fog and alpha-test reference values expressed as 0–255
  integers.
- The 24-bone constant-register palette and integer-packed texture coordinates.
- Half-pixel offsets (the engine passes resolution constants for them).
- The Direct3D 9 counter-clockwise default culling convention. Verify winding
  against the decoded ALO data rather than assuming it.
- Stencil shadow volumes as the shadow technique.

## Legacy-mode implementation status

The D3D12 sandbox renders legacy materials through engine-native
`VisualMaterial` data and newly written HLSL (`src/render/shaders/sandbox.hlsl`).
It reproduces the documented behavior, not the original code:

| Behavior | Status |
| --- | --- |
| Fill plus key light, 2x diffuse, exponent-16 highlight | Implemented. The fill is ambient plus two directional fill lights evaluated directly rather than projected into spherical harmonics, and the highlight is per pixel for all families (legacy vertex-lit families computed it per vertex). |
| Normal maps (bump families, terrain meshes, planets) | Implemented with the ALO tangent frame. |
| Gloss from base alpha, normal alpha, or gloss-texture red | Implemented per profile. |
| Team color (colorize mask in base alpha) | Implemented; the color is the `Color` of the type's first `Affiliation` faction, with a player-slot palette for unaffiliated types. |
| Self-illumination mask (`FSAMeshGlow`) | Implemented. |
| Diffuse, Specular, Emissive, Color parameters | Implemented from exact ALO values; absent Specular defaults to white. |
| Alpha test, alpha blend, additive | Implemented; transparency is not depth-sorted. |
| UV scroll (`UVScrollRate`, nebula at 3x) | Implemented; `UVOffset` (always zero in stock data) is not applied. |
| Per-map lighting from TED environments | Implemented from the active environment; single-model inspection uses an engine default rig. |
| Cube reflection (no stock material uses the reflective profiles), planet atmosphere/clouds/city lights, shield layers, nebula wobble, wind bend, heat, occluded silhouettes, shadow volumes, fog, fog of war | Not yet. |
