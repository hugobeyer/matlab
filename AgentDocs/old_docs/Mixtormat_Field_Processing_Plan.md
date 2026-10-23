# - ref files at end of doc -

# Mixtormat Field Processing Plan

## Scope

This plan covers the next implementation pass for:

- Color Ramp as a real RGB/albedo producer
- Color Output References driving Base Color
- shared ramp/curve point crossing and selection
- Height Curve becoming Height Remap
- Height Blend source correctness
- the minimum field architecture needed before Noise / Flow / UV modules

This is deliberately **not** a node-graph redesign.

The existing ordered child/module stack remains the core authoring model.

## Constraints

- No tests.
- Do not add test infrastructure.
- Do not recreate removed tests.
- Do not generalize everything at once.
- Preserve serialized enum values and existing assets wherever possible.
- Prefer display-name changes and appended fields over enum/value churn.
- Keep mask `0..1` math separate from signed scalar math.
- Keep Color as RGB throughout the pipeline.
- Keep current generator modules generator-scoped unless a generalization is genuinely cheap and unambiguous.

---

# Phase 1 — Correctness Fixes First

## 1. Fix Height Blend source validity

Audit results indicate a possible mismatch between:

- `SourceChildIndex != INDEX_NONE`
- successful lookup of the actual source height texture

The shader's missing-source neutral behavior is correct, but the caller may still set `HasSource = true` after the texture lookup fails and then substitute `RunningHeight`.

That can produce incorrect results such as:

- Add → `A + A`
- Subtract → `A - A`
- Difference → zero

### Required change

Resolve the source texture first.

Set `HasSource` from **actual source texture availability**, not merely from a stored child index.

Pseudo-flow:

```text
SourceTexture = null

if SourceChildIndex valid:
    SourceTexture = GeneratorModuleHeights.Find(...)

HasSource = SourceTexture != null

if !HasSource:
    bind a safe texture for RDG validation
    shader uses operation-specific missing-source neutral behavior
```

Do not change operation semantics in this pass.

### Also inspect

`SourceLayerId` appears unused.

Do not wire cross-layer Height Blend yet.

Document it as future scalar-field work unless removing it is serialization-safe and clearly desirable.

---

# Phase 2 — Shared Ramp Editor Foundation

This must be fixed before polishing either Height Remap or Color Ramp.

## 2. Allow horizontal point crossing

Current ramp points cannot cross because both scalar and color editors clamp X against neighboring points.

The runtime ramp math already sanitizes/sorts.

The editor should stop treating array index as immovable point identity during drag.

### Preferred behavior

During drag:

1. move the active point freely within its allowed domain
2. when it crosses a neighboring point, swap those adjacent points
3. update the dragged point index immediately
4. preserve selection/hover state
5. keep the array sorted continuously

Prefer adjacent swapping over full-array resort/permutation every frame.

This minimizes state repair because the current UI already uses index-based state.

### Shared editor state to update

The shared editor should own:

- `SelectedPoint`
- `DragPoint`
- `HoverPoint`

Selection must persist after mouse-up.

When a point crosses:

- swap payload entries
- swap any parallel per-point state
- update `SelectedPoint`
- update `DragPoint`
- update `HoverPoint`

### Scalar-specific parallel state

`EscapeArms` must move with its scalar ramp point.

Do not allow it to remain indexed to the previous array position.

### Endpoint policy

Do **not** hard-code one endpoint policy into the base editor.

The base editor should provide shared crossing/reordering mechanics.

Recommended current policy:

- Scalar signed remap endpoints: keep current canonical locked-X behavior unless explicitly redesigned later.
- Color ramp: allow normal stop movement according to its current domain rules.
- Interior points: may cross freely.

### Minimum point spacing

Avoid zero-width segments.

Maintain a tiny epsilon separation after a crossing.

Do not prevent crossing with the epsilon; choose the new side once the pointer crosses the neighbor.

## 3. Improve point hit testing

Current hit testing is too X-centric.

For scalar curves, hit testing should consider the actual 2D point position.

Prefer:

1. nearest point within hit radius
2. selected point on a tie
3. stable result when points have similar X positions

This becomes more important once points can cross.

## 4. Add persistent selected-stop state

Color Ramp currently relies mainly on hover/drag and a Ctrl-click picker.

