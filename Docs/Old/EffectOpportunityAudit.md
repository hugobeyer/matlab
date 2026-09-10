# Material Lab — Effect Opportunity Audit

Date: 2026-09-04
Method: static inspection of `Plugins/MaterialLab/Shaders`, `Source`, and
`Handoff/Houdini/OpenCl`. Nothing was built or run.

## Verdict

The compositor has more reusable machinery than the effect list suggests, and one missing
primitive blocks a disproportionate share of what is worth building.

```text
Cheap now, no new foundations      gradient map, HSV filter, cavity tint, flow smear,
                                   UV transform, ridge as a mask signal
Blocked on one primitive           craquelure, chipping, per-cell colour, cell UV
(tileable cellular noise)          randomisation, tile/brick/plank layout
Blocked on a second primitive       domain warp, generic grunge, colour variation
(tileable FBM in HLSL)
Genuinely hard, flag before quoting masked transform, stochastic tiling
```

The single highest-leverage addition is **tileable cellular noise**. It is absent from HLSL
entirely, and it is the shared foundation of craquelure, chipping, and every cell-based colour
or layout effect on the list.

## Where new work belongs

The codebase has two homes for new behaviour, and picking the wrong one is the most likely way
to waste a week.

**Patterns belong on the generated mask node.** `MixtormatGeneratedMask.usf` already provides
blend modes, weight, bias, contrast, offset, balance, invert, smoothing and a domain warp, all
composited over a mask stack. A craquelure *pattern* added there inherits all of it. Added as an
effect instead, every one of those controls would have to be rebuilt.

Its current signals are Curvature (cavity/convex), Direction, AO and Height — all derived from
the surface underneath. There is **no noise or pattern signal at all**, which is the gap.

**Transforms of composited channels belong in the effect list**, as Filters. Erosion and Grade
established the shape: run post-layer, read what the stack accumulated, write it back, be the
identity at zero amount.

### Two dead things worth reclaiming first

- **The erosion ridge map is written and never read.** `EroRidge` is allocated, cleared and
  written by every carving pass (`MixtormatGpuCompositor.cpp` ~2148–2345) and nothing consumes
  it. `ErosionEffectPlan.md` always intended it to become a generated-mask signal. That is a
  free drainage/crest mask already being computed at full resolution.
- **`EMixtormatEffectClass` is declared and referenced nowhere.** The Surface/Filter split
  exists in `MixtormatEffect.h` as documentation only; every dispatch decision switches on
  `EMixtormatEffectType` instead. Either wire it up or delete it — as it stands it implies a
  contract the code does not enforce.

## The substrate that already exists

Worth knowing before proposing anything, because several "new" effects are mostly assembly.

| Primitive | File | Reusable for |
|---|---|---|
| Periodic gradient noise, analytic derivatives, tiles on an integer period | `MixtormatGully.ush` | any noise-driven effect; already the tiling-safe pattern to copy |
| 32-bit hash | `MixtormatGully.ush` | cell IDs, per-cell randomisation |
| Coherent tangent flow (structure tensor, double-angle, coherency) | `MixtormatFlow.ush/.usf` | directional smear, streaking, flow-aligned anything |
| Curvature from normals (Sobel, multi-ring) | `MixtormatCurvature.ush` | cavity/convex masks, edge wear |
| Hessian curvature: mean / valley / ridge | `MixtormatErosion.usf` | crack seeding, crest detection |
| **Non-uniform eikonal distance solver** | `MixtormatPeelField.usf` | see below — the most underused thing here |
| Soft subtract, one-sided | `MixtormatGully.ush` | any subtractive height op |
| Coverage → colour/roughness over composited output | `MixtormatErosionShade.usf` | the shape every "exposes fresh material" effect wants |
| Post-layer filter dispatch + scratch/copy-back | `MixtormatGpuCompositor.cpp` | every new Filter |

**The eikonal solver deserves attention.** `MixtormatPeelField.usf` solves a *speed-field-weighted*
distance from a mask's threshold contour — a true non-uniform SDF, which a jump flood or plain
distance transform cannot produce (the doc records that both were tried and came out radial).
Craquelure width, chip falloff, mortar recession and rust creep are all "distance from a seed
set, biased by a field". That solver already does it and is only wired to peeling.

## What is missing

### 1. Tileable cellular noise — blocks the most

Absent: no Voronoi, Worley, or cellular anything in HLSL. Confirmed by search across
`Plugins/MaterialLab/Shaders`.

