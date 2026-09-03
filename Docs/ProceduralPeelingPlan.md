# Mixtormat — Procedural Peeling

Status: Implemented and building. Not visually approved. One design change outstanding.

## What it is

A second source for `MixtormatPeeling.usf`. Instead of the authored PDM/MSK/H/SDF texture
set, the peel field is generated per composition from a mask and from the material
composited below the owning layer.

The peeling shader itself is unchanged between the two paths. The generator writes the same
channel layout into transient float targets, and `ProceduralSource` selects which the
shader reads. A Peeling child with a null effect asset takes the procedural path, matching
how Erosion identifies itself.

```text
Mask + surface signals  ->  MixtormatPeelField.usf  ->  transient field targets  ->
                            MixtormatPeeling.usf (unchanged)
```

## How it works today

`Shaders/Private/MixtormatPeelField.usf`, three dispatch modes:

```text
Seed     threshold the peel's mask into a starting region, and precompute the speed field
Solve    one Godunov eikonal iteration (Eikonal2 / Solve8, ported from peel.cl)
Resolve  turn arrival into the authored channel layout
```

Seed and Solve run at composition / `PeelSolveDivisor`; Resolve runs at full resolution and
filters arrival up. Arrival is a smooth field, so it survives that, and the solve dominates
cost: dividing the side both quarters the texels and halves the passes the front needs to
cross them.

Iteration count is bounded by reach, not resolution:
`ceil((|Front| + 4|Width|) * SolveRes.X)`, capped at 256.

### Why eikonal and not a distance transform

An earlier build used jump flooding. It produced visibly radial, star-shaped peels, because
jump flooding computes Euclidean distance from a seed set and nothing else can influence
it. The eikonal solve propagates a front through a **speed field**, so convexity, occlusion
and height reshape the outline rather than merely gating coverage afterwards. That is the
whole reason the extra passes are worth paying for.

Those signals belong to growth, not to seeding. On the seed they would only have decided
where a peel started; on the speed they decide how it spreads, which is what the artist
actually sees.

### Seeding

The peel has its own mask slot (`PeelMask` / `PeelMaskTexture`, with tiling and invert),
independent of the layer's ordered mask children. Unset, it falls back to the accumulated
child mask, which is what recipes written before the slot existed were built against.

There is deliberately no noise in the generator. An earlier build generated macro/micro
noise fields and seeded from them; that was removed. The mask is the seed field.

## Outstanding change: make it a true signed distance field

This is the next task and the reason this document exists.

### The problem

Seeding marks the whole thresholded region as `T = 0`. That makes the field a one-sided
dilation distance:

- Every cell inside the mask has `T = 0`, so `D = -Front` uniformly. The interior carries
  **no gradient at all**, and the coverage smoothstep has nothing to shape there.
- `Front` can only dilate outward. It can never erode inward.
- The threshold snaps the boundary to whole cells, discarding the mask's sub-cell position.

### The fix

Solve distance from the mask's **isocontour** rather than from its interior, and sign the
result. One solve, not two:

1. Mark cells where the mask crosses `SeedThreshold` between neighbours. Those are the zero
   level set, `T = 0`. Interpolate the crossing position so the boundary is sub-cell rather
   than snapped.
2. Solve the eikonal outward from that boundary exactly as now, producing unsigned distance.
3. Sign by side: `SDF = (mask > threshold) ? -T : +T`.

Then `D = SDF + MacroWarp/MicroWarp terms - Front` erodes at negative `Front`, dilates at
positive, and has a real gradient on both sides of the boundary.

The speed field still applies, so this remains a **non-uniform** dilation — the part a plain
distance transform cannot do.

Cost is unchanged: still one solve over the same iteration count.

### What this should fix

- Flat, gradientless peel interiors.
- `Front` being one-directional.
- Coverage falloff being one-sided, which currently makes `Width` behave asymmetrically.
- Hard cell-quantised boundaries at low `PeelSolveDivisor`.

### Validation

1. `Front = 0` puts the coverage boundary exactly on the mask's threshold contour.
2. Negative `Front` erodes inside the mask; positive dilates outside. Both are smooth.
3. The interior shows a gradient rather than a constant.
4. Growth weights at zero give a uniform, isotropic dilation.
5. Growth weights non-zero visibly reshape the outline, not just its coverage.
6. Output tiles with no seam at every supported resolution.
7. An authored peeling child renders identically before and after the change.

## Known issues elsewhere in the file

- `WrapPixel` is called four times where two would do, and the diagonal taps mix an
  x-wrapped and a y-wrapped coordinate. The values come out correct because each call only
  offsets one axis, but it is fragile and should be tidied with the SDF work.
- `MixtormatGully.ush` is now included only for `MixtormatHashU`; the noise it was there for
  is gone.

## Controls

```text
Peel Mask / Tiling / Invert   the peel's own seed mask; unset uses the child mask
Seed - Mask Gain              scales the mask before the threshold
Amount (Seed Threshold)       the threshold itself
Growth - Convexity            speed weight; Convex Bias picks cavity vs ridge
Growth - Occlusion            speed weight, inverted AO
Growth - Height               speed weight
Growth Strength               exponent on speed; 0 propagates uniformly
Normalize Weights             divide the growth mix by total weight
Size Variation                per-cell speed factor, so flakes reach different extents
Lift Variation                per-cell lift factor
Flake Cells                   cell count for both variations
Curled                        peel type 0 flat chip / 1 flap and fold, ported from peel.cl
Edge Sharpness                exponent on the crest term
Contact AO                    procedural occlusion strength under the lifted edge
Solve Scale                   divides the resolution the front is solved at
Height Amount / Invert Height on the Peeling section; routes relief into composited height
```

## Peel relief

The peel writes height into a dedicated `R16F` ping-pong pair, added because the effect
data target's four channels were already spent on keep, normal xy and AO. The composite
adds it to `ResultHeight` scaled by effect visibility. Before this, peeling changed coverage
and normals while height silently reverted to whatever sat underneath.

The flat type keys height off `Intact`, not `Coverage`. Weighting by coverage raised the
peeled area instead of dropping it to the substrate, which read as blisters rather than
flaking. The curled type already keyed off `Intact`.

## Rules

- Do not modify or generate `M_MaterialLab_Substrate`.
- Preserve serialized fields and neutral defaults; recipes predating each change must load.
- The authored texture path must render identically regardless of any procedural work.
- Do not run builds, Unreal, or tests without explicit permission.
