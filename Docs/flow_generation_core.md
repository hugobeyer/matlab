# Mixtormat — Flow Generation Tools (core proposal)

Status: staged implementation. Rock Formation now caches a local signed boundary field alongside its
height and existing outputs. Generator-scoped Shape Deform, Generator Flow, Flow Carve, the
shared propagation solve and selected-item previews are not implemented yet.

## Goal

Build generator-owned deformation and carving from a coherent **2D direction field**.
A slope is an initial direction, not a flow solve; the existing Flow Warp effect uses curl
and optional mask/height slopes and is not this tool. Keep existing Flow Warp behavior intact.

## Existing files and integration points

Paths below are relative to the plugin root. These files exist; none implements the proposed
Generator Flow solve yet.

| Concern | Existing file(s) | Integration point |
| --- | --- | --- |
| Saved effect identity and settings | [`Source/MixtormatRuntime/Public/MixtormatEffect.h`](../Source/MixtormatRuntime/Public/MixtormatEffect.h), [`Source/MixtormatRuntime/Public/MixtormatMaterial.h`](../Source/MixtormatRuntime/Public/MixtormatMaterial.h) | Append new effect enum values without changing serialized values; add authored controls to layer-effect settings. |
| Existing Flow Warp reference (not generator flow) | [`Shaders/Private/MixtormatFlowWarp.usf`](../Shaders/Private/MixtormatFlowWarp.usf), [`Source/MixtormatShaders/Private/MixtormatGpuEffectPasses.cpp`](../Source/MixtormatShaders/Private/MixtormatGpuEffectPasses.cpp) | Reuse channel-resampling and normal-transform ideas, not its curl/slope source or behavior. |
| Generator field and output | [`Shaders/Private/MixtormatRockFormation.usf`](../Shaders/Private/MixtormatRockFormation.usf), [`Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`](../Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp) | First source candidate: Rock Formation's height and `RockEdgeDistance` output; verify SDF units and validity before using as a distance solve seed. |
| Pass data, gather and scheduling | [`Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`](../Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h), [`Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp`](../Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp), [`Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`](../Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp), [`Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`](../Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp) | Carry new effect data, validate generator ownership and schedule field/deform/carve passes in child order. |
| Layer items and controls | [`Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`](../Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp), [`Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`](../Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp) | Add scoped item placement/menu entries and inspector controls. Existing Flow Warp currently scopes to mask/effect or layer, not a generator. |
| Preview routing | [`Source/MixtormatShaders/Public/MixtormatGpuCompositor.h`](../Source/MixtormatShaders/Public/MixtormatGpuCompositor.h), [`Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`](../Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp), [`Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp`](../Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp), [`Source/MixtormatShaders/Private/MixtormatGpuDebugPreviewPasses.cpp`](../Source/MixtormatShaders/Private/MixtormatGpuDebugPreviewPasses.cpp) | Define selectable output kinds and preview textures for flow, UV grid, influence and carve mask. |

**New work (no existing file yet):** a shared distance/direction solve and the generator-owned
deform/carve shaders and passes. Choose their names when implementing those milestones;
`Docs/openclref/carve.cl` is a COP reference, not a running Mixtormat pass.

## Layer items and order

```text
Layer
└─ Generator (e.g. Rock Formation)
   ├─ Shape Deform     SDF expand/erode, bulge/pinch
   ├─ Generator Flow   derive, bend, extend and feather a direction field; warp UVs
   │  └─ Mask          optional restriction
   └─ Flow Carve       cut or raise height along that field
      └─ Mask          optional restriction
```

Items are scoped to their owning generator, evaluated top to bottom, and can be disabled.
An item's mask gates only that item. If a required field is unavailable, disable that
mode rather than silently substituting a different field. Publishing the resulting flow
and masks for other layers can follow after local behavior is validated.

## Field contract