Add persistent selection to the shared editor.

Selection rules:

- clicking a point selects it
- inserting a point selects the new point
- dragging keeps it selected
- deleting the selected point selects a sensible adjacent point
- crossing keeps selection attached to the dragged payload

## 5. Fix widget data resynchronization

Audit the ramp widgets for stale state when switching between multiple children.

The inspector is persistent, while ramp widgets may currently snapshot the ramp only during construction.

Required behavior:

- when the selected child changes, the visible ramp must represent the newly selected child's data
- do not overwrite live drag edits mid-interaction
- resync from the bound attribute when not dragging/editing

Do this in the shared ramp architecture if possible.

## 6. Wire interactive edit delegates consistently

Height Curve / Height Remap and Color Ramp should use the same begin/end interactive edit lifecycle already used by other ramp consumers.

This should prevent unnecessary compose/history churn during drag.

No test work.

---

# Phase 3 — Color Ramp UX

## 7. Rename the user-facing module

Keep serialized child/struct enum values stable for now.

Change the user-facing concept from:

`Height Color Ramp`

to:

`Color Ramp`

or, where additional context is needed:

`Height → Color`

Recommended primary display name:

**Color Ramp**

Its current input is generator running height, but the transform itself is scalar → RGB and should not be semantically branded as height forever.

## 8. Keep the current simple authoring flow

The first scalable UX should be:

```text
COLOR RAMP

[ gradient ramp with stop handles ]

[ selected swatch ]  [ compact color controls / picker ]

Interpolation: ...
```

The stop handles themselves remain the primary visual swatches.

Do not add a second redundant large palette UI.

### Selected-point color editing

When a stop is selected:

- show its current color immediately below the ramp
- provide a compact swatch/button to invoke Unreal's color picker
- optionally expose compact RGB/HSV/hex controls only if an existing reusable control already exists

Do not build a large detached color editor.

Keep Ctrl-click picker as an accelerator if already useful.

### Presets

Architect the UI so presets can be added later.

Do not implement the preset library in this pass unless trivial.

### Histogram

Defer histogram implementation.

There is no established histogram reduction/readback UI path yet, and it would expand this task substantially.

Leave room in layout/design, but do not add GPU readback work now.

### Palette/perceptual modes

Defer palette generation, Oklab/Oklch/HSV-style palette authoring, procedural palette tools, etc.

Those should be separate future modules/features.

---

# Phase 4 — Make Color a Real Base Color Field

## 9. Fix published Color completeness

`FPublishedField::IsComplete()` currently needs explicit support for `Color`.

Add a Color case that validates the expected RGBA texture format.

This is required for:

- normal Color OutputReference resolution
- prefix cache snapshot persistence/restoration

Avoid silent failure for future field kinds where practical.

At minimum, keep the switch exhaustive and obvious.

## 10. Do not use `Bundle.NamedColors` as the architecture

`Bundle.NamedColors["Color"]` appears unused.

The canonical architecture should be:

`PublishedFieldOutputs`

Do not build future Color/Noise/Flow systems around `NamedColors`.

If the map is confirmed dead and safe to remove, remove it in the appropriate cleanup pass.

Otherwise leave it unused until cleanup.

## 11. Add resolved Color reference state to layer composition

Mirror the existing UV-reference idea.

Recommended runtime compose path:

```text
OutputReference(Color)
        ↓
AddOutputReferencePasses
        ↓
LayerCtx.ReferencedColor
        ↓
resolved layer input / Base Color path
        ↓
normal layer composition
```

Do not convert Color to grayscale.

Do not send it through mask infrastructure.

## 12. Resolve referenced Color through layer input

Prefer integrating Color into the existing resolved layer-input path rather than bolting a separate late override into the final compositor.

Target behavior:

- source Color field is already at output resolution
- it becomes the destination layer's Base Color input
- existing HSV, Base Color blend, influence, opacity and layer compositing continue to work afterward

Where appropriate, resolve:

```text
OutputBC.rgb = ReferencedColor.rgb
```

while preserving expected alpha semantics.

Do not accidentally apply the destination layer UV transform a second time to a field already produced at output resolution.

## 13. Fix Fill / Generator Base Color eligibility

Verify the shader branch that chooses whether `SampledBaseColor` is taken from the resolved layer input.

