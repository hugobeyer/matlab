# Mixtormat — ID-Driven Edge Wear Effect

Target: **UE 5.8 / Mixtormat / MatLab**

Status: **Separate implementation task. Do not mix this with the Driver/Reference work.**

Start from the current HEAD where:
- Pattern IDs already use **per-cell elevation + signed chamfer/profile**.
- Pattern tangent-normal rotation is already fixed.
- Pattern IDs expose/retain the existing Region ID producer behavior.
- C++ and SM6 shaders compile cleanly.

Work in small steps.  
After each step: compile, report changed files, and stop.

---

# 0. Context & Scope Division

To ensure architectural clarity, the boundary between existing ID features and this new effect is strictly defined:

### What has already been done (Do NOT duplicate or recreate):
1. **Pattern IDs (Structure & Clean Geometric Relief)**:
   - Lattice generation (Rows, Columns, RowOffset, Jitter).
   - Analytic signed edge distance (`GapPixels`, `GapHeight`).
   - Clean geometric relief: per-cell elevation (`HeightAmount`, `HeightRandom`), chamfer profiles (`Profile`, `ProfileRandom`, bullnose/cove curvature), bevels (`BevelHeight`, `BevelWidthPixels`), and normal generation.
2. **Ramp From IDs (Non-Coplanar Fragment Settling)**:
   - Evaluates a linear gradient per region (`HeightAmount`, `NormalStrength`, `bRotateRandom`).
   - Tilts and settles individual tiles/stones so they do not lie on an unnaturally flat plane.
3. **Cluster IDs (Unsupervised Segmentation)**:
   - Segments raw scan surfaces into discrete integer region IDs.

### What the Edge Wear Effect should ONLY do:
- It is strictly a **post-process weathering and degradation filter** along the boundary of those existing shapes.
- It does **NOT** build tiles, bricks, or bevels.
- It does **NOT** tilt cells or alter planar elevation.
- It takes the clean geometry and degrades the edges using **Substance Designer-style noisy slope-blur erosion (`min(BaseHeight, DisplacedHeight)`)**, localized micro-chipping, edge fracture/crackiness, roughness degradation, and per-ID mask variation.

---

# 1. Goal

Add an **Edge Wear effect** driven by Region IDs and edge/slope information.

This is not just a simple edge mask.

The effect should create worn, chipped, eroded, slightly cracked material edges using a **Substance Designer-style slope blur concept**, especially:

```text
height/sample displacement along a noisy slope field
+
MIN-style height blending
```

The result should make edges look:

```text
worn
eroded
chipped
slightly broken
locally cracked
irregular
```

while preserving the larger shape of each Region ID cell.

Primary use cases:

```text
brick edges
stone blocks
tiles
concrete cells
Pattern ID cells
Cluster ID regions
```

---

# 2. Recommended classification

Implement this as an **Effect**, not as an ID producer.

Reason:

```text
Pattern / Cluster IDs
        ↓
Edge Wear
        ↓
modifies material channels
```

It consumes IDs and existing material data rather than producing IDs.

It may modify:

```text
Height
Normal
Roughness
AO / optional cavity contribution
```

and optionally expose a wear mask for later use.

Do not change the existing Region ID producer/consumer contract.

---

# 3. Source IDs

The effect should use the nearest valid Region ID producer according to Mixtormat's existing ordering rules.

Supported sources:

```text
Pattern IDs
Cluster IDs
```

Reuse the existing:

```text
RegionIdMaps
FindRegionIdsAbove(...)
nearest-producer behavior
demand culling
```

Do not create a separate ID dependency system.

If no valid ID producer exists above the Edge Wear effect:

```text
effect should safely do nothing
```

or show a clear invalid-source state in the inspector.

---

# 4. Core wear model

The edge wear should be based on a constrained slope-blur / erosion idea.

Conceptually:

```text
Region boundary / signed edge distance
        ↓
edge band
        ↓
noise-modulated slope / offset field
        ↓
sample neighboring height
        ↓
MIN-style blend
        ↓
worn/chipped edge
```

Do not implement this as:

```text
simple noise multiplied by edge mask
```

That will look synthetic.

The deformation should actually move/sample the height shape locally.

---

# 5. Slope-blur behavior

Reference behavior:

```text
Substance Designer Slope Blur
```

Use the same broad idea:

```text
for several samples:
    offset sampling position along a slope/noise direction
    compare displaced height
    combine using MIN for erosion/wear
```

The Mixtormat version should be tile-safe.

Suggested controls:

```text
Wear Amount
Wear Width
Iterations / Samples
Slope Scale
Noise Scale
Noise Strength
Direction Variation
```

Keep the V1 parameter count reasonable.

Prefer:

```text
4–12 samples
```

rather than very large loops.

The effect should remain practical at 2K/4K.

---

# 6. MIN blend

Default wear mode:

```text
ResultHeight = min(BaseHeight, WornHeight)
```

This should cut/erode the edge rather than inflate it.

Optional later mode:

```text
MAX
```

may be useful for buildup/lips, but V1 should focus on **MIN erosion**.

Do not accidentally erode the whole cell interior.

The ID boundary / edge band must constrain the operation.

---

# 7. Edge band

For Pattern IDs, the current Pattern system now has useful edge information:

```text
signed edge distance
negative = gap/grout side
0        = gap wall
positive = inside cell
```

Use this if available.

For generic Cluster IDs, derive an edge band from neighboring Region IDs.

The effect should support:

```text
Inside Width
Outside Width
```

or a single signed offset + width.

Recommended V1:

```text
Edge Width
Edge Offset
```

where:

```text
negative offset -> moves wear toward grout/outside
0               -> centered at region wall
positive offset -> moves wear into the region face
```

This should behave continuously, like the current Pattern chamfer inset.

---

# 8. Noise / breakup

The wear must vary per cell and along the edge.

Use two scales if practical:

```text
Macro breakup
Micro breakup
```

or one noise scale plus a detail amount.

Noise should affect:

```text
wear depth
sampling offset
edge width
local direction
```

but not all from the exact same hash.

Use independent salts/draws where needed to avoid correlated variation.

Avoid obvious repeated Voronoi blobs.

---

# 9. Chips

Add controlled local chips on the edge.

Concept:

```text
edge band
+
low-frequency sparse breakup
+
MIN height cut
```

Controls:

```text
Chip Amount
Chip Scale
Chip Depth
Chip Threshold / Sparsity
```

Chips should remain attached to the edge region.

Do not generate random pits across the entire cell face.

---

# 10. Crackyness

Add optional small edge cracks.

These are not a full crack-generation system.

They should look like:

```text
short fractures
small splits
edge-connected cuts
```

not long global crack networks.

Possible approach:

```text
edge band
+
anisotropic / directional narrow noise
+
small MIN depth cut
```

Controls:

```text
Crack Amount
Crack Scale
Crack Depth
Crack Length / Direction Variation
```

Keep this subtle.

If a robust crack model would require a much larger system, implement only a lightweight edge-fracture mask in V1.

---

# 11. Per-ID variation

Every region should vary independently.

Use the Region ID hash to vary:

```text
Wear Amount
Wear Width
Noise Offset
Noise Rotation / direction
Chip Amount
Crack Amount
Roughness contribution
Mask offset
```

Suggested controls:

```text
ID Variation
Seed
```

Do not make every cell receive the same wear profile.

---

# 12. Optional mask source

The effect should allow an optional Mixtormat mask to control where edge wear appears.

Examples:

```text
only wear the lower half
only wear moss-covered cells
only wear selected regions
only wear a painted mask area
```

The mask should be selectable from existing compatible mask outputs.

Do not build the full Driver/Reference system inside this task.

For this effect, a simple dedicated mask input/reference is enough if that is what the current architecture supports.

Suggested behavior:

```text
Wear *= Mask
```

with:

```text
Mask Strength
Invert Mask
```

---

# 13. Per-ID mask variation / offset

Important feature:

The selected mask should optionally vary **per Region ID**.

Example:

```text
same mask texture
but each Pattern cell samples it with a different offset
```

This lets one mask create different wear shapes per tile/cell.

Support:

```text
Mask Offset Variation
Mask Rotation Variation
Mask Scale Variation
Seed
```

At minimum V1 should support:

```text
random 2D mask offset per ID cell
```

The offset must be deterministic from:

```text
RegionId + Seed
```

and tile-safe.

Do not offset the Region ID map itself.

Only transform the sampled mask coordinates.

---

# 14. Roughness contribution

Edge wear should optionally affect roughness.

Suggested model:

```text
FinalRoughness =
    lerp(BaseRoughness,
         WornRoughness,
         WearMask * RoughnessWeight)
```

Controls:

```text
Roughness Weight
Roughness Value / Offset
```

Possible V1 interpretation:

```text
Roughness Offset
```

so:

```text
Final = Base + WearMask * RoughnessOffset
```

then clamp to valid range.

This is preferable if Mixtormat already treats roughness modifications as offsets.

Do not hardcode worn edges as always rougher; allow negative values if the existing channel model supports it.

---

# 15. Wear mask output

If practical, expose the resolved wear mask internally/publicly:

```text
EdgeWearMask
```

Useful later for:

```text
Driver system
color variation
dust
AO
secondary roughness
```

Do not expose implementation-only scratch buffers.

The mask should represent the final resolved edge-wear coverage after:

```text
edge band
noise
chips
cracks
optional mask
per-ID variation
```

---

# 16. Height / normal handling

Height is the primary deformation source.

After modifying height:

```text
normals must remain consistent with the new height
```

Use the current Mixtormat normal/height conventions.

Do not reintroduce the old tangent-normal rotation bug.

If the compositor already regenerates/reorients normals from modified height in the relevant effect path, reuse that path.

Otherwise explicitly document how normals are updated.

---

# 17. Suggested V1 parameters

Keep the first version compact.