- Current Rock Formation `BoundaryField` is an internal `RG32F` texture cached at slot 6:
  R is the signed distance to a nearby gapped chunk outline in UV units (negative inside),
  G indicates that a chunk outline was sampled. It queries the same periodic 5×5-cell
  neighbourhood as the height field. It is **not** a global exact SDF: the nearest edge
  can lie outside this neighbourhood, and overlapping chunk outlines can leave internal
  edges. Do not expose it as SDF source or feed it to the shared solve until a global
  distance/validity pass addresses these cases. `RockEdgeDistance` now writes the same
  signed local UV distance from this calculation for existing scalar copy-output users;
  the height-winner-only calculation and its `1e3` no-hit sentinel were removed.
- Source: the owning generator's **unclamped signed distance** and/or height in the
  generator's UV frame. An output called an SDF must declare its distance units and sign
  (negative inside); a normalized/clipped field is not an exact distance.
- Derive a local vector from `∇SDF` or `∇height` at a configurable texel radius. Use
  `∇SDF` for boundary-normal flow, its perpendicular for contour flow, and a continuous
  normal/tangent mix between them. Avoid direction normalization where the gradient is
  too small; mark those seeds invalid until extension fills them.
- The field carries direction, signed distance/reach, influence, and validity separately.
  Do not store direction in height or confuse distance with flow speed.
- All samples and noise respect the same tile period. If a generator does not publish
  an SDF yet, first add a typed distance output for that generator; do not label an
  arbitrary height mask as an exact SDF.

## Shared solve

1. **Seed:** derive oriented directions on or near the generator's boundary; optionally
   blend height downhill. Bend the 2D vectors with a rotation and low-frequency periodic
   noise; provide signed along-flow and across-flow offsets. A coordinate deformation
   changes gradients by the Jacobian transpose when derivatives are reused.
2. **Extend:** propagate the nearest valid seed direction and distance outward in a
   separate tile-aware distance/label solve (Eikonal/fast sweeping or jump flooding plus
   refinement). Avoid averaging opposing vectors into zero or treating a per-pixel
   gradient as an already-extended flow field.
3. **Feather:** compute influence from propagation distance with explicit reach and
   softness. Multiply by the item's scoped mask. No `fmod` of travel distance: wrapping
   travel resets direction/offset abruptly and is not feathering.
4. **Deform:** Shape Deform offsets the signed boundary for expand/erode; bulge/pinch
   moves UVs along/opposite the extended direction with distance falloff. Generator Flow
   traces that direction in small midpoint/RK2 steps and samples the original generator
   channels once from the traced UVs (not once per step). Cap or visualize excessive
   displacement/Jacobian compression rather than silently clamping heights.
5. **Carve:** Flow Carve gathers height along the same trace, taking a distance-biased
   minimum for grooves or maximum for deposits. Its Depth, Reach, Width and Falloff
   affect height and an explicit carve mask, not the generator's original SDF. Rebuild
   normals from the resulting height; preserve channel alignment when UVs are deformed.

## Controls and previews

- Shared: Source (SDF or Height), normal/tangent mix, direction angle, bend, offset,
  reach, feather, seed, and amount. Expose controls only for applicable item modes.
- Shape Deform: signed SDF offset and bulge/pinch strength.
- Generator Flow: trace length, steps and signed warp strength; keep trace cost bounded.
- Flow Carve: groove/deposit mode, depth, width and falloff.
- Selected-item previews: **Flow Direction** (hue/strength), **Warped UV Grid**
  (stretch/folds), **Influence** (extension/feather/mask), and **Carve Mask**.
  UV Grid is the default Generator Flow tuning preview. Existing layer previews remain.

## Implementation milestones

1. Publish one generator's distance/height field in consistent UV units. Verify tiling,
   sign and gradients at seams; do not change existing Flow Warp.
2. Implement a generator-scoped Shape Deform and a reusable direction + distance solve.
   Preview direction, validity and influence before modifying any material channels.
3. Implement feathered RK2 Generator Flow with one final resample; compare height and
   normals and inspect the UV Grid for foldovers at several resolutions.
4. Add Flow Carve using the shared field; verify groove/deposit masks, negative/over-one
   height handling, normal reconstruction, ordering, masks and saved parameter defaults.

Avoid calling a smoothstep on raw distance an Eikonal solve. Validate visual output and
shader bindings in Unreal after each milestone; this document is not that validation.