Audit found that Fill / Generator layers may otherwise fall back to white unless `HasSurface` or another prepared-input condition is true.

When a valid referenced Color exists, the destination layer must use it even without a material surface.

Add an explicit resolved/referenced-color condition rather than pretending the layer has a material surface.

## 14. Define override precedence

When both exist:

- authored flat Fill/Base Color
- referenced Color field

the reference should act as the layer's actual Base Color source.

Recommended rule:

**Referenced Color wins as the input source; the existing Base Color blend/influence stage still applies afterward.**

Do not silently multiply a referenced Color by the flat Fill Color unless that is already an explicit blend operation.

Inspector can later show a small "from reference" state on the flat color control if useful.

## 15. Add Color OutputReference preview

A Color reference should be previewable just like its source Color output.

Reuse the existing color blit preview path.

Do not create another preview shader unless necessary.

## 16. Preserve cache behavior

Ensure Color published fields survive:

- prefix snapshot save
- prefix restore
- recomposition after cache reuse

The `IsComplete(Color)` fix should make this work through the generic published-field snapshot path.

Verify statically that no Color-specific cache omission remains.

---

# Phase 5 — Height Curve → Height Remap

## 17. Preserve serialization, change the user-facing concept

Keep existing serialized child type / enum values where possible.

Rename display text:

`Height Curve`
→ **Height Remap**

The current struct can keep its serialized identity while its payload expands.

Longer term it can become a generic scalar remap without another data migration.

## 18. Introduce a reusable signed remap payload

Prefer a reusable lower-level structure such as:

```text
FMixtormatScalarRemap
```

embedded by the existing Height Curve/Remap child.

Do not make `FMixtormatMaskShaping` the base class/data structure.

Mask semantics are fundamentally different.

Suggested authored fields:

```text
bNormalizeInput
InputMin
InputMax
Balance
Contrast
Offset
bInvert
Curve
Amount
```

Defaults must reproduce the current identity behavior.

## 19. Lock the signed processing order

Recommended signed pipeline:

```text
input
→ optional zero-preserving normalization
→ signed input-range remap
→ curve
→ balance
→ contrast
→ offset
→ invert
→ Amount lerp against original input
```

If implementation constraints favor contrast before balance, settle it once and document it.

Do not let CPU/UI/GPU use different orders.

## 20. Signed normalization

Never use ordinary min-max normalization that shifts zero.

Use zero-preserving max-absolute normalization:

```text
x / max(abs(min), abs(max))
```

A constant/degenerate input should resolve deterministically.

No NaNs.

## 21. Signed input range

Input range must respect zero as the pivot.

Default:

```text
InputMin = -1
InputMax = 1
```

Recommended conceptual mapping:

```text
x >= 0:
    x / InputMax

x < 0:
    x / abs(InputMin)
```

Handle zero/degenerate range safely.

Do not force final saturation unless explicitly part of the authored operation.

## 22. Signed balance

Use a sign-preserving power transform:

```text
sign(x) * pow(abs(x), exponent)
```

Identity at the center/default Balance value.

Do not reuse mask balance math directly.

## 23. Signed contrast

Contrast pivots around zero:

```text
x * Contrast
```

Default `1`.

## 24. Signed offset

Offset is simple signed addition:

```text
x + Offset
```

Default `0`.

## 25. Signed invert

Invert means:

```text
-x
```

Not `1-x`.

## 26. Curve

Keep `FMixtormatScalarRamp`.

The curve should remain signed-domain capable.

Do not saturate signed X to `0..1`.

Maintain the authored domain and allow extended values where the current auto-frame system supports them.

## 27. Amount

Keep the current useful behavior:

```text
result = lerp(original, remapped, Amount)
```

Default `1`.

---

# Phase 6 — Fix the Height Remap Widget

## 28. Correct canonical signed-range visualization

The current shared grid visually treats `0..1` as canonical, which makes the negative half of a `-1..1` signed curve look like an invalid/out-of-range region.

Make canonical Y range configurable per ramp/editor mode.

For Height Remap:

```text
Canonical Y = -1..1
```

Only shade outside the actual canonical range.

## 29. Emphasize zero axes

For signed remap:

- horizontal Y=0 line: major
- vertical X=0 line: major

They should be visually equivalent.

This is the main semantic reference of the widget.

