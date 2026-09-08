Worn Edges — Production Implementation

Start from the current validated Pattern branch/main state.

Implement Worn Edges as a production Mixtormat filter/effect.

Primary reference

Read this file first:

Docs/houdini_scripts/opencls/wornedges.cl

Treat that OpenCL as the algorithmic reference. It contains the Houdini prototype we already tuned visually.

Do not redesign this as generic edge noise or a simple blur/erosion shader.

There is already an older integration prototype:

Shaders/Private/MixtormatEdgeWear.usf

Inspect it for existing UE plumbing if useful, but do not treat its algorithm as authoritative. Replace/adapt it as needed to reproduce the newer wornedges.cl behavior.

1. Effect type / serialization

Append Worn Edges to the existing serialized effect enum.

Current values must not move.

Peeling  = 0
Stain    = 1
Erosion  = 2
Grade    = 3
Chipping = 4
WornEdges = 5

Classify it as a Filter.

UI display name:

Worn Edges

Do not reorder existing enum values.

2. Production defaults

IMPORTANT:

The values currently written as val= defaults inside wornedges.cl are older prototype defaults.

Use these exact defaults in the Mixtormat plugin:

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

These are the requested plugin defaults.

3. Core algorithm

Port the actual erosion behavior from wornedges.cl.

The fundamental operation is directional MIN-style erosion against nearby height:

candidate = NeighborHeight + allowed directional slope
target    = minimum valid candidate
erosion   = SourceHeight - target

It should wear exposed raised edges by searching outward into lower neighboring/grout regions.

Preserve:

Radius-based directional search
Slope
Strength
Feather
Gravity
Gravity Angle
exact variable Direction count
Angular AA
combined multi-family noise
deterministic per-ID variation

Do not replace the solver with an edge-distance darkening/noise effect.

4. Direction sampling

Use exact direction counts:

clamp Directions to 8..32

Generate uniform angular directions:

a = TAU * d / DirectionCount + stableAngleOffset
dir = float2(cos(a), sin(a))

Do not use an integer direction lookup table such as (3,2), because unequal vector lengths bias search radius.

For radial stepping:

round dir * step to integer pixel offset
skip duplicate rounded offsets
compute actual physical pixel distance from the resulting offset
reject samples outside LocalRadius + 0.5

Keep the best four directional minima and use Angular AA as in the prototype:

averaged = (best0 + best1 + best2 + best3) * 0.25
target   = lerp(best0, averaged, AngularAA)

Do not spatially blur the source Height to solve angular aliasing.

5. Noise architecture

Port the combined periodic noise system from wornedges.cl.

It consists of:

Macro  = periodic gradient FBM
Cell   = Chebyshev cellular
Ridge  = ridged turbulence
Micro  = higher-frequency gradient FBM
Warp   = periodic domain warp

The cellular component specifically uses Chebyshev distance:

D = max(abs(dx), abs(dy))

Preserve the current body/interior/corner shaping from the OpenCL unless UE testing exposes a real issue.

Noise must affect more than visual intensity.

As in the prototype, it should influence things such as:

resistance
local search radius
family weighting

so the silhouette and depth of the wear become irregular.

Do not merely multiply a cloud texture over otherwise uniform erosion.

All procedural noise must remain tileable.

6. Region ID integration

Worn Edges should consume the appropriate Region ID producer when available.

Follow Mixtormat's existing Region ID ownership semantics rather than inventing another ID system.

For Pattern IDs:

use the stable Pattern Region IDs
use the Pattern signed edge field for an inexpensive active-edge band / early-out where available

For Cluster IDs:

use the Region IDs
derive an edge band from neighboring ID differences if necessary

If there is no valid Region ID source, the effect should still have a safe deterministic behavior rather than crash.

Important conversion from Houdini:

wornedges.cl treats ID 0 as no-ID
Mixtormat invalid Region ID is MIXTORMAT_INVALID_REGION = 0xffffffffu

Adapt accordingly.

Do not change Mixtormat's Region ID sentinel.

Do not hash/replace Pattern centre-pixel IDs.

7. Per-ID variation

Port the independent deterministic ID draws from the OpenCL.

Controls:

ID Variation
ID Radius
ID Slope
ID Strength
ID Noise

Use independent salted random draws per Region ID.

ID variation should affect:

search radius
slope
strength
relative noise-family behavior

