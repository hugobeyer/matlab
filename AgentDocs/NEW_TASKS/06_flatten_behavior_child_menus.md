# Behavior Menu — Promote Child Actions

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Requested changes

Remove the unnecessary **Masks >** submenu in the Behavior context menu. Show applicable child-creation commands directly at the top.

Proposed upper menu rows (only when supported by that parent):

- **Mask** — regular Mask child, not synonym for Noise.
- **Noise Mask** — Mask child driven by procedural Noise/Noise Value.
- **Blur / Feather** — show the applicable existing child operation independently; do not collapse distinct implementations.

Then keep relevant other actions, Duplicate, Copy, Copy as Instance, Group/Rename where applicable, and a final **Remove** row.

- Remove the redundant **Noise Gate** menu action; retain its useful mask creation capabilities through **Noise Mask**.
- Preserve textured masks, layer-value masks, inline Noise masks, and supported copied/published Noise Value paths; do not collapse their data contracts.
- No extra nested submenu for these common Behavior children.
- Do not expose unsupported children merely for visual parity.

Primary location: `Widgets/Layers/MixtormatLayerMenus.cpp`; existing Noise Gate helper: `MixtormatMaskSources.cpp`; ownership support: `MixtormatLayerChildren.cpp`.
