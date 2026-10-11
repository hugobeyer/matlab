# Sources Shelf — Consistent Creation and Context Menus

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Requested changes

Keep Sources as non-compositing document data, separate from layer stack members. Ensure source creation and menu presentation match the generic naming and context-menu rules.

- Source creation uses generic sequential `Source0`, `Source1`, etc., independent of whether the underlying generator is Noise, Cracks, Rock Formation or another type.
- Keep the generator kind visible through icon/type badge rather than injecting it into the default name.
- Standardize Source context actions: **Copy**, **Copy as Instance** (when supported), **Duplicate**, **Rename**, **Remove**.
- Keep paste-source and paste-as-instance paths where supported, without redundant duplicate entries.
- Make Sources usable as compatible typed inputs through source-reference and mask drag/drop workflows, without turning Sources into layer children by default.
- Retain persisted IDs, binding scopes, copy/paste remapping, and existing source dependency behavior.

Relevant code: `Widgets/SMixtormat_Layers.cpp`, `UI/Layers/SMixtormatSourcesShelf.*`, `SMixtormatSourceRow.*`, `Widgets/Layers/MixtormatLayerClipboard.cpp`, and Runtime's Source entry/binding types.
