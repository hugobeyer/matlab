# Material Lab — Erosion Effect Plan

Status: Implemented and building. Not visually verified in the editor.

## Goal

Add an ordered layer effect that reshapes the height accumulated below its owning layer,
carving slope-aligned gullies so harsh height transitions gain believable relief instead of
reading as steps. It also produces a ridge map, which becomes a new signal for generated masks.

## Independent implementation

Material Lab implements this technique itself. Rune Skovbo Johansen's Advanced Terrain Erosion
Filter (Shadertoy `sf23W1`, recorded in `shadertoyerosion.md`) is the reference for the idea,
not a source to port. That shader is itself the third in a line — Clay John, then Fewes, then
Rune, whose header notes his version retains little beyond the high-level concept.

The concept, which is what carries over:

```text
Carve gullies whose direction follows the local downhill slope.
Stack them across octaves, each octave steering the next.
Gate carving by slope, so flat ground is left alone.
Track where gullies converge to produce a ridge and drainage map.
```

What must not carry over is the expression: loop structure, parameter set, variable roles and
naming. A renamed paraphrase is a derivative work and MPL-2.0 would still attach. Our
implementation is written from the description above and from our own constraints, which differ
enough that a faithful port would be the wrong shape regardless.

Credit Rune Skovbo Johansen as the inspiration in the plugin documentation. This is not a
license obligation under an independent implementation; it is ordinary practice.

Do not copy `PhacelleNoise` or `ErosionFilter` into the plugin. Do not keep a copy of
`shadertoyerosion.md` in shipped content; it is a reference document and stays in `Docs/`.

## Why our constraints force a different design

```text
Terrain reference                     Material Lab
never repeats                         must tile exactly on an integer period
generates its own fBm heightfield     consumes composited layer height
uniform parameters across the frame   parameters vary per pixel from the mask stack
one full-screen pass, real time       one pass per recipe change, editor time
screen-space domain                   UV domain, resolution independent at 1K/2K/4K
```

Tiling is the deepest of these. A cell lattice that closes on a period changes how cells are
laid out and hashed, so it is a different noise construction rather than the same one with
wrapped inputs. Design for the period from the start instead of retrofitting a wrap.

The editor-time budget cuts the other way and is worth exploiting: cost is paid once per recipe
change, not per frame, so we can afford more per pixel than a real-time shader would.

## Our design

A directional gully field evaluated per pixel over a bounded octave loop.

Each octave:

```text
1. Read the current downhill direction from the working slope.
2. Evaluate a periodic gully field oriented to that direction, returning
   a signed offset and its analytic derivative.
3. Weight it by a slope gate, so flat regions receive nothing.
4. Accumulate offset into height and derivative into slope.
5. Steer the next octave with the updated slope; scale frequency and
   amplitude by the octave falloff.
```

Requirements on the gully field:

- Periodic on an integer cell count so the result tiles at every supported resolution.
- Oriented by an arbitrary 2D direction, not axis aligned.
- Returns an analytic derivative, so slope stays exact without extra sampling.
- Deterministic from UV alone: no time, no screen space, no frame history.

Ridge and drainage accumulate alongside, tracking where successive octaves agree.

The slope gate is what keeps this from being a blur. Carving scales with existing slope, so a
harsh transition gains relief while flat ground is untouched by construction.

## Inputs

The filter needs height plus analytic slope. Both already exist below the owning layer:

```text
Height  HeightTargets[LayerReadIndex]
Slope   central-difference gradient of that height field
```

Take the slope from the height field rather than from the accumulated normal, so the gradient
matches the field being reshaped. Reuse the Sobel approach already in `MaterialLabCurvature.ush`.

## Tiling

This is the one part with real technical risk and the only place the algorithm itself changes.

A cell-based directional field samples an integer lattice through a hash. A terrain never
repeats, so the original has no tiling requirement. A Material Lab surface must tile exactly.

The lattice coordinate must be wrapped to an integer period before hashing:

```text
gridPoint = fmod(gridPoint + Period, Period)
```

`Period` must equal the cell count across one UV repeat, so `freq`, `cellScale` and `scale` are
no longer free: they must resolve to a whole number of cells. Octaves multiply `freq` by
`lacunarity`, so every octave's period must also stay integral. Restricting `lacunarity` to 2 and
choosing a power-of-two base period is the straightforward way to guarantee this.

Prototype the wrap on compute.toys before porting. If seams persist, the fallback is to erode at
a single scale with a tileable period and accept fewer octaves.

## Implementation notes

