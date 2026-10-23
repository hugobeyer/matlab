# Generic Names for Sources, Layers, Masks and Groups

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Requested changes

Display names must not be derived from object types or generator preset names. The icon and type badge convey type; the text is an independent editable name.

| Created object | Default sequence |
|---|---|
| Source | `Source0`, `Source1`, `Source2` ... |
| Layer | `Layer`, `Layer1`, `Layer2` ... |
| Mask child | `Mask`, `Mask1`, `Mask2` ... |
| Group | `Group`, `Group1`, `Group2` ... |

- Assign the generic default immediately on creation when the user does not provide a name.
- Do not create `Fill Layer 1`, `Generator Layer 1`, `Rock Formation`, `Cracks`, `Worley F1`, etc. as default object names.
- Names may be changed freely and need not be unique; repeated custom names such as `dumblayers` are valid.
- Assign generated names without overwriting existing customized labels.
- Preserve user-authored names through duplicate, instance, clipboard, save/open, and type changes.
- Default numbering should avoid collisions with currently generated names but not prohibit duplicate user-authored names.

Use the current creation/default helpers and name storage in Runtime, `MixtormatLayerActions.cpp`, `MixtormatLayerChildren.cpp`, source shelf creation, and group creation. Avoid duplicating naming algorithms across UI paths.