## Main

```text
Amount
Width
Offset
Samples
Seed
ID Variation
```

## Breakup

```text
Noise Scale
Noise Strength
```

## Chips

```text
Chip Amount
Chip Scale
Chip Depth
```

## Cracks

```text
Crack Amount
Crack Scale
Crack Depth
```

## Mask

```text
Mask Source
Mask Strength
Invert Mask
Mask Offset Variation
```

## Material

```text
Roughness Weight
Roughness Offset
```

Do not add more knobs unless needed by the implementation.

---

# 18. Files to inspect first

Do a short targeted inspection, not a large audit.

## Runtime

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
Source/MixtormatRuntime/Public/MixtormatEffect.h
Source/MixtormatRuntime/Private/MixtormatEffect.cpp
```

Find the existing effect enum/struct patterns.

---

## Compositor

```text
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
```

Inspect:

```text
Effect render data
RegionIdMaps
FindRegionIdsAbove(...)
Pattern ID edge data
effect dispatch order
height/normal/RAM ping-pong
preview/debug handling
```

---

## Existing effects/shaders

Inspect the closest existing implementations:

```text
MixtormatErosion*.usf
MixtormatChipping*.usf
MixtormatEdgeShade.usf
MixtormatEdgeShading.ush
MixtormatPatternIds.usf
MixtormatRampIdRelief.usf
```

Reuse shared helpers where appropriate.

Do not duplicate hash/Region ID logic unnecessarily.

---

## Editor

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat.h
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp
```

Use existing effect creation, selection, badge and inspector patterns.

---

# 19. Step 1 — Add effect skeleton

Implement only:

```text
Edge Wear effect type
runtime struct
render-data plumbing
editor add/remove/select
inspector section
shader class + empty/pass-through dispatch
```

No wear algorithm yet.

Compile:

```text
MatLabEditor Win64 Development
SM6 shaders
```

Return changed files and stop.

---

# 20. Step 2 — ID edge band

Add:

```text
nearest Pattern/Cluster Region IDs
edge detection / signed edge band
Width
Offset
Seed
ID Variation
```

For Pattern IDs, reuse signed edge information if available.

For Cluster IDs, derive edge distance/band from Region IDs.

Output a debug/wear mask first.

Do not deform height yet.

Compile and stop.

---

# 21. Step 3 — Slope-blur MIN erosion

Implement the core wear:

```text
noisy slope/displacement sampling
multiple taps
MIN blend
edge constrained
```

Controls:

```text
Amount
Samples
Noise Scale
Noise Strength
```

Keep it tile-safe and bounded.

Modify Height only in this step.

Compile/test and stop.

---

# 22. Step 4 — Chips + crackyness

Add:

```text
Chip Amount / Scale / Depth
Crack Amount / Scale / Depth
```

Both must remain edge-connected.

Do not turn this into a general-purpose crack generator.

Compile/test and stop.

---

# 23. Step 5 — Roughness

Apply the resolved wear mask to roughness.

Add:

```text
Roughness Weight
Roughness Offset
```

Reuse the same final wear mask.

Compile/test and stop.

---

# 24. Step 6 — Optional mask + per-ID mask offset

Add mask selection/input.

Then add deterministic per-ID mask UV offset:

```text
MaskOffset = hash2(RegionId, Seed) * Variation
```

Optional later:

```text
rotation variation
scale variation
```

V1 must at least support 2D offset variation.

Compile/test and stop.

---

# 25. Step 7 — Normal/bake/preview validation

Verify:

```text
height
normal
roughness
preview
bake
save/reopen
PCD3D_SM6
Pattern IDs
Cluster IDs
```

Specifically check:

```text
90° / 45° Pattern texture rotations
tangent-space normal correctness
tile seams
grout boundaries
negative/positive edge offsets
2K and 4K performance
```

---

# 26. Non-goals

Do not:

- replace or recreate Pattern chamfer relief, bevels, or bullnose/cove profiles;
- recreate or interfere with Ramp From IDs planar tilt/settling;
- modify Pattern elevation semantics;
- create new Region IDs;
- create a dependency graph;
- add expressions;
- implement the global Driver system;
- implement recursive sub-children;
- turn edge cracks into a full fracture simulation;
- reorder existing serialized enums;
- remove existing effect parameters/features.

---

# 27. Final intended behavior

Example:

```text
Pattern IDs
    ↓
Edge Wear
    Source: nearest Pattern IDs

Width              10 px
Offset              +2 px
Amount              0.18
Samples             8

Noise Scale         6
Noise Strength      0.45

Chip Amount         0.30
Chip Depth          0.08

Crack Amount        0.12
Crack Depth         0.035

Mask                Dirt Mask
Mask Offset Var     0.65

Roughness Weight    0.8
Roughness Offset    +0.15
```

Each Region ID cell receives deterministic variation, the selected mask shifts independently per cell, and the effect erodes/chips the edge using a noisy MIN slope-blur rather than merely darkening or masking the boundary.
