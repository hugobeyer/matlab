# Mixtormat — Layer Groups, Rename, Cross-Layer Dragging, and Precise Drop UX

**Implementation brief for Opus**  
**Repository audited:** `hugobeyer/matlab`  
**Audited HEAD:** `7d0790f7a2816b85583d9ba3d45656e08324d85e`  
**Date:** 2026-09-18  
**Target:** Unreal Engine 5.8 / Slate / current Mixtormat architecture

---

## 1. Goal

Implement a proper layer-group workflow without destabilizing Mixtormat's current flat GPU compositor.

The feature must solve the actual authoring problem: today the same effect is repeatedly copied/instanced into several layers just to keep their treatment consistent. A group must let the artist author that child/effect stack once and have it apply automatically to every member layer.

At the same time, improve the layer stack interaction model:

- compact, nameable layer groups;
- every normal layer can be renamed;
- `F2` renames the selected layer or group;
- double-clicking a layer/group row toggles expanded/collapsed;
- the existing chevron still toggles with one click;
- effects/masks/procedural children can be dragged directly between layers;
- dragging a child onto a layer header appends it to that layer;
- dragging a child onto a group makes it a shared group child;
- dragging new library materials into the stack shows an exact insertion position before dropping;
- layer/group/material drag feedback must clearly show **above**, **below**, or **inside group**.

Do not remove or simplify existing Mixtormat features to implement this.

---

## 2. Important findings from the current code

### 2.1 The compositor is deliberately flat

The runtime/editor model centers on:

```cpp
TArray<FMixtormatLayer>
```

`FMixtormatGpuCompositor::RequestCompose()` and `RequestComposeInternal()` iterate that array directly. Render data is gathered one authored layer at a time and the output ping-pongs by layer index.

This matters because several systems rely on the exact layer order/index:

- `HeightReferenceLayerIndex`;
- height snapshot collection;
- debug layer indices;
- source composition recursion;
- driver snapshot demand;
- output ping-pong target parity.

**Do not turn the compositor input into a recursive tree for this feature.**

### 2.2 Layers already have stable IDs

`FMixtormatLayer` already owns:

```cpp
FGuid LayerId;
FText DisplayName;
TArray<FMixtormatParameterBinding> ParameterBindings;
TArray<FMixtormatLayerChild> Children;
```

Children have stable `ChildId`s and the parameter/reference/instance system already has robust identity remapping.

Important existing helpers include:

```cpp
MixtormatParameterBinding::EnsureStableIds(...)
MixtormatParameterBinding::RegenerateLayerIdentity(...)
MixtormatParameterBinding::RegenerateLayerIdentities(...)
MixtormatParameterBinding::RegenerateChildIdentity(...)
MixtormatParameterBinding::RemapChildParent(...)
```

Preserve this ID-based architecture.

### 2.3 Cross-layer child movement mostly already exists

`SMixtormat::MoveChildToLayer(...)` already:

- finds the complete scoped subtree;
- moves the root plus descendants;
- invalidates only the moved root's old `ScopeOwnerChildId`;
- preserves the descendants' local scope relationships;
- calls `MixtormatParameterBinding::RemapChildParent(...)`;
- inserts at a specific destination child or appends.

The current UI limitation is that cross-layer child dragging is accepted by `SMixtormatChildDropTarget`, but the **layer header drop target does not accept `FMixtormatChildDragDropOp`**.

Do not rewrite the working move implementation unnecessarily. Expose it through the missing drop paths.

### 2.4 Surface/material dragging currently only appends

The whole stack is wrapped by `SMixtormatLayerDropTarget`, and `HandleSurfaceDropped(...)` ultimately calls `AddWorkingLayer(...)`.

There is no insertion model for a dragged material. Therefore an insertion line cannot be implemented honestly without adding explicit drop-position semantics.

### 2.5 Current layer expansion is index-based

The editor currently stores:

```cpp
TSet<int32> ExpandedLayerIndices;
```

This is fragile once groups and block moves exist because array indices change frequently.

Use stable IDs for expansion state as part of this work.

### 2.6 `SMixtormatLayerGroup` is already taken — but it is not a real group

Current files:

```text
UI/Layers/SMixtormatLayerGroup.h
UI/Layers/SMixtormatLayerGroup.cpp
```

represent **one layer plus its child rows**. It is only a visual enclosure.

Rename this widget before adding real layer groups. Suggested name:

```text
SMixtormatLayerContainer
```

Then reserve names such as:

```text
FMixtormatLayerGroup
SMixtormatLayerGroupRow
```

for the actual multi-layer grouping feature.

### 2.7 Current row input behavior

`SMixtormatLayerRow::OnMouseButtonDown(...)` currently:

