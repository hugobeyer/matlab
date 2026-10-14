# Boolean — layer stack container

Status: approved design; implementation pending. Replaces the previous
`BooleanOps` Generator architecture. Do not present the old Generator mode
selector as the finished UI.

## Canonical structure

A dedicated `Boolean` child occupies one position in a Generator layer's
ordinary **Layers stack**. It is not an `EMixtormatGeneratorType`, a layer type,
or an ID Group with repurposed semantics.

The parent has exactly two authored, indented child slots:

- **Target** — height-producing child; first operand (A).
- **Source** — height-producing child; second operand (B).

Both are ordinary layer-stack child entries, sharing the parent through the
existing `ScopeOwnerChildId` hierarchy. They retain identity and their own
parameters, masks and visual controls. Use the established ID Group hierarchy
painting, disclosure/expand/collapse, selection, subtree move, paste/duplicate,
and drop-target patterns; **do not use** the Region-ID group evaluator.

### Example

```text
▾ Boolean                 [Subtract ▾] [Round ▾] [Lock]
  ├ Target   Rock Formation
  └ Source   Cracks
```

`Union / Subtract / Intersect` is an inline operation badge on the **Boolean
parent row**, in the same right-hand badge region as existing blend-mode
controls. `Hard / Round / Bevel` is a second compact row control if room
permits; otherwise include edge styles in the same row-level picker.
Neither picker nor the Source/Target hierarchy goes in the inspector.
Existing theme tokens, badge recipes and hierarchy gutters are mandatory.

The inspector, when Boolean is selected, has numeric controls only: join width
(visible only for Round or Bevel), signed source scale/bias, and amount. No
Generator-mode dropdown, Source dropdown, hierarchy editor or duplicated child
rows.

## Authoring gestures

1. Select two eligible stack items → `Group as Boolean`. The selection's
   authored order determines Target then Source unless the user swaps them.
2. Drop a compatible item on an empty Target/Source slot; moving a child keeps
   its identity. Copy/paste into either slot clones and remaps child GUIDs.
3. Context verbs on a compatible child: `Copy as Boolean Source` and
   `Paste as Boolean Target` should use shared clipboard mechanisms, not a
   second clip store.
4. Parent row's `Swap` exchanges roles atomically without changing textures,
   ownership or child IDs.
5. Locked (default): group moves/duplicates as one contiguous subtree.
   Unlocked: roles can be replaced, reordered or detached using existing
   stack operations. Lock is an authoring restraint, **not** a shader switch.
6. Collapse hides the children but does not change their evaluation.
   Missing operand is neutral/no-op and visibly unfilled on the parent.

## Composition contract

The compositor must gather and evaluate each nested operand **privately** in
authored order. These are signed height contributions, not Euclidean SDFs.
Neither child is also added to the outer running chain or published as a
second independent root. The parent combines A/B once and contributes one
signed delta. Existing child source publication and scoped mask ownership must
stay valid without colliding with root generator outputs.

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
   and locking in the stack only.
3. Gather: resolve two owned child indices, signed-height-producing
   eligibility, and authored order; prevent cross-owner references/cycles.
4. GPU: evaluate owned subtrees into private signed outputs, dispatch the
   field Boolean once, publish the parent result and avoid double-composite.
5. Preview: parent primary Height, secondary Boundary / Affected Area /
   Difference; retain signed-height preview semantics.
6. Retire old `BooleanOps` Generator creation and inspector ownership once
   the new execution path is connected; no stranded creation entries,
   inactive payloads, or misleading compatibility scaffolding.

Do not mark the feature complete based on Slate structure alone.
Compilation/runtime verification belongs to the user per `AGENTS.md`.
