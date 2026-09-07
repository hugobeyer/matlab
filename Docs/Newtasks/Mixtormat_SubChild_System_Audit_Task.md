# Mixtormat — One-Level Sub-Child System

Status: **Audit/design task only. Do not implement yet.**

Project: **Mixtormat / MaterialLab / MatLab**  
Target: **Unreal Engine 5.8**

## 0. First instruction: audit before touching code

Before changing anything:

1. Read the current working tree.
2. Read:
   - `Docs/PatternIdsPlan.md`
   - `Docs/GroupLayerAndMaskInstancePlan.md`
3. Audit the current child data model, layer tree Slate widgets, selection/reorder logic, inspector visibility, compositor child execution, mask accumulator, Region ID producer/consumer chain, effect execution and post-composite filters.
4. Verify every line anchor against the current source.
5. **Do not implement the sub-child system yet.**
6. Return the audit report requested at the end and stop.

This task is intentionally separate from the Driver/Reference task. The two may eventually share reference structures, but do not combine their implementation before the audits establish the correct architecture.

---

# 1. Goal

Mixtormat currently has:

```text
Layer
  ├─ Child
  ├─ Child
  └─ Child
```

We want to investigate adding exactly one extra persistent hierarchy level:

```text
Layer
  └─ Primary Child
       ├─ Sub-child
       ├─ Sub-child
       └─ Sub-child
```

Examples:

```text
Concrete
 ├─ Pattern IDs
 │   ├─ Random Per ID
 │   └─ Edge Driver
 │
 ├─ Edge Wear
 │   ├─ Mask Input -> Pattern IDs / Edge
 │   └─ Roughness Driver -> Pattern IDs / Random Per ID
 │
 └─ Craquelure
     └─ Mask Input -> Generated Mask
```

The key constraint:

```text
ONE nested level only.
```

Do not allow:

```text
Child
  └─ Sub-child
       └─ Sub-child
            └─ ...
```

The purpose is to expose dependencies/modifiers where they matter without turning Mixtormat into a node graph.

---

# 2. What a sub-child should mean

A sub-child must be owned by and scoped to its parent child.

It should not automatically become another globally ordered layer child.

The audit must separate these possible meanings.

## A. Input sub-child

Supplies data to the parent before the parent executes.

Example:

```text
Edge Wear
  └─ Mask Input -> Pattern IDs.Edge
```

This is analogous to plugging an input into a node, while still presenting it as a compact tree.

---

## B. Parameter Driver sub-child

Represents a persistent driver attached to one parent parameter.

Example:

```text
Edge Wear
  └─ Roughness Driver
       Source: Pattern IDs.RandomPerID
       Combine: Multiply
       Amount: 0.75
```

The inspector popover from the separate Driver task may create/edit this object.

The sub-child is its persistent visible representation in the layer tree.

---

## C. Output modifier sub-child

Takes the parent's generated signal and modifies it before the signal is consumed.

Example:

```text
Generated Mask
  └─ Remap
```

Potential flow:

```text
Raw Generated Signal
      ↓
Sub-child modifier(s)
      ↓
Mask shaping
      ↓
Blend into CombinedMask
```

or:

```text
Raw Generated Signal
      ↓
Mask shaping
      ↓
Sub-child modifier(s)
      ↓
Blend into CombinedMask
```

These two orders are not equivalent.

The audit must determine which is correct per sub-child type. Do not invent one universal order if the current compositor cannot support it cleanly.

---

## D. Derived output sub-child

A parent such as Pattern IDs or Cluster IDs may expose a derived signal:

```text
Pattern IDs
  └─ Random Per ID
```

This does **not** necessarily mean that `Random From IDs`, which is currently a sibling child type, should immediately be migrated or nested.

Audit whether:

1. existing sibling consumers remain exactly as they are;
2. a sub-child is only a visual/input binding to the same underlying mechanism;
3. selected existing consumers should later be allowed as either sibling or nested form;
4. duplication of semantics would be confusing.

Do not change the existing Region ID producer/consumer contract during the audit.

---

# 3. Intended V1 sub-child categories

Audit this list.

Likely useful categories:

```text
Mask Input
Reference
Driver
Remap
ID Mapping
Transform / local input transform
Combine
```

Possible later categories:

```text
Curve/Ramp remap
Channel extract
Clamp
Smooth
Threshold
Noise modulation
```

Do not allow arbitrary full effects as recursive children in V1.

For example, this should **not** automatically be legal:

```text
Erosion
  └─ Chipping
       └─ Craquelure
```

That is a node graph disguised as a tree.

---

