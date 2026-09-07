# Mixtormat — Driver + Reference System (Stepped Implementation)

Target: **UE 5.8 / Mixtormat / MatLab**

Work in **small steps**.  
Do **not** implement the whole system in one pass.

Important current state:
- Tangent-space normal rotation has already been fixed.
- Pattern ID relief is being changed separately to **random height + random chamfer/profile**, with no slope/tilt behavior.
- Start this work only from the commit/HEAD where that Pattern relief work is finished and compiling.

For every step:
1. Inspect only the files needed for that step.
2. Preserve all existing features and parameters.
3. Do not reorder serialized enums.
4. Build `MatLabEditor Win64 Development`.
5. Return changed files + compile result.
6. **Stop after the step. Do not continue automatically.**

---

# Step 1 — Stable IDs + Direct Parameter References

Implement only exact parameter references.

## Goal

Allow:

```text
Destination Parameter  ↗  Source Parameter
```

Example:

```text
Edge Width ↗ Pattern IDs.Bevel Width
Seed       ↗ Pattern IDs.Seed
```

## Inspect

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat.h
Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
shared inspector row helpers
```

## Implement

Add stable identity:

```cpp
FGuid LayerId;
FGuid ChildId;
```

Rules:
- keep valid IDs on load;
- old assets receive missing IDs once;
- duplicated layers/children receive new IDs;
- moving layers/children must not break references.

Add direct parameter references supporting initially:

```text
float -> float
int   -> int
bool  -> bool
enum  -> same enum type
```

Suggested reference identity:

```cpp
SourceLayerId
SourceChildId
SourceParameter
```

Resolve exact parameter references **CPU-side during render-data gathering**.

Do not allocate textures or shader passes for direct parameter references.

## UI

Parameter RMB:

```text
Copy Reference
Paste Reference
Clear Reference
Go to Source
```

Show a small referenced state:

```text
Scale X   [8.0]  ↗
```

Reject/fallback safely for:
- self-reference;
- type mismatch;
- missing source;
- obvious circular reference.

A broken reference should fall back to the destination's local stored value.

## Do not implement yet

- Drivers
- masks as modulation
- ID modulation
- Combine modes
- sub-children
- child-level instances

Stop after Step 1.

---

# Step 2 — Driver Data Model + Inspector Popover

No GPU modulation yet.

## Goal

Add the persistent definition and UI for:

```text
Final = Combine(BaseValue, DriverSignal)
```

but do not wire real mask/ID textures yet.

## Driver V1 data

One driver per parameter.

Fields:

```text
Enabled
SourceLayerId
SourceChildId
SourceOutput
Combine
Amount
InputMin
InputMax
OutputMin
OutputMax
Invert
```

Combine enum:

```text
Replace
Multiply
Add
Subtract
Min
Max
Lerp
```

Keep direct Reference and Driver as separate concepts.

## UI

Add a tiny driver button to compatible parameter rows:

```text
○ = no driver
● = driver
↗ = direct reference
```

Click opens a compact popover:

```text
Drive Edge Roughness

