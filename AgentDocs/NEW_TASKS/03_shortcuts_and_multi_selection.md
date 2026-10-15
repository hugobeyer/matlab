# Shortcuts and Multi-Selection

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Current implementation status — source review only

Layer-child selection and bulk menu/keyboard dispatch are wired, but not ready to
call complete. No Unreal build or runtime validation has been performed.

Completed safety fixes:
- Duplicate/Remove normalize selection to topmost roots, avoiding repeated subtree actions.
- Group as Boolean accepts one root generator or two adjacent generator subtrees;
  nonadjacent operands are rejected rather than moved across intervening operations.

Remaining before completion:
- Ctrl-deselect must clear active-row highlighting and must not restore selection via fallback.
- Plain clicks must collapse selection; right-clicks must preserve it.
- Shift ranges must follow visible rows, not hidden authored descendants.
- Duplicate all selected subtrees with one reference-remapping pass; select the copies.
- Bulk Duplicate/Remove now defer preview, Inspector sync and list rebuilds until the batch ends.
  Single-item helpers retain their default refresh behavior. Runtime verification is pending.
- Bulk edits still need history-boundary review and Boolean lock/operand-capacity validation.
- Prune selection on document changes, source selection, undo/redo and deletion.
- Boolean grouping still needs dependency, instance and scope eligibility review.

## Requested changes

Use one command-dispatch path for toolbar, context menu, and keyboard actions.

| Shortcut | Operation |
|---|---|
| Ctrl+D | Duplicate selection |
| Ctrl+Shift+C | Copy as Instance when supported |
| Ctrl+G | Group selected compatible items |
| F2 | Rename active item |
| Delete | Remove selection |

- Apply actions to the full compatible selection, not only the last clicked item.
- Keep existing Ctrl-click toggling, Shift-click range selection, Ctrl+Shift-click additive range selection.
- Duplicate multiple selected items in display order with valid ownership and fresh IDs; retain child hierarchies and internal references.
- Group multiple selected Layers using existing `CreateGroupFromSelection`; extend child grouping only to compatible ownership/type scopes and avoid silently relocating unrelated children.
- Mixed incompatible items must not be grouped into an invalid hierarchy.
- Respect edit-field focus and Unreal shortcut routing; preserve existing unmodified `G` handling and undo/redo behavior.
- Menus and shortcut execution must share eligibility logic and action handlers.

Primary locations: `Widgets/SMixtormat.cpp`, `Widgets/Layers/MixtormatLayerHierarchy.cpp`, `MixtormatLayerActions.cpp`, `MixtormatLayerClipboard.cpp`, `MixtormatLayerMenus.cpp`.
