# Sources-to-Hierarchy Mask Drag and Drop

> Repository: `hugobeyer/matlab`  
> Branch: `feature/behavior-system-v2`  
> Read root `AGENTS.md` and `AgentDocs/UI.md` before editing. Follow canonical ownership, Slate theme tokens, and existing parameter bindings. No tests, builds, diagnostics, automated validation, or extra documentation unless explicitly requested.

## Requested changes

Allow compatible **Mask** Sources to be dragged into target ownership areas in the layer tree, including compatible Behaviors, Generators, and scoped children.

- A **Mask** is an influence/coverage field; **Noise** is a procedural field generator that *can feed* a Mask. They are not equivalent types.
- Drop creates a linked/reference or instance-style mask child where supported, leaving the original Source intact.
- An available procedural Noise Value can be consumed as mask coverage, without changing the original Noise generator type.
- Preserve mask shaping, remapping, inversion, blending, coverage and UV controls under the existing mask contract.
- Allow reordering/scoping through existing drag/drop hierarchy behavior.
- Make compatible drop targets clear using existing hierarchy visuals; reject incompatible target types without implicit conversion to Noise.
- Maintain source IDs, child IDs, dependency order and typed published fields; preserve updates when the Source changes.

Relevant areas: `Widgets/Layers/MixtormatLayerDragDrop.cpp`, `MixtormatMaskSources.cpp`, `MixtormatLayerChildren.cpp`, and source/reference handling in Runtime and gather. Follow `AgentDocs/UI.md` and `AgentDocs/COMPOSITION.md` for the existing Source/Mask distinction.