It must be **periodic**, and for exactly the reason `ErosionEffectPlan.md` records: a lattice
that closes on an integer period is a different noise construction, not the same one with a
wrapped input. `MixtormatCellGradient` already demonstrates the pattern — wrap the cell
coordinate to the period *before* hashing.

What a single `MixtormatCellular.ush` should return, because these cover every downstream use:

```text
F1              distance to nearest feature      blobs, pebbles, scales
F2 - F1         distance to the cell edge        craquelure, grout, tile seams
CellId          hash of the owning cell          per-cell colour, per-cell UV, chip variation
CellCentre      offset to the feature point      per-cell transforms, radial falloff
```

Jitter needs to be a control: 0 gives a regular grid (brick, tile, plank), 1 gives full Voronoi
(cracks, stone). That one parameter is what makes it serve both the layout effects and the
organic ones.

### 2. Tileable FBM in HLSL

The gully field sums octaves, but that loop lives inside the erosion driver and is shaped by it.
There is no general multi-octave noise a mask signal or a warp filter could call.

This already exists in Houdini and is not ported: `FBM_Noise.cl` (398 lines, multi-layer with
blend modes) and `FBM_Noise_RGB.cl` (188 lines, explicitly tileable, with layer-onto-layer
domain warp). The tiling approach in the RGB one is worth reading before writing the HLSL.

### 3. UV transform is one integer scalar

This is the concrete state of "UV transforming" today:

```text
MixtormatMaterial.h:98     int32 Tiling = 1        per layer, integer, uniform
MixtormatComposite.usf:162 frac(UV * Tiling)
MixtormatMask.usf:67       frac(UV * max(Tiling, 1))
```

No offset. No rotation. No per-axis scale. No flip. Nothing per-layer beyond that one integer.

Adding offset, per-axis scale and flip is parameter plumbing plus two lines of shader maths, and
it tiles cleanly. **Rotation does not** — rotating a tiled UV breaks the tile at any angle that
is not a multiple of 90°. Ship rotation either snapped to 90°, or only with a warning, or paired
with stochastic sampling that removes the tiling requirement. Getting this wrong ships seams.

### 4. No domain warp for composited channels

The generated mask has a warp (`WarpAmount`, `WarpSource`, `WarpRadius`) but it is mask-local.
Nothing warps the composited surface. The stain effect does a hand-rolled 2-step gradient trace
(`MixtormatStain.usf`) which is that idea in miniature.

## Proposed effects

Ordered within each group by value-to-cost. "Cost" assumes the primitives above exist where
noted.

### Colour

| Effect | Home | Needs | Notes |
|---|---|---|---|
| **Gradient map / ramp** | Filter | nothing | Remap luminance through a colour ramp. The single highest value-to-cost item on this list — rust, patina, oxidation, moss are all one ramp over an existing mask. Needs a ramp control in the UI, which is the real work. |
| **HSV filter** | Filter | nothing | Hue/sat/value already exist per-layer (`MixtormatMaterial.h:733`) but not as a *maskable* filter over the accumulated stack. Same shape as Grade; hours, not days. |
| **Cavity / edge tint** | Filter | nothing | `MixtormatCurvature.ush` already produces cavity and convex. Tint by either. Dirt in crevices, polished wear on edges. |
| **Per-cell colour variation** | Filter | cellular | Brick-to-brick, plank-to-plank, tile-to-tile hue and value offset. The effect that most reliably makes a tiling texture stop reading as tiled. |
| **Colour by ridge / drainage** | Filter | reclaim ridge map | Free once the ridge output is wired to a signal. Streaking and mineral deposit follow drainage. |

### Warping

| Effect | Home | Needs | Notes |
|---|---|---|---|
| **Flow smear** | Filter | nothing | `MixtormatFlow` already produces a coherent oriented field with a coherency term. Smear the composited channels along it — weathering streaks, brushed metal, wood grain drift. Cheapest real warp available. |
| **Domain warp** | Filter | FBM | Warp all composited channels by a noise field. The catch: BC, N, RAM and H must be resampled by the *same* offsets or they desynchronise, and normals should ideally be rotated by the warp Jacobian, not just moved. Worth stating in the plan up front. |
| **Height-driven warp** | Filter | nothing | Generalise what stain does in `WarpStainUV` — trace along the height gradient. Sagging, slumping, runs. |

### Craquelure and cracking