- selects on left mouse;
- calls `DetectDrag(...)`;
- selects and opens the context menu on right mouse.

There is currently no row `OnMouseButtonDoubleClick(...)` implementation.

`SMixtormat::OnKeyDown(...)` currently handles:

- Backspace hover-reset;
- Ctrl/Cmd+Z;
- Ctrl/Cmd+Y;
- `G` gallery toggle.

`F2` is free.

### 2.8 UE 5.8 Slate API

Verified against UE 5.8 API:

- `SWidget::OnMouseButtonDoubleClick(...)` can be overridden;
- `SInlineEditableTextBlock::EnterEditingMode()` exists;
- however `SInlineEditableTextBlock` itself is designed to enter editing through double-click.

Because Mixtormat wants **double-click row = collapse/expand**, do **not** rely on the inline text widget's default double-click editing behavior.

Prefer a controlled name widget (`STextBlock` / `SEditableTextBox` through a `SWidgetSwitcher`) or otherwise explicitly suppress inline double-click rename. `F2` and the context menu should be the rename entry points.

---

# 3. Architecture decision

## 3.1 Keep authored layers flat

Keep:

```cpp
UMixtormatMaterial::Layers
```

as the same ordered `TArray<FMixtormatLayer>` used today.

Do **not** place group header entries inside this array.

Reason: inserting non-renderable entries into `Layers` would disturb existing integer layer indexing, especially `HeightReferenceLayerIndex` and render target parity.

## 3.2 Add groups beside the layer array

Add a new serializable structure:

```cpp
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayerGroup
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid GroupId = FGuid::NewGuid();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Group")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Group")
    bool bEnabled = true;

    UPROPERTY()
    TArray<FMixtormatParameterBinding> ParameterBindings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Children")
    TArray<FMixtormatLayerChild> Children;
};
```

Add to `FMixtormatLayer`:

```cpp
UPROPERTY()
FGuid GroupId;
```

Invalid `GroupId` means ungrouped.

Add to `UMixtormatMaterial`:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
TArray<FMixtormatLayerGroup> LayerGroups;
```

### Rules

For the first implementation:

- one level of groups only;
- no nested groups;
- every group owns a contiguous run of layers in `Layers`;
- a layer belongs to zero or one group;
- empty groups are automatically removed;
- a group's visual position is derived from the first member layer;
- group collapse is editor UI state and should not affect rendering;
- group enable is serialized and affects rendering.

This avoids introducing another ordering array or recursive stack structure.

---

# 4. What a group effect means

## 4.1 Do not implement isolated Photoshop group compositing yet

A full isolated group framebuffer would require changing the semantics of the current compositor, including height, normal, effects that inspect the surface underneath, masks, and partial opacity.

That is not required to solve the current pain.

## 4.2 Shared group children are broadcast to member layers

A group owns one authored `Children` stack.

At compose time, those children are **transiently appended to every member layer**.

Conceptually:

```text
GROUP Weathering
    shared: Worn Edges
    shared: Grade

    Rust Base
        local: Pattern IDs

    Rust Flakes
        local: Runoff
```

renders as if the authored layers were temporarily:

```text
Rust Base
    Pattern IDs
    Worn Edges
    Grade

Rust Flakes
    Runoff
    Worn Edges
    Grade
