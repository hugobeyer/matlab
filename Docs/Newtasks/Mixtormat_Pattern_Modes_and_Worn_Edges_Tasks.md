# Mixtormat — Pattern Modes + Worn Edges Implementation Handoff

Repository: `hugobeyer/matlab`  
Target: Unreal Engine 5.8 / Mixtormat  
Baseline reviewed: current `main` containing the Driver/Reference/Instance work and the committed `Docs/houdini_scripts/opencls/patterns.cl` prototype.

This document defines two separate implementation tasks:

1. **Task A — Pattern Modes**
2. **Task B — Worn Edges**

They share Region IDs, but they are not the same system.

---

# 0. Shared architectural rules

## Pattern IDs are producers

Pattern IDs publish regions into the existing Region ID pipeline.

Do not create another ID dependency system.

Reuse:

```text
RegionIdMaps
FindRegionIdsAbove(...)
nearest-producer semantics
existing demand culling
```

Pattern IDs must remain usable by:

```text
HSV From IDs
Random From IDs
Ramp From IDs
Drivers
future Worn Edges
```

## Worn Edges is a consumer/effect

Worn Edges consumes:

```text
current composited Height
nearest Region IDs
Pattern signed edge field when available
```

and modifies:

```text
Height
Normal
later: Roughness / published WearMask
```

It must not become another Region ID producer.

---

# 1. Pattern output contract — do not regress

The current Pattern shader already publishes four coordinated outputs:

```hlsl
RWTexture2D<uint>   OutputIds;
RWTexture2D<float2> OutputUV;
RWTexture2D<float2> OutputRamp;
RWTexture2D<float2> OutputEdge;
```

Preserve their meaning.

## Region ID

Pattern Region IDs are not arbitrary hashes.

They are the linear pixel index of the region centre:

```hlsl
RegionId = CentrePixel.x + CentrePixel.y * OutputSize.x;
```

This is required by existing consumers such as Ramp From IDs.

Grout/background uses:

```hlsl
MIXTORMAT_INVALID_REGION
```

Do not replace it with `0`.

## OutputUV

Keep publishing the region centre/source UV used by per-region texture transforms.

## OutputRamp

Keep the current per-cell elevation + feather semantics.

Pattern modes define topology only. They must not reintroduce the removed per-cell slope system.

Per-cell tilt remains the job of Ramp From IDs.

## OutputEdge

Keep the signed edge contract:

```text
negative = grout / outside side
0        = region wall
positive = inside cell face
```

This is the primary field Worn Edges should consume for Pattern regions.

Do not clamp it to zero in the producer.

---

# 2. Task A — Pattern Modes

## Goal

Turn Pattern IDs from the current single cellular/sheared lattice into a topology-selectable procedural pattern producer while preserving all existing relief, UV, Region ID and Driver behavior.

The committed `patterns.cl` is a useful prototype/reference only. Do not port it literally; several prototype cases were intentionally improved during Houdini testing.

The target mode set is:

```text
0 Grid
    grid mode 0 = Straight
    grid mode 1 = Staggered
    grid mode 2 = Diamond / 45 degree

1 Running Bond
2 Herringbone
3 Basketweave
4 Hex
5 Octagon + Square
6 Flagstone
7 Voronoi
8 Hopscotch
9 French / modular ashlar
```

Names can be adjusted for UI clarity, but serialized enum values must be append-only once committed.

---

## 2.1 Pattern mode responsibilities

Every mode must solve:

```text
cell ownership
region centre
signed edge distance
periodicity / tiling
intrinsic cell geometry
```

Then feed the existing shared Pattern pipeline:

```text
cell solve
    ↓
RegionId
CentreUV
SignedEdge
    ↓
existing per-cell elevation
existing feather
existing chamfer / bevel
existing Pattern UV transforms
existing Region ID consumers
```

Do not duplicate the relief system inside every pattern mode.

---

## 2.2 Grid

### Straight

Regular rectangular cells.

Use independent Rows / Columns.

### Staggered

Periodic row offset.

The seam must close exactly. Quantize or otherwise constrain the authored row offset so the full vertical period returns to the same X phase.

### Diamond / 45 degree

This must genuinely tile.

The tested concept is a rotated lattice:

```text
q.x = (p.x + p.y) * 0.5
q.y = (p.y - p.x) * 0.5
```

The periodic lattice must use compatible even periods.

Important: do not wrap the rotated coordinates independently and call that sufficient. The periodic ID basis must map back onto the original torus basis before wrapping.

Conceptually:

```text
ix = qCell.x - qCell.y
iy = qCell.x + qCell.y
```

Texture X and Y wrap must return the same region ownership and same Region ID.

Acceptance check:

```text
left/right seam identical
top/bottom seam identical
same IDs at the seam, not only same shape
```

---

## 2.3 Running Bond

Use actual alternating row staggering.

Do not apply the base offset equally to every row.

Target:

```text
row 0 = 0
row 1 = authored offset
row 2 = 0
row 3 = authored offset
...
```

Jitter may vary:

```text
row shift
individual brick width
```

but total widths in a row must renormalize so the row closes exactly over the tile period.

No seam drift.

---

## 2.4 Herringbone

Implement a real interlocking domino / brick motif.

Do not use a simple checker parity that only flips 1x2 rectangles without forming a true herringbone lattice.

Use a periodic motif with dimensions constrained to a period that closes cleanly; the Houdini prototype used a multiple-of-4 unit lattice.

Each brick needs a stable:

```text
owner
centre
edge distance
intrinsic 0/90 degree orientation
```

### Intrinsic orientation

Herringbone and Basketweave contain tiles that are inherently rotated 90 degrees.

Do not fake this only in the visual edge shape while leaving source UV/normal orientation unaware.

First attempt to carry this through the existing Pattern UV system without changing the four-output contract. If that is insufficient, add one compact orientation/basis output rather than repurposing `OutputUV`, `OutputRamp` or `OutputEdge`.

Any new output must be demand-driven and must not break existing Pattern consumers.

---

## 2.5 Basketweave

Use checkerboard macro-cells containing paired parallel strips.

Use a motif period that closes exactly; a multiple-of-4 unit lattice is a safe target.

Alternate horizontal and vertical pair orientation.

Each physical strip is its own Region ID.

---

## 2.6 Hex

Hex must be **uniform**.

The user should primarily control scale through X count. Do not expose a second count that stretches hexes into arbitrary non-regular shapes.

Derive row count from output aspect ratio.

Reference relationship:

```text
ny ≈ nx * (resY / resX) * 1.154700538
```

Then choose the nearest compatible even row count for staggered periodic tiling.

Requirements:

```text
cellsx controls size
cellsy ignored/hidden for this mode
rows forced to a tile-safe even period
hex ownership solved from the actual staggered lattice
no uncovered regions at gap = 0
```

Do not use a rectangular row assignment that leaves unowned wedges between cells.

---

## 2.7 Octagon + Square

Periodic truncated-square tiling:

```text
large octagons
small square inserts at shared corners
```

Both octagons and squares are valid regions with distinct Region IDs.

Signed edge distance must work for both region types.

---

## 2.8 Flagstone

Irregular stone-like cellular layout.

Use the existing periodic cellular infrastructure where possible.

Do not use `0.5 * (F2 - F1)` as the final physical edge distance.

Mixtormat already has perpendicular-bisector `EdgeDistance` in `MixtormatCellular.ush`; reuse the correct edge metric.

Flagstone may bias/jitter feature points differently from Voronoi, but the output contract stays identical.

---

## 2.9 Voronoi

Use the existing periodic cellular solver.

Requirements:

```text
periodic by construction
true nearest-cell ownership
existing bisector EdgeDistance
centre-pixel Region IDs
grout from signed edge threshold
```

Do not emit the cellular hash directly as the Region ID.

---

## 2.10 Hopscotch

Use a periodic modular motif.

Reference prototype uses a repeating 3x3 unit module with:

```text
one larger 2x2 region
several surrounding smaller / rectangular regions
```

The exact visual layout may be refined, but:

```text
motif closes every 3 units
each physical tile gets a unique region
gap remains absolute in pixels
```

---

## 2.11 French / modular ashlar

Use a larger repeating modular stone motif.

Reference prototype uses a 6x6 unit module mixing approximately:

```text
1x1
1x2
2x1
2x2
2x3
3x1
```

The final layout must:

```text
fully cover the module at gap = 0
contain no overlaps
contain no holes
tile at all four boundaries
give every stone one stable Region ID
```

This is a modular French/ashlar pattern, not a random Voronoi approximation.

---

## 2.12 Shared Pattern controls

Keep existing controls where they still make sense:

```text
Rows
Columns
Row Offset
Jitter
Gap Pixels
Seed
Swap Axes
Feather
Feather Random
Rounding
Edge Relative
existing elevation / bevel controls
```

Add only the mode controls needed:

```text
Pattern Mode
Grid Mode
```

Hide or disable irrelevant controls per mode rather than giving them fake behavior.