```text
File          Shaders/Private/MaterialLabGully.ush
Octave loop   bounded, [unroll] with a compile-time maximum
Hash          Material Lab's own, shared with future noise work
Precision     R16F height target, as the compositor already uses
```

Write it against our own hash rather than borrowing one, since the hash determines the cell
layout and therefore the look. A single shared hash also serves the noise signals the generated
mask will want later, so it belongs in its own header from the start.

Keep the gully field separate from the erosion driver. The field is reusable — warping, mask
generation and any later directional-noise work can all call it — while the driver is specific
to reshaping height.

## Filter effects

Erosion is a filter, not a generator. It reads a channel that already exists and returns a
transformed version of it. Nothing is introduced that was not already there, and with its
amount at zero it is the identity.

This is a different kind of thing from the effects that exist today:

```text
Peeling, Stain    consume the mask, modify the owning layer's coverage, normal and AO
Erosion           reads a composited channel, returns a filtered version of that channel
```

Model it as an explicit category so the distinction survives contact with later work:

```text
EMaterialLabEffectClass
    Surface     writes coverage / normal / AO through EffectData   (Peeling, Stain)
    Filter      transforms one composited channel in place         (Erosion)
```

Filters share one contract. Each declares the channel it filters, is the identity at zero
amount, and must not read state produced later in its own layer. That makes the obvious
successors — height blur, slope limiting, mask dilate or erode, curvature sharpen — additions
to an existing category rather than a new special case each time.

The category also decides UI: a Filter child names the channel it acts on, so a layer carrying
three effects reads as what it does rather than as three opaque entries.

## Integration

An ordered effect child, alongside Peeling and Stain:

```text
EMaterialLabEffectType::Erosion
```

Unlike those, it has no source textures. It ships as a stub `UMaterialLabEffect` asset with
`EffectType = Erosion` so `FMaterialLabRegistry::GetEffects()` discovers it through the existing
path and it can carry authored defaults.

The effect pass currently writes coverage, normal and AO into `EffectData`, which is the
Surface contract. Erosion is the first Filter, so the dispatch gains a second path: bind the
height ping-pong targets, read the accumulated height, write the filtered result back. This is
the same structural change already made for generated masks reading the accumulated surface,
and it is the path every later filter reuses.

## Exposed controls

Ranges below are the inspector's scrub range only. No erosion control carries a hard clamp:
`ClampMin`/`ClampMax` are absent from the recipe struct and the compositor passes every value
through raw, so a typed value outside the scrub range reaches the shader intact. The shader
keeps epsilon guards at its own division sites; those are not UI limits and stay.

```text
Amount            0..1        blend against the unmodified height; 0 is the identity
Repose            0..32       critical slope. nothing carves below it
Repose Softness   0..32       width of the transition above the critical slope
Slope Radius      1..32       pixel radius of the local slope measurement
Slope Blur        0..16       low-pass on the guidance height before measuring slope
Cavity Bias      -16..16      signed. positive favours concave, negative convex, 0 ignores
Cavity Contrast   0..32       smoothstep contrast separating concave from convex
Height Influence -16..16      signed. positive erodes raised ground first, negative low ground
Height Contrast   0..32       smoothstep contrast separating high ground from low
Gully Weight      0..8        how deep each pass cuts and how strongly it steers the next
Blend Softness    0..8        crossover width of the subtractive height blend
Normal Strength   0..32       how strongly the carve perturbs the layer normal; 0 is height only
Direction Mode    Weight|Lerp Weight competes with slope magnitude, Lerp blends evenly
Direction Angle   0..360      authored flow direction, 90 is +Y
Direction Amount  0..1        how much the authored direction displaces the downhill flow
```

Internal, not exposed: Iterations 8, Period 32, Strength 1.0, Gully Length 1.5, Lic Steps 5,
Gain 0.5, Deriv Scale 0.6, Deriv Min 1, Seed 1.

`Iterations` and `Period` were exposed and are now fixed. Their recipe fields
(`ErosionIterations`, `ErosionPeriod`) remain serialized so existing recipes load, but nothing
reads them. `Cavity Contrast` and `Height Contrast` are relabels of the former `Cavity Scale`
and `Height Scale`; the underlying fields keep their old names for the same reason. The
smoothstep was already in the shader — widening the range from `0..1` to `0..32` is what makes
it usable as a contrast.

Repose is exposed because its units are height per UV, so the useful value depends entirely
on the composited height range and cannot be fixed at a prototype value.

## Internal resolution

Erosion runs at `min(2 x composition, 4096)` and resamples back to composition resolution.
Carving is high-frequency work: at composition resolution the octave loop reaches its
two-pixels-per-cell floor with passes still to run, so the finest gullies have nowhere to cut.
At 4096 composition the cap makes this a no-op and the pass falls back to a plain copy.