| Effect | Home | Needs | Notes |
|---|---|---|---|
| **Craquelure pattern** | Mask signal | cellular | `F2 - F1` is the crack network directly. As a mask signal it inherits contrast, bias and blending for free. Jitter takes it from regular tile grout to organic paint crazing. |
| **Crack widening / depth** | Filter | cellular + eikonal | Distance from the crack set, via the existing solver, gives width and a soft shoulder rather than a hard line. This is where the peel field pays off a second time. |
| **Curvature-seeded cracks** | Mask signal | reclaim Hessian | The erosion filter already computes valley/ridge curvature. Cracks follow tension, which tracks convexity — seeding a crack network from real surface curvature rather than pure noise is the difference between generic and specific. |

Also note `brickserosion.cl` already outputs a `cracks` channel alongside layered masonry
erosion — see the port section below.

### Chipping and flaking

| Effect | Home | Needs | Notes |
|---|---|---|---|
| **Chipping** | Surface | cellular | `brickschips.cl` is a working 229-line prototype with grout level and softness, chip amount, size, depth and irregularity. It is a port, not a design problem. |
| **Edge chipping** | Surface | cellular + curvature | Gate chip placement by convexity so chips land on exposed edges rather than uniformly. Both inputs already exist. |
| **Exposed substrate** | — | done | `MixtormatErosionShade.usf` already solved "carve depth drives colour and roughness". Chipping should reuse that shape rather than reinvent it. |

### UV and masked transforming

| Effect | Home | Needs | Notes |
|---|---|---|---|
| **UV transform** | layer property | nothing | Offset, per-axis scale, flip. Tiles cleanly. Rotation needs the caveat above. |
| **Per-cell UV randomisation** | layer property | cellular | Random offset/flip/90°-rotation per cell. Breaks repetition on brick, plank and tile layouts without touching the source texture. Restricting rotation to 90° multiples keeps it tiling. |
| **Stochastic / hex-tile sampling** | layer property | histogram work | The Heitz–Neyret style approach: sample the source at three hex-grid offsets and blend variance-preservingly. It genuinely removes visible repetition. It also changes how *every* layer samples its source and interacts with the whole composite, so it is a project rather than an effect. |
| **Masked transform** | Filter | — | See the honest problems below. |

## Unported Houdini prototypes

Working code sitting in `Handoff/Houdini/OpenCl` with no HLSL equivalent:

```text
brickschips.cl     229   chipping: grout level/softness, chip amount/size/depth, irregularity
brickserosion.cl   789   hydraulic erosion + cracks + masonry layering, outputs 4 channels
growth_peel.cl     256   growth-driven peel, an alternative to the current eikonal peel
cavity.cl           68   curvature/cavity/convex — largely superseded by MixtormatCurvature.ush
FBM_Noise.cl       398   multi-layer FBM with blend modes
FBM_Noise_RGB.cl   188   tileable FBM with layer-onto-layer domain warp
antialiasing.cl    142   sample-based AA, threshold and softness
```

`brickschips.cl` is the best value here: a complete chipping effect whose only blocker is the
cellular primitive. `brickserosion.cl` is the largest and overlaps the erosion filter that
already shipped — worth mining for its crack and masonry-layering outputs rather than porting
whole.

One licensing note: unlike the Shadertoy erosion reference, these are first-party prototypes, so
porting them carries none of the constraints `ErosionEffectPlan.md` records.

## Honest problems

**Masked transform leaves a seam.** Transforming content inside a mask boundary means the
transformed and untransformed regions do not line up at the edge. Feathering hides it on organic
masks and fails visibly on hard-edged ones. There is no general fix — the options are to accept
feathering, to restrict it to masks that are already soft, or to transform whole layers only.
Decide before promising it.

**Rotation and tiling are in conflict.** Stated above; repeated because it is the most likely
thing to get shipped broken.

**Warping composited channels is not one texture fetch.** BC, N, RAM and H must move together,
and normals want the warp's Jacobian applied, not just a resample. A warp that moves only base
colour will look wrong under light and the cause will not be obvious.

**Everything must tile.** This constrains every proposal above and is the one thing the codebase
already does well — `WrapI` in the erosion and flow shaders, `AM_Wrap` samplers throughout, and a
noise lattice that closes on an integer period. New primitives must hold that line; it is much
harder to retrofit than to design in, which `ErosionEffectPlan.md` learned the hard way.

## Status

