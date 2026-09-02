# Material Lab — Erosion Effect Plan

Status: Design only. Not implemented. Independent implementation from a referenced technique.

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

Settled against the Houdini prototype (`Handoff/Houdini/OpenCl/gullyerosion.cl`). Six controls
reach the artist. Everything else is internal, fixed at the prototype values.

```text
Amount            0..1        blend against the unmodified height; 0 is the identity
Iterations        1..8        carving passes
Cavity Bias      -16..16      signed. positive concentrates carving in concave regions,
                              negative inverts it onto convex ridges. 0 ignores curvature.
Cavity Scale      0..1        how sharply concave and convex are told apart
Gully Weight      0..8        how strongly each pass steers the next and how deep it cuts
Blend Softness    0..         crossover width of the subtractive height blend
```

Cavity Bias is deliberately wide and signed. Past 1 it stops being a blend and becomes a
contrast expansion on the concavity signal, which is where it does its most useful work;
the negative half carves ridges instead of channels. Clamp the result, not the input.

Internal, not exposed:

```text
Strength         1.0     Repose            0.30    Deriv Scale   0.6
Period           8       Repose Softness   0.25    Deriv Min     1
Gully Length     1.5     Gain              0.5     Ridge Sharp   2.0
Lic Steps        5       Passes Per Octave 1       Limit Nyquist 1
Mode             0       Subtractive       1       Seed          1
```

Subtractive stays on and is not exposed. Erosion removes material and never deposits; a
signed field added to the height builds the surface up half the time, which reads as noise
laid over the height rather than as something cut into it. The blend is a smooth minimum
rather than a hard one, because a hard min leaves a derivative discontinuity along every
crossover and those stamp crease lines into the normals derived downstream. Blend Softness
is that rounding width.

Mode 0 keeps the directional gullies. If the isotropic mode proves indistinguishable at
texture scale, the effect collapses to a single pass with no ping-pong and this plan should
be revised down accordingly.

## Ridge map

Ridge and drainage fall out of the octave loop at negligible extra cost. Publish the ridge map
as a new weighted signal on the generated
mask node, so drainage lines and creases become mask input without a second evaluation.

This requires the erosion pass to write the ridge map to a target the mask pass can read, which
means one additional full-resolution channel when any erosion effect is present.

## Ordering

Erosion runs while preparing its owning layer, before that layer's height composites forward. The
reshaped height therefore drives the height blend mask, contact AO and border normals, so the
whole transition softens coherently rather than only the displacement output.

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