```

But only the group owns the shared settings.

This reproduces the workflow Hugo is currently doing manually with copied/instanced effects while eliminating duplicate authoring.

### Required semantics

- local layer children run first;
- shared group children are appended after the local child stack;
- group child ordering is preserved;
- scoped group child subtrees remain scoped;
- disabling a group does **not** edit member-layer `bEnabled` values;
- disabling a group makes its members effectively disabled in the transient render copy;
- removing a layer from a group immediately removes the shared children from its effective render stack;
- editing a shared effect updates every member on the next preview refresh.

---

# 5. Build an effective render stack centrally

Do not duplicate group expansion in random editor call sites.

Add a central helper, preferably in Runtime:

```cpp
namespace MixtormatLayerGroups
{
    void BuildEffectiveLayers(
        const TArray<FMixtormatLayer>& Layers,
        const TArray<FMixtormatLayerGroup>& Groups,
        TArray<FMixtormatLayer>& OutLayers);
}
```

Properties:

- output layer count must equal input layer count;
- layer order must remain identical;
- `LayerId` remains identical;
- `HeightReferenceLayerIndex` remains valid without remapping;
- ungrouped layers copy unchanged;
- grouped layers receive transient copies of the group's children;
- disabled group -> transient member `bEnabled = false`;
- do not mutate authored assets.

## 5.1 Child ID remapping during transient expansion

Group children cannot be appended to several layers with identical effective `ChildId`s if those IDs participate in drivers, publishing, instances, or lookup.

For each `(GroupId, GroupChildId, MemberLayerId)` create a deterministic transient child ID or generate a local mapping for the current compose.

When cloning the complete group subtree into one member layer:

1. build `OldGroupChildId -> EffectiveChildId` map;
2. clone all group children;
3. rewrite cloned `ChildId`;
4. rewrite cloned `ScopeOwnerChildId` through the map;
5. rewrite group-local instance sources through the map;
6. clone/remap any relevant group `ParameterBindings` onto the effective member layer;
7. destination addresses for group child parameters must become the effective member `LayerId` + effective `ChildId`;
8. references to external authored layer children remain pointed at their authored IDs;
9. references between shared group children map to the effective IDs for that member.

Do not modify the original group child IDs.

## 5.2 Parameter-binding support

Do not silently strip parameter references/drivers from shared group children.

The cleanest path is to resolve/clone group bindings while building each effective layer and then let the existing per-layer binding code continue operating normally.

The resulting effective layer should look exactly like an ordinary authored layer to the existing compositor.

This is preferable to teaching every GPU pass what a group is.

## 5.3 Compositor API

Update the central composition API rather than expanding only in the editor.

Suggested overload:

```cpp
bool RequestCompose(
    const TArray<FMixtormatLayer>& Layers,
    const TArray<FMixtormatLayerGroup>& Groups,
    FSimpleDelegate OnComplete = FSimpleDelegate(),
    FMixtormatDebugPreviewSettings DebugSettings = {},
    bool bRotateOutput90 = false,
    const FSoftObjectPath& OwnerPath = {});
```

Keep the old overload temporarily and forward it with an empty group array if useful for compatibility/tests.

Inside `RequestComposeInternal(...)`:

```text
Authored Layers + Groups
        ↓
BuildEffectiveLayers()
        ↓
existing validation/reference/render-data gather
        ↓
existing render graph
```

When recursively composing a `SourceComposition`, pass:

```cpp
Source->Layers
Source->LayerGroups
```

Do not make referenced recipes lose their groups.

---

# 6. Runtime persistence and migration

## Files

Primary:

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
Source/MixtormatRuntime/Private/MixtormatMaterial.cpp
Source/MixtormatRuntime/Public/MixtormatParameterBinding.h
Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp
```

## `PostLoad()` requirements

Old assets must load unchanged.

Because old assets contain no `LayerGroups` and every new `Layer.GroupId` defaults invalid, their render output must remain byte-for-byte semantically unchanged.

For assets with groups:

- ensure every `GroupId` is valid and unique;
- clear a layer `GroupId` if the group no longer exists;
- delete groups with zero member layers;
- validate group members are contiguous;
- if corrupted serialized data contains separated runs with one `GroupId`, **do not reorder layers silently**;
- preserve the first contiguous run and clear the invalid membership from later disconnected layers, logging a warning;
- ensure group child IDs are valid/unique relative to other group children and authored layer children if global uniqueness is required by helper code.

Do not renumber ordinary layers during migration.

---

# 7. Editor working state and undo/redo

Current edit history only stores layers.

Extend:

```cpp
struct FEditHistoryState
{
    TArray<FMixtormatLayer> Layers;
    TArray<FMixtormatLayerGroup> Groups;
    bool bRotateUV90 = false;
};
```

Add editor state:

```cpp
TArray<FMixtormatLayerGroup> WorkingLayerGroups;
TArray<FMixtormatLayerGroup> SavedLayerGroups;
```

Update:

- new recipe;
- open recipe;
- save;
- Save As;
- dirty comparison;
- undo;
- redo;
- current saved-state comparison;
- structural comparison;
- bake call;
- preview refresh.

Group operations must be one undo step each.

Do not serialize UI-only expanded/collapsed state into the material unless there is a strong existing editor-state convention for doing so.

---

# 8. Stable UI expansion state

Replace:

```cpp
TSet<int32> ExpandedLayerIndices;
```

with stable identity:

```cpp
TSet<FGuid> ExpandedLayerIds;
TSet<FGuid> ExpandedGroupIds;
```

Do not let moving/deleting/inserting layers randomly transfer expansion state to a different row.

When a group is collapsed:

- hide its shared children;
- hide its member layer rows;
- do not modify individual member `ExpandedLayerIds`;
- reopening the group restores each layer's previous expanded/collapsed state.

---

# 9. Rename the existing visual `SMixtormatLayerGroup`

Before adding a real group row, rename:

```text
SMixtormatLayerGroup
```

to:

```text
SMixtormatLayerContainer
```

Suggested file rename:

```text
UI/Layers/SMixtormatLayerGroup.h
    -> UI/Layers/SMixtormatLayerContainer.h

UI/Layers/SMixtormatLayerGroup.cpp
    -> UI/Layers/SMixtormatLayerContainer.cpp
```