# 4. UI principle

Collapsed:

```text
▸ Edge Wear
```

Expanded:

```text
▾ Edge Wear
   ↳ Mask Input
   ↳ Roughness Driver
```

Or:

```text
▾ Pattern IDs
   ↳ Random Per ID
   ↳ Edge Driver
```

Keep the existing layer hierarchy readable.

Suggested meaning of icons:

```text
↗  Reference
◉  Driver
▧  Mask Input
≈  Remap
#  ID Mapping
```

Do not rely on these exact glyphs if the existing icon system has better matching assets.

---

# 5. Existing layer-tree architecture to audit

## 5.1 `SMixtormatLayerGroup`

Files:

```text
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroup.h
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroup.cpp
```

Current architecture already provides:

```text
Layer header
SVerticalBox Children
AddChild(...)
```

The current group is essentially:

```text
Layer row
  ↓
flat list of child rows
```

Audit whether the cleanest sub-child design is:

### Option A

Each primary child row owns a nested `SVerticalBox`.

### Option B

Introduce a `SMixtormatChildGroup` analogous to `SMixtormatLayerGroup`:

```text
primary child header
nested sub-child box
```

### Option C

Keep Slate flat and calculate indentation from a path.

Prefer the least fragile design that preserves drag/drop and compact row sizing.

---

## 5.2 `SMixtormatLayerChildRow`

Files:

```text
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerChildRow.h
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerChildRow.cpp
```

The current child row is approximately 20 px tall and already provides:

```text
Icon
Name
Kind
Badge
status dot
selection
RMB SMenuAnchor
drag source
```

It is deliberately compact.

Audit whether to:

```text
add an expand/collapse affordance only when SubChildren.Num() > 0
```

or wrap it in a separate group widget so the existing row stays unchanged.

Do not make all child rows taller.

---

# 6. Runtime data model to audit

Primary file:

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
```

Current important types:

```text
EMixtormatLayerChildType
FMixtormatLayerChild
FMixtormatLayer
```

Potential direction:

```cpp
UENUM(BlueprintType)
enum class EMixtormatSubChildType : uint8
{
    MaskInput,
    Reference,
    Driver,
    Remap,
    IdMapping
};

USTRUCT(BlueprintType)
struct FMixtormatSubChild
{
    GENERATED_BODY();

    EMixtormatSubChildType Type;
    ...
};

// FMixtormatLayerChild
TArray<FMixtormatSubChild> SubChildren;
```

This is only a starting proposal.

Audit whether a more semantic name such as:

```text
FMixtormatChildModifier
FMixtormatChildInput
```

is better.

Do not create recursive `TArray<FMixtormatLayerChild>` nesting.

---

# 7. Identity and references

If a sub-child can reference a layer/child/output, stable identity is required.

Current source still contains index-based reference behavior such as:

```text
HeightReferenceLayerIndex
```

Audit:

```cpp
FGuid LayerId;
FGuid ChildId;
FGuid SubChildId; // only if needed
```

Do not use:

```text
LayerIndex + ChildIndex
```

as persistent serialized identity for the new system.

Indexes remain valid for transient UI/render traversal, but not saved references.

The audit must coordinate this with the separate Driver/Reference design without implementing either yet.

---

# 8. Selection model

Current editor selection is largely represented through:

```text
SelectedLayerIndex
SelectedMaskIndex
SelectedEffectIndex
GetSelectedChildIndex()
```

Primary files:

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat.h
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
```

One extra hierarchy level may make the current parallel-index approach fragile.

Audit a path-like selection structure, for example:

```cpp
struct FMixtormatSelectionPath
{
    int32 LayerIndex;
    int32 ChildIndex;
    int32 SubChildIndex;
};
```

or preferably a stable-ID equivalent for persistent selection if appropriate.

Do not replace the existing selection system during the audit. Report whether it should be refactored before sub-children are implemented.

---

# 9. Drag/drop and reorder semantics

Current child reorder logic is in:

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
```

Trace:

```text
FMixtormatChildDragDropOp
SMixtormatChildDropTarget
Move/reorder child code
DuplicateLayerChild
remove functions
selection index remapping
```

The audit must define:

### Primary child reorder

```text
Child A
Child B
Child C
```

continues to behave exactly as today.

### Sub-child reorder

Allowed only within its parent for V1:

```text
Edge Wear
  Driver A
  Driver B