Examples:

```text
Hex        -> Cell Y count hidden/ignored
Voronoi    -> Row Offset irrelevant
French     -> Row Offset irrelevant
Herringbone -> row offset irrelevant
Running Bond -> Row Offset relevant
```

Do not remove existing relief controls.

---

## 2.13 Pattern files likely to change

Inspect before editing:

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
Shaders/Private/MixtormatPatternIds.usf
Shaders/Private/MixtormatCellular.ush
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
```

Only touch `MixtormatCellular.ush` if the required reusable geometry really belongs there.

Do not regress Craquelure/Chipping callers.

---

## 2.14 Pattern implementation milestones

### P1 — Data model + UI mode

Add Pattern Mode / Grid Mode serialization and inspector controls.

No shader behavior change except passing the mode.

Build and stop.

### P2 — Grid + Running Bond + Diamond

Implement:

```text
Straight
Staggered
Diamond
Running Bond
```

Verify tiling and ID continuity.

Build and stop.

### P3 — Herringbone + Basketweave

Implement proper motifs and intrinsic orientation handling.

Build and stop.

### P4 — Hex + Octagon/Square

Implement uniform regular hex and octagon/square.

Build and stop.

### P5 — Voronoi + Flagstone

Reuse `MixtormatCellular.ush` and its true edge distance.

Build and stop.

### P6 — Hopscotch + French

Implement modular motifs.

Build and stop.

### P7 — regression

Check every mode against:

```text
Pattern elevation
Gap Height
Feather
signed chamfer / roundness
Pattern UV transforms
tangent normal transforms
Ramp From IDs
Random/HSV From IDs
Region-ID scalar Drivers
tile seams
```

---

# 3. Task B — Worn Edges

## Goal

Implement the Houdini-prototyped worn-edge solver as a real Mixtormat **Effect** consuming Region IDs.

The desired look is already established by the prototype.

The current repository contains an older/partial `MixtormatEdgeWear.usf` using selectable Perlin / Cellular / Curl direction noise. Treat it as an integration prototype, not as the final algorithm.

The new target is the combined-noise, per-ID, directional MIN-erosion system described below.

---

## 3.1 Requested default values

Use these as the plugin defaults for a newly created Worn Edges effect:

```text
Radius          24
Slope           0.35
Strength        0.75
Feather         1.0

Directions      16
Angular AA      0.35

Gravity         0.0
Gravity Angle   90.0

Seed            1

Macro Scale     12
Macro Amount    0.75

Cell Scale      16
Cell Amount     1.0

Ridge Scale     12
Ridge Amount    1.0

Micro Scale     32
Micro Amount    0.5

Warp Scale      24
Warp Amount     0.25

Noise Contrast  0.5

ID Variation    1.0
ID Radius       0.5
ID Slope        0.3
ID Strength     0.25
ID Noise        1.0
```

These values are deliberate. Do not replace them with “safer” generic defaults.

Slider ranges may be wider, but typed values should remain practical and not be unnecessarily clamped in the editor.

---

# 3.2 Core Worn Edges algorithm

The prototype is not:

```text
edge mask * noise
```

It is a directional height erosion / slope-blur style operation.

Concept:

```text
Height
+
Region ID
+
multi-scale resistance field
+
directional height search
+
MIN erosion
=
Worn Height
```

The local solver finds lower nearby height reachable within a radius, allowing a slope threshold, then erodes toward that lower target.

Conceptually:

```text
for each direction:
    for each step up to local radius:
        allowed = neighborHeight + directionalSlope * normalizedDistance
        excess  = currentHeight - allowed

        if excess > 0:
            candidate = currentHeight - feathered(excess)

