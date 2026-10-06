# Mixtormat Surfacing / Generator Audit

## Audited Baseline

Repository: `hugobeyer/matlab`  
Audited default-branch HEAD:

`0775b86c6f0b0ceb39f13519570c1b2642220499`

Commit:

`Add Mixtormat field processing plan and ramp audits`

This document focuses on the next surfacing requirements after the current generator expansion:

- Cliff Strata
- Rock Formation
- Cracks
- Pebbles
- Strata Carver
- Height Blend
- Height Remap / Height Curve
- Color Ramp
- existing Generator Flow infrastructure

No test work is part of this plan.

---

# Core Direction

Mixtormat now has enough macro-shape generators.

The next major quality increase should come from **shared surface-processing modules**, not from continuously adding more specialized macro generators.

Recommended conceptual split:

```text
Generators create structure.
Fields describe structure.
Processors transform/weather structure.
Consumers turn fields into material channels.
```

Example:

```text
Cliff Strata
→ Jagged
→ Edge Thaw
→ Gravity Erosion
→ Noise
→ Height Remap
→ Height Blend
→ Color Ramp
```

The ordered child/module stack should remain the authoring model.

Do not introduce a node graph for this.

---

# Existing Strength: Generator Flow

The current flow implementation is already a strong foundation and should be expanded instead of duplicated.

Existing functionality in `MixtormatGeneratorFlow.usf` includes:

- periodic/tileable flow behavior
- boundary-derived direction
- height-gradient-derived direction
- direction smoothing
- RK2 coordinate tracing
- Shape Deform
- Generator Flow
- Flow Carve
- groove/deposit behavior
- influence
- validity
- warped UV output
- coverage transport
- tiled wrapping

Therefore:

**Do not implement a second independent “Flow Push” system.**

Instead, evolve the current flow path into the common transport/deformation system.

---

# Existing Edge / Distance Data

Useful edge information already exists across the system.

## Rock Formation

Existing useful outputs include:

- `RockEdgeDistance`
- `RockTop`
- `RockChamfer`
- `RockWall`
- `RockTopRamp`
- `RockChamferRamp`
- `RockWallRamp`
- `RockHeight`
- `RockGap`

## Pebbles

Useful outputs include:

- `PebbleEdgeDistance`
- `PebbleCoverage`
- `PebbleRandom`

## Cracks

Useful outputs include:

- `CrackDistance`
- `CrackMask`
- `ChamferCut`
- `PieceRandom`

## ID systems

Existing infrastructure already produces:

- boundaries
- distance-like fields
- ID-derived gradients
- relief masks

These should become sources for reusable surfacing processors.

---

# Field Architecture Gap

Current typed published fields are still limited to:

```text
RegionIds
Flow
UVMap
Color
```

Scalar outputs still rely heavily on the older mask publication path.

That becomes limiting for the new surfacing system because these are semantically different:

- mask
- signed height
- unsigned scalar
- signed distance
- slope
- curvature
- erosion
- sediment
- noise value
- flow strength

Recommended future typed field kinds:

```text
Scalar01
ScalarSigned
SignedDistance
```

Potentially later:

```text
Vector2
```

if generic vectors need to exist independently of semantic `Flow`.

Do not represent these distinctions only through output names.

---

# Important Current Color Field Issue

At the audited HEAD, `FPublishedField::IsComplete()` still handles:

- RegionIds
- UVMap
- Flow

but not Color.

The field-processing plan already identifies this.

The Color case should be added as part of the current ramp / Color OutputReference work.

This is required for:

- Color reference validity
- cache persistence
- restored prefix snapshots
- future generalized field handling

---

# Cliff Strata Audit Finding

Cliff Strata currently computes useful projected flow internally.

In `AddCliffStrataPasses`:

```text
O[1] = Mixtormat.CliffStrata.Flow
```

using a two-channel float texture.

However:

- the flow is not currently exposed as a published field
- Cliff Strata is not included in `MixtormatCanOwnGeneratorFlow()`
- Cliff Strata explicitly does not currently claim a signed `BoundaryField`

Current generator-flow eligibility includes:

```text
StrataCarver
RockFormation
Pebbles
Cracks
```

but not:

```text
CliffStrata
```

This should be revisited.

Cliff Strata is one of the strongest candidates for:

- Gravity Erosion
- Flow deformation
- vertical runoff
- directional weathering
- sedimentation

Its existing internal flow should be reusable rather than discarded after its native solve.

---

# Recommended New Surface Processors

## 1. Edge Thaw / Edge Weathering

Highest priority.

This should be a reusable **height processor**, not a new generator.

Purpose:

- soften geometric cuts
- round hard block edges
- thaw chamfers
- erode rigid procedural silhouettes
- create natural weathered transitions

Suggested controls:

```text
Width
Amount
Roundness
Bias / In-Out
Noise
Noise Scale
Jaggedness
Preserve Peaks
Mask
```

Preferred behavior:

- height-domain operation
- optional signed-distance / boundary input
- no simple blur-only solution
- preserve macro relief while modifying edge shape
- usable after Rock Formation, Cracks, Cliff Strata, Pebbles and future masonry systems

The important distinction:

**Edge Thaw should alter profile geometry, not merely blur pixels.**

---

# 2. Gravity Erosion

Add a true vertical / directional erosion processor.

The current flow system derives directions from boundaries or height gradients.

Gravity Erosion needs an explicit global direction.

Suggested default:

```text
GravityDirection = vertical/downward
```

Optional authored control:

```text
Gravity Angle
```

Suggested controls:

```text
Gravity
Length
Erosion
Deposit
Talus
Spread
Falloff
Iterations
Noise
Mask
```

Recommended outputs:

```text
Erosion
Deposit
Flow
```

Potentially later:

```text
Sediment
Accumulation
```

This is particularly important for Cliff Strata.

It should convert clean block formations into:

- sedimentary cliffs
- rain-cut vertical streaks
- eroded ledges
- runoff cavities
- deposition shelves

---

# 3. Flow Push / Advect

Do not create a separate transport engine.

Extend the existing Generator Flow system.

Recommended future source selection:

```text
Flow Source:
- Height
- Boundary
- Gravity
- Published Flow
- Constant Direction
```

Recommended transport modes:

```text
Warp
Push Height
Carve
Deposit
```

Potential extra operations:

```text
Scale
Rotate
Normalize
Blend
Mask/Gate
```

The current implementation already owns most of the expensive infrastructure.

---

# 4. Jagged / Edge Distort

This should be a cheap reusable processor.

Purpose:

- break overly procedural silhouettes
- fracture straight edges
- create layered stone irregularity
- create non-uniform block outlines
- make cracks and strata less synthetic

Preferred design:

- tangent displacement along boundaries
- smaller normal displacement
- optional signed-distance / boundary input
- tileable
- deterministic seed
- controllable frequency / scale

Suggested controls:

```text
Amount
Scale
Tangent
Normal
Seed
Octaves / Detail
Mask
```

Avoid simply adding value noise to height.

The most useful result comes from distorting the edge position itself.

---

# 5. Noise Generator

Noise should become a real **field producer**, not a height-specific generator.

Recommended initial noise set:

```text
Value / Gradient
FBM
Ridged
Billow
Worley F1
Worley F2
Worley F1-F2
Bars / Stripes
Directional / Phasor-like
```

Recommended outputs:

```text
Value
IDs
Gradient / Flow
Coordinates / UV
```

Not every noise needs every output.

The important part is that Noise does not decide material meaning.

Consumers decide whether noise becomes:

- height
- mask
- erosion amount
- roughness
- Color Ramp input
- flow influence
- surface breakup
- UV distortion

---

# 6. Slope / Curvature Fields

Add cheap shared analytic fields.

Recommended outputs:

```text
Slope
Convex
Concave
VerticalFacing
```

These become universal gates for:

- erosion
- deposits
- edge thaw
- micro detail
- albedo
- roughness
- AO-like darkening
- sediment
- stain/runoff logic

A lot of related gradient work already exists internally.

It should not be repeatedly reimplemented by each generator.

---

# 7. Sediment / Accumulation

Add after Gravity Erosion is stable.

This does not need to be a full hydraulic simulation.

A practical directional accumulation model is sufficient.

Possible inputs:

```text
Height
Flow
Slope
Erosion
```

Possible outputs:

```text
Sediment
Deposit
Accumulation
```

Primary use:

```text
Cliff
→ Gravity Erosion
→ Sediment
→ Color / Roughness / Micro Detail
```

---

# Processor Stack Direction

Generator-layer modules should increasingly support ordered surfacing chains.

Example:

```text
Cliff Strata
↓
Jagged
↓
Edge Thaw
↓
Gravity Erosion
↓
Noise
↓
Height Remap
↓
Height Blend
↓
Color Ramp
```

Another example:

```text
Rock Formation
↓
Jagged
↓
Edge Thaw
↓
Flow Carve
↓
Noise
↓
Height Remap
```

This produces more variety from fewer macro generators.

---

# Capability-Driven Flow Eligibility

The current function:

```text
MixtormatCanOwnGeneratorFlow()
```

hard-codes generator types.

Longer term, replace generator-name logic with capability logic.

Possible capabilities:

```text
HasHeight
HasBoundary
HasFlow
HasCoverage
HasRegionIds
```

Then:

```text
Rock Formation → Height + Boundary
Pebbles        → Height + Boundary + Coverage
Cracks         → Height + Boundary
Strata Carver  → Height + Boundary
Cliff Strata   → Height + Flow
Noise          → optional Flow / Scalar
Gravity        → Flow
```

This prevents new processors from needing generator-specific lists.

---

# Cliff Strata Recommended Outputs

Cliff Strata should eventually expose:

```text
Height
Region IDs
Block Seam
Row Seam
Cavity
Coverage
Voronoi
Flow
```

Potential future addition:

```text
Boundary / Signed Distance
```

once a canonical block/coverage boundary definition is chosen.

The existing projected flow is especially valuable and should not remain internal-only.

---

# What Not to Add

Do not add a generic “Weathering Generator”.

Do not duplicate functionality already covered by:

- Rock Formation
- Cracks
- Pebbles
- Strata Carver
- Cliff Strata

The missing diversity is now in **shared processing**, not more macro-shape families.

Do not create separate systems for:

```text
Rock Noise
Cliff Noise
Crack Noise
Pebble Noise
```

Create one reusable Noise field producer.

Likewise, do not create independent Flow Push infrastructure when Generator Flow already exists.

---

# Recommended Implementation Order

After the current Ramp / Color Ramp / Height Blend work:

## Phase A — Field readiness

1. finish Color field completeness
2. add scalar-domain planning
3. avoid more parallel named-output registries
4. prepare SignedDistance / Scalar01 / ScalarSigned contracts

## Phase B — Cliff flow access

1. expose Cliff Strata flow
2. allow it to participate in common flow workflows
3. keep its existing internal projected-flow solve

## Phase C — Core surfacing

1. Edge Thaw
2. Gravity source for existing Flow
3. Gravity Erosion
4. Jagged / Edge Distort

## Phase D — Field producers

1. Noise
2. Slope
3. Convex / Concave
4. Vertical Facing

## Phase E — Layered geology

1. Sediment
2. accumulation / deposit
3. erosion-driven masks
4. surfacing presets later

---

# Primary Reference Files

## Runtime / data model

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
- `Source/MixtormatRuntime/Public/MixtormatOutputReference.h`

## Generator gather

- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`

## Generator passes

- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`

## Typed field state

- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`

## Flow

- `Shaders/Private/MixtormatGeneratorFlow.usf`

## Existing generators

- `Shaders/Private/MixtormatCliffStrata.usf`
- `Shaders/Private/MixtormatCracks.usf`
- `Shaders/Private/MixtormatRockFormation.usf`
- Strata Carver / Pebbles shader paths

## Published outputs / preview capabilities

- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`

## Existing field-processing plan

- `AgentDocs/Mixtormat_Field_Processing_Plan.md`

---

# Target End State

Mixtormat should be able to create a macro structure once and then non-destructively transform it through reusable surfacing modules.

The important long-term workflow is:

```text
Create structure
→ derive fields
→ weather/process height
→ reuse those fields for color/material response
```

That is the scalable direction for Cliff Strata, rocks, masonry, concrete, sedimentary surfaces and future procedural materials.