```

### Cross-parent drag

Prefer **not allowed in V1** unless the audit shows it is trivial and semantically clear.

### Promote/demote

Do not silently support dragging a sub-child into the global sibling list.

If useful later, make it an explicit operation.

---

# 10. Current compositor architecture

Primary file:

```text
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
```

The audit must map sub-child semantics onto the real execution stages.

---

## 10.1 Flatten/gather

Trace:

```text
FChildRenderData
FLayerRenderData
capture of FMixtormatLayerChild
SourceChildIndex
```

Decide whether render data should become:

```cpp
FChildRenderData
{
    ...
    TArray<FSubChildRenderData> SubChildren;
}
```

or whether sub-children should compile into the parent's render-data fields during gather.

Strong preference:

If a sub-child only changes a uniform parameter/reference, resolve it during gather rather than creating another GPU dispatch.

---

## 10.2 ID producer stage

Trace:

```text
RegionIdMaps
Cluster IDs
Pattern IDs
FindRegionIdsAbove(...)
HsvFilter
RandomId
RampId
producer demand culling
selected preview
```

Do not break current sibling semantics.

Important question for the audit:

If:

```text
Pattern IDs
  └─ Random Per ID
```

exists as a sub-child, should it:

1. materialize an R16F scalar map owned by Pattern IDs;
2. remain virtual and be evaluated by its consumer;
3. simply represent a Driver configuration that consumes the parent's R32_UINT Region ID map?

Option 3 may be cheapest.

Determine this from the actual consumers.

---

## 10.3 Ordered mask accumulator

Trace:

```text
CombinedMask
MaskPassIndex
MaskTargets
Generated
Craquelure
RandomId
ColorId
Mask
Stain mask-producing behavior
```

The current mask chain is ordered.

If a parent is a mask-producing child:

```text
Generated Mask
  └─ Remap
```

the sub-child must not accidentally consume another global `MaskPassIndex` unless it truly is another ordered layer-mask operation.

Prefer keeping the sub-child internal to the parent and performing its transform inside the parent's pass when possible.

Audit exactly where this is possible.

---

## 10.4 Main composite

Shader:

```text
Shaders/Private/MixtormatComposite.usf
```

CPU binding:

```text
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
FMixtormatCompositeCS
```

Trace:

```text
SamplePlacementMask()
SamplePlacementHeightMask()
LayerSourceUV()
Region variation
Pattern UV variation
normal sampling
coverage
height
RAM
effects
debug
```

If a sub-child drives a main-composite parameter, it may need a driver signal available here.

That belongs to the separate Driver system, but the sub-child audit must verify the tree representation does not imply an execution order that the composite cannot support.

---

## 10.5 Effects

Audit every `EMixtormatLayerChildType::Effect` path.

Current effects include several different execution models:

```text
Stain
Erosion
Grade
Chipping
Peeling
asset-backed effects
```

Some participate in or produce masks before the composite; others run after the composite.

A sub-child on an effect cannot be implemented by one universal "run all modifiers after parent" rule.

Example:

```text
Erosion
  └─ Mask Input
```

must affect erosion before the erosion pass runs.

Example:

```text
Grade
  └─ Amount Driver
```

must be available when the Grade shader parameters are set.

Map these exact integration points.

---

# 11. Parent-output semantics

The audit must define what each parent can expose.

Produce a table like:

| Parent child type | Base output | Possible sub-child inputs | Possible published outputs |
|---|---|---|---|
| Mask | shaped scalar | reference/remap/driver | scalar mask |
| Generated | generated scalar | driver/remap | raw/shaped scalar |
| Craquelure | crack signal / relief | mask/driver | crack mask, distance if public |
| ColorId | scalar mask | remap | resolved mask |
| Cluster IDs | R32_UINT IDs | ID mapping | Region IDs |
| Pattern IDs | R32_UINT IDs + existing Pattern fields | ID mapping | IDs, edge/interior if viable |
| Random From IDs | scalar | remap | scalar |
| Ramp From IDs | scalar/relief | remap | ramp signal |
| Effect | effect-specific | mask/driver/reference | coverage where viable |

Do not publish implementation-only scratch buffers just because they exist.

---

# 12. Child ordering versus sub-child ordering

Current global child order has semantics.

For example:

```text
Pattern IDs
Random From IDs
Mask
Effect
```

may depend on nearest-producer or mask-chain order.

A nested child must not silently reorder the global execution.

The audit must specify:

```text
Global Child Order
   +
Local Sub-child Order
```

and how the two combine.

Recommended conceptual rule:

```text
For each primary child in existing global order:
    resolve its local inputs/drivers
    run the primary child at its normal compositor stage
    resolve only local output modifiers that belong to that primary output