Items 1, 3, 4, 5 and 6 of the order below are implemented; 1, 3, 4 and 5 are built. Item 2, the
gradient map, is not started -- it was dropped to last deliberately, because the ramp UI is the
only piece needing new Slate vocabulary and it should not gate the primitive everything else
waits on.

```text
1  Reclaim      DONE   ridge map -> generated mask signal; EMixtormatEffectClass made load-bearing
3  UV transform DONE   per-axis integer scale, offset, flip. No rotation -- see below
4  Cellular     DONE   MixtormatCellular.ush: F1, F2, EdgeDistance, CellId, CellOffset, periodic
5  Craquelure   DONE   its own mask node -- was a generated-mask signal, moved out
2  Gradient map TODO
6  Chipping     DONE   ported; needed no cellular primitive -- see the correction below
7  FBM + warp   TODO
8  Stochastic   TODO
```

**Correction: chipping was never blocked on the cellular primitive.** This audit filed it under
"blocked on one primitive" on the assumption it needed a Voronoi lattice. Reading
`brickschips.cl` rather than its description, bricks come from thresholding the composited
height at a grout level -- there is no cell lattice anywhere in it. That is strictly better than
what was assumed: chipping works on whatever was actually built rather than only on a lattice we
generate. The dependency graph at the top of this document overstates what item 4 unlocked.

Three decisions taken while implementing, all worth knowing because later work inherits them:

**Craquelure is its own node, not a generated-mask signal.** It shipped as a signal on the
generated mask and that was wrong. Every other signal there is derived from the surface
accumulated below the layer, and the node early-returns when there is none; craquelure is
generated from a lattice and means something on the bottom layer. Living there forced that
early return to be picked apart into a per-signal guard, so the node's contract became
conditional on which weights happened to be non-zero -- a contract that reads differently
depending on the recipe is not a contract.

It is now `EMixtormatLayerChildType::Craquelure` with its own shader, and the generated mask is
back to a single `SurfaceValid` test. A mask node rather than a filter, so it can drive anything
downstream -- chipping placement, a stain, a peel -- rather than only cutting the surface.

The tail both nodes share -- blend modes, and the bias / contrast / balance / invert chain --
moved to `MixtormatMaskOps.ush`. A second copy would have drifted; the balance curve in
particular is not something anyone would reproduce identically from memory. Any further
mask-producing node should include that header rather than grow a signal on an existing one.

**Craquelure uses `EdgeDistance`, not `F2 - F1`.** The point-difference form varies crack width
with cell size -- heavy in large cells, hairline in small ones. The boundary distance holds an
even width, which is what actually reads as cracking. Chipping should use the same measure.
Both are returned by the primitive so the choice stays visible rather than baked in.

**`MixtormatErosionShade.usf` became `MixtormatCarveShade.usf`, shared by Erosion and Chipping.**
It grew a coverage texture input and a switch: erosion still recovers coverage from its height
pair, chipping reads its chip mask directly. Anything else that carves should join it rather
than duplicate it. Chipping reads the mask because at depths around 0.035 a height difference
reintroduces the cancellation the R32F work removed, and would go to zero with Depth even
though the chip is still there.

**The ridge is ping-ponged per layer, and every layer writes it.** A single shared target would
have been read-after-write across layers with no versioning, and a layer that left its slot
alone would hand the next one the ridge from two layers back. Layers without erosion copy read
to write. Last erosion wins rather than accumulating a max across several -- documented rather
than solved, because recipes with two eroding layers are not the common case.

## Suggested order

1. **Reclaim what exists.** Wire the ridge map to a generated-mask signal; resolve
   `EMixtormatEffectClass` either way. Small, and clears dead weight before adding to it.
2. **Gradient map filter.** Highest value-to-cost item, needs no new primitive, and the ramp UI
   it requires is reusable by everything after it.
3. **UV transform** — offset, per-axis scale, flip. Ship rotation separately, with the tiling
   question decided.
4. **`MixtormatCellular.ush`** — F1, F2−F1, cell id, cell centre, periodic, jitter-controlled.
   This is the unlock; everything in groups C, D and half of A depends on it.
5. **Craquelure as a mask signal**, first consumer of the above, and the cheapest way to prove
   the primitive is right before three other effects depend on it.
6. **Chipping**, porting `brickschips.cl` and reusing the erosion shade pass for exposed material.
7. **FBM**, then the domain warp filter on top of it.
8. **Stochastic tiling**, last, scoped as a project rather than an effect.

Items 1–3 are independent of each other and of the rest, so they parallelise. Everything from 5
onward serialises behind 4.
