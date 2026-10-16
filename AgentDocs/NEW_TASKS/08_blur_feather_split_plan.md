# Blur + Feather Split — Plan and Inspector Decision

## Review status

Planning only; implementation and inspector layouts require user approval.
Blur consolidation can proceed independently after approval. Feather's numerical solver,
Flow semantics, and revised controls are design gates, not settled implementation details.

## Goal

- **Blur** is the single authored filtering module and the single GPU Gaussian owner.
- **Feather** is Eikonal-only slope enforcement; it never averages neighbouring values.
- No inspector dropdowns: exclusive choices use `SMixtormatSegmentedControl`.
- Modules process the current field at their authored chain position.
- Remove replaced types, shader stages, bindings, metadata, and docs together.

## Current audit

| Concern | Current owner | Problem |
|---|---|---|
| Mask blur | `FMixtormatMaskBlur` / child `Blur` | Author-facing node, but limited to Masks. |
| Layer blur | procedural `LayerBlur` Effect | Separate data, inspector, shader, and deferred scheduling. |
| Feather Blur | `FMixtormatBehaviorFeatherSettings::Mode` | Incorrectly makes Feather a Gaussian filter. |
| Height Feather | `MixtormatBehaviorFeather.usf` | Existing Eikonal envelope uses an artist iteration budget. |
| Flow Feather | `MixtormatBehaviorFlowFeather.usf` | Eikonal seed/relaxation exists, but Blur mode averages Flow. |
| Internal height smoothing | `MixtormatGpuMaskPasses.cpp` | Reuses the mask-only blur shader for signed Height. |

The current code contains four Gaussian implementations/paths:

1. `Shaders/Private/MixtormatMaskBlur.usf`
2. `Shaders/Private/MixtormatLayerBlur.usf`
3. Height Gaussian stages in `Shaders/Private/MixtormatBehaviorFeather.usf`
4. Flow Gaussian stage in `Shaders/Private/MixtormatBehaviorFlowFeather.usf`

## Proposed architecture

### 1. One GPU Gaussian owner

Create one private shared blur dispatch API in `MixtormatShaders`.

- One shader family; typed variants only for scalar, float4, normal, and valid-Flow.
- One radius convention: `sigma = radius / 2`, unit-texel sampling to `3 * sigma`.
- Wrap at tile edges.
- Both axes filter first; masking, Strength/Amount, clamping, and normal normalization occur
  only after the completed filter.
- Scalar Height is never clamped. Masks clamp after filtering. Flow weights only valid samples.
- Each caller owns its scheduling and input/output semantics, not another Gaussian kernel.

### 2. One authored Blur module

Promote the existing `EMixtormatLayerChildType::Blur` from a Mask-only node to the canonical
`Blur` module. Replace `FMixtormatMaskBlur` with a generic `FMixtormatBlur` payload.

- Existing Mask placement: filters coverage before the Mask merges.
- Layer placement: replaces procedural `LayerBlur`; filters the owning layer's full surface output
  (Base Color, Normal, RAM, and Height).
- Generator placement: filters only the current signed Height in generator-chain order.
  It never implicitly filters every published Generator output.
- Flow placement: filters the current Flow in flow-chain order and preserves validity.
- The source is inferred from the legal owner or a typed connection; there is no target dropdown.

User decision: Blur's placement and parent define its target and execution point. It can be
attached to a Layer, Mask, Curvature, Effect, or another eligible module and filters that owner's
output before downstream consumers continue. Do not expose Layer Coverage / Whole Composite
scope controls or use that distinction to select the target. The previous proposal to preserve
accumulated-composite LayerBlur semantics is superseded; scheduling must follow the approved
ownership contract rather than silently retain the old deferred behavior. Legal owners must
provide a defined typed output. Generator ownership means Height only; no multi-output routing
is inferred.

### 2a. Mask Blur / Curvature ownership

Do not add a free-form interleaved Blur/Curvature sibling-operation system. Blur nested under
Curvature filters Curvature's completed output; it does not pre-filter Curvature's input.
A Mask-owned Blur independently filters the Mask's coverage before it merges. Remove the current
summed-mask-Blur implementation as part of the generic Blur migration; do not retain its flattened
path as a fallback.

### 3. Feather is Eikonal-only

Replace `FMixtormatBehaviorFeatherSettings` with one Eikonal contract:

- Width controls slope distance / rate enforcement footprint.
- Strength and scoped mask blend the finished Feather result back into the source.
- **Side** chooses Both, Inside, or Outside as a final spatial gate where signed boundaries exist.
  It is not propagation direction and is not equivalent to raising lows or lowering highs.
- **Slope operation** is approved as three segmented buttons: High to Low raises surrounding
  lows; Low to High lowers surrounding highs; Symmetric combines both envelopes, retaining
  the current two-envelope approach. Operation is separate from Side = Both.
- Preserve `DirectionBias` and its current controls/bindings until a replacement is approved.
- Global Speed controls propagation rate.
- Optional scalar **Availability** is a typed field socket: `0` blocks propagation, `1` leaves
  speed unchanged, intermediate values slow travel. It is distinct from the final blend mask.