continue existing global order
```

But verify this against the real multi-stage compositor before approving it.

---

# 13. Editor inspector behavior

Files:

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat.h
```

Current inspector uses many type-specific:

```text
GetSelected*()
Build*Controls()
visibility lambdas
MakeMemberSlider<T>()
MakeMemberSliderInt<T>()
MixtormatRow helpers
```

Audit how selecting a sub-child should work.

Preferred behavior:

```text
select primary child
    -> existing parent inspector

select sub-child
    -> compact sub-child inspector
```

Do not show both stacked simultaneously.

Pattern IDs already exposed a known trap where both the child-inspector and layer-inspector visibility lists must know every child type. A new selection category must not repeat that bug.

---

# 14. Context menus

Primary file:

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
```

Current categories have separate menus:

```text
BuildLayerContextMenu
BuildMaskContextMenu
BuildGeneratedContextMenu
BuildEffectContextMenu
```

Audit desired additions.

On a primary child:

```text
Add Input...
Add Driver...
Add Reference...
Add Remap...
Copy Reference
Duplicate
Remove
```

On a sub-child:

```text
Go to Source
Replace Source...
Disable
Duplicate
Remove
```

Do not show operations that are invalid for the parent type.

---

# 15. How the Driver popover and sub-child tree should cooperate

This is the intended relationship between the two separate tasks.

Example:

```text
Edge Roughness   [0.35]  ○
```

User clicks `○`.

Popover:

```text
Source   Pattern IDs
Output   Random Per ID
Combine  Multiply
Amount   0.75
```

After confirmation, the persistent hierarchy may show:

```text
▾ Edge Wear
   ◉ Roughness Driver
```

Selecting that sub-child opens the same driver configuration in the inspector.

Therefore:

```text
Popover = convenient creation/edit UI
Sub-child = persistent visible dependency
```

Do not make the user manually create a sub-child before using the popover.

The audit must decide whether every Driver should appear as a sub-child or whether there is a threshold/visibility rule to avoid noisy trees.

Recommended V1: every persistent driver appears as a sub-child, but parent rows remain collapsed by default.

---

# 16. References as sub-children

A whole-child reference may sometimes be represented directly by the parent row:

```text
↗ Pattern IDs
```

instead of wasting another row:

```text
Pattern IDs
  └─ Reference
```

Audit this distinction.

Recommended rule:

### The primary child itself is an instance/reference

Display:

```text
↗ Pattern IDs
```

with source metadata on the row/inspector.

### One input of a normal parent is referenced

Display:

```text
Edge Wear
  └─ ↗ Mask Input
```

This keeps the hierarchy semantically meaningful.

---

# 17. Serialization traps

Audit all of the following:

- `EMixtormatLayerChildType` serialized by value: append-only.
- if a new `EMixtormatSubChildType` is introduced, also treat it as append-only.
- old assets have no `SubChildren`.
- old assets have no GUID identity unless the separate reference work adds it.
- duplicate behavior.
- undo/redo.
- source deletion.
- dangling references.
- copy/paste.
- child reorder.
- layer reorder.
- editor restart.
- cooked/baked material data if relevant.

Do not require destructive asset migration.

---

# 18. Performance rules

A sub-child should not automatically mean another full-resolution compute pass.

Audit each proposed type.

Prefer:

```text
CPU/gather-time resolution
shader uniform
reuse of existing source texture
inline remap in parent shader
```

before:

```text
new R16F texture
new compute pass
copy pass
snapshot
```

Examples:

### Cheap

```text
parameter direct reference
invert flag
amount
uniform remap
ID hash evaluated in existing consumer shader
```

### Potentially expensive

```text
persistent effect coverage snapshot
post-parent spatial blur
arbitrary chain of full-resolution mask modifiers
```

Recommend explicit bounds for V1.

---

# 19. Preview/debug behavior

Audit:

```text
selected child preview
mask eye toggle
Pattern/Cluster ID previews
debug output target
sub-child preview
```

A selected sub-child should have a defined preview:

```text
Mask Input -> scalar mask
Driver -> resolved driver signal
ID Mapping -> mapped scalar result
Reference -> referenced signal
Remap -> post-remap signal
```

Do not let a later child overwrite an earlier selected preview.

Reuse the existing selected-preview gating model where possible.

---

# 20. Baking parity

Audit:

```text
Source/MixtormatEditor/Private/MixtormatBakeService.cpp
MixtormatLayerPreview.*
SMixtormatPreviewViewport.*
```

Sub-children must affect:

```text
live preview
bake
save/reopen
```

identically.

If baking uses a different compositor path, document every duplicate integration point.

---

# 21. Suggested V1 scope to evaluate

Audit and either approve or revise.

### Hierarchy

```text
Layer
  -> Primary Child
       -> Sub-child