## 30. Improve graph readability

Use existing Mixtormat tokens where possible.

Recommended visual adjustments:

- points: ~8–9 px
- selected point: clear accent outline/ring
- curve: approximately 2 px if current 1.6 feels weak
- secondary grid: quieter than zero axes
- viewport padding: enough that endpoints never visually clip
- locked endpoints: visually distinguishable if still locked
- signed area fill should reference zero baseline, not 0..1 mask conventions

Do not turn it into a large graph editor.

Keep current inspector density.

## 31. Auto-frame behavior

Canonical `-1..1` should remain the default view when the curve fits inside it.

If authored points exceed it, frame the actual curve with reasonable padding.

The `F`/frame behavior should remain deterministic.

---

# Phase 7 — Share Only the Right Ramp/Remap Infrastructure

## 32. Keep mask shaping behavior intact

Do not rewrite working mask shaping merely to share code.

Mask semantics remain:

- domain `0..1`
- pivot `0.5`
- saturation/clamping
- `1-x` invert
- mask-specific balance behavior

Height Remap semantics remain signed.

## 33. Share low-level ramp evaluation where valuable

Audit identified divergent constant-interpolation/evaluation implementations.

A low-level shared ramp-point evaluator may be worthwhile if it can unify behavior without forcing mask and signed-domain semantics together.

Safe shared concepts:

- ramp point storage
- interpolation mode
- sorting/sanitize
- GPU payload preparation
- generic ramp segment evaluation

Domain transforms and clamping remain caller-specific.

---

# Phase 8 — Height Blend Future Compatibility

## 34. Keep Height Blend generator-scoped for now

Its math is already close to a generic scalar combiner.

Do not generalize the module yet.

Current generator-local source lookup remains acceptable after the source-validity bug is fixed.

## 35. Do not add cross-layer scalar Blend yet

Future generalization should happen only after there is one real scalar field source abstraction.

At that point the current source lookup can become something like:

```text
ResolveScalarOperand(...)
```

shared by:

- Height/Scalar Remap
- Scalar Blend
- Color Ramp scalar input
- future Noise consumers

Do not invent this resolver prematurely in the current implementation pass unless the code naturally demands it.

---

# Phase 9 — Material Channels Later

## 36. Do not create channel-specific producer modules

Avoid future one-off modules such as:

- Roughness Ramp
- Metallic Ramp
- AO Ramp

The producer should publish a typed scalar field.

The destination determines how that field is interpreted.

## 37. Future destination-channel binding

Likely future model:

```text
Scalar field reference
Target Channel:
    Roughness
    Metallic
    AO
    ...
```

Potential modes can later include:

- Replace
- Multiply
- Add
- Min
- Max
- Lerp / influence

Do not implement these now.

## 38. Keep Drivers separate

Parameter Drivers are not the same system as per-pixel field transport.

Do not merge them.

Drivers modulate authored scalar parameters.

Published fields transport per-pixel data.

---

# Phase 10 — Prepare for Noise / Flow / UV

Before adding many new modules, harden the field architecture just enough to avoid repeating the Color bug.

## 39. Published field kinds need explicit semantic domains

Future likely semantic field types:

```text
RegionIds
Color
UVMap
Flow / Vector2

later:
ScalarSigned
Scalar01
SDF
```

Do not represent signed/unsigned semantics only through output names.

A scalar's domain must travel with its field contract.

## 40. Avoid kind/name duplication

Current risks include:

- `IsComplete()` switch
- kind → expected output-name mapping
- capability descriptors
- preview-kind mapping
- scattered literal names such as:
  - `RegionIds`
  - `FlowDirection`
  - `WarpedUV`
  - `Color`

Do not add many more field kinds until these choke points are clearly centralized or at least audited together.

## 41. Keep the published-field registry canonical

The canonical runtime registry should remain the typed published-field registry.

Avoid parallel future registries such as:

```text
NamedMasks
NamedColors
NamedFlows
NamedNoise
...
```

Those become impossible to reason about once modules can cross-reference each other.

## 42. Avoid LayerCtx slot explosion

A few resolved slots are acceptable now:

- Referenced UV
- Referenced Color

But before adding:

- Referenced Scalar
- Referenced SDF
- Referenced Flow
- Referenced Roughness
- Referenced AO
- ...