- Solver quality is internal. Remove the artist `Iterations` field, inspector control, metadata,
  render field, and hard-limit helper.

**Solver unresolved:** JFA alone does not solve variable-speed Eikonal propagation or respect
zero-speed barriers. Select a solver that handles local travel cost and obstacle boundaries;
fast sweeping is a candidate, not an approved implementation. Define convergence/error tolerance,
GPU work bounds, wrap handling, and behavior when convergence is not reached. Do not merely hide
a fixed iteration budget or silently return a partially propagated field. Define how Width and
Speed interact, including units, before implementing either control.

**Flow semantics unresolved:** Eikonal solves a scalar arrival/slope problem. It does not define
how to feather a vector field. Before replacing active Flow behavior, specify whether the operation
enforces component slopes, transports vectors using scalar arrival, or uses another defined
contract. Address direction, magnitude, cancellation, validity, and publication to later consumers.
Do not call Gaussian vector averaging Eikonal Feather. Height-only v1 requires explicit approval;
it must not silently disable an existing Flow operation.

Reference distinction: Houdini's [Feather COP](https://www.sidefx.com/docs/houdini/nodes/cop/feather.html)
enforces a slope and has no Iterations control. Its
[Eikonal COP](https://www.sidefx.com/docs/houdini/nodes/cop/eikonal.html) does expose Iterations and a
per-pixel speed field; zero speed forms a barrier. Availability is a proposed input for this project,
not a claim that Houdini Feather has that socket.

## Proposed inspector layout — requires approval

All binary/mutually-exclusive choices are segmented controls; no dropdowns.

### Blur

```
BLUR [enable]

  FOOTPRINT   Radius X | Radius Y
  AMOUNT      Amount
  CHANNELS    [ Appearance ] [ Height ]   // root Layer only; Appearance is BC/Normal/RAM
```

- No owner exposes `Scope`; placement and ownership determine the target.
- Mask, Generator, and Flow owners omit `Channels` because their target is inherent.
- Generator owner: Height; Flow owner: Flow; Mask owner: Coverage.
- A Blur with a typed field connection displays a source chip/connection row, never a dropdown.
- `Radius X` and `Radius Y` remain separately bindable and retain existing anisotropic behavior.

### Feather

```
FEATHER [enable]
  SIDE        [ Both | Inside | Outside ]
  OPERATION   [ High to Low | Low to High | Symmetric ]  // full-width strip
  WIDTH       Width
  RESPONSE    Strength | Speed
  BIAS        Direction Bias                         // preserve until replacement approved
  INPUT       Availability  [connected scalar chip / Connect action]
```

- `Availability` is optional; absent means `1` everywhere.
- The existing scoped mask remains the final blend gate, not propagation speed.
- No Blur/Eikonal mode control. No Blur option. No artist Iterations control in the completed design.
- `SIDE` gates output; `OPERATION` selects slope enforcement. Keep these meanings separate.
- Direction Bias remains until its replacement/removal is approved; do not silently discard it.
- Flow-specific eligibility of Side and Operation remains undecided with the Flow contract.
- If a geometry/metric choice is needed, use segmented buttons after approval; do not pre-add it.
- Grouped exclusive choices use a direct, full-width `SMixtormatSegmentedControl`, matching
  Mask Source and Layer Composition. Do not wrap them in `MixtormatRow::MakeTrailing`.
  The card title or caption supplies the group label.
- Apply that full-width rule to all inspector grouped segmented choices: Feather, Boolean,
  Warp, Deform, Field Combine, and future equivalent groups.
- Reuse existing group/card/slider styles. These layouts remain proposals, not authorization to edit.

## Required deletion/update set

| Remove or replace | Required files |
|---|---|
| Feather `Mode`, `Iterations`, `EMixtormatBehaviorFeatherMode` after replacement; retain `DirectionBias` pending approval | `Source/MixtormatRuntime/Public/MixtormatBehaviorTypes.h` |
| Update Feather member metadata/bindings; retain `BehaviorFeatherSettings` parameter-owner identity and active mappings | `Config/MixtormatParameterAuthoring.json`; `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`; `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`; `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp` |
| Feather Blur UI and Eikonal-only controls | `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` |
| Feather Blur shader stages and Gaussian bindings | `Shaders/Private/MixtormatBehaviorFeather.usf`; `Shaders/Private/MixtormatBehaviorFlowFeather.usf`; `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` |
| Artist iteration gather/render fields/helpers | `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`; `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` |
| Mask-only Blur payload/UI/placement | `Source/MixtormatRuntime/Public/MixtormatMaskBlur.h`; `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`; `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`; `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`; `MixtormatLayerActions.cpp`; `MixtormatLayerMenus.cpp` |
| Procedural LayerBlur type, fields, UI, gather, queue, dispatch | `Source/MixtormatRuntime/Public/MixtormatEffect.h`; `Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp`; `Source/MixtormatShaders/Private/Effects/MixtormatLayerBlurPasses.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`; `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`; `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp` |
| Procedural Flow Warp Effect; use Flow → Warp instead | `Source/MixtormatRuntime/Public/MixtormatEffect.h`; `Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.*`; `Source/MixtormatShaders/Private/Effects/MixtormatFlowWarpPasses.cpp`; `Shaders/Private/MixtormatFlowWarp.usf`; `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`; `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp`; `MixtormatLayerMenus.cpp`; `MixtormatLayerChildren.cpp`; layer badges/capabilities and directly related docs |
| Duplicate Gaussian classes/shaders | `Shaders/Private/MixtormatMaskBlur.usf`; `Shaders/Private/MixtormatLayerBlur.usf`; `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`; `Source/MixtormatShaders/Private/Effects/MixtormatLayerBlurPasses.cpp` |
| Summed Mask Blur radii and separate Curvature list → ordered filter operations | `Source/MixtormatShaders/Private/Compositing/MixtormatMaskGather.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`; `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp` |
| Internal smoothing callers | `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp` |
| Current architecture description | `AgentDocs/COMPOSITION.md`; `AgentDocs/SHADERS.md`; `AgentDocs/UI.md` |

Do not retain forwarding wrappers, fallback shaders, legacy inspector branches, deprecated
properties, or parallel Blur paths. This is unshipped development: update every current reader in
the same implementation series.

## Implementation order

1. Approve Blur ownership and inspector layout independently of Feather.
2. Define `FMixtormatBlur` and legal owners; implement shared Gaussian dispatch.
3. Migrate internal smoothing, Mask, Layer, Generator Height, and Flow Blur; delete replaced paths.
4. Implement approved ordered Mask filters as a separate behavior-changing step.
5. Finalize Feather solver, Width/Speed units, scalar Availability input, and vector-field contract.
6. Obtain approval for Feather's revised Side/Operation/Bias controls before changing its model.
7. Implement Eikonal-only Feather; remove replaced modes/iteration fields with all their readers.
8. Update menus, capabilities, binding/member metadata, cache dependencies, and docs per step.
   Preserve active parameter-owner identities; no duplicate fallback or deprecated-property paths.
9. Perform shader compilation and visual chain validation only with explicit approval.

## Confirmed decisions

- Blur may be a child of Curvature and filters Curvature's completed output before downstream
  consumers continue. It uses the shared generic Gaussian implementation; no target dropdown.
- Layer-owned Blur is shown at the end of the layer rail. Placement and parent determine what
  Blur filters; no Layer Coverage / Whole Composite selector is wanted. The earlier agreement
  to retain accumulated-composite execution is superseded by this ownership decision.
- Module-owned Blur is nested under its owner and filters that owner's completed output before
  downstream consumers continue. Dragging onto a module reparents Blur; creating inside does
  the same. Legal module owners still require typed-output eligibility.
- Remove the procedural `Flow Warp` Effect. Warp is authored through the existing Flow → Warp
  path; do not retain a parallel curl-warp effect, an alternate Blur owner, or a fallback path.
- Feather Operation is approved as High to Low / Low to High / Symmetric full-width
  segmented buttons. Side remains a separate gate.
- Inspector grouped segmented choices are full-width, using the direct Mask Source / Layer
  Composition pattern rather than `MixtormatRow::MakeTrailing`. This applies to Feather,
  Boolean, Warp, Deform, Field Combine, and future equivalent groups.
- Curvature does not need Blur-before-Curvature support: Blur nested under Curvature applies
  after extraction. No free-form Mask Blur/Curvature sibling ordering will be implemented.
- Generator-owned Blur filters only the Height chain, never all Generator outputs. Layer-owned
  Blur filters the layer surface channels (Base Color, Normal, RAM, Height). Blur remains a normal
  child: copy, paste, or instance it under another eligible owner when separately desired.
- Remove the procedural Flow Warp Effect. Author warping as Flow → Warp; delete its complete
  runtime, gather, pass, shader, inspector, hierarchy/menu, capability, metadata, and docs path.
- These approvals do not decide remaining inspector controls/layouts, ambiguous Generator output
  selection, or the remaining Feather solver, Flow, Side, Bias, and Availability decisions.

## Decisions requested

1. Approve the remaining Blur inspector controls (Radius X/Y, Amount, and channel controls)?
   Scope is resolved: no Layer Coverage / Whole Composite controls; target follows ownership.
2. Generator Blur target is resolved: only the current Height chain. No all-output behavior or
   target selector; use a separate copied/instanced Blur under another eligible owner instead.
3. Confirm replacing summed Mask Blur radii with independent Mask-owned Blur modules, with no
   free-form Blur/Curvature sibling ordering?
4. Approve Feather Side controls separately from the approved three Operation buttons?
5. Retain Direction Bias, or approve a specifically defined replacement?
6. Finalize Flow Feather semantics before implementation; Height-only v1 is not assumed.
7. Approve `Availability` as a separate scalar connection, distinct from the final blend mask?
