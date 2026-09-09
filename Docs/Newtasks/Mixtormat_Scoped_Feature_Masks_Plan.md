# Mixtormat — Scoped Feature Masks Plan

## 1. Goal

Add reusable mask stacks beneath supported effect and filter children.

A scoped mask must:

- Appear visually as a child of its owning feature.
- Use the existing mask inspector and mask asset workflow.
- Support blend mode, weight, balance, contrast, invert, and offset.
- Support existing parameter drivers and direct references.
- Support duplicate, copy, paste, and instance behavior.
- Affect only its owning feature.
- Preserve existing layer-mask behavior and existing recipes.

This architecture replaces new effect-specific mask pickers. Existing serialized fields remain compatible until migrated safely.

## 2. Why not use a recursive child tree?

Do not add `TArray<FMixtormatLayerChild>` inside `FMixtormatLayerChild`.

A recursive model would require broad changes to:

- Selection and inspector addressing.
- Tree rendering and expansion state.
- Drag/drop and reorder logic.
- Copy, duplicate, paste, and deletion.
- Child identity and parameter references.
- Instance resolution and cycle checks.
- Compositor traversal and debug selection.
- Serialization and recipe migration.

Instead, preserve the existing flat `FMixtormatLayer::Children` array and add an ownership link.

## 3. Proposed data model

Add a persistent owner field to `FMixtormatLayerChild`:

```cpp
UPROPERTY()
FGuid ScopeOwnerChildId;
```

Semantics:

- Invalid GUID: normal layer child with current behavior.
- Valid GUID: scoped child owned by another child in the same layer.
- Initial implementation permits only mask-capable child types as scoped children.
- Initial implementation permits one nesting level only.
- An owner cannot itself be scoped beneath another owner.
- The owner must exist in the same layer.

The scoped mask remains a complete `FMixtormatLayerChild`.

It therefore retains:

- Its own `ChildId`.
- `SourceLayerId` and `SourceChildId` instance support.
- `ParameterBindings`.
- The existing `FMixtormatMaskLayer` payload.
- Existing mask asset and texture assignment.
- Existing mask placement and shaping controls.

Do not duplicate mask fields inside every effect struct.

## 4. Supported owners

Use an explicit capability check rather than assuming every non-mask row is maskable.

### Initially supported

All `FMixtormatLayerEffect` rows are supported, including effect types classified internally as filters:

- Peeling.
- Stain.
- Erosion.
- Grade.
- Chipping.
- Worn Edges.

### Not initially supported

These rows already are masks or produce discrete data:

- Texture Mask.
- Generated Mask.
- Color ID Mask.
- Random From IDs mask.
- Cluster IDs.
- Pattern IDs.
- Craquelure.
- HSV From IDs.
- Ramp From IDs.

Cluster IDs and Pattern IDs produce integer region data. Weighted mask blending must not alter those IDs. Their consumers should be masked instead.

Pattern implementation, controls, shaders, and output contracts are out of scope.

## 5. UI behavior

### Context menu

Add an `Add Mask` action to the RMB menu of each supported owner.

The action:

1. Creates a regular mask child.
2. Assigns its `ScopeOwnerChildId` to the owner’s `ChildId`.
3. Places it directly after the owner’s current scoped-mask block.
4. Selects the new mask.
5. Opens the normal mask inspector.

The layer RMB menu keeps its existing `Add Mask` action for layer-scoped masks.

### Layer stack presentation

Scoped masks remain stored in the flat array but render indented beneath their owner.

Recommended presentation:

- Owner row keeps its current feature badge.
- Scoped masks use the normal mask badge and thumbnail.
- Scoped masks are indented one level.
- An owner with masks receives a disclosure arrow.
- Collapsing is visual only; it does not disable masks.
- Selection uses the existing child index and inspector flow.

### Inspector

Selecting a scoped mask opens the existing mask controls:

- Blend Mode.
- Weight.
- Tiling and UV placement.
- Rotation and flips.
- Invert.
- Balance.
- Contrast.
- Offset.

No effect-specific copy of these controls should be created.

## 6. Runtime semantics

### Layer masks

Unscoped masks continue to update the layer’s ordered `CombinedMask` exactly as they do now.

### Scoped masks

Scoped masks must not update the layer’s global `CombinedMask`.

For each supported owner:

1. Capture the `CombinedMask` visible at the owner’s position.
2. Use that mask as the starting input for the owner’s scoped mask stack.
3. Evaluate scoped masks with the existing mask blend and shaping implementation.
4. Snapshot the resulting texture as the owner’s `FeatureMask`.
5. Use `FeatureMask` only when blending or gating that owner’s output.
6. Leave `CombinedMask` unchanged for later sibling children.

When an owner has no scoped masks, `FeatureMask` is the current `CombinedMask`.