```

one level only.

### Initial sub-child types

```text
Mask Input
Reference/Input Reference
Parameter Driver
Remap
ID Mapping
```

### Rules

- sub-child owned by one parent;
- no cross-parent drag in V1;
- no recursive nesting;
- no arbitrary full Effect as sub-child;
- primary global child order remains unchanged;
- local sub-child order only affects the parent;
- parent collapsed by default;
- add expand affordance only when required;
- driver popover may automatically create a Driver sub-child;
- source references use stable identity, not persistent indices.

---

# 22. Files that must be audited

## Runtime

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
Source/MixtormatRuntime/Private/MixtormatMaterial.cpp
Source/MixtormatRuntime/Public/MixtormatMask.h
Source/MixtormatRuntime/Public/MixtormatEffect.h
```

## Compositor

```text
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
Shaders/Private/MixtormatComposite.usf
Shaders/Private/MixtormatMaskOps.ush
Shaders/Private/MixtormatRegionId.ush
Shaders/Private/MixtormatPatternIds.usf
Shaders/Private/MixtormatRampIdRelief.usf
Shaders/Private/MixtormatEdgeShade.usf
```

Also inspect every effect/mask shader actually used by the parent types proposed for V1.

## Layer tree

```text
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroup.h
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroup.cpp
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerChildRow.h
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerChildRow.cpp
Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat.h
```

## Inspector

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp
Source/MixtormatEditor/Private/UI/Controls/SMixtormatSlider.*
Source/MixtormatEditor/Private/UI/Rows/MixtormatRow.*
Source/MixtormatEditor/Private/UI/Menus/MixtormatMenuBuilder.*
Source/MixtormatEditor/Private/UI/Atoms/*
```

## Preview/bake

```text
Source/MixtormatEditor/Private/MixtormatLayerPreview.*
Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.*
Source/MixtormatEditor/Private/MixtormatBakeService.*
```

Search for actual locations if paths have moved.

---

# 23. Do not implement during this audit

Do not:

- add `SubChildren`;
- add a new enum;
- add Slate widgets;
- add GUIDs;
- modify child selection;
- modify drag/drop;
- modify compositor passes;
- alter mask ordering;
- alter ID producer ordering;
- alter Pattern IDs;
- expose new scratch textures;
- change serialized enum ordering;
- remove existing functionality.

This phase is only to establish the correct design and exact touchpoints.

---

# 24. Required audit report

Return one report with these sections.

## A. Current tree/selection model

Exact files/line anchors for:

```text
layer row
child row
selection
RMB
drag/drop
duplicate/remove
inspector selection
```

## B. Current compositor execution model

Explain:

```text
capture/gather
ID producer stage
mask accumulator
composite
effects
post-composite passes
preview
bake
```

## C. Parent-type taxonomy

For every current primary child type, report:

| Parent Type | Can own sub-children? | Useful types | Execution hook | Risks |
|---|---:|---|---|---|

Include Pattern IDs and every effect family.

## D. Recommended runtime data model

Choose and justify:

```text
FMixtormatSubChild
FMixtormatChildModifier
another model
```

Include stable identity strategy but do not implement it.

## E. Selection/refactor recommendation

State whether the current:

```text
SelectedMaskIndex / SelectedEffectIndex
```

can safely extend another level or should become a unified selection path first.

## F. Slate hierarchy recommendation

Choose:

```text
nested SVerticalBox owned by child
new SMixtormatChildGroup
flat rows with indent/path
```

and explain why.

## G. Driver-popover integration

Explain exactly how a driver created in the inspector becomes a visible sub-child, and how editing/removal stays synchronized.

## H. Evaluation semantics

Define exact ordering for:

```text
Input
Driver
Parent pass
Output modifier
Global next child
```

and identify parent types that require exceptions.

## I. Performance estimate

For the proposed V1, estimate:

```text
extra dispatches
extra full-resolution textures
snapshots
CPU structures
Slate cost
```

## J. Serialization/migration plan

Explain:

```text
old assets
new enum values
GUID initialization
duplicate
move
delete
undo/redo
dangling source
```

## K. Proposed V1 file map

List all files that would be changed.

Do not modify them yet.

## L. Risks / conflicts with the separate Driver+Reference task

Explicitly state what should be shared between the two features and what must remain separate.

## M. Questions requiring Hugo's decision

Only include genuine product/UX choices not answerable by source inspection.

Then stop and wait for approval.
