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
- Root Layer placement: replaces procedural `LayerBlur`; filters the current composited surface.
- Generator placement: filters the current signed Height in generator-chain order.
- Flow placement: filters the current Flow in flow-chain order and preserves validity.
- The source is inferred from the legal owner or a typed connection; there is no target dropdown.

A Blur module with a root Layer owner keeps the existing **Layer Coverage / Whole Composite**
meaning: both read the accumulated composite; Layer Coverage adds a coverage gate. Neither
isolates the layer's contribution. Preserve the current post-composite stage during consolidation;
a broader effect-order change requires a separate scheduling design.
A Blur owned by another module affects that owner’s output before its consumer continues.

### 2a. Ordered mask filters — separate implementation step

`MixtormatMaskGather.cpp` currently sums all scoped Blur radii and collects Curvature separately.
`AddMaskFilterPasses` then runs one combined Blur before every Curvature filter, regardless of
interleaving in the authored stack. Summed radii are not equivalent to sequential Gaussian blurs.

Replace `FMaskRenderData::BlurRadiusX`, `BlurRadiusY`, and `CurvatureFilters` with one ordered
filter-operation list. Gather in authored order and pass each result to the next operation.
Retain each child's identity, enabled state, resolved bindings, and diagnostic ownership.
Update gather, render data, and execution together; do not keep the flattened path as a fallback.
This intentionally changes reordered/interleaved filter results and must be approved as such.

### 3. Feather is Eikonal-only

Replace `FMixtormatBehaviorFeatherSettings` with one Eikonal contract:

- Width controls slope distance / rate enforcement footprint.
- Strength and scoped mask blend the finished Feather result back into the source.
- **Side** chooses Both, Inside, or Outside as a final spatial gate where signed boundaries exist.
  It is not propagation direction and is not equivalent to raising lows or lowering highs.
- **Slope operation** needs approval: High to Low raises surrounding lows; Low to High lowers
  surrounding highs. Existing Height Feather averages both envelopes; retaining that as a
  symmetric operation is a separate choice, not the meaning of Side = Both.
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
  SCOPE       [ Layer Coverage | Whole Composite ]  // root Layer only
  FOOTPRINT   Radius X | Radius Y
  AMOUNT      Amount
  CHANNELS    [ Appearance ] [ Height ]   // root Layer only; Appearance is BC/Normal/RAM
```

- Mask, Generator, and Flow owners omit `Scope` and `Channels` because their target is inherent.
- Generator owner: Height; Flow owner: Flow; Mask owner: Coverage.
- A Blur with a typed field connection displays a source chip/connection row, never a dropdown.
- `Radius X` and `Radius Y` remain separately bindable and retain existing anisotropic behavior.

### Feather

```
FEATHER [enable]
  SIDE        [ Both | Inside | Outside ]
  OPERATION   [ High to Low | Low to High | Symmetric ]  // proposed; approval required
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

## Decisions requested

1. Approve `Scope [Layer Coverage | Whole Composite]`, preserving post-composite semantics?
2. For ambiguous Generator outputs, prefer owner inference or an explicit typed input connection?
3. Approve replacing summed Mask blurs with authored-order Blur/Curvature operations?
4. Approve separate Feather Side and High-to-Low / Low-to-High / Symmetric operation buttons?
5. Retain Direction Bias, or approve a specifically defined replacement?
6. Finalize Flow Feather semantics before implementation; Height-only v1 is not assumed.
7. Approve `Availability` as a separate scalar connection, distinct from the final blend mask?