This is the compatibility identity: old recipes produce the same result without migration.

### Multiple scoped masks

Multiple scoped masks are evaluated in their visible order.

They use the existing mask vocabulary:

```text
FeatureMask[0] = CombinedMask at owner
FeatureMask[n + 1] = BlendAndShape(FeatureMask[n], ScopedMask[n])
```

The existing mask shader should be reused. Do not create separate blend mathematics for effects.

## 7. Ordering rules

Scoped masks form one contiguous block immediately after their owner in serialized order.

UI operations must preserve this invariant:

- Moving an owner moves its scoped-mask block with it.
- Duplicating an owner duplicates its scoped-mask block.
- Removing an owner removes its scoped masks after confirmation through the existing action.
- Reordering a scoped mask is limited to its owner’s scoped block.
- Moving a scoped mask onto another supported owner reassigns its owner GUID.
- Moving a scoped mask to layer scope clears its owner GUID.
- Moving an owner to another layer moves the complete block and preserves internal ownership.

Malformed ownership must fail safely:

- Missing owner: treat the mask as unscoped or repair it during normalization.
- Cross-layer owner: clear the owner link during normalization.
- Unsupported owner type: clear the owner link during normalization.
- Nested scoped owner: reject or flatten during normalization.

Use one deterministic normalization function shared by loading and editing paths.

## 8. Identity, references, and instances

Because a scoped mask remains a normal child, existing parameter addressing can continue using:

- Layer ID.
- Child ID.
- Parameter owner type.
- Parameter name.

Required identity updates:

- Regenerating an owner ID must remap copied scoped masks to the new owner ID.
- Duplicating an owner block must regenerate every copied child ID.
- Internal references within a duplicated block must remap to copied IDs.
- Moving a block across layers must remap parent layer addresses.

A scoped mask can be independently copied or pasted as an instance.

Initial instance rule:

- The mask payload and bindings come from its source instance.
- Scope ownership remains local to where the instance is placed.
- Instancing an owner does not implicitly instance all masks beneath it.
- Duplicate-owner operations may duplicate the whole visible block.

This keeps mask reuse modular without turning a feature row into a recursive asset graph.

## 9. Effect integration contract

Each supported owner receives a resolved `FeatureMask` render input.

Apply it at the owner’s final output boundary:

- Surface effects: gate their coverage or final channel write.
- Surface filters: blend filtered and original channels by `FeatureMask`.
- Mask-producing effects: blend their generated result into the mask chain by `FeatureMask`.
- Mixed-output features such as Craquelure: gate mask and relief consistently.

Do not sample the mask independently in every internal pass unless the algorithm requires it. Prefer one final gate to keep behavior predictable and costs bounded.

## 10. Worn Edges W7 integration

Implement scoped feature masks before adding a bespoke Worn Edges picker.

Worn Edges uses two distinct signals:

### `FeatureMask`

The generic scoped-mask result.

Purpose:

- Controls where Worn Edges is allowed to operate.
- Falls back to the current `CombinedMask` when no scoped masks exist.

### `EdgeWearMask`

The generated Worn Edges result from `MixtormatEdgeWear.usf`.

Purpose:

- Drives regenerated worn-edge normals.
- Drives roughness output.
- Remains available internally through the deferred Worn Edges passes.

Roughness formula:

```text
RoughnessDelta = RoughnessOffset * RoughnessWeight * EdgeWearMask
```

The generated wear result is also gated by `FeatureMask`.

Neutral defaults:

```text
RoughnessWeight = 0
RoughnessOffset = 0
```

There is no separate roughness mask. The scoped feature mask controls placement; `EdgeWearMask` controls generated wear coverage.

## 11. Existing bespoke effect masks

Known effect-specific mask fields include Peeling, Stain, Erosion, and Chipping paths.

Do not remove serialized fields during the first implementation.

Recommended migration approach:

1. Add scoped-mask runtime support with no behavior changes.
2. Add a deterministic conversion for populated legacy mask fields.
3. Materialize equivalent scoped mask children when recipes are upgraded.
4. Preserve old fields as deprecated serialization compatibility data.
5. Remove bespoke mask rows from the UI only after conversion is reliable.
6. Keep one active runtime source of truth after conversion.

Avoid maintaining permanent duplicate mask evaluation paths.

## 12. Performance constraints

- Reuse existing mask compute shaders and render-data structures.
- Allocate scoped mask targets only for owners that have enabled scoped masks.
- Reuse ping-pong targets where lifetimes do not overlap.
- Snapshot a scoped result when deferred effects need it after later passes.
- Skip weight-zero masks where the existing identity rules allow it.
- Do not rerun an expensive effect once per scoped mask.
- Keep mask evaluation outside Pattern and Cluster ID generation.

## 13. Implementation phases

### Phase S1 — Schema and normalization

