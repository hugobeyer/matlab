# Readiness 01 — Composition, Identity, and References

**Branch:** `feature/behavior-system-v2`  
**Report IDs:** `REL-01`, `COMP-01`, `COMP-02`, `COMP-03`, `COMP-04`, `COMP-06`  
**Status:** Not started. Read `AGENTS.md` and `AgentDocs/COMPOSITION.md` first.

## REL-01 — Duplicated asset GUID identity (P1)

**Reported evidence:** Repository-wide audit found no `PostDuplicate` / `RegenerateIdentity` path in `Source/`. Concern: duplicating a material asset can preserve Layer/Child/Group GUIDs and cause ambiguous cross-asset references. **Absence is confirmed; actual corruption remains unverified.**

- Inspect duplication lifecycle for `UMixtormatMaterial`, source shelf, layers, groups, child IDs, instances and parameter bindings.
- Establish **one** asset-duplication remap path. Reuse `RegenerateLayerIdentities` and existing child/group remap helpers rather than building a second identity system.
- Reassign owned IDs as a coherent transaction; remap **internal** references to the duplicate's newly owned IDs while retaining deliberate external references.
- Distinguish duplicating an **asset** from normal `Copy`, `Copy as Instance`, group expansion, and undo/save. Do not break intentional instances or existing group identity conventions.
- Preserve serialized identity semantics; avoid a blanket regeneration on load.

**Trace:** `Source/MixtormatRuntime/Private/MixtormatMaterial.cpp`, `Source/MixtormatRuntime/Public/MixtormatMaterial.h`, `MixtormatLayerGroups.cpp`, parameter binding, clipboard and OutputReference logic.

## COMP-01 — Visual stack vs compositor order (P1)

**Reported evidence:** `SMixtormat_Layers.cpp::RebuildLayerList` shows index 0 first; compositor treats layer 0 as bottom; “above” drop may insert a lower index. Static discrepancy is credible, **user-facing intended convention is not confirmed**.

- Inspect hierarchy iteration, drag insertion and compositing from the same index convention.
- Resolve the intended visible “above/below” meaning against actual order.
- If discrepancy remains, change **presentation or drag semantics only as necessary**, not the compositor's storage/order convention.
- Preserve selected rows, collapsed groups, inserts and reorder gestures; avoid a hidden reversal in multiple call sites.

**Trace:** `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`, `Widgets/Layers/MixtormatLayerDragDrop.cpp`, `MixtormatGpuCompositor.cpp`, `MixtormatGpuComposePipeline.cpp`.

## COMP-02 — Group-expanded child bindings (P2, likely)

**Reported evidence:** `MixtormatLayerGroups.cpp::BuildEffectiveLayers` may remap layer-level fields without remapping `Child.ParameterBindings`.

- Inspect expansion of layer/group/child IDs and all parameter binding reference owners.
- Reuse the canonical mapping logic for child bindings, drivers, instances and published field references; avoid double-remapping existing fields.
- Account for copies of groups and nested groups; invalid/out-of-scope references must fail closed rather than silently target the wrong source.

**Trace:** `MixtormatLayerGroups.cpp`, `MixtormatParameterBinding.cpp`, `MixtormatOutputReference.*`.

## COMP-04 — Disabled-layer field publication (P2, likely)

**Reported evidence:** Mask children appear to publish while ID/generator outputs do not, across disabled-layer paths.

- Document current publication semantics for disabled parent layers and children; distinguish invisible **material contribution** from independently readable **published fields**.
- Select one intentional rule consistent with canonical capability and dependency checks. Do not introduce source fallbacks.
- Ensure group expansion, instances, demand-gather and layer ordering follow that rule.

**Trace:** `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`, `MixtormatGpuPatternPasses.cpp`, `MixtormatGpuMaskPasses.cpp`, `MixtormatOutputReference.*`.

## COMP-03 — Effect mask under non-HeightBlend (P2, needs investigation)

**Reported evidence:** `Shaders/Private/MixtormatComposite.usf` around composite relief/AO may bypass the layer mask for non-HeightBlend operations.

- Trace the actual mask/influence application for color, height, normals, AO and each height operation.
- Change only the inconsistent branch if confirmed; preserve authored height operation math and existing blend semantics.

## COMP-06 — No-op mask debug reset (P2, confirmed static)

**Reported evidence:** A `DebugMask == CombinedMask` branch around `MixtormatGpuComposePipeline.cpp` L940–948 is reported to do nothing.

- Identify intended preview lifetime and ownership; use the existing debug-preview data path.
- Correct the stale preview behavior if traceable. Do not replace the preview system or touch unrelated compositing.

## Boundaries

Preserve existing height math, layer ping-pong parity, caches, GUID remapping paths and undo. Do not infer runtime failures from grep results. Do not add tests, builds or validation harnesses.