Update includes and construction sites.

Do this as a mechanical rename only. Do not alter its current visual behavior in the same step.

Then add the real:

```text
SMixtormatLayerGroupRow.h
SMixtormatLayerGroupRow.cpp
```

---

# 10. Group row design

Hugo's requirement: **basically the same row as a layer, but shorter and nameable.**

Current tokens:

```cpp
LayerRowHeight = 28.0f;
LayerChildRowHeight = 22.0f;
```

Add:

```cpp
LayerGroupRowHeight = 22.0f;
```

Suggested group row contents:

```text
[eye] [group/folder icon]  Weathering                       [chevron]
```

No material thumbnail is necessary.

No source label is necessary.

No composition badge is necessary initially.

Use the existing layer palette/selection language so it reads as part of the same stack, but keep it visually tighter than a material layer.

A small member count is acceptable if it stays subtle:

```text
Weathering                                      3  ▾
```

Do not add a large boxed Photoshop-style folder UI.

---

# 11. Layer stack rendering

`RebuildLayerList()` currently loops `WorkingLayers` directly.

Replace that with a group-aware display traversal while leaving the underlying `WorkingLayers` order unchanged.

Pseudo:

```cpp
for each LayerIndex in WorkingLayers:
    if Layer.GroupId invalid:
        add normal layer row
        continue

    Group = find group

    if this is first member of Group:
        add group row

        if group expanded:
            add group shared child rows

    if group expanded:
        add member layer row, indented as group member
```

Because groups are contiguous, a group row is emitted exactly once.

Do not rebuild the compositor order from this visual traversal.

---

# 12. Selection model

Avoid a large rewrite of every existing inspector selection path unless necessary.

Current layer/child code relies heavily on:

```cpp
SelectedLayerIndex
SelectedEffectIndex
SelectedMaskIndex
```

Add group selection beside it:

```cpp
FGuid SelectedGroupId;
int32 SelectedGroupChildIndex = INDEX_NONE;
```

Create small helpers so selection remains mutually exclusive:

```cpp
void ClearLayerSelection();
void ClearGroupSelection();
FMixtormatLayerGroup* GetSelectedGroup();
const FMixtormatLayerGroup* GetSelectedGroup() const;
FMixtormatLayerChild* GetSelectedGroupChild();
```

Selecting a group clears layer-child selection.

Selecting a layer/normal child clears group selection.

Do not overload `SelectedLayerIndex` with magic values to mean a group.

---

# 13. Renaming layers and groups

Every layer must be renameable.

Every group must be renameable.

## Required UX

- select layer/group;
- press `F2` -> name enters edit mode;
- context menu -> `Rename` -> same edit mode;
- `Enter` commits;
- `Escape` cancels;
- empty/whitespace-only name should either reject or normalize to a safe default;
- rename records undo history and dirty state;
- rename must not trigger a GPU recomposition because names do not affect rendering.

## Do not make double-click rename

Double-click row is reserved for collapse/expand.

Use a controlled text/edit widget instead of relying on the default double-click behavior of `SInlineEditableTextBlock`.

Suggested row implementation:

```text
SWidgetSwitcher
  0 -> STextBlock
  1 -> SEditableTextBox
```

Expose methods on the row:

```cpp
void BeginRename();
void CancelRename();
bool IsRenaming() const;
```

or drive it by owner state.

Track current row widgets by stable ID during `RebuildLayerList()`:

```cpp
TMap<FGuid, TWeakPtr<SMixtormatLayerRow>> LayerRowWidgets;
TMap<FGuid, TWeakPtr<SMixtormatLayerGroupRow>> GroupRowWidgets;
```

`F2` resolves the selected stable ID and calls `BeginRename()`.

Do not use the current layer array index as the rename identity.

---

# 14. Click and collapse behavior

## Normal layer

- single left-click row -> select;
- left-drag -> drag layer;
- single click chevron -> expand/collapse;
- double left-click on row body -> expand/collapse;
- right click -> select then context menu;
- `F2` -> rename.

## Group

Same behavior.

Implement:

```cpp
virtual FReply OnMouseButtonDoubleClick(
    const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent) override;
```

in both row classes.

Double-click should select the row and then execute the same toggle delegate used by the chevron.

Do not collapse from an ordinary single row click; otherwise selection and drag initiation become unpredictable.

Interactive children such as eye and chevron keep consuming their own clicks.

---

# 15. Group operations

Implement at minimum:

```text
Create Group from selected layer
Rename
Enable / Disable
Duplicate Group
Ungroup
Delete Group and Layers
```

### Create Group

Because the editor currently has no multi-layer selection, start with:

- context menu on a layer -> `Group Layer` / `Create Group`;
- create group;
- assign selected layer's `GroupId`;
- name `Group 1`, `Group 2`, etc.;
- select group;
- expand group.

Additional layers can then be dragged into it.

### Ungroup

- clear `GroupId` on member layers;
- remove group data;
- preserve layer order and all layer contents.

### Delete Group and Layers

- remove the complete contiguous member block;
- repair height references with a block-aware helper;
- remove group.

### Duplicate Group

- duplicate the member layers as one contiguous block;
- regenerate all duplicated `LayerId`/`ChildId` identities as a coherent set;
- duplicate group shared children;
- generate a new `GroupId`;
- assign copied members to new group;
- preserve internal references within the duplicated block by remapping them to the duplicates;
- preserve references to external sources where appropriate.

Do not call `RegenerateLayerIdentity()` independently on each copied member if it breaks cross-layer references among members. Use or add a block-level remap helper.

---

# 16. Moving groups and grouped layers

## Group drag

Add:

```cpp
FMixtormatGroupDragDropOp
```

Payload should carry stable `GroupId` and display name.

Moving a group moves the whole contiguous layer block.

Add a block move utility rather than calling the existing one-layer `HandleLayerDropped()` in a loop.

Suggested helper:

```cpp
FReply MoveLayerBlock(
    int32 FirstLayerIndex,
    int32 LayerCount,
    int32 DestinationIndex);
```

Height-reference remapping must happen once from the old-to-new permutation.

Do not repeatedly call `RemapHeightReferencesAfterMove()` per member; repeated index mutation is easy to get wrong.

## Drag layer into group

When a normal layer is dropped in a group's `Into` zone:

- move it into the group's contiguous range at the indicated insertion position;
- set its `GroupId`;
- if it came from another group and that group becomes empty, remove the old group;
- preserve its `LayerId` and children.

## Drag layer out of group

Dropping it at an insertion position outside the group's contiguous region:

- move it there;
- clear `GroupId`.

If moving the last member out, remove the empty group.

---

# 17. Dragging effects/masks/procedural children between layers

The core implementation exists. Finish the UX.

## Layer header drop

Extend `SMixtormatLayerRowDropTarget` to accept:

```cpp
FMixtormatChildDragDropOp
```

A child dropped onto the destination layer body means:

```cpp
MoveChildToLayer(
    SourceLayerIndex,
    SourceChildIndex,
    TargetLayerIndex,
    INDEX_NONE);
```

That appends it to the destination layer.

Before accepting the drop, apply the same validity checks used by `MoveChildToLayer()`.

A standalone scoped filter such as Blur/Curvature must remain invalid if its owner is not moving with it.

Tooltip example:

```text
Move Worn Edges to Paint
```

The target row should visibly highlight as a valid whole-row destination.

## Child-row drop

Keep the existing child-row behavior:

- same layer -> reorder;
- different layer -> move at exact child position.

Do not regress scoped subtree movement.

---

# 18. Dragging children to/from groups

A group should own the same reusable child representation used by layers.

## Layer child -> group

Dropping a top-level child/effect on a group header:

- remove its complete subtree from source layer;
- append it to `Group.Children`;
- preserve root/descendant subtree relationships;
- remap any container-sensitive references;
- it now becomes a shared child applied to all member layers.

Tooltip:

```text
Move Worn Edges to Weathering shared effects
```

## Group child -> layer

Dragging a shared child onto a normal layer:

- remove it from group shared children;
- append/insert it into that specific layer;
- it stops being broadcast to the rest of the group.

## Group child -> group

Move the shared child/subtree between groups.

## Scoped children

As with normal layers:

- do not allow moving Blur/Curvature away from its owner alone;
- moving an owner moves its complete subtree.

Generalize the existing subtree helpers so they can operate on either a layer's `Children` or a group's `Children` rather than duplicating the algorithms.

---

# 19. Exact drag/drop insertion feedback

This is required, not optional polish.

Current `SMixtormatLayerRowDropTarget` treats a row as one undifferentiated destination. Replace that behavior for stack ordering.

## Add explicit drop zones

Suggested enum:

```cpp
enum class EMixtormatStackDropZone : uint8
{
    None,
    Above,
    Below,
    IntoGroup,
    Append
};
```

Suggested transient target:

```cpp
struct FMixtormatStackDropLocation
{
    EMixtormatStackDropZone Zone = EMixtormatStackDropZone::None;
    FGuid TargetLayerId;
    FGuid TargetGroupId;
};
```

Use stable IDs in the target, not only indices.

## Layer row zones

For a layer row:

```text
upper half -> Above
lower half -> Below
```

Render a clear insertion line on the appropriate edge.