Implementation status: schema and identity remapping complete. Runtime validation makes malformed links fall back to layer scope; a shared load-time data normalizer remains pending.

- Add `ScopeOwnerChildId`.
- Add owner capability checks.
- Add block and ownership lookup helpers.
- Add deterministic normalization.
- Update child identity remapping.

### Phase S2 — UI creation and presentation

Implementation status: RMB creation, indentation, and shared inspector complete. Optional owner disclosure/collapse is deferred.

- Add `Add Mask` to supported RMB menus.
- Render scoped masks indented beneath owners.
- Add owner disclosure state.
- Reuse the existing mask inspector.
- Preserve selection without rebuilding an open context menu.

### Phase S3 — Editing operations

Implementation status: complete. Same-layer and cross-layer owner moves preserve the full scoped-mask block; selection is restored by child GUID.

- Constrain scoped reorder behavior.
- Move owner blocks together.
- Duplicate owner blocks together.
- Handle scoped copy, paste, and instances.
- Handle owner removal safely.

### Phase S4 — Compositor mask scopes

Implementation status: complete for all `FMixtormatLayerEffect` types. Malformed or unsupported owner links fall back to layer scope.

- Separate unscoped and scoped masks during render-data gathering.
- Evaluate scoped mask stacks from the owner-position `CombinedMask`.
- Route `FeatureMask` to supported effects and filters.
- Ensure sibling masks and effects remain isolated.

### Phase S5 — Legacy mask migration

Implementation status: generic placement rows are removed from Erosion, Chipping, and Grade UI. Serialized legacy fields remain compatible when no scoped mask exists; an enabled scoped mask takes runtime precedence so gates never stack. Automatic data conversion remains pending.

Peeling's seed mask and Stain's liquid/dirt masks remain visible because they are algorithm inputs, not generic placement gates.

- Convert populated bespoke effect masks into scoped children.
- Preserve serialized compatibility fields.
- Ensure converted recipes have one runtime mask source.

### Phase S6 — Worn Edges W7

Implementation status: roughness controls and internal `EdgeWearMask` routing complete. GPU identity/sign/base-color tests are added but not run.

- Add roughness weight and signed offset.
- Preserve generated wear coverage as `EdgeWearMask`.
- Gate wear generation/output with `FeatureMask`.
- Apply roughness through `EdgeWearMask`.
- Keep base color unchanged.

## 14. Validation plan

Implementation status: focused data and GPU automation coverage has been added. Tests have not been run.

### Data and migration

- Existing recipes with no scoped masks render identically.
- Legacy effect masks convert without visual changes.
- Invalid owner links normalize deterministically.
- Save/load preserves scoped ownership.

### UI

- RMB `Add Mask` creates and selects an indented mask.
- Context menus remain stable and do not blink or close unexpectedly.
- Scoped masks expose the normal mask inspector.
- Collapsing an owner changes presentation only.
- Pattern and Cluster ID rows do not expose unsupported masking.

### Ordering

- A scoped mask affects only its owner.
- Later siblings receive the unchanged layer `CombinedMask`.
- Multiple scoped masks blend in visible order.
- Moving or duplicating an owner preserves its mask block.

### References and instances

- Scoped mask parameters accept drivers.
- Scoped masks accept direct parameter references.
- A pasted mask instance follows its source values.
- Local ownership is preserved when an instance resolves.
- Duplicate-owner identity remapping does not target the original block.

### Worn Edges

- No scoped mask uses the current `CombinedMask` fallback.
- Scoped masks limit Worn Edges placement.
- Roughness weight zero is an exact identity.
- Positive and negative roughness offsets work.
- Roughness follows generated `EdgeWearMask`, not the input mask directly.
- Base color remains unchanged.
- Normal and roughness coverage agree at worn edges.

## 15. Files likely to change

Expected areas only; confirm exact declarations before editing:

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`
- `Shaders/Private/MixtormatEdgeWear.usf`
- Targeted editor/compositor automation tests

Pattern files must not change.

## 16. Scope assessment

This is a medium refactor when implemented as flat children with owner GUIDs.

It becomes a large refactor if implemented as a recursive serialized tree.

Recommended order:

1. Implement schema, UI ownership, and editing invariants.
2. Implement generic compositor scoping.
3. Convert one existing effect as the proof path.
4. Convert remaining bespoke effect masks.
5. Finish Worn Edges W7.
6. Add targeted regression coverage before broader grouping work.

## 17. Layer grouping readiness

The flat GUID ownership model is ready for one-level effect sublayers and avoids a recursive serialization refactor.

Broader arbitrary layer grouping should wait for:

- The focused GPU tests to pass.
- Save/load coverage for ownership GUIDs.
- A shared load-time ownership normalizer.
- Explicit UX for group collapse and cross-owner mask moves.
