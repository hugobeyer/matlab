# Boolean — layer stack container

Status: core stack execution integrated (source review only; not user-compiled or
runtime-verified). A dedicated Boolean child now owns up to two scoped
height-producing generators. One authored enabled operand aliases the single
native signed field for Self; authored invalid/disabled second operands fail
neutral. Per-operand UV and height offsets and signed Source Scale are gathered
and sampled by the private Boolean stack GPU pass. Nested operands are not
double-added to the outer signed-zero generator chain. Parent row operation
and edge-style picker, offset inspector, signed Height, Affected Area, Join
Boundary, and Difference descriptors are connected.

Still pending: selected-pair Group as Boolean, empty-slot drag/drop, Copy as
Boolean Source/Paste as Boolean Target, clipboard-specific remap UX and full
source/publication/preview integration. Operand roles follow direct drag
reordering; no Swap or Lock actions exist. The retired `BooleanOps` Generator cannot be newly
created through the normal Add Generator menus, but its pre-existing execution and
inspector remain while finite-volume Scatter Carve is reclassified separately.

## Canonical structure

A dedicated `Boolean` child occupies one position in a Generator layer's
ordinary **Layers stack**. It is not an `EMixtormatGeneratorType`, a layer type,
or an ID Group with repurposed semantics.

The parent has at most two authored, indented child slots:

- **Target** — height-producing child; first operand (A).
- **Source** — optional second height-producing child (B). When the parent
  contains exactly one enabled height-producing child, it enters **Self**
  automatically: that one child's signed field supplies both A and B, with
  independent per-operand offsets. Self is a reference to the same field, never
  a duplicated authored child or a second generator evaluation.

Both are ordinary layer-stack child entries, sharing the parent through the
existing `ScopeOwnerChildId` hierarchy. They retain identity and their own
parameters, masks and visual controls. Use the established ID Group hierarchy
painting, disclosure/expand/collapse, selection, subtree move, paste/duplicate,
and drop-target patterns; **do not use** the Region-ID group evaluator.

### Example

```text
▾ Boolean                 [Subtract ▾] [Round ▾]
  ├ Target   Rock Formation
  └ Source   Cracks

▾ Boolean · Self          [Subtract ▾] [Bevel ▾]
  └ Target   Rock Formation
```

`Union / Subtract / Intersect` is an inline operation badge on the **Boolean
parent row**, in the same right-hand badge region as existing blend-mode
controls. `Hard / Round / Bevel` is a second compact row control if room
permits; otherwise include edge styles in the same row-level picker.
Neither picker nor the Source/Target hierarchy goes in the inspector.
Existing theme tokens, badge recipes and hierarchy gutters are mandatory.

## Boolean inspector — independent operand offsets

Selecting the **Boolean parent** shows compact numeric **filled-well draggers**,
reusing the existing Mixtormat row/drag controls and design tokens:

```text
TARGET
  Offset X      [ 0.000 ]  Offset Y       [ 0.000 ]
  Height Offset [ 0.000 ]

SOURCE
  Offset X      [ 0.000 ]  Offset Y       [ 0.000 ]
  Height Offset [ 0.000 ]

SHAPING
  Source Scale  [ 1.000 ]  Amount         [ 1.000 ]
  Join Width    [ 0.100 ]  (Round/Bevel only)
```

- **Target X/Y** and **Source X/Y** are independent, tile-periodic output-UV
  offsets, default `(0, 0)`. Signed, not hard-clamped, with normal drag/fine-drag
  and per-value Reset. A unit shift wraps a tile; scrolling/moving either
  operand does not change its underlying generator's authored placement.
- **Target Height Offset** and **Source Height Offset** independently translate
  each sampled signed field vertically, default `0`, in raw signed height units.
  Keep these as the canonical two vertical offsets. **Retire the old Source
  Height Bias control** from the misclassified Generator inspector instead of
  stacking a redundant bias with the Source Height Offset.
- **Source Scale** remains signed so negative relief can be flipped; applied
  to Source before its Height Offset. For Self the same field may be transformed
  differently on each side.
- Offset controls must stay available in **Self**. The Source set moves the
  virtual copy; the Target set moves the base instance. Resetting all offsets
  and Source Scale to defaults makes Self Union/Intersect an exact identity.
- Separate XY and Height units visually: label the latter `Height Offset`,
  never imply that UV shifts and relief shifts are comparable values.
- No modal editor, Source selector, generator-mode dropdown, hierarchy editor,
  or duplicated child rows inside the inspector. The **operation and edge-style
  selectors stay on the Layers stack parent row**, following badge recipes.

## Authoring gestures

1. Select two eligible stack items → `Group as Boolean`. The selection's
   authored order determines Target then Source; dragging reorders roles.