introduce a small typed resolved-field structure rather than adding unrelated texture members forever.

Do not implement the generic map yet unless the upcoming Noise/Flow pass clearly requires it.

## 43. Flow semantics

When Flow work begins, keep these concepts distinct:

```text
Vector2 / Flow
    direction/displacement-like vector field

UVMap
    absolute or transformed coordinates
```

Typical use:

```text
UV' = UV + Flow * Strength
```

A Flow field is not itself a UV map.

Vector operations will eventually need:

- Add
- Subtract
- Scale
- Rotate
- Normalize
- Blend
- gate/mask

Do not treat vectors as RGB just because both can occupy multi-channel textures.

## 44. Noise semantics

Noise producers should publish fields, not decide material meaning themselves.

Examples:

```text
Value       → ScalarSigned or Scalar01
IDs         → RegionIds
Gradient    → Vector2
Coordinates → UVMap
```

A separate consumer/module decides whether Value becomes:

- height
- gate
- roughness
- color-ramp input
- flow strength

This is the main reason to finish the field contracts before a large Noise module pass.

---

# Recommended Implementation Order

## Pass A — Immediate correctness

1. Height Blend source-validity fix
2. `IsComplete(Color)`
3. Color prefix-cache completeness
4. Color OutputReference preview
5. Color → resolved Base Color
6. Fill/Generator referenced-color eligibility

This pass should make the current Color Ramp genuinely useful.

## Pass B — Shared Ramp UX

1. persistent selection
2. point crossing via adjacent swaps
3. scalar parallel-state swapping
4. improved hit testing
5. widget resync
6. interactive edit lifecycle
7. selected Color stop inline editing

This pass benefits both scalar and color ramps.

## Pass C — Height Remap

1. user-facing rename
2. signed remap payload
3. GPU signed remap math
4. signed inspector controls
5. canonical `-1..1` graph visualization
6. zero axes / point styling / framing

## Pass D — Field Architecture Cleanup

Only after the above works:

1. remove or deprecate dead `NamedColors` path if confirmed
2. centralize field completeness expectations
3. reduce kind/name duplication
4. document typed scalar domains
5. prepare a common scalar-source resolver design

## Pass E — New Modules

Then begin:

- Noise
- Flow field tools
- UV Transform / UV Warp
- scalar channel consumers
- palette/preset systems

---

# Explicit Non-Goals

Do not do any of the following in these passes:

- No tests.
- No test infrastructure.
- No node graph.
- No generic non-generator module system yet.
- No Roughness-specific module.
- No Metallic-specific module.
- No AO-specific module.
- No histogram GPU readback yet.
- No procedural palette generator yet.
- No Oklab/Oklch palette system yet.
- No cross-layer Height Blend yet.
- No mass enum renumbering.
- No conversion of RGB Color fields to grayscale.
- No reuse of mask `0..1` math for signed Height Remap.
- No parallel `NamedNoise` / `NamedFlow` / `NamedScalar` registries.

---

# End State of This Plan

After these passes, Mixtormat should have:

- a Color Ramp that produces real RGB albedo
- Color references that can be copied/instanced into other layers
- consistent Color preview behavior
- ramps whose points can cross naturally
- persistent ramp-point selection
- a compact selected-color editing workflow
- Height Remap with correct signed semantics
- a visually correct signed curve editor
- Height Blend with reliable source handling
- the existing ordered child-stack workflow preserved
- a clear typed-field direction for Noise / Flow / UV without prematurely building a node graph


## Reference Files / Likely Touchpoints

Use these as the primary implementation references for the plan.

### Color Ramp → Albedo / OutputReference

- `Source/MixtormatRuntime/Public/MixtormatOutputReference.h`
  - `EMixtormatPublishedFieldKind`
  - `FMixtormatOutputReference`
- `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp`
  - source validation / resolution
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
  - `FPublishedField`
  - `FPublishedField::IsComplete()`
  - `FMixtormatLayerPassContext`
  - published-field/cache state
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
  - `AddGeneratorHeightColorRampPass`
  - `AddOutputReferencePasses`
  - `AddGeneratorLayerPasses`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`
  - layer input resolution
  - composite shader parameters
  - Base Color binding
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`
  - ordering of reference resolution vs layer-input preparation
  - prefix snapshot save/restore path