Source    ...
Output    ...
Combine   Multiply
Amount    0.75
Remap In  0.20  0.80
Remap Out 0.00  1.00
Invert    []
```

Use existing Mixtormat tokens/widgets.

Prefer integrating at the shared parameter-row helper rather than editing every inspector row manually.

For this step, source/output menus may be limited to structurally valid placeholder/known entries. Do not add GPU signal binding yet.

## Do not implement yet

- mask texture driving
- ID driving
- effect coverage driving
- multiple drivers per parameter
- expressions
- sub-children

Stop after Step 2.

---

# Step 3 — Mask/Scalar Signal Drivers

Wire the first real GPU driver source.

## Goal

Allow scalar parameters to be modulated spatially by an existing mask signal.

Example:

```text
Edge Roughness = 0.35 * Mask
```

## First supported sources

Prefer already-existing signals:

```text
CombinedMask
existing child mask output where lifetime permits
generated mask output where lifetime permits
```

Do not snapshot every intermediate automatically.

## Requirements

- reuse existing RDG textures where possible;
- one source texture may drive several parameters;
- demand-cull snapshots if a source must survive longer;
- avoid one new full-resolution texture per driven parameter.

Start with scalar destinations only.

Apply:

```text
source
-> optional invert
-> input remap
-> output remap
-> amount
-> combine with base value
```

Only add shader bindings to the passes that actually need them.

## Test

At least:

```text
Replace
Multiply
Add
Invert
Remap
```

Verify preview and bake parity.

Stop after Step 3.

---

# Step 4 — Pattern / Cluster IDs as Driver Sources

Add ID-aware modulation.

## Goal

Allow:

```text
Pattern IDs -> Random Per ID -> parameter
Cluster IDs -> Random Per ID -> parameter
```

Example:

```text
Edge Roughness
  Base: 0.35
  Driver: Pattern IDs
  Mapping: Random Per ID
  Range: 0.4..1.0
  Combine: Multiply
```

## Important

Do not multiply raw integer Region IDs directly into parameters.

Add explicit ID mapping.

V1 mapping:

```text
Random Per ID
```

Optional if cheap:

```text
Specific ID
ID Range
```

Use the existing `RegionIdMaps` producer/consumer system.

Prefer evaluating the hash/mapping in the consuming shader from the existing `R32_UINT` ID texture rather than materializing another full-resolution scalar texture.

Preserve:
- nearest producer semantics;
- producer demand culling;
- Pattern IDs behavior;
- Cluster IDs behavior.

Stop after Step 4.

---

# Step 5 — Effect Parameters + Published Outputs

Expand the Driver system to useful effect parameters.

## Goal

Examples:

```text
Erosion Amount      <- Mask Driver
Chipping Amount     <- Pattern Random Per ID
Edge Roughness      <- Pattern Edge
Grade Strength      <- Mask
```

## Inspect per effect

Do not assume every effect shares the same execution point.

Audit individually:

```text
Stain
Erosion
Chipping
Grade
Craquelure relief
Pattern relief / chamfer
other post-composite filters
```

Classify each parameter:

```text
uniform only
safe scalar driver
unsafe/meaningless spatial driver
requires source snapshot
```

Expose only sensible destinations.

If a useful effect output already exists, publish it explicitly instead of exposing scratch buffers.

Stop after Step 5.

---

# Step 6 — Child-Level Copy/Paste Reference

This is separate from parameter references.

## Goal

Support Houdini-like:

```text
RMB child -> Copy Reference
RMB destination layer -> Paste Reference Above
```

A pasted referenced child should point to the original child instead of duplicating its data.

UI example:

```text
↗ Pattern IDs
```

Context actions:

```text
Go to Source
Break Reference
Replace Source
Copy Reference
```

Use stable:

```text
LayerId
ChildId
```

Do not use persistent array indices.

For V1:
- only allow placements valid under the current compositor evaluation order;
- do not add arbitrary forward-reference dependency scheduling;
- do not build a node graph.

Stop after Step 6.

---

# Step 7 — Cleanup / Validation

Only after Steps 1–6 are stable.

Validate:

```text
save/reopen
duplicate layer
duplicate child
move layer
move child
delete source
undo/redo
broken references
preview
bake
PCD3D_SM6 shader compile
old assets
Pattern IDs
Cluster IDs
```

Confirm no regression to:
- Pattern random height/chamfer relief;
- tangent normal rotation;
- mask accumulation order;
- Region ID nearest-producer behavior.

Return a final changed-file map and any remaining limitations.

---

# Architecture Summary

Keep these concepts separate:

```text
REFERENCE
A = B
```

```text
DRIVER
A = Combine(LocalValue, SourceSignal)
```

```text
CHILD REFERENCE
Child B -> instance/reference of Child A
```

No expressions.

No arbitrary dependency graph in V1.

No unlimited driver stacks.

No sub-child hierarchy in this task; that is a separate feature.