2. Drop a compatible item on an empty Target/Source slot; moving a child keeps
   its identity. Copy/paste into either slot clones and remaps child GUIDs.
3. Context verbs on a compatible child: `Copy as Boolean Source` and
   `Paste as Boolean Target` should use shared clipboard mechanisms, not a
   second clip store.
4. Drag Target or Source within the container to change operand order and
   roles without changing textures, ownership or child IDs.
5. Moving or duplicating the Boolean parent carries its contiguous subtree;
   dragging child rows reorders, replaces or detaches operands using existing
   stack operations. No Lock or Swap control is needed.
6. Collapse hides the children but does not change their evaluation.
7. A Boolean with **exactly one eligible enabled child** automatically exposes
   the `Self` state on its stack parent row (informational chip, not a mode
   dropdown); it samples that single child as both Target and Source. Adding a
   second eligible child replaces the virtual Self Source with that explicit
   Source without resetting the two offset sets. Removing the second eligible
   child restores Self. Parent with zero eligible children is a neutral no-op.
8. A disabled, unresolved or invalid second child must not silently fall back
   to Self while remaining authored in the Source slot: show it as unavailable
   and fail neutral until the user fixes/removes it. This prevents hidden edits
   when toggling a child off.

## Composition contract

The compositor must gather and evaluate each nested operand **privately** in
authored order. These are signed height contributions, not Euclidean SDFs.
Neither child is also added to the outer running chain or published as a
second independent root. The parent combines A/B once and contributes one
signed delta. Existing child source publication and scoped mask ownership must
stay valid without colliding with root generator outputs.

**Offset evaluation:** Evaluate each owned generator once to a native signed
height field in output UV space. At the Boolean combine stage, sample that
field at `frac(outputUV - operandOffsetXY)` and apply its Height Offset. The
same periodic sampling convention must be used for both operands; XY offsets
are authored in tile UV units and independent of image resolution. Avoid
resolving this by repeatedly dispatching or rebuilding the generator. For a
zero XY offset use an exact native texel read (no filtering) to preserve the
original height bit-for-bit; nonzero fractional shifts use the normal periodic
filtering contract. For a Source operand, apply signed Source Scale before
Source Height Offset. Do not re-apply global generator normalization or clamps.

**Self evaluation:** A single eligible child supplies one native field texture,
read twice at the independently offset UVs; do not generate another child,
clone a GUID, run a second generator pass, or add the lone child's uncombined
height to the running stack. One child with zero offsets produces identity
for Union/Intersect; Subtract may remove positive relief from itself as an
intentional unary effect. On 0 eligible children, return exact zero delta.
The parent can still be moved or collapsed as one ordinary subtree.

Union/intersect use hard max/min of vertical surfaces. Subtract is one-sided
relief subtraction; no true 3D undercuts are promised. Round/Bevel apply signed
height join treatments and must preserve full subtraction depth outside their
width bands. Avoid passing ordinary signed height to metric SDF visualizers.
The outer Generator signed-zero height contract remains unchanged.

The previous `BooleanOps` Generator type, inspector Mode dropdown and source
chip are **not** the accepted final architecture. Keep the finite-volume
Scatter Carve shader work available for a separate, correctly classified
operation while retiring wrong Boolean Generator authoring entries; do not
silently discard the shader or mutate unrelated generator contracts.

## Implementation routing

1. Runtime: append a dedicated `EMixtormatLayerChildType` and Boolean payload
   in the child model (not the Generator payload union).
2. Hierarchy/editor: reuse ID Group child indentation, drop wrappers,
   clipboard remap and context verbs. Add row operation badges using the
   existing blend-mode badge API; implement pair selection/role assignment
   through direct stack dragging. Display `Self` when exactly one eligible
   child is present. Expose Target/Source XY/Height draggers in the inspector,
   not child pickers.
3. Gather: resolve one or two owned child indices, signed-height-producing
   eligibility, Self aliasing rules, and authored order; prevent cross-owner
   references/cycles. Reject unresolved second children rather than silently
   resolving Self.
4. GPU: evaluate owned subtrees into private signed outputs, dispatch the
   field Boolean once with independent UV/Height offsets, publish the parent
   result and avoid double-composite; Self shares a single input texture.
5. Preview: parent primary Height, secondary Boundary / Affected Area /
   Difference; retain signed-height preview semantics.
6. Retire old `BooleanOps` Generator creation and inspector ownership once
   the new execution path is connected; no stranded creation entries,
   inactive payloads, or misleading compatibility scaffolding.

Do not mark the feature complete based on Slate structure alone.
Compilation/runtime verification belongs to the user per `AGENTS.md`.
