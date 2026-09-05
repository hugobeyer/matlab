# Mixtormat — Pre-Group and Mask Instance Stabilization Plan

Status: Required before `Group` and `MaskInstance` implementation.

Related design: `Docs/GroupLayerAndMaskInstancePlan.md`.

## Goal

Stabilize the existing layer-child model before adding another layer type and another mask-child type.

The new features should extend one clear contract instead of adding more branches to behavior that is already inconsistent.

This plan covers:

- ordered effect execution;
- child selection and removal;
- layer child-capability rules;
- mask taxonomy and menus;
- bulk mask removal;
- stable expanded-row state;
- regression coverage.

## Already fixed

These are no longer blockers:

- Disabled layers are exact composite identities.
- Disabled layers omit effect children but still resolve mask children.
- Chipping visibly seeds from smooth height mixed with cavity.
- Chipping exposes height, cavity, placement-mask, and shaping controls.
- Chipping can use its own mask or the layer's accumulated child mask.
- Texture and generated mask children now use the child inspector.
- Mask selection no longer exposes layer composition and surface controls.
- Layer height blending and normal / height / AO influence remain layer-only.
- The base layer is gone. Every layer is an ordinary layer at an ordinary position.
- A layer can be dragged, moved, hidden, deleted and given children at any position, index 0 included.
- Material and Effect layers can have their surface swapped in place via `Replace Material`.

## Blockers at a glance

| Priority | Problem | Why it blocks groups / instances |
| --- | --- | --- |
| 1 | Filter order is not the displayed child order | The stack promises ordering that the compositor does not execute |
| 2 | Child selection/removal uses fragile split indices | `MaskInstance` would add another missed-case risk |
| 3 | Child-type dispatch relies on mask defaults | A new child type can silently read or mutate the wrong payload |
| ~~4~~ | ~~Base-layer child policy is contradictory~~ | Resolved: there is no base layer. See section 4 |
| 5 | Mask types are split across unrelated menus | A group must expose masks while excluding effects |
| 6 | “Remove All Masks” removes texture masks only | It would leave most of a group mask chain behind |
| 7 | Expanded rows are tracked by array index | Group block insert/delete/move would expand unrelated rows |

## 1. Make filter execution match the child stack

### Current behavior

`MixtormatGpuCompositor.cpp` traverses children in order, but defers filter effects until after the layer composite.

The deferred state is currently:

```cpp
const FEffectRenderData* PendingErosion = nullptr;
const FEffectRenderData* PendingChipping = nullptr;
TArray<const FEffectRenderData*> PendingGrades;
```

Consequences:

- Adding two erosion nodes executes only the last one.
- Adding two chipping nodes executes only the last one.
- Chipping always runs after erosion, regardless of row order.
- Every grade always runs after erosion and chipping, regardless of row order.
- Deferred filters use the final `CombinedMask`, not necessarily the mask state at their row.
- Reordering these rows can change the UI without changing the result.

This differs from the existing ordered behavior tested for a mask before versus after a peeling effect.

### Required contract

The child stack should mean:

1. Mask-like children update the accumulated mask in row order.
2. An effect captures the accumulated mask visible at its row.
3. Surface effects keep their existing pre-composite preparation.
4. Post-composite filters execute in their relative row order.
5. Every enabled, configured filter executes; none is silently replaced by a later filter.

Examples:

```text
Mask A → Erosion → Mask B → Chipping
```

- Erosion uses Mask A.
- Chipping uses the result of Mask A combined with Mask B.
- Erosion runs before chipping.

```text
Chipping → Grade → Erosion
```

- Chipping runs first.
- Grade processes the chipped result.
- Erosion processes the graded result.

### Implementation direction

Replace the singleton pointers and grade-only array with one ordered filter list.

A pending item needs at least:

```cpp
struct FPendingFilter
{
    const FEffectRenderData* Effect = nullptr;
    int32 SourceChildIndex = INDEX_NONE;
    FRDGTextureRef MaskSnapshot = nullptr;
};
```

The exact storage can differ, but it must preserve source-child order.

A filter must not retain a reused mask ping-pong target that a later mask pass overwrites. Copy or snapshot the accumulated mask when required.

Consecutive filters with no mask between them may share one snapshot later as an optimization. Correctness comes first.

After the layer composite:

- execute pending filters in order;
- each filter reads the previous filter's outputs;
- non-height filters carry height and normal through;
- non-color filters carry color and RAM through;
- non-erosion filters carry the current ridge signal through;
- the latest executed erosion defines the outgoing ridge signal.

### Acceptance tests

Add compositor tests proving:

- two erosion nodes both affect the result;
- two chipping nodes both affect the result;
- swapping erosion and chipping changes the result;
- a grade between two filters executes between them;
- a mask after a filter does not retroactively gate that filter;
- a mask before a filter does gate it;
- disabled filters are exact identities;
- amount-zero filters are exact identities.