Resampling is one dispatch that moves height and normal together, in either direction. At 2:1
a bilinear tap at the destination texel centre lands on the 2x2 centroid, so the downsample is
an exact four-texel average. Normals are decoded, renormalised and re-encoded; renormalising
the 0..1 encoded value would aim at the corner of the unit cube rather than the surface.

Known consequence: `Concave = -Lap * CavityScale * Resf.x` is half-normalised. A second
derivative converted to UV units needs `Resf.x` squared, and it carries one factor, so cavity
gating weakens as resolution rises. Doubling the internal resolution therefore changes how
`Cavity Bias` bites on recipes authored before this change. Left uncorrected deliberately, so
that a visual regression here is attributable to the resolution change rather than to a
simultaneous maths fix.

## Ridge map

Ridge and drainage fall out of the octave loop at negligible extra cost. Publish the ridge map
as a new weighted signal on the generated
mask node, so drainage lines and creases become mask input without a second evaluation.

This requires the erosion pass to write the ridge map to a target the mask pass can read, which
means one additional full-resolution channel when any erosion effect is present.

## Ordering

Erosion is a post-layer filter. It runs after the owning layer composites, reads the height
and normal that layer just produced, carves the height, and writes both back.

The plan originally specified running it before the layer composited, on the accumulated
height underneath. That was wrong in practice: the layer then paints its own height straight
back over the carve, so the effect only appeared if the layer was hidden. Erosion belongs on
the layer whose height you want carved, and must therefore run after it.

Normals are derived from what was removed. The gradient of `source - carved` becomes a tangent
normal and is combined with the layer normal by RNM, the same construction border lift uses.
Without this the surface displaces but shades flat.

## Limitations

- Gullies are a stylistic approximation, not a physical erosion simulation.
- Tiling constrains scale and lacunarity to values that resolve to whole cells.
- Cost is octaves x gully-field evaluations per pixel per erosion effect; it is not free at 4K.
- The filter assumes a height field with meaningful slope. A flat layer produces nothing, which
  is correct but may read as the effect being broken.

## Validation

1. `Amount = 0` is bit-identical to the unmodified height.
2. A flat height field is returned unchanged.
3. A known ramp gains gullies aligned down the slope.
4. Output tiles with no seam at every supported resolution.
5. Octave count changes detail without shifting large-scale shape.
6. The ridge map is non-uniform for an eroded slope and flat for a flat one.
7. Eroded height drives blend mask, contact AO and border normals together.
8. Baked height matches the preview.
9. Recipes without an erosion effect are unchanged.
10. No third-party licensed source is present in the plugin.

Do not run builds, Unreal, or tests without explicit permission.

## Current state

Built and linking. Never confirmed to produce a visible result in the editor.

Implemented:

```text
EMaterialLabEffectType::Erosion          appended as 2
EMaterialLabEffectClass                  Surface / Filter split
FMaterialLabLayerEffect::ProceduralType  type for effects with no asset
Shaders/Private/MaterialLabGully.ush     hash, periodic noise with derivatives, LIC, soft subtract
Shaders/Private/MaterialLabErosion.usf   carving passes plus a final normal pass
MaterialLabGpuCompositor.cpp             shader class, render data, post-layer dispatch
SMaterialLab.cpp                         Add Child > Effect > Erosion, inspector section
```

Open, in the order worth attacking:

1. Nothing has been seen working. The last report was that erosion only took effect when the
   owning layer was hidden, and that normals did not follow. Both were addressed by the
   post-layer restructure, which has not been tested.
2. Repose scale is unknown for Material Lab height. Set Repose to 0 first: the gate is then
   fully open and carving should be violent and obvious. If 0 still does nothing, the gate is
   not the problem and the next step is a debug view of the height and slope erosion actually
   receives, rather than more parameter guessing.
3. Normal Strength scale is unknown for the same reason. Try well above 1 before concluding
   it does nothing.
4. The ridge map is computed and written but consumed by nothing. It was meant to become a
   weighted signal on the generated mask node.
5. The isotropic mode from the Houdini prototype was never ported. Only the directional LIC
   path exists in HLSL, so the cheap single-pass alternative has never been compared.
6. The erosion child row in the layer stack has a blank name, because the row derives its
   label from the effect asset path and a procedural effect has none.

The Houdini prototype at `Handoff/Houdini/OpenCl/gullyerosion.cl` is the reference for what
the filter should look like, and is the faster place to try changes to the kernel itself.