take strongest erosion candidates
angular-AA them
apply local strength
```

This is why the effect can roll/chip a raised brick edge instead of merely darkening or masking it.

---

# 3.3 Noise model

Use combined noise, not one generic value noise.

The current tested mixture is:

```text
Macro periodic gradient FBM
Chebyshev cellular
Ridged turbulence
Micro periodic gradient FBM
domain warp
```

## Macro

Broad material-scale changes in erosion resistance / radius.

## Chebyshev cellular

Use Chebyshev distance:

```text
D = max(abs(dx), abs(dy))
```

rather than Euclidean distance.

Use both nearest and second-nearest information to form chunky/blocky weak zones.

This was preferred over soft circular cellular blobs.

## Ridge

Ridged multi-octave gradient noise.

Adds sharper fracture-like bands and directional breakup.

## Micro

Fine gradient FBM.

Adds small irregularity without becoming the main shape.

## Warp

Low-frequency periodic vector warp applied to the larger noise fields.

All procedural fields must be tile-safe.

---

# 3.4 Per-ID variation

Region ID variation is part of the look, not an optional afterthought.

Use deterministic salted Region ID hashes to vary independently:

```text
Radius
Slope
Strength
relative Macro/Cell/Ridge/Micro mixture
```

Current controls:

```text
ID Variation
ID Radius
ID Slope
ID Strength
ID Noise
```

Do not use the same random draw for all properties.

Do not randomly offset the entire noise domain per ID in V1; hard per-cell domain jumps can create visible discontinuities exactly at boundaries.

---

# 3.5 Direction sampling + AA

The original 8/16 fixed integer-vector approach produced angular dithering/star stepping.

Target:

```text
Directions = any clamped count in a practical 8..32 range
```

Generate uniformly distributed angular rays:

```text
angle = TAU * d / DirectionCount + stableSeedOffset
dir   = float2(cos(angle), sin(angle))
```

March rounded pixel offsets along those rays.

Skip duplicate rounded offsets.

Keep the strongest few directional results.

The tested prototype keeps four:

```text
best0
best1
best2
best3
```

Then:

```text
AverageBest = (best0 + best1 + best2 + best3) / 4
Target      = lerp(best0, AverageBest, AngularAA)
```

This is **angular anti-aliasing**.

Do not blur the source Height to hide ray artifacts.

---

# 3.6 Edge localization

The Houdini prototype was tested directly on ID-generated raised cells.

The plugin effect must explicitly constrain expensive wear to region edges.

## Pattern source

Use Pattern's signed `OutputEdge` field when available.

That gives:

```text
negative grout
0 wall
positive cell face
```

The wear solver should normally act on the cell side of the wall, with an adjustable usable edge band if needed.

## Cluster source

Cluster IDs do not have Pattern's analytic edge field.

Derive a small edge band from neighboring Region IDs.

Do not invent a second ID representation.

## No valid Region ID source

Safely no-op.

---

# 3.7 Gravity

Keep:

```text
Gravity
Gravity Angle
```

Gravity biases the allowed slope by ray alignment.

At Gravity = 0 the solver is isotropic.

Do not bake a downward direction into the algorithm.

---

# 3.8 Feather

Feather softens the erosion threshold.

It should not Gaussian-blur the finished height.

The source height and unaffected cell interior must remain sharp.

---

# 3.9 Normal handling

The current old `MixtormatEdgeWear.usf` should not simply compute a Sobel gradient of the original SourceHeight after carving and call that the worn normal.

The final normal must correspond to the **final worn height**.

Preferred architecture:

```text
Pass 1: Worn Edges -> WornHeight
Pass 2: existing/reused normal-from-height path -> WornNormal
```

Reuse Mixtormat's existing normal/height conventions and reorientation logic.

Do not reintroduce the old Pattern tangent-normal rotation bug.

---

# 3.10 Roughness

Do not block V1 on roughness.

First get the Houdini-proven height silhouette correct.

After Height + Normal are stable, add:

```text
Roughness Weight
Roughness Offset
```

driven by the resolved wear amount/mask.

Allow positive or negative offset.

---

# 3.11 Wear mask

Once the core result is stable, preserve/publish the resolved wear amount internally as:

```text
EdgeWearMask
```

It can later feed:

```text
Drivers
Roughness
color
dust
AO
```

Do not expose scratch textures.

---

# 3.12 Worn Edges performance constraints

The Houdini prototype can do roughly:

```text
Directions * Radius
```

height samples per active pixel.

At defaults:

```text
16 * 24 = up to 384 samples
```

That is too expensive to run blindly over every 2K/4K pixel.

Use the Region edge band to early-out before entering the heavy directional loop.

Other acceptable optimizations after visual parity:

```text
skip duplicate rounded ray samples
early stop when local radius exhausted
compile-time maximum direction/radius bounds
lower-cost preview mode only if quality is preserved
```

Do not “optimize” by replacing the solver with noise multiplied by an edge mask.

---

# 3.13 Worn Edges files likely to change

Inspect first:

```text
Source/MixtormatRuntime/Public/MixtormatEffect.h
Source/MixtormatRuntime/Public/MixtormatMaterial.h

