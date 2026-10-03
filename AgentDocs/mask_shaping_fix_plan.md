# Mask Shaping — GPU Wiring Fix Plan

## Status

Backend modularization and missing-control wiring are implemented in source. GPU regression coverage has been added, but build/runtime validation remains pending: editor diagnostics cannot locate Unreal headers (`CoreMinimal.h` / `Misc/AutomationTest.h`). No build, test command, or profiling was run.

Stop point: do not implement the ramp UI or curve serialization yet. Prototype its visual interaction separately with the owner before choosing a curve format.

## Confirmed issue

Before this fix, the shared mask controls **Normalize Input**, **Input Min**, and **Input Max** were exposed in the inspector and copied into render data, but were not consumed by GPU mask evaluation. They are now routed through shared bindings and evaluation.

Affected render-data families:

- Texture masks, including applicable published/source-value inputs.
- Generated masks.
- Craquelure masks.
- Random ID masks.
- Color ID masks.

Existing contrast, offset, balance, and invert behavior is retained. The fault was missing GPU wiring, not UI ranges or UI clamps.

### Source references

- `Source/MixtormatRuntime/Public/MixtormatMaskShaping.h`: shared authored controls.
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`: `AddMaskShapingRows`.
- `Source/MixtormatShaders/Private/Compositing/MixtormatMaskGather.cpp`: mask parameter gathering.
- `Source/MixtormatShaders/Private/Compositing/MixtormatIdGather.cpp`: ID mask parameter gathering.
- `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`: shader parameter binding and mask passes.
- `Shaders/Private/MixtormatMask.usf`: texture mask evaluation.
- `Shaders/Private/MixtormatMaskOps.ush`: shared shaping helper.

## Constraints

- UI ranges remain UI ranges; do not turn them into runtime clamps.
- UI hard clamps remain owner-controlled and unchanged by this fix.
- Preserve authored values, existing APIs, mask placement, and blend behavior.
- Preserve current output when normalization is off and input levels are 0–1.
- Reuse existing GPU/RDG patterns; no dependencies or CPU readback.
- Inspect each producer's current blur/shaping order before changing it.

## Modular boundary — shared shaping, separate generators

- Keep mask generators separate: each producer owns how it evaluates its raw scalar field.
- Use one shared shaping implementation for normalization, input levels, balance, contrast, offset, and invert. Preserve the existing order within shaping.
- Share parameter binding and normalization scheduling where practical; do not create five independent implementations of these controls.
- Route node previews and final composition through the same shaping logic.
- Shared logic does not require a separate GPU pass for every mask. Reuse shader math within existing passes when possible; schedule extrema reduction only when normalization is enabled.
- Respect existing blur ordering and processed-mask paths so shaping is applied exactly once.

## Implementation plan

1. **Define processing order.** Evaluate the node's raw scalar mask, optionally normalize, apply input levels, run existing shaping, then blend into the accumulated mask.
2. **Establish a shared GPU shaping path.** Reuse `MixtormatMaskOps.ush` and existing pass infrastructure across all affected families, following the modular boundary above. Share shaping math, parameter binding, and normalization scheduling where practical, without merging generators or requiring an extra pass for every node.
3. **Wire Input Min/Max.** Bind the authored endpoints and map their interval to 0–1 before existing shaping. Guard the denominator without altering authored values or UI ranges.
4. **Decide endpoint edge cases.** Explicitly define equal and reversed endpoint behavior before implementation. Do not silently replace these with UI restrictions.
5. **Measure raw mask extrema on GPU.** When Normalize Input is enabled, reduce the node's evaluated raw scalar field to its minimum and maximum. Measure the field after source selection/placement, before levels, shaping, and accumulated-mask blending.
6. **Normalize per node.** Use measured extrema to map the raw field to 0–1. Explicitly decide constant-mask behavior and non-finite handling; do not normalize the combined layer mask instead.
7. **Preserve scoped masks and blur.** Respect processed-mask paths such as `UsePreShaped`; do not normalize, place, or shape a mask twice. Keep each producer's existing blur relationship unless an explicit behavior change is approved.
8. **Add regression coverage.** Test all affected families, neutral defaults, independent levels, normalization, constant fields, equal/reversed endpoints, scoped masks, blur, and isolation from neighboring nodes.
9. **Validate output and cost.** Run targeted GPU tests and compare node previews with final composition. Verify that normalization-disabled nodes avoid extrema-reduction work and that no synchronous CPU readback is introduced.

## Implemented backend boundary

- All five private render-data families reuse `FMixtormatMaskShaping`; gathering copies the shared payload rather than enumerating its fields independently.
- `Source/MixtormatShaders/Private/MixtormatGpuMaskShaping.h` owns the common shader binding contract and producer-pass scheduling.
- `Source/MixtormatShaders/Private/MixtormatGpuMaskShaping.cpp` owns the normalized-mask shaping/merge pass.
- `Shaders/Private/MixtormatMaskShaping.ush` owns input levels and routes through the existing `MixtormatMaskOps.ush` balance/contrast/offset/invert helper. Individual producers do not duplicate shaping math.
- Normalization reuses `AddNormalizeFieldPasses` / `MixtormatFieldRange.usf`. The producer emits its raw scalar field at output resolution before shaping and blending; measurement stays on the GPU.
- No normalization work is scheduled when disabled. Existing single-pass producers remain single-pass unless their existing filters already need scratch targets.
- Texture normalization is resolved locally before blur/curvature/warp. `UsePreShaped` consumers bypass reshaping. Debug previews and final composition consume the same evaluated result.
- Existing UI and authored scalar Balance are unchanged. The later ramp belongs at this shared shaping boundary, not inside each producer.

### Defined edge cases

- Equal input endpoints (within `1e-6`) produce a threshold at Input Min.
- Reversed input endpoints reverse the mapping.
- Neutral input levels (0–1) preserve the original incoming value until existing shaping saturation, including signed/HDR inputs.
- Normalization uses the existing field-range utility's zero-span policy: constant fields map to zero before levels/shaping. This differs from the earlier standalone OpenCL prototype's constant-field policy.
- Non-finite raw samples contribute zero when normalization is enabled.
- Generated masks without an underlying surface retain their existing identity behavior.

### Added regression coverage (not executed)

`Source/MixtormatEditor/Private/Tests/MixtormatCompositorTests.cpp` now includes:

- `Mixtormat.Compositor.MaskShaping.InputLevels`: neutral defaults, independent levels, reversed/equal endpoints, measured normalization, levels after normalization, blur without double-shaping, and constant fields.
- `Mixtormat.Compositor.MaskShaping.AllProducers`: levels with normalization off/on for texture, generated, both Craquelure modes, Color ID, and Random ID producers.
- Extended scoped Grade mask coverage: normalized input levels affect the scoped contribution once without changing the global sibling mask.

Acceptance criteria below still require Unreal GPU execution to confirm; source inspection is not a runtime test.

## Acceptance criteria

- Input Min and Input Max visibly affect each supported mask family.
- Normalize Input uses the individual node's measured raw field range.
- Neutral settings preserve existing output.
- Processed masks are not shaped twice.
- All affected generators use shared shaping logic, rather than family-specific copies.
- Node previews and final composition use the same shaping behavior.
- UI ranges, UI hard clamps, and saved authored values remain unchanged.
- Edge-case behavior is documented and tested.

## Future work — modular curved-ramp bias UI

We will create a **reusable curved-ramp UI module for bias controls** in a separate follow-up.

- Reuse the project's existing UI styling and parameter-binding patterns.
- Keep the UI component separate from parameter state and GPU evaluation.
- Prototype the supplied graph reference first: draggable endpoints/interior control points, a filled curve area, and linear/smooth mode controls. Include a non-monotonic peaked curve, not only a monotonic bias curve.
- Discuss interaction, neutral reset, and how the ramp relates to existing scalar Balance during that visual prototype.
- Decide the curve representation, interpolation, and applicable bias controls only after the visual prototype is approved.
- Define how neutral/default curves preserve existing behavior.
- Preserve current bias controls and saved values unless a migration or replacement is explicitly approved.
- Do not bundle this future UI work into the mask-wiring fix.

The curved-ramp UI is planned, not implemented. Its data format and shader integration remain design decisions.
