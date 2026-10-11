# Shortcuts and Multi-Selection

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

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