Do not just tint the whole row for ordering operations.

## Group row zones

Suggested:

```text
top 25%    -> Above group
middle 50% -> IntoGroup
bottom 25% -> Below group
```

Visual states:

- Above/Below -> insertion line;
- IntoGroup -> group-row highlight.

## Empty area

The empty area below the last stack item is `Append`.

An empty working stack should show a large valid material drop region.

---

# 20. Dragging a new library material

The current behavior globally appends through `HandleSurfaceDropped(...)`.

Add a new positional path, e.g.:

```cpp
FReply HandleSurfaceDroppedAt(
    FText DisplayName,
    FSoftObjectPath AssetPath,
    const FMixtormatStackDropLocation& Drop);
```

Do not implement insertion by adding at the end and then repeatedly swapping.

Create the new `FMixtormatLayer` directly at the resolved insertion index, then perform the height-reference insert remap once.

### Required feedback while hovering

Examples:

```text
Insert Steel above Paint
Insert Steel below Rust
Add Steel to Weathering
Append Steel
```

Use the drag operation's tooltip through its existing `SetToolTip(...)` mechanism and reset it on drag leave.

### Group material drop

Dropping in a group's `IntoGroup` zone:

- create the material layer at the group's chosen insertion point;
- assign `GroupId` immediately.

The visible insertion line/highlight must match the final location exactly.

---

# 21. Drag-operation payload improvements

Current operations are primarily index based.

Indices are convenient but unstable across rebuild/reorder operations.

Without deleting current fields, add stable identity where possible:

```cpp
FMixtormatLayerDragDropOp
    FGuid LayerId;

FMixtormatChildDragDropOp
    FGuid LayerId;
    FGuid ChildId;

FMixtormatGroupDragDropOp
    FGuid GroupId;
```

At drop time resolve the current index from the GUID.

Do not capture a stale `LayerIndex` and assume it still points to the dragged object after another mutation/rebuild.

---

# 22. Drop-target classes

Current files:

```text
UI/DragDrop/MixtormatDragDropOps.h
UI/DragDrop/SMixtormatDropTargets.h
```

Recommended approach:

- keep `SMixtormatChildDropTarget` for child-to-child exact insertion;
- upgrade `SMixtormatLayerRowDropTarget` to understand:
  - layer reorder above/below;
  - surface material insertion above/below;
  - child-to-layer append;
- add `SMixtormatGroupRowDropTarget` for:
  - layer above/below/into;
  - group reorder above/below;
  - surface above/below/into;
  - child-to-group shared-child move;
- retain a stack-level target only as an empty-area/append fallback.

Avoid one giant widget with a long switch over every possible source/target combination if it becomes difficult to reason about.

---

# 23. Layer/group context menus

## Layer

Add near Duplicate:

```text
Rename                 F2
Create Group
```

Keep all existing mask/effect/filter creation entries.

## Group

Suggested menu:

```text
Add
  Effect
  Filter
  Mask / Generated Mask where valid

Rename                 F2
Disable
Duplicate Group
Ungroup
Delete Group and Layers
```

Group `Add Effect` writes one shared child, not copies into every member authored layer.

---

# 24. Group inspector behavior

When a group itself is selected, the inspector should remain simple initially:

```text
Group
  Name
  Enabled
  Members: N
```

When a shared group child is selected, reuse the existing inspector controls for that child's payload.

Do not duplicate effect-specific inspector implementations.

Generalize child resolution helpers so the inspector can resolve a child from either:

```text
Layer container
Group container
```

If a large refactor is needed, introduce a small editor-only container view rather than branching in every individual slider.

Example concept:

```cpp
struct FMixtormatChildOwnerView
{
    FGuid OwnerId;
    TArray<FMixtormatLayerChild>* Children = nullptr;
    TArray<FMixtormatParameterBinding>* ParameterBindings = nullptr;
};
```

Use it for generic child operations.

---

# 25. Group enabled state

Do not propagate group disable by rewriting each member layer's `bEnabled`.

That would destroy the member's authored visibility state.

Instead in `BuildEffectiveLayers()`:

```cpp
EffectiveLayer.bEnabled = AuthoredLayer.bEnabled && Group.bEnabled;
```

When group is re-enabled, each member returns to its own previous state.

Update any non-GPU aggregate computations that currently iterate authored layers directly, including fuzz influence/color, so disabled groups do not contribute.

---

# 26. Height references

This is a high-risk area.

Group metadata must never alter the number/order of authored render layers unless the user explicitly moves a layer/group.

Add robust helpers for:

```text
insert one layer
move one layer
move contiguous layer block
remove contiguous layer block
```

The existing `MixtormatUI::RemapHeightReferencesAfterInsert/Delete/Move` logic should be extended rather than bypassed.