## 2. Use one selected child index

### Current behavior

`SMixtormat` stores:

```cpp
int32 SelectedEffectIndex;
int32 SelectedMaskIndex;
```

Every selection, removal, reorder, preview, and inspector path must decide which index applies.

This has already produced inconsistent behavior:

- `RemoveGeneratedFromLayer` clears only an exact selected mask.
- It does not decrement a later selected mask index.
- It does not decrement a later selected effect index.
- `RemoveMaskFromLayer` remaps indices but does not resync the inspector header.
- `ClearLayerMask` can leave the inspector header stale.

Adding `MaskInstance` would require updating every mask-type list and every fallback branch.

### Required change

Replace the split fields with:

```cpp
int32 SelectedChildIndex = INDEX_NONE;
```

A selected layer has `SelectedChildIndex == INDEX_NONE`.

A selected child uses the same index regardless of type. Typed getters validate the child type before returning its payload.

Examples:

```cpp
FMixtormatLayerEffect* GetSelectedLayerEffect();
FMixtormatMaskLayer* GetSelectedLayerMask();
FMixtormatGeneratedMask* GetSelectedGeneratedMask();
FMixtormatMaskInstance* GetSelectedMaskInstance();
```

### Centralize index repair

Add shared helpers for structural child edits:

```cpp
RemapSelectedChildAfterRemove(LayerIndex, RemovedChildIndex);
RemapSelectedChildAfterMove(LayerIndex, SourceChildIndex, TargetChildIndex);
ClearSelectedChildForLayer(LayerIndex);
```

Every removal path should:

1. update selection;
2. clear child bypass/debug state if its target disappeared;
3. synchronize the inspector header;
4. refresh the preview;
5. rebuild the affected row list.

### Acceptance tests

Cover:

- removing the selected child;
- removing a child before the selected child;
- removing a child after the selected child;
- removing a mask before a selected effect;
- removing an effect before a selected mask;
- reordering the selected child;
- undo/redo after each operation;
- header, badge, bypass, and debug preview following the correct child.

## 3. Make child-type dispatch exhaustive

### Current risk

Several paths treat every unrecognized child type as a texture mask.

Examples include logic equivalent to:

```cpp
default: return Child.Mask.bEnabled;
```

and preview bypass logic that falls through to:

```cpp
Child.Mask.bEnabled = false;
```

That is safe only while `Mask` is the sole fallback type.

After adding `MaskInstance`, a missed switch case could read or mutate the unrelated `Mask` payload while appearing to work.

### Required change

Create shared child classification and state accessors:

```cpp
bool IsMaskChildType(EMixtormatLayerChildType Type);
bool IsEffectChildType(EMixtormatLayerChildType Type);
bool IsLayerChildEnabled(const FMixtormatLayerChild& Child);
void SetLayerChildEnabled(FMixtormatLayerChild& Child, bool bEnabled);
EMixtormatMaskBlendMode GetMaskChildBlendMode(const FMixtormatLayerChild& Child);
void SetMaskChildBlendMode(FMixtormatLayerChild& Child, EMixtormatMaskBlendMode Mode);
```

Use exhaustive switches for:

- row enable state;
- row enable mutation;
- preview bypass;
- child inspector routing;
- badge and kind text;
- blend-mode menus;
- duplicate/remove operations;
- compositor render-data capture.

An unsupported type should return safely and trigger a development assertion where appropriate. It must not fall through to a different payload.

### Acceptance tests

One table-driven editor test should visit every current child type and verify:

- selected type;
- enabled state;
- toggle behavior;
- bypass behavior;
- inspector route;
- blend-mode access for mask-like children.

Extend that table when `MaskInstance` is added.

## 4. Define one child-capability policy

### Decision: there is no base layer

Resolved 2026-09-05. The alternative the original plan flagged -- "if base-layer children are
desired product behavior, decide that explicitly" -- was chosen, and taken further: the base
layer is not a layer with permissions, it does not exist.

The stack now composites onto an **invisible substrate**: a neutral gray fill with no height and
no features, seeded into the accumulation buffers before the first layer runs. It has no row, no
selection, no children and no inspector. It is not addressable and cannot be edited.

Consequences:

- Every layer is an ordinary layer. Position 0 carries no meaning beyond being the bottom.
- Any layer can be dragged or moved to any position, the bottom included.
- Any layer can be hidden, deleted, or given mask and effect children.
- Deleting the last layer is now reachable; the stack renders as the bare substrate.
- A layer renders identically wherever it sits. That was the point: previously the bottom layer
  seeded the buffers by replacing them, which made it ignore its own mask, feature influence and
  height blend, so dragging a layer to the bottom silently changed what it did.

