# Blur + Feather Split — Plan and Inspector Decision

## Review status

**Approved specification; implementation in progress.** The shared owner-typed
Gaussian Blur, separate Height Eikonal Feather, retention of existing Flow Feather,
and full-width segmented UI are settled. See the checkpoint below for actual progress.

Implement **Blur consolidation first**, then **Height Eikonal**. Numerical solver
choice, solver work bounds and Width/Speed units need engineering definition; these
do not reopen the architecture. A separate scalar Availability socket is **not
approved**. Future vector Eikonal/Flow reconstruction is deferred: **preserve
existing Flow Feather functionality, including its current Flow Blur mode.**

## Implementation checkpoint — shared Gaussian rewrite

- Shared GPU service rewritten to match Radius X/Y, wrapped unit-texel taps
  and full three-sigma support; removed the unapproved directional contract.
- Scalar/float4/encoded-normal/Flow variants carry full-precision sums and mass
  through both axes, then apply Amount/mask and semantic resolve once.
- Flow consumes separate validity, not packed Z; validity remains caller-owned.
- Migrated internal border Height, Height-mask and layer-input Height smoothing.
  Signed border Height is no longer clamped; coverage clamps at final resolve.
- Canonical authored `FMixtormatBlur` now replaces the mask-only payload, with
  Amount, parameter contracts/metadata, inspector and scoped copy/paste/instances.
- Mask Blurs run independently in authored order on completed coverage before
  merge. Curvature-owned Blurs post-filter each completed Curvature result.
  The summed radii path and old Mask Blur shader/class are removed.
- Latest controls supersede the earlier inspector sketch: master Radius × X/Y
  Scale (defaults 1), separate Amount, and seven Layer channel weights:
  Base Color, Normal, Roughness, AO, Metallic, F0 and Height. Automatic X/Y linking
  is removed; masks still gate the finished result. Radius-mask mode is not implemented.
- Display: Layer header → aligned Blur/mask subtree → modules. The layer disclosure
  collapses all of them. Cross-layer dragging reuses existing handlers; root Blur
  stays layer-owned. Disabled layers dim these rows without changing their enable flags.
- Root Blur accepts scoped masks. New Mask → Mask nesting is blocked; existing
  scopes are preserved. Copy/duplicate/delete carry the subtree; masked instances
  do not inherit it yet. User explicitly deferred that instancing work.
- Layer GPU integration remains unfinished: prepared-surface effect weighting,
  structural masking and Pass Through normals need resolution. Broader typed owners,
  FlowWarp retirement and Height Eikonal remain pending. Flow Feather is unchanged.
- Separately fixed ID producer demand for nested Relief/Ramp/UV consumers.
- User confirmed earlier Gaussian, Mask and initial footer checkpoints compiled.
  Latest changes have no compile confirmation; agents ran no builds/tests.

## Goal

- **Blur:** one authored module and canonical shared typed Gaussian GPU implementation,
  with target determined by the owner and no Scope/target dropdown.
- **Height Feather:** separate Eikonal-only behavior; no Height Gaussian Feather mode.
- **Flow Feather:** keep the current vector behavior, shader, controls and active
  readers unchanged until a separately approved vector contract exists.
- Modules filter at their authored chain position; nested Blur post-filters the
  owner module's completed typed output.
- Exclusive inspector choices are full-width direct `SMixtormatSegmentedControl`.
- Remove genuinely superseded data, GPU stages, menus, bindings and docs together;
  never delete dependencies of preserved Flow Feather or Flow → Warp.

## Current audit

| Concern | Current owner | Problem |
|---|---|---|
| Mask blur | `FMixtormatMaskBlur` / child `Blur` | Author-facing node, but limited to Masks. |
| Layer blur | procedural `LayerBlur` Effect | Separate data, inspector, shader, and deferred scheduling. |
| Feather Blur | `FMixtormatBehaviorFeatherSettings::Mode` | Incorrectly makes Feather a Gaussian filter. |
| Height Feather | `MixtormatBehaviorFeather.usf` | Existing Eikonal envelope uses an artist iteration budget. |
| Flow Feather | `MixtormatBehaviorFlowFeather.usf` | Existing Flow behavior is intentionally preserved until vector Eikonal is specified. |
| Internal height smoothing | `MixtormatGpuMaskPasses.cpp` | Reuses the mask-only blur shader for signed Height. |

Pre-migration code has four Gaussian implementations/paths; the existing Flow Feather path remains a temporary, explicitly preserved exception:

1. `Shaders/Private/MixtormatMaskBlur.usf`
2. `Shaders/Private/MixtormatLayerBlur.usf`
3. Height Gaussian stages in `Shaders/Private/MixtormatBehaviorFeather.usf`
4. Flow Gaussian stage in `Shaders/Private/MixtormatBehaviorFlowFeather.usf`

## Proposed architecture

### 1. One GPU Gaussian owner

Create one private shared blur dispatch API in `MixtormatShaders`.

- One canonical shader family for authored Blur, with scalar, float4, normal, and valid-Flow typed variants. The separately preserved Flow Feather Gaussian is a temporary exception.
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
A Mask-owned Blur independently filters coverage before merge. Multiple Mask-owned
Blur modules apply independently in **authored order**, not by summing radii.
Remove the flattened summed-mask-Blur path. Curvature-owned Blur applies only
**after Curvature's completed output**, with no free-form Blur/Curvature sibling ordering.

### 3. Height Feather is Eikonal-only; Flow Feather is preserved

- **Operation:** full-width `High → Low` (raises lows), `Low → High`
  (lowers highs), `Symmetric` (both Height envelopes).
- **Side:** retain `Both / Inside / Outside` as an independent *final spatial gate*
  wherever a signed boundary exists. Side does not select propagation direction.
- **Direction Bias:** preserve the existing horizontal/fixed-axis propagation-cost
  bias and all active bindings; do not remove it.
- **Flow Influence:** optional signed Height-Feather control. Positive favors
  propagation with valid Flow; negative against; zero adds no Flow contribution.
  Missing, zero or invalid Flow adds no directional contribution. Existing
  Direction Bias still acts when Flow Influence is zero.
- **Strength and scoped mask:** blend/gate the *finished Height result*, not
  the travel-cost solve.
- **Width and Speed:** Width shapes Height slope enforcement footprint, Speed
  sets base propagation rate. Specify their numerical units and mapping
  before finalizing the solver.
- **Iterations:** no Height artist-facing Iterations control or Height
  Gaussian/Eikonal mode selector in the completed design. Preserve any shared
  Mode/Iterations fields still actively used by Flow Feather; split the
  Height/Flow data paths as required rather than removing Flow functionality.
- **Availability:** a distinct scalar speed-field socket is **not approved**.
  Do not introduce it for this migration or repurpose the final blend mask.

#### Approved asymmetric directional travel cost

For step length `ds` and travel unit direction `d`:

```text
combinedBias = bound(DirectionBias_fixed_axis + FlowInfluence * normalizedValidFlow)
stepCost     = ds * (1 - dot(combinedBias, d)) / F_base(x)
```

This is a positive, asymmetric Randers-type metric provided the **magnitude
of the combined bias vector is strictly below 1**, with a safety margin.
Bound the *sum*, not just each independent control. `FlowInfluence = 0`
does **not** imply isotropic propagation while fixed-axis Direction Bias
is nonzero. Direction alters the propagation cost, not an extra final Height
offset; the Height envelope still changes as a consequence of the solve.

`F_base = 0` means **blocked travel**; do not divide by epsilon or jump
through barriers. Invalid/zero Flow gives zero vector bias, not a barrier
unless base speed is independently zero. Respect tile wrapping.

Asymmetric distance is **direction dependent**. Define seed sets and
source-to-destination propagation orientation independently for High → Low
and Low → High. Do not reverse directed path distances silently; Symmetric
combines the correctly oriented Height envelopes.

#### Solver requirements

An isotropic JFA or conventional isotropic fast-marching solver is inadequate
for this directed variable-cost problem. Use **Ordered Upwind** or a suitable
anisotropic marching/upwind solver with a GPU strategy. Directed-grid Dijkstra
is an acceptable explicitly chosen *graph approximation*, exact on that graph
but not the continuous metric (stencil-direction error). Define barrier-safe
travel, accuracy/convergence, deterministic execution, GPU work bounds,
wrap handling and incomplete-solve behavior. Do not hide a fixed iteration
cap or silently accept partial propagation.

#### Preserve current Flow Feather

Leave the present Flow Feather `.usf` stages, active Mode/Blur path, UI,
gather/dispatch, bindings and publication functional. Future vector Eikonal
may reuse scalar arrival-time computation, but vector transport,
direction/magnitude, cancellation and validity require their own contract.
Do not call the existing Flow vector Gaussian an Eikonal solution.