Add tests for a layer above, inside, and below a moved group referencing an earlier layer by `HeightReferenceLayerIndex`.

After every move, all references must still identify the same logical layer they identified before the move, unless that referenced layer was deleted.

---

# 27. Source composition references

Current compositor recursively calls:

```cpp
SourceCompositor.RequestComposeInternal(Source->Layers, ...)
```

Change recursion to include `Source->LayerGroups`.

A referenced Mixtormat recipe must look identical whether opened directly or used as a composition source.

Group metadata itself must not create a composition reference cycle; only actual `SourceComposition` layers remain relevant to that graph.

---

# 28. Debug preview

Normal existing layer/child debug preview indices must not change.

Because effective group children are transiently appended, do not expose their effective child index as persistent editor state.

For the first implementation, use stable group child identity in editor debug state if group child preview is supported.

Possible extension:

```cpp
FGuid DebugGroupId;
FGuid DebugChildId;
```

Then map it to each effective member clone during render-data gathering.

If a shared effect's debug signal exists on multiple members, combine using a clear rule such as max/union for mask-like previews.

Do not let group expansion shift the debug index of ordinary authored layer children.

---

# 29. Files likely to change

## Runtime

```text
Source/MixtormatRuntime/Public/MixtormatMaterial.h
Source/MixtormatRuntime/Private/MixtormatMaterial.cpp
Source/MixtormatRuntime/Public/MixtormatParameterBinding.h
Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp
```

Add a focused group utility if needed:

```text
Source/MixtormatRuntime/Public/MixtormatLayerGroups.h
Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp
```

## Shaders/compositor C++

```text
Source/MixtormatShaders/Public/MixtormatGpuCompositor.h
Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp
```

No shader changes should be necessary just to support broadcast shared group children.

That is a major advantage of this architecture.

## Editor

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat.h
Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Document.cpp

Source/MixtormatEditor/Private/UI/DragDrop/MixtormatDragDropOps.h
Source/MixtormatEditor/Private/UI/DragDrop/SMixtormatDropTargets.h

Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerRow.h
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerRow.cpp

rename existing:
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroup.h/.cpp
    -> SMixtormatLayerContainer.h/.cpp

add:
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroupRow.h
Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroupRow.cpp

Source/MixtormatEditor/Private/Style/MixtormatDesignTokens.h
```

Potentially icon/style registrations if a new group glyph/drop brush is required.

---

# 30. Tests to add

Do not stop after manual UI testing.

## Runtime/group expansion

1. ungrouped stack expands identically;
2. group with no shared children expands identically;
3. group shared effect is appended to every member layer;
4. local child order remains before shared group child order;
5. shared scoped subtree remaps owner IDs correctly per member;
6. two member layers receive different effective transient child IDs;
7. group child references to another group child remap within the same member clone;
8. disabled group makes all members effectively disabled without editing authored `bEnabled`;
9. re-enable restores member visibility states;
10. old assets with no groups remain unchanged.

## Group structure

11. create group around selected layer;
12. move layer into group;
13. move layer out;
14. move layer between groups;
15. last member moved out removes empty group;
16. group member run remains contiguous;
17. duplicate group regenerates IDs correctly;
18. ungroup preserves exact layer order;
19. corrupted noncontiguous group membership is repaired without layer reorder.

## Height references

20. inserting material above/below maintains correct references;
21. moving a group block maintains references;
22. deleting a group block updates/invalidates references correctly;
23. moving one grouped member maintains references.

## Child drag

24. effect layer A -> layer B header appends;
25. effect layer A -> exact child in layer B inserts at requested position;
26. effect subtree moves with scoped mask/filter children;
27. standalone Blur/Curvature move remains rejected;
28. effect layer -> group becomes shared;
29. shared group effect -> normal layer becomes local;
30. shared group subtree -> other group works.

## Rename

31. F2 begins rename for selected layer;
32. F2 begins rename for selected group;
33. Enter commits;
34. Escape cancels;
35. rename records history/dirty state;
36. rename does not request GPU recomposition;
37. double-click row collapses instead of renaming.

## Drag feedback

38. surface upper half -> visible top insertion line + inserts above;
39. surface lower half -> visible bottom insertion line + inserts below;
40. surface group center -> group highlight + inserts into group;
41. layer group-center drop -> moves into group;
42. invalid child drop displays invalid feedback;
43. drag leave clears insertion feedback and restores default tooltip.

## Save/open/source compositions

44. groups save and reopen;
45. group shared children save and reopen;
46. referenced recipe containing groups renders same as direct recipe;
47. bake uses groups/shared effects exactly like preview.

Run all existing Mixtormat tests after these additions.

---

# 31. Suggested implementation sequence

Keep commits small and reviewable.

## P1 — Data model only

- add `FMixtormatLayerGroup`;
- add `FMixtormatLayer::GroupId`;
- add `UMixtormatMaterial::LayerGroups`;
- PostLoad validation;
- stable-ID tests;
- no UI yet.

## P2 — Effective layer expansion

- `BuildEffectiveLayers()`;
- group enable state;
- shared children broadcast;
- child/subtree ID remap;
- parameter-binding remap;
- compositor overload + source-composition recursion;
- runtime tests.

At this checkpoint, a unit-authored group should already render correctly even without editor UI.

## P3 — Editor state/save/history

- Working/Saved groups;
- open/save/Save As;
- undo/redo;
- preview/bake plumbing;
- dirty-state comparison.

## P4 — Mechanical widget rename

- rename old visual `SMixtormatLayerGroup` -> `SMixtormatLayerContainer`;
- no behavior changes.

## P5 — Group row and display traversal

- add compact group row;
- stable expansion IDs;
- select/collapse/group visibility;
- create group/ungroup.

## P6 — Rename UX

- controlled editable name for layer/group;
- F2;
- context Rename;
- double-click collapse;
- no double-click rename.

## P7 — Existing child drag UX completion

- child -> layer header;
- child -> exact child remains working;
- scoped subtree validation;
- clear hover feedback.

## P8 — Group shared-child drag

- layer child -> group;
- group child -> layer;
- group child -> group;
- generic child-container subtree helpers.

## P9 — Exact stack insertion model

- drop zone struct;
- above/below lines;
- group Into zone;
- group block movement;
- stable-ID drag payloads.

## P10 — Surface/material positional drops

- material above/below;
- material into group;
- append fallback;
- explicit drag tooltip feedback.

## P11 — regression / polish

- all tests;
- source composition groups;
- bake;
- debug preview;
- keyboard focus edge cases;
- context menus;
- no stale expansion/selection after delete/move.

---

# 32. Acceptance behavior

The finished stack should support this naturally:

```text
▼ Weathering
    Worn Edges        [shared group child]
    Grade             [shared group child]

    ▼ Rust Base
        Pattern IDs
        Roughness Scratches

    ▼ Rust Flakes
        Runoff