Shaders/Private/MixtormatEdgeWear.usf
Shaders/Private/MixtormatRegionId.ush
Shaders/Private/MixtormatCellular.ush

Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp

Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp
```

Existing `MixtormatEdgeWear.usf` is not authoritative for the final visual model.

Reuse integration plumbing only where it is still correct.

---

# 3.14 Worn Edges implementation milestones

### W1 — effect/data/UI skeleton

Append `EdgeWear` to the effect enum without reordering existing serialized values.

Add the full requested parameter model with the defaults in §3.1.

Add/remove/select/inspector support.

Pass-through shader only.

Build and stop.

### W2 — ID + edge source

Resolve nearest Region ID source.

Pattern:

```text
use signed Pattern edge field
```

Cluster:

```text
derive local boundary band
```

Debug the edge band only.

Build and stop.

### W3 — core MIN erosion

Implement:

```text
Radius
Slope
Strength
Feather
Directions
Gravity
Gravity Angle
Angular AA
```

No complex noise yet.

Height only.

Build and stop.

### W4 — combined noise

Add:

```text
Macro FBM
Chebyshev cellular
Ridged
Micro FBM
Warp
Noise Contrast
```

Apply noise to:

```text
resistance
local radius
local strength / mixture
```

Build and stop.

### W5 — ID variation

Add independent deterministic per-ID variation:

```text
ID Radius
ID Slope
ID Strength
ID Noise
```

Build and stop.

### W6 — correct normal regeneration

Regenerate/update normals from the final worn height.

Build and stop.

### W7 — roughness + published WearMask

Only after visual parity with Houdini.

---

# 4. Do the two tasks depend on each other?

Yes, but only through a stable contract.

Worn Edges should not know whether a region came from:

```text
Grid
Herringbone
Hex
French
Voronoi
Cluster IDs
```

It should consume:

```text
RegionIds
+
edge information when available
+
Height
```

Therefore:

```text
Pattern topology changes
        ↓
same Region ID / edge contract
        ↓
Worn Edges unchanged
```

That is the desired architecture.

---

# 5. Two-agent coordination

Both tasks touch large shared files, especially:

```text
MixtormatMaterial.h
MixtormatGpuCompositor.cpp
SMixtormat_Inspector.cpp
SMixtormat_Layers.cpp
```

Do not let two agents independently make large edits to the same files on `main`.

Recommended split:

## Agent A — Pattern Modes

Branch/worktree:

```text
feature/pattern-modes
```

Own:

```text
Pattern mode enum/data
Pattern shader topology
Pattern inspector mode controls
Pattern-specific compositor plumbing
```

## Agent B — Worn Edges

Branch/worktree:

```text
feature/worn-edges
```

Can develop the new Worn Edge shader/model in parallel, but before final integration:

```text
rebase onto Pattern Modes
resolve shared-file edits deliberately
```

If only one branch is desired, merge Pattern Modes first, then Worn Edges.

Pattern Modes should land first because Worn Edges consumes the Pattern edge/ID contract.

---

# 6. What neither task should touch

Do not casually refactor:

```text
Direct References
Follow / Link
Child Instances
Driver semantics
Region-ID scalar Drivers
Ramp From IDs bbox/rotation work
Pattern Gap Height
signed chamfer/profile system
tangent normal rotation
Cluster ID producer semantics
```

Do not perform architecture cleanup or branding/path cleanup as part of these tasks.

Do not remove existing controls unless explicitly approved.

---

# 7. Build / validation requirements

For each milestone:

```text
MatLabEditor Win64 Development
PCD3D_SM6 shader compilation
```

Then targeted runtime checks.

## Pattern checks

```text
all modes tile on U and V
same Region IDs across seams
grout uses invalid sentinel
Gap Height still works
bevel/chamfer still works
Pattern UV random transforms still work
Ramp From IDs still works
Region-ID Drivers still work
```

## Worn Edges checks

```text
no-op with no valid IDs
Pattern ID input
Cluster ID input
tile seams
raised brick/tile height
per-ID variation
Directions 8 / 16 / 24 / 32
Angular AA 0 / 0.35 / 1
Gravity 0 and nonzero
2K performance
4K performance
normal follows final worn height
```

---

# 8. Recommended immediate order

```text
1. Pattern P1
2. Pattern P2
3. Pattern P3/P4
4. Worn Edges W1/W2 may proceed in parallel
5. finish remaining Pattern modes
6. Worn Edges core erosion
7. combined noise + ID variation
8. normal / roughness / mask polish
```

The main quality goals are:

```text
Pattern modes must be mathematically tile-safe and share one stable output contract.

Worn Edges must preserve the Houdini prototype's actual height-search erosion behavior,
not collapse into a decorative edge-noise mask.
```