Reference: Houdini's [Feather COP](https://www.sidefx.com/docs/houdini/nodes/cop/feather.html)
enforces slopes; [Eikonal COP](https://www.sidefx.com/docs/houdini/nodes/cop/eikonal.html)
supports speed fields and zero-speed barriers. Neither approves this
project's separate Availability socket.

## Approved inspector layout and control semantics

Exclusive grouped selections use a direct, full-width
`SMixtormatSegmentedControl`, as in Layer Composition and Mask Source.
**Never** wrap these groups in `MixtormatRow::MakeTrailing`. Apply this to
Feather, Boolean, Warp, Deform, Field Combine and equivalent future groups.
Use existing style/card/slider tokens.

### Blur

```text
BLUR [enable]
  FOOTPRINT    Radius X | Radius Y
  AMOUNT       Amount
  CHANNELS     [ Appearance ] [ Height ]    // root Layer only
```

- **Layer:** filters its own finished layer surface BC/Normal/RAM/Height at
  the layer-rail end, **not** the accumulated composite. Appearance groups
  BC/Normal/RAM.
- **Layer presentation:** root Blur appears below the layer card, aligned with
  it, without child indentation or hierarchy connectors. Footer filters remain
  visible while the child tree is collapsed; multiple Blurs keep authored order.
  They retain canonical child identity, enable, Inspector, copy/instance and removal.
  Footer drag/reparent-to-module interaction is not implemented in this UI slice.
  The separate Layer GPU effect-weighting issue remains unfinished.
- **Generator:** filters only current signed Height chain.
- **Mask:** filters coverage; **Curvature:** finished Curvature result;
  **Flow:** valid-weighted Flow.
- Owner or typed connection determines target; no Scope, Coverage/Composite
  or target dropdown. Typed source connections may use a source chip.
- Radius X/Y remain individually bindable. Both filter axes complete
  **before** Amount/masking, channel clamps or normal renormalization.
  Signed Height stays unclamped; coverage masks clamp after filtering.

### Height Feather

```text
FEATHER [enable]
  OPERATION        [ High → Low | Low → High | Symmetric ]
  SIDE             [ Both | Inside | Outside ]
  WIDTH            Width
  RESPONSE         Strength | Speed
  DIRECTION        Direction Bias
  FLOW STEERING    Flow Influence (signed; 0 = off)
  FLOW INPUT       optional valid Flow connection
```

- Operation and Side are independent; final mask gating is unchanged.
- No Height Blur/Eikonal mode or artist Iterations; no new Availability socket.
- Preserve `BehaviorFeatherSettings` parameter-owner identity and bindings.
- Existing Flow Feather UI and its current controls **remain accessible**.
- No additional solver/geometry mode is approved.

## Required deletion/update set

| Remove or replace | Required files |
|---|---|
| Height Feather Gaussian mode and Height artist `Iterations`; preserve DirectionBias and any active Flow Feather data/enum readers | `Source/MixtormatRuntime/Public/MixtormatBehaviorTypes.h` (split Height/Flow settings where necessary; do **not** globally remove active Flow mode/iterations) |
| Update Feather member metadata/bindings; retain `BehaviorFeatherSettings` parameter-owner identity and active mappings | `Config/MixtormatParameterAuthoring.json`; `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`; `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`; `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp` |
| Height Feather inspector: Operation, Side, Direction Bias, signed Flow Influence; preserve existing Flow Feather inspector/modes | `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` |
| Height Gaussian stages replaced by Height Eikonal directed-cost solver; existing Flow Gaussian must remain working | `Shaders/Private/MixtormatBehaviorFeather.usf`; `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`. **PRESERVE** `Shaders/Private/MixtormatBehaviorFlowFeather.usf` and current Flow dispatch/contract |
| Height-only iteration gather/render fields/helpers; Flow-specific readers/data retained | `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`; `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` |
| Mask-only Blur payload/UI/placement | `Source/MixtormatRuntime/Public/MixtormatMaskBlur.h`; `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`; `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`; `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`; `MixtormatLayerActions.cpp`; `MixtormatLayerMenus.cpp` |
| Procedural LayerBlur type, fields, UI, gather, queue, dispatch | `Source/MixtormatRuntime/Public/MixtormatEffect.h`; `Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp`; `Source/MixtormatShaders/Private/Effects/MixtormatLayerBlurPasses.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`; `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`; `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp` |
| Procedural Flow Warp Effect; use Flow → Warp instead | `Source/MixtormatRuntime/Public/MixtormatEffect.h`; `Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.*`; `Source/MixtormatShaders/Private/Effects/MixtormatFlowWarpPasses.cpp`; `Shaders/Private/MixtormatFlowWarp.usf`; `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`; `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp`; `MixtormatLayerMenus.cpp`; `MixtormatLayerChildren.cpp`; layer badges/capabilities and directly related docs |
| Superseded authored Gaussian classes/shaders; preserve temporary Flow Feather Gaussian exception | `Shaders/Private/MixtormatMaskBlur.usf`; `Shaders/Private/MixtormatLayerBlur.usf`; `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`; `Source/MixtormatShaders/Private/Effects/MixtormatLayerBlurPasses.cpp` |
| Summed Mask Blur radii → independent owner-ordered Mask Blur operations; nested Curvature Blur runs after completed Curvature output, no mixed sibling ordering | `Source/MixtormatShaders/Private/Compositing/MixtormatMaskGather.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`; `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp` |
| Internal smoothing callers | `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`; `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp` |
| Current architecture description | `AgentDocs/COMPOSITION.md`; `AgentDocs/SHADERS.md`; `AgentDocs/UI.md` |

Do not retain forwarding wrappers, fallback shaders, legacy inspector branches,
deprecated properties or parallel **authored Blur** paths. Update every replaced reader;
**preserve existing Flow Feather**, which remains a deliberate temporary exception.
This is unshipped development with no need for historical compatibility scaffolding.

## Implementation order

1. Create canonical `FMixtormatBlur`, define eligible typed owners, owner-relative
   scheduling, copy/paste/instancing and source connection semantics.
2. Reuse/extract the working **Height Feather Gaussian** for the shared Blur
   shader family. Migrate Mask, Layer, Generator Height, post-Curvature,
   validity-aware Flow, and internal smoothing; apply Amount/mask/renormalization
   only after both Gaussian axes.
3. Remove procedural LayerBlur and FlowWarp Effects completely; retain
   Flow → Warp. Replace summed Mask radii with authored-order independent
   Mask Blur modules. Update all affected menus, dispatches and readers.
4. Implement **Height** Eikonal with Operation, Side, fixed-axis Direction
   Bias and optional signed Flow Influence under the approved directed-cost
   contract. Specify solver accuracy/work, Height envelope orientation,
   Speed/Width units, wrap and hard barriers during implementation.
5. Remove Height Feather Gaussian mode and Height artist Iterations with
   readers; **preserve the existing Flow Feather mode, shader, controls and
   functionality**, splitting shared data as needed.
6. Update capabilities, clipboard/instancing, binding metadata, cache
   dependencies, inspector and related docs alongside each implementation.
7. Do not run builds, shader compilation or tests without explicit consent
   under `AGENTS.md`.

## Confirmed decisions

- Universal, owner-typed Gaussian Blur under Layer (BC/Normal/RAM/Height),
  Mask (coverage), Generator (current Height only), Curvature (completed
  output), Flow (validity-aware), or another eligible typed owner.
- Blur as a normal child supports copy, paste and instance; no Scope,
  Coverage/Composite or target dropdown. Layer applies at end of its own
  layer rail, not to the accumulated composite.
- Independently authored Mask Blurs execute in order; Blur under Curvature
  post-filters Curvature. No interleaved Blur/Curvature sibling ordering.
- Remove procedural LayerBlur and FlowWarp Effects; preserve Flow → Warp.
- Height Eikonal Operation = High → Low / Low → High / Symmetric.
  Side = Both / Inside / Outside is a separate final gate.
- Preserve fixed-axis Direction Bias; add signed Flow steering via **combined,
  bounded, asymmetric travel costs**, not final Height addition.
- `FlowInfluence = 0` disables only Flow steering; zero base speed is a true
  barrier; invalid Flow contributes no directional bias.
- Asymmetric directed distances require correctly oriented propagation
  for each Height envelope and Symmetric composition.
- Preserve existing **Flow Feather** behavior, Gaussian path, modes and
  active readers. Vector Eikonal is deferred.
- Preserve `BehaviorFeatherSettings` parameter-owner identity.
- Full-width grouped segmented controls for Feather, Boolean, Warp,
  Deform and Field Combine. No new scalar Availability socket approved.

## Remaining engineering definitions (not blockers to Blur)

1. Select a suitable GPU anisotropic Eikonal solver and document accuracy,
   termination/work bounds, barrier and wrapping rules. Ordered Upwind or
   anisotropic marching are options; directed Dijkstra is only graph-exact.
2. Define Width/Speed units and seed-to-destination orientation for both
   Height envelopes; do not hide a silent fixed iteration budget.
3. Choose how the optional valid Flow input is sourced for Height steering.
   This is **not** an implementation of vector Flow Feather.
4. Do not infer approval for Availability, extra inspector modes or new
   vector-Eikonal semantics.

**Begin with Gaussian Blur consolidation, then Height Eikonal.**
The checkpoint above records implemented work; the remaining specification is not
an implementation or runtime-validation claim.