Do not offset the entire noise domain independently per region. That creates hard visible discontinuities at ID borders.

Also:

Do not constrain neighbor Height samples to the same Region ID.

A brick edge needs to see the lower grout or neighboring geometry in order to erode.

IDs control variation, not visibility of search samples.

8. Edge-band optimization

Default cost can reach approximately:

16 directions × 24 radius
≈ 384 height samples / active pixel

At 2K/4K this matters.

Use an inexpensive edge-band early-out before the heavy directional search.

For Pattern IDs, leverage the existing signed Pattern OutputEdge where available.

For non-Pattern Region IDs, derive a suitable local edge indication.

Pixels clearly outside the possible wear radius should pass through without executing the expensive loop.

Optimize the proven algorithm; do not substitute a cheaper decorative effect.

9. Height semantics

Input:

final accumulated Height at this point in the child/effect chain

Output:

WornHeight

Worn Edges should carve/round the actual structural + material height.

It must work correctly with the Pattern P3.1 height fix:

structural tile elevation
+
preserved local material detail

Do not reintroduce per-texel height compression.

Preserve material detail away from worn areas.

Feather controls the erosion transition, not a Gaussian blur of Height.

10. Normal regeneration

Do not use the old Edge Wear prototype's approach of Sobel-filtering the original pre-wear height while merely gating it by carve delta.

Correct architecture:

Pass 1
SourceHeight
    ↓
Worn Edges
    ↓
Final WornHeight

Pass 2
Final WornHeight
    ↓
existing normal-from-height path
    ↓
Worn Normal

Reuse Mixtormat's existing height→normal infrastructure where practical.

Normals must represent the actual final worn height.

11. UI

Add a Worn Edges inspector using existing Mixtormat styling/tokens.

Suggested grouping:

WORN EDGES

Shape
  Radius
  Slope
  Strength
  Feather

Direction
  Directions
  Angular AA
  Gravity
  Gravity Angle

Variation
  Seed
  Macro Scale / Amount
  Cell Scale / Amount
  Ridge Scale / Amount
  Micro Scale / Amount
  Warp Scale / Amount
  Noise Contrast

Per ID
  ID Variation
  ID Radius
  ID Slope
  ID Strength
  ID Noise

Do not introduce bespoke styling.

Keep the UI reasonably compact.

12. Feature-local masks

Do not implement the new generalized feature-local mask system as part of this task.

That has been deferred separately.

Worn Edges should integrate with the current mask/effect architecture without inventing a second temporary nested-mask implementation.

We will add generalized draggable/context-sensitive feature masks afterward.

13. Roughness / published mask

The primary acceptance target is:

Height + correct regenerated Normal

If the integration is clean after that, also expose/publish the normalized wear amount internally as:

EdgeWearMask

suitable for later:

Roughness modulation
Drivers
feature masks/composition

Do not let roughness work delay or compromise Height + Normal parity.

14. Preserve existing systems

Do not regress/refactor:

Pattern modes 0–9
Pattern P3.1 structural-height behavior
Pattern UV orientation
tangent-space normal transforms
Region ID producer/consumer semantics
Cluster IDs
Ramp From IDs
Random From IDs
HSV From IDs
Drivers
Follow/Link references
child instances
layer masks
Stain / Chipping / Erosion unless a tiny shared fix is genuinely necessary

Do not perform unrelated cleanup.

15. Validation

Build:

MatLabEditor Win64 Development

Compile the Worn Edges shaders for:

PCD3D_SM6

Check at least:

Pattern brick/stone surface
raised detailed height
Region IDs present
Region IDs absent
Gravity = 0
nonzero Gravity
low/high Directions
ID Variation = 0
ID Variation = 1
noise family amounts individually
Pattern seams U/V

At Strength = 0, output must be an exact/no-op pass-through.

At ID Variation = 0, Region IDs must not change the erosion parameters.

Tile seams must remain clean.

Report

When complete, report:

changed files
exact render-pass architecture
how Region IDs are resolved
how Pattern OutputEdge is used for early-out
how the OpenCL directional MIN solver was translated
how Chebyshev/Macro/Ridge/Micro/Warp noise was translated
how per-ID variation is applied
how final normals are regenerated
GPU-cost/early-out strategy
any deviations from wornedges.cl
any remaining limitations
build result
SM6 shader compile result

Stop after Worn Edges. Do not begin the deferred import/mask/gallery tasks.