This is a behavior change for existing saved recipes, not only a refactor. A recipe whose bottom
layer carries a mask, feature influence, or height blending will render differently, because
those now apply where they were previously discarded.

### Implementation status

Done:

- `MixtormatSubstrate` constants and the substrate seed in `MixtormatGpuCompositor.cpp`.
- `Initialize` removed from `FMixtormatCompositeCS` and `MixtormatComposite.usf`, along with
  the five branches that gated behavior on it. It was the base-layer mechanism.
- Every `LayerIndex == 0` / `> 0` policy guard removed from the editor: delete, move, drag/drop
  targets, enable/disable, mask assignment, normal assignment, effect assignment, bulk mask
  removal, the row's `bIsBase` treatment, the `BASE` source label, the context menu, the mask
  gallery, and the four inspector sections that were hidden on the bottom layer.
- `ReplaceSurfaceInLayer` plus its picker, wired to `Replace Material` in the layer context menu.

Deliberately kept, because it is positional rather than policy:

- `SurfaceValid = LayerIndex > 0` in the generated-mask and peel-field passes. Its shader
  contract is "there is accumulated surface beneath this layer to derive from", and the
  substrate is featureless: curvature, AO and height read off it are constants, so a derived
  mask on the bottom layer would produce a flat value rather than the documented no-op. The
  guard is still literally true at position 0.
- `PreviewOverrideLayers[0]` in the solo and composition-before paths, which indexes the single
  layer just pushed into an emptied array.
- `MixtormatLayerPreview.cpp`, the unused legacy preview service, which stays out of scope.

### Required API

Capability checks stay centralized, but they no longer need a layer index -- the layer's own
type is the whole answer, which is a real simplification for the group phase:

```cpp
bool CanLayerAcceptMaskChild(const FMixtormatLayer& Layer);
bool CanLayerAcceptEffectChild(const FMixtormatLayer& Layer);
bool CanLayerAcceptChildType(const FMixtormatLayer& Layer, EMixtormatLayerChildType ChildType);
```

Use the same checks in:

- menus;
- drag/drop;
- gallery assignment;
- every add handler;
- duplication into another owner, if added later;
- load validation and defensive flattening.

Unavailable actions should be disabled with a reason, not displayed as actions that silently do
nothing.

### Acceptance tests

Verify each layer type against each child category. Position is not a column, and that is the
point:

| Owner | Texture mask | Generated | Craquelure | Color ID | Effect |
| --- | ---: | ---: | ---: | ---: | ---: |
| Material | Yes | Yes | Yes | Yes | Yes |
| Fill | Yes | Yes | Yes | Yes | Yes |
| Group | Yes | Yes | Yes | Yes | No |

Add `MaskInstance = Yes` for Material, Fill, and Group when implemented.

Add substrate and position-independence coverage:

- a layer renders identically at the bottom and above another layer;
- a mask on the bottom layer gates that layer against the substrate;
- height blending on the bottom layer blends against substrate height, not against itself;
- an empty stack renders as the bare substrate;
- deleting the last layer empties the stack without invalid selection;
- `Replace Material` preserves children, transform, overrides, position and selection.

## 5. Put every mask type under one UI taxonomy

### Current behavior

The layer context menu presents mask-like children in unrelated places:

- Texture masks are under `Mask`.
- Generated Mask and Color ID are separate top-level actions.
- Craquelure is under `Effect` even though it is a mask child.

This conflicts with the compositor and inspector, where all four participate in the mask accumulator.

There is also a concrete context-menu bug:

- Generated, Craquelure, and Color ID share `BuildGeneratedContextMenu`.
- Its Blend Mode submenu calls `BuildGeneratedBlendModeMenu`.
- That setter accepts only `Generated`.
- Blend Mode therefore silently does nothing for Craquelure and Color ID rows.

### Required menu structure

Use one mask submenu:

```text
Add
  Mask
    Texture Mask...
    Generated Mask
    Craquelure
    Color ID Mask
    Mask Instance...   // added later
  Effect
    Peeling
    Stain
    Erosion
    Chipping
    Grade
```

A Group row reuses the same Mask submenu and disables or omits the Effect submenu according to the capability policy.

### Shared mask context menu

Replace generated-specific context plumbing with mask-child plumbing:

```cpp
BuildMaskChildContextMenu(LayerIndex, ChildIndex);
BuildMaskChildBlendModeMenu(LayerIndex, ChildIndex);
RemoveMaskChildFromLayer(LayerIndex, ChildIndex);
```

The generic blend menu must dispatch through the actual child type.

Each mask-like row should consistently provide:

- Blend Mode;
- Duplicate;
- Remove;
- the same enabled/bypassed semantics;
- its own inspector only.

Texture masks may additionally provide Replace Mask.

### Inspector ordering

Use a predictable common order where fields exist:

1. Source or generated signal;
2. Blend Mode and Weight;
3. Invert;
4. Balance, Contrast, and Offset;
5. type-specific placement or generation controls;
6. enabled state if it remains duplicated in the inspector.

Do not show layer composition, color adjustment, height blending, or surface influence controls for child selection.

## 6. Correct “Remove All Masks”

### Current behavior

`ClearLayerMask` removes only children whose type is exactly `Mask`.

The menu label says `Remove All Masks`, but it leaves:

- Generated masks;
- Craquelure;
- Color ID masks;
- future Mask Instances.

For a Group, that command would appear to clear its mask chain while leaving most of it active.

### Recommended behavior

Make `Remove All Masks` remove every mask-like child through `IsMaskChildType`.

It must preserve effect children on Material and Fill layers.

After removal:

- repair the selected child index;
- clear child bypass/debug state when needed;
- resync the inspector header;
- refresh once;
- rebuild once.

If product intent is texture-only removal, rename the action to `Remove All Texture Masks`. Do not keep the ambiguous current combination.

### Acceptance tests

Start with one child of every current type and verify:

- all mask-like children are removed;
- every effect child remains;
- selection points to the intended surviving child or the owner layer;
- undo restores the exact original ordering.

## 7. Track expanded layers by stable identity

### Current behavior

Expanded rows are stored as:

```cpp
TSet<int32> ExpandedLayerIndices;
```

The set is not remapped after layer insertion, deletion, swap, or drag/drop.

An expanded index can therefore begin referring to another layer after a structural edit.

Groups make this worse because one operation inserts, removes, or moves an entire contiguous block.

### Required change

When `LayerId` is introduced by the group data-model phase, replace index-based expansion state with:

```cpp
TSet<FGuid> ExpandedLayerIds;
```

Rows query expansion through their current `LayerId`.

Benefits:

- insertions do not affect expansion;
- deletion naturally drops an unreachable ID;
- moves preserve expansion;
- moving a group block preserves every member's UI state;
- nested rendering does not need index remap code for expansion.

Prune IDs that no longer resolve after delete, load, migration, and undo/redo.

### Interim option

If this bug must be fixed before GUID migration, remap `ExpandedLayerIndices` in every structural operation. Replace that temporary repair with IDs during group phase one.

Do not maintain both systems as fallbacks.

## 8. Tests required before feature work

### Editor operation tests

Add focused tests for:

- unified child selection;
- remove-before-selection index repair;
- remove-selected header repair;
- child reorder selection repair;
- mask-like enable and bypass dispatch;
- generic mask blend-mode dispatch;
- base-layer capability enforcement;
- `Remove All Masks` semantics;
- expanded-state stability across insert/delete/move;
- undo/redo for all structural edits.

### Compositor tests

Keep the existing disabled-filter identity coverage and add:

- multiple erosion execution;
- multiple chipping execution;
- filter order;
- per-filter mask ordering;
- disabled and zero-amount identities;
- ridge carry behavior between filters.

### Group-plan tests remain required

After stabilization, implement the existing group cases:

- empty group pass-through;
- black group mask hides members;
- nested groups multiply;
- ping-pong parity below a group;
- dangling and forward instance identity;
- member solo includes ancestor masks.

## Order of work

1. Add exhaustive child classification and state accessors.
2. Replace split effect/mask selection with one selected child index.
3. Route every child removal/reorder through shared selection repair.
4. Fix stale headers, bypass state, and debug child targeting.
5. ~~Define and enforce the shared layer child-capability policy.~~ Decided and applied: no base layer, see section 4. The typed `CanLayerAccept*` helpers remain to be written.
6. Consolidate mask creation and mask-child context menus.
7. Fix Craquelure and Color ID Blend Mode context actions.
8. Correct or rename `Remove All Masks`.
9. Replace deferred singleton filters with an ordered filter pipeline.
10. Add filter-order and editor-operation regression tests.
11. Introduce `LayerId` and move expanded state to GUIDs.
12. Begin `Group` and `MaskInstance` implementation.

## Definition of ready

Group and Mask Instance work can begin when:

- every visible child reorder has defined compositor behavior;
- every enabled filter row executes exactly once;
- mask state is captured at the correct child position;
- one selected-child path serves every child type;
- all child-type switches are exhaustive;
- ordinary and group child capabilities have one source of truth, keyed on layer type alone;
- every mask-like child uses one menu and inspector taxonomy;
- bulk mask removal has unambiguous semantics;
- expanded state survives structural layer edits;
- focused regression tests cover these contracts.

## Out of scope

- Group compositing and mask snapshots themselves;
- Group nesting UI;
- Mask Instance shader and source picker;
- forward mask references;
- child-level mask references;
- isolated Photoshop-style group buffers;
- removal of the unused legacy preview service.

Those remain covered by `Docs/GroupLayerAndMaskInstancePlan.md` or separate cleanup work.
