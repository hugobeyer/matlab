# Context Menu — Unified Actions and Labels

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Requested changes

Use consistent verbs, ordering, icon sizing, destructive emphasis, and shortcut hints across Layers, Generators, Behaviors, Masks, Filters, Effects, and Sources.

- Change `Remove Behavior`, `Remove Generator`, `Remove Effect`, `Remove Mask`, `Remove Filter`, `Remove Generated Mask`, and layer `Delete` to **Remove**.
- Standardize `Delete Source` to **Remove**; preserve Source deletion semantics.
- Change `Create Group` / `Group N Layers` to **Group**; preserve **Ungroup** when appropriate.
- Keep **Duplicate**, **Copy**, **Copy as Instance**, **Rename** consistent, with `Copy as Instance` appearing only for types that support it.
- Structure: applicable child creation at top; type-specific actions (Masks, Outputs, etc.) next; Duplicate/Copy/Copy as Instance/Group/Rename; Remove last.
- Preserve specialized behavior and source/output references; do not remove an action solely because its label is being unified.
- Right-click on one item of a multi-selection must not collapse the selection.

Canonical menu implementation: `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`. Sources menu also lives in `SMixtormat_Layers.cpp`. Reuse existing icon and menu styles rather than introducing custom palette values.