▶ Paint

Metal Substrate
```

Expected interactions:

- double-click `Weathering` -> collapse group;
- click its chevron once -> same action;
- select `Weathering`, press F2 -> rename;
- select `Rust Flakes`, press F2 -> rename layer;
- drag `Runoff` from `Rust Flakes` onto `Rust Base` header -> move it there;
- drag `Runoff` onto `Weathering` -> it becomes a shared group effect and automatically appears in every group member's effective render stack;
- drag a library material between `Paint` and `Metal Substrate` -> insertion line appears there and the new layer lands exactly there;
- drag a library material into the center of `Weathering` -> group row highlights and the new layer becomes a group member;
- drag a group -> all member layers move as one contiguous block;
- disabling `Weathering` hides all members, but preserves their individual eye states for when the group is re-enabled.

---

# 33. Things Opus must not do

- Do not convert the compositor to a recursive tree.
- Do not insert fake group layers into `TArray<FMixtormatLayer>`.
- Do not break `HeightReferenceLayerIndex` by changing the meaning of layer indices.
- Do not implement group effects by permanently duplicating child payloads into every layer.
- Do not make group disable overwrite member `bEnabled`.
- Do not use array indices as persistent group identity.
- Do not make row double-click rename; it must collapse/expand.
- Do not remove scoped-child legality checks.
- Do not allow a Blur/Curvature scoped child to be orphaned from its owner.
- Do not lose child references/drivers/instances during cross-container moves.
- Do not show a drop highlight that does not correspond exactly to the final insertion position.
- Do not implement only the visual group row while leaving shared group effects unsolved; the authoring duplication problem is the reason this feature exists.
- Do not touch shader quality or unrelated GPU effects as part of this task.

---

# 34. Final deliverable expected from Opus

After implementation, report:

1. files changed;
2. data-model changes;
3. migration behavior for old assets;
4. exact shared-group-child rendering semantics;
5. drag/drop target rules;
6. rename/collapse keyboard and mouse behavior;
7. how height references are preserved during block moves;
8. how group child IDs/bindings are remapped into effective member layers;
9. tests added;
10. existing tests run and results;
11. UE build result;
12. SM6 shader compilation result (should be unchanged unless incidental C++ shader bindings changed);
13. known limitations.

The implementation is complete only when a user can create a group, drag layers into it, author one shared effect on the group, drag effects between layers/groups, rename rows with F2, collapse rows by double-click, and place a newly dragged material at an exact visually indicated stack position without changing existing compositor behavior outside the requested feature.
