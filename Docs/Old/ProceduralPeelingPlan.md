# Mixtormat — Procedural Peeling

Status: Implemented. Not built, not visually approved. The signed-distance change is written
but has not been compiled or run.

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
Seed     precompute the speed field, and lay the zero level set on the mask's threshold
         isocontour, interpolating each crossing so it falls between cells
Solve    one Godunov eikonal iteration (Eikonal2 / Solve8, ported from peel.cl)
Resolve  sign the solved distance by side, then write the authored channel layout
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

What is seeded is the mask's threshold **contour**, not its interior. See the signed-distance
section below for why, and for what the sign, the seed units and the macro warp term now
read.

## The field is a signed distance (done, unverified)

### What was wrong

Seeding marked the whole thresholded region as `T = 0`. That made the field a one-sided
dilation distance:

- Every cell inside the mask had `T = 0`, so `D = -Front` uniformly. The interior carried
  **no gradient at all**, and the coverage smoothstep had nothing to shape there.
- `Front` could only dilate outward. It could never erode inward.
- The threshold snapped the boundary to whole cells, discarding the mask's sub-cell
  position.

### What it does now

Distance is solved from the mask's **isocontour** and then signed. One solve, not two:

1. **Seed** marks cells whose signed seed field changes sign against a four-neighbour, and
   stores the linearly interpolated distance to that crossing, so the boundary sits between
   cells instead of being snapped to them. Those cells are flagged and the solve pins them,
   which keeps the zero level set where the mask put it.
2. **Solve** runs the same Godunov eikonal outward from that band, now inward and outward
   at once, giving unsigned travel time.
3. **Resolve** signs by side: `SDF = (seed field > 0) ? -T : +T`.

`D = SDF + M * MacroWarp + U * MicroWarp - Front` therefore erodes at negative `Front`,
dilates at positive, and has a real gradient on both sides of the boundary.

The speed field still applies, so this remains a **non-uniform** dilation -- the part a
plain distance transform cannot do. Cost is unchanged: one solve over the same iteration
count, because inward and outward propagation happen on the same passes and the reach
needed each side is what it always was.

### Decisions worth knowing

**The sign is read at full resolution, not carried through the solve.** `Resolve` recomputes
the seed field from the mask it already samples. Carrying a per-cell side through the coarse
solve grid would snap the zero crossing back to the nearest half-texel, which is exactly the
quantisation the interpolated seed removes. The cost is a small magnitude kink at the
contour, bounded by one solve texel and smallest where the seed interpolation is most
accurate; the contour's *position* is exact, which is what validation checks.

**The seed distance is converted to travel time.** The solve propagates `|grad T| = s`, not
geometric distance, so the boundary condition is multiplied by the local slowness. Without
it the band is wrong by up to a factor of the local speed, and `Solve8`'s `min()` can only
correct that downwards -- slow regions would stay permanently short by about a texel. `Seed`
and `Solve` now share one `PeelSlowness` helper so they cannot drift apart.

**Macro warp is driven by the signed seed field, not by the raw mask.** `M` was
`Mask * 2 - 1`, which at the default threshold is `+0.24` on the contour, so `MacroWarp`
injected a constant `0.24 * MacroWarp` offset and `Front = 0` did not land on the mask's
contour. `M` is now `(saturate(Mask * MaskWeight) - SeedThreshold) * 2`, zero on the contour
by construction, doubled to keep the control's usable range. This changes `MacroWarp` and
`Detail` for existing procedural recipes. Trade-off: where `MaskWeight` drives the mask above
1 the field saturates and `M` flattens, where the old term kept varying; `MicroWarp` still
supplies variation there.

**The unreached sentinel is half-representable.** `Resolve` clamped unreached cells to
`1.0e6f`, which is `+inf` in the `RGBA16F` field target. That was survivable while only the
far outside was unreached. Signed, the sentinel also covers the whole unreached *interior* --
the region the peel lives in -- and a texel of bilinear filtering against `inf` would poison
the edge of every large peel. It is now `1024`, which saturates every `Smooth01` and
underflows every `exp(-2X*X)` at any usable `Width`. The threshold test became a `min`, which
is monotone and removes the band where a bilinear mix of `FAR` against a solved neighbour
fell the wrong side of a comparison.

**`Front` now clamps from `-1`, not `0`,** in both the UPROPERTY meta and the Slate control.
Widening a clamp changes no serialized value, so the authored path resolves identically for
anything it already held.

**The neutral default is preserved.** `PeelSeedMaskWeight` defaults to `0`, so the seed field
is `-SeedThreshold` everywhere, nothing crosses, every cell stays unreached, the side is
positive and coverage is 0 -- identical to before.

### Validation, still to run

1. `Front = 0` puts the coverage boundary exactly on the mask's threshold contour.
2. Negative `Front` erodes inside the mask; positive dilates outside. Both are smooth.
3. The interior shows a gradient rather than a constant.
4. Growth weights at zero **and `Size Variation` at zero** give a uniform, isotropic
   dilation. `FlakeRandom01` keeps the speed non-uniform at the `0.5` default, so leaving it
   alone will fail this test for the wrong reason.
5. Growth weights non-zero visibly reshape the outline, not just its coverage.
6. Output tiles with no seam at every supported resolution.
7. An authored peeling child renders identically before and after the change.

### Known limitation

With `Solve Scale` above 1 and either a noisy mask or a high `Peel Mask Tiling`, the
full-resolution sign can flip inside a region where the coarse solve found no crossing to
seed. Those cells resolve to the unreached sentinel with an inside sign, giving hard-edged
specks with no gradient. The levers are `Solve Scale = 1` or a smoother mask.

## Known issues elsewhere in the file

- `MixtormatGully.ush` is now included only for `MixtormatHashU`; the noise it was there for
  is gone.
- `GrowthField` is bound as both SRV and UAV on every pass, including the solve, which does
  not write it. Pre-existing and harmless in practice, but it is a real RDG aliasing hazard.

Fixed with the SDF work: `WrapPixel` was called four times where two would do, and the
diagonal taps mixed an x-wrapped coordinate with a y-wrapped one. It is now two calls,
`WrapPixel(Pixel - 1)` and `WrapPixel(Pixel + 1)`, whose components compose the diagonals.

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
Front                         signed offset of the contour; negative erodes, positive dilates
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