- `Shaders/Private/MixtormatComposite.usf`
  - resolved Base Color path
  - `SampledBaseColor`
  - Fill / Generator Base Color conditions
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
  - `GatherGeneratorHeightModuleChild`
  - Height Color Ramp render-data gather
- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`
  - `Color` published-output capability
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerClipboard.cpp`
  - `CopyChildOutput`
  - OutputReference creation
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`
  - published-field reference creation / placement

### Color Ramp Runtime / Shader / UI

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
  - `FMixtormatGeneratorHeightColorRamp`
  - `FMixtormatLayerChild`
- `Source/MixtormatRuntime/Public/MixtormatColorRamp.h`
- `Source/MixtormatRuntime/Private/MixtormatColorRampMath.cpp`
  - sanitize
  - interpolation/evaluation
  - GPU payload prep
- `Shaders/Private/MixtormatGeneratorHeightColorRamp.usf`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.h`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.cpp`
  - stop drag
  - color picker
  - insertion/deletion
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
  - Color Ramp inspector construction

### Shared Ramp / Point Crossing / Selection

- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.h`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.cpp`
  - `DragPoint`
  - `HoverPoint`
  - mouse handling
  - hit testing
  - graph transforms
  - grid / zero-axis drawing
  - framing
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatScalarRamp.h`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatScalarRamp.cpp`
  - `ApplyPointDrag`
  - `InsertPointAt`
  - `EscapeArms`
  - scalar point rendering
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.h`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.cpp`
  - color-stop X constraints
  - stale index captures
- `Source/MixtormatRuntime/Private/MixtormatScalarRamp.cpp`
  - `Sanitize`
- `Source/MixtormatRuntime/Private/MixtormatColorRampMath.cpp`
  - `Sanitize`

### Height Curve → Height Remap

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
  - `FMixtormatGeneratorHeightCurve`
  - `EMixtormatLayerChildType::HeightCurve`
  - parameter-owner entries
- `Source/MixtormatRuntime/Public/MixtormatScalarRamp.h`
- `Source/MixtormatRuntime/Private/MixtormatScalarRamp.cpp`
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
  - Height Curve gather block
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
  - `FGeneratorHeightCurveRenderData`
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
  - `FMixtormatGeneratorHeightCurveCS`
  - `AddGeneratorHeightCurvePass`
- `Shaders/Private/MixtormatGeneratorHeightCurve.usf`
- `Shaders/Private/MixtormatGeneratorHeightModules.ush`
  - signed scalar-ramp evaluation
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
  - `BuildHeightCurveControls`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerBadges.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`
  - user-facing Height Curve labels / creation menu

### Existing Mask-Shaping Reference

Use these for structure only, not for direct signed math reuse:

- `Source/MixtormatRuntime/Public/MixtormatMaskShaping.h`
  - normalize
  - input min/max
  - curve
  - balance
  - contrast
  - offset
  - invert
- `Shaders/Private/MixtormatMaskOps.ush`
  - existing `0..1` shaping math
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`
  - mask shaping UI construction

Important: preserve the distinction between mask `0..1` semantics and signed Height Remap semantics.

### Height Blend

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
  - `FMixtormatGeneratorHeightBlend`
  - height operation enum
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
  - source child resolution
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
  - `FMixtormatGeneratorHeightBlendCS`
  - `AddGeneratorHeightBlendPass`
  - source texture lookup in `AddGeneratorLayerPasses`
- `Shaders/Private/MixtormatGeneratorHeightBlend.usf`
- `Shaders/Private/MixtormatHeightOps.ush`
  - actual height-combine semantics
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
  - `BuildHeightBlendModuleControls`

Primary thing to verify here: `HasSource` must reflect successful source-texture resolution, not merely a valid stored source index.

### Future Typed-Field / Noise / Flow / UV Reference

Do not implement these yet, but keep these files in mind while preventing new special cases:

- `Source/MixtormatRuntime/Public/MixtormatOutputReference.h`
- `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
  - typed published-field registry
- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`
  - published-output descriptors
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
  - Flow / UV publication and resolution
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`
  - cache/published-field lifetime

Avoid growing parallel systems like `NamedColors`, `NamedFlows`, `NamedNoise`, etc. The typed published-field registry should remain the canonical path.