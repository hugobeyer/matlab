# Strata structural warp — design handoff for Sol

Status: steps 5–6 implementation present, 2026-10-07; targeted source review only.
This document records step 5; unrelated seam fixes, UI redesign and step-7 targets stay out of scope.
Read [step 6 — output alignment](generator_warp_output_alignment_design.md) alongside it.
Only the user's earlier step-2 compile is confirmed. The gather missing-header issue is fixed,
but the newest build is unconfirmed. Broken StructuralWarp tests were removed at user request;
no agent compile, build, runtime, visual or test results are claimed.

## 1. Decision

Implemented: independently ordered, append-only Runtime **Structural Warp** child alongside
Height Push, with its own enable flag and `FMixtormatGeneratorStructuralWarp` payload.
`Source` uses `FMixtormatOutputReference`: Flow or UVMap, not Vector2. Sources must already
be completed; the explicit target is a later, enabled same-layer Strata generator.

Evaluate geology at warped coordinates; do not resample finished Strata height, IDs or
bed-position/random outputs for this operation. Existing post-generation flow children
remain a different, unchanged operation.

The existing generator-level `WarpSource`/`Inputs.WarpUV` preparation is not an ordered
module. Do not activate it as an additional implicit warp or mix it with layer-wide
`ReferencedUV`. Preserve saved socket data; only explicit structural modules act here.

## 2. Coordinate contract — lock this before coding

- `x`: destination/output UV, before generator placement.
- `W(x) = x + D(x)`: destination-to-structural-source coordinate map.
- `D`: periodic, **unwrapped displacement**, in destination tile units.
- `B(x)`: signed bedding-coordinate shift accumulated by Height Push.
- `A`: derivative of existing generator placement, including scale, flip and rotation.
- `q = MixtormatGeneratorUV(W(x))`: the generator's structural evaluation coordinate.

Apply placement once. Do **not** also pass displacement through
`MixtormatGeneratorSourceVector` when using `MixtormatGeneratorUV(W(x))`.
Do not apply `frac` to W or shorten D with `round`; multi-tile travel is meaningful.
Texture addressing wraps, while the stored map retains its winding.

Accepted warp UV maps must be lifts with `W(x + k) = W(x) + k` for integer tile shifts.
Current flow-tool UV outputs are built as destination coordinates plus displacement.
A generic wrapped UV image, a differently winding map, or an arbitrary Vector2 field does
not satisfy this contract just because it fits in two channels. Do not silently reinterpret
one; future external producers need explicit semantics/validation.

## 3. Ordered structural state: (D, B), per target

Initial state is identity coordinates and zero bedding shift. Store state by target child
index, not in a layer-wide slot. Use RDG ping-pong outputs; never read/write one texture.
Implemented state maps are `GeneratorStructuralDisplacements` (RG32F displacement) and
existing `GeneratorHeightPushFields` (R32F bedding shift), keyed by target child index.
`MixtormatGeneratorStructuralWarp.usf::MainCS` writes fresh D/B resources per operation.
Missing initial resources represent identity/zero, not fallback to another source.

### Height Push

At that module's position, resolve completed signed height H and its scoped mask M:

    D_new(x) = D_old(x)
    B_new(x) = B_old(x) + Amount * M(x) * H(x)

No 0.5 subtraction, normalization or clamping of H. This preserves step 4's push behavior.
Source and mask are sampled in destination space at the moment this module is applied.

### Structural Warp

Resolve the module's pullback map `psi(x) = x + d(x)`:
- Flow: reuse the existing stage-8 trace and its reference amount/length/steps.
- UVMap: use its coordinate map directly, interpreted under the lift contract above.
- Apply a scoped mask to the displacement: `psi(x) = x + M(x) * d(x)`.
  This specifies displacement gating, not changing integration velocity at every trace step.
  Do not multiply FlowAmount twice. Do not add a second generic strength control initially.

Compose the entire accumulated structural state:

    D_new(x) = (psi(x) - x) + sample_periodic(D_old, psi(x))
    B_new(x) = sample_periodic(B_old, psi(x))

Consequences:
- Push then Warp moves the already-pushed bedding, including the earlier push mask's imprint.
- Warp then Push adds new destination-anchored height influence after that warp.
- Warp 1 then Warp 2 gives `W_final = W_1 o psi_2`, not summed displacements.
- Masks on later modules gate those modules at their own destination positions.

These passes resample structural state, not finished geological outputs. Raster composition
is still an approximation and may smooth B/D across repeated operations; do not promise
analytic exactness or add a second execution backend in this step.

## 4. Jacobian convention and sampling

Use column vectors for coordinates. Define:

    J = dW/dx = [[dWu/dx, dWu/dy],
                [dWv/dx, dWv/dy]]

Thus X and Y are its columns, and the existing HLSL constructor is:

    float2x2(X.x, Y.x, X.y, Y.y)

For row-vector gradients, destination gradient is `mul(g_source, J)`.
Do not transpose J a second time. Do not use inverse J for a scalar gradient.

### Periodic sampling

For an input coordinate texture, recover displacement at each wrapped texel as
`stored_W(pixel) - texel_center_UV(pixel)`, then bilinearly interpolate those displacements.
Reconstruct a coordinate as `query_UV + sampled_D`. This is the pattern in
`MixtormatComposite.usf::ReferencedUVDisplacement/ReferencedCoordinate`.
Never bilinearly sample absolute coordinate values across the unit-tile seam.

For the final displacement raster at a destination texel, use centered wrapped differences:

    hx = 1 / width; hy = 1 / height
    X = (1,0) + (D(x + (hx,0)) - D(x - (hx,0))) / (2*hx)
    Y = (0,1) + (D(x + (0,hy)) - D(x - (0,hy))) / (2*hy)

This is a finite-resolution derivative estimate, not an analytic flow derivative. Width
and height must be independent. No shortest-torus subtraction is needed for periodic D.

Differentiate the final state actually consumed; do not also apply a separately accumulated
Jacobian. For reasoning/tests, the analytic composition would be:

    J_new(x) = J_old(psi(x)) * J_psi(x)
    grad(B_new)(x) = grad(B_old)(psi(x)) * J_psi(x)

Mask gradients matter: for `psi = x + M*d`,
`J_psi = I + M*J_d + outer(d, grad(M))`. Differencing the final masked displacement includes
this term approximately. Scaling only the unmasked Jacobian is incorrect.

## 5. Strata evaluation and gradients

Before folds and interface evaluation:

    q = MixtormatGeneratorUV(W(x))
    Strike = dot(q, StrikeVector)
    S = dot(q, BedWave) + B(x)
    gStrike = mul(MixtormatGeneratorDestinationGradient(StrikeVector), J)
    gS = mul(MixtormatGeneratorDestinationGradient(BedWave), J) + grad(B)(x)

B has already been composed by the ordered modules. Its final destination-space gradient
must **not** receive J again. This is the main double-transform risk after Height Push.

From there retain the current analytic chain for fold shears, interfaces, TGradient,
lamination PhaseGradient and UGradient. They inherit the warp through gStrike/gS.
Use the same q/Strike/S/Bed everywhere, including joint-cell positions and random keys.

### Legacy Height Follow and destination influences

Keep existing Height Follow unchanged: sample raw composite-below height at x, after folds,
including its current `(Source - 0.5)` offset. Its gradient is already destination-space;
add it directly, without J. It is an external destination-anchored driver, not part of B.
This deliberately preserves existing semantics; making it travel with structural warps
would be a separate, explicit behavior decision.

Keep final Strata mask and external Region-ID influence sampled at x. Do not warp external
Region IDs or apply J to their discrete values. Their current GPU input flags are still
hardcoded off; restoring those bindings is separate work, not something warp fixes.

## 6. Filtering, distances, and singular maps

- Keep the existing `StrataFootprint(g) = |gx|/width + |gy|/height` convention.
- Propagate the corrected gradients into all existing footprints; do not retune geology.
- The current joint `max(component footprints)` still underfilters oblique planes.
  Do not call that corrected merely because J is now present; retain it as a separate defect.
- A finite singular J still supports forward coordinate evaluation and gradient pullback.
  No inverse is needed. Do not replace a collapsed/folded warp with identity silently.
- Track non-finite maps, near-collapse and foldovers for validation. Test det(J) separately
  from placement: a negative placement determinant caused by an authored flip is legitimate.
  det(J) <= 0 diagnoses the identity-winding warp itself, not the placement.

### Boundary field for an active structural warp

The active structural path now writes an internal RG32F distance+validity boundary directly
from interface functions, rather than wrapped/half-precision BedPosition:

    FL = S - Lower.x;   gL = gS - Lower.y * gStrike
    FU = Upper.x - S;   gU = Upper.y * gStrike - gS
    d = -min(FL / length(gL), FU / length(gU))

Use finite, nondegenerate gradients and valid bracketing interfaces; otherwise mark the
boundary invalid. This is a local, negative-inside per-bed interface-distance estimate in
destination UV units, **not a global exact SDF**. Do not apply another warp metric correction
at publication: destination gradients already supply it.

Preserve the existing stage-8 boundary construction on the inactive structural-warp path,
so merely adding this feature does not change existing flow-tool seeding. For active warp,
bind the direct boundary to the bundle. Legacy post-generation flow may subsequently remap
it once as usual. This is an existing-behavior branch, not a substitute for invalid warps.

## 7. Output and identity guarantees

Regenerate Height, BedIds, BedPosition and BedRandom together in Strata's resolve pass.
IDs remain integers; random remains keyed by Bed; bed position remains its existing 0..1
coordinate. No interpolation of these finished fields in the structural warp path.
The later signed normalization/Height Scale, publication and layer combine remain unchanged.

Disabled/unconnected/known-neutral modules schedule no structural modification. Preserve the
original shader arithmetic path when no structural warp is active. An arbitrary UV texture
that happens to be identity can be compared within numerical tolerance; do not claim CPU
bit-identity detection of its contents. Do not change the existing generator socket behavior
or reinterpret missing connections as the previous available field.

Step 6 now types existing post-generation Shape Deform/Generator Flow companion remapping:
ID-anchored random, owner/phase-aware bed T and validity-aware distances. It remains raster
resampling, not exact geological reevaluation; structural outputs are not remapped twice.

## 8. Implemented sequence and file ownership

1. Runtime child/payload and parameter owner registrations are append-only, with own enable
   and explicit source-before-module/later-enabled-same-layer-Strata target validation.
2. `GatherGeneratorHeightModuleChild` fills `FGeneratorStructuralWarpRenderData.Source`
   (identity, kind, FlowAmount/FlowTraceLength/FlowSteps) and `TargetChildIndex`.
   `EnqueueCompose` registers published-field demand before prefix reuse.
3. The stage-8 reference Flow trace helper applies amount once; the new compose pass masks
   displacement after tracing, composes periodic D and transports existing B into fresh outputs.
   Later Height Push rows add to the resulting B independently.
4. Strata consumes final per-target D/B, evaluates placement once, chains `g*A*J`, and adds
   final destination B gradients without another J. Active warp binds the direct boundary;
   inactive warp retains existing stage-8 construction.
5. Source/target/reference-flow controls and enable/menu integration are present. Step 7 now
   routes non-Strata targets through the typed completed-bundle pullback documented in the
   output-alignment design; this Strata path remains regeneration-only. No geological fixes
   are included.
6. Evidence here is source review only. The cases below remain acceptance criteria, not
   executed results; build/runtime validation requires explicit approval.

Primary files (plugin-root-relative):
- `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`
- `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp`
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`
- `Shaders/Private/MixtormatStrataCarver.usf`
- `Shaders/Private/MixtormatGeneratorHeightPush.usf`
- `Shaders/Private/MixtormatGeneratorStructuralWarp.usf`
- `Shaders/Private/MixtormatGeneratorWarp.ush`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`

Reuse as references: `MixtormatGeneratorPlacement.ush` (placement gradients),
`MixtormatComposite.usf` (periodic coordinate sampling/matrix convention),
`MixtormatGeneratorFlow.usf` stage 8 (trace), `MixtormatGeneratorBundle.usf` (existing
boundary semantics). No need to rewrite the composite or bundle-remap implementation.

## 9. Acceptance examples (not executed)

- Identity/no module/disabled/zero flow amount: original result and original boundary path.
- Constant displacement: J = I; geological position moves, footprint does not scale.
- Periodic shear `W=(x+a*sin(2*pi*y), y)`:
  `J=[[1,2*pi*a*cos(2*pi*y)],[0,1]]`; catches transposition and axis errors.
- Same shear plus flipped/rotated/nonuniform integer placement: gradient is g*A*J once.
- Push then Warp: B=Amount*H(psi(x)); Warp then Push: B=Amount*H(x). Visibly different
  with a varying H. Check source masks move only when a later warp composes their imprint.
- Two noncommuting warps: correct composition order, not displacement addition.
- Large multi-tile displacement: no half-tile clipping; tile-edge derivative has no spike.
- Non-square output: independently scaled x/y finite differences.
- Scoped mask transition: gradients include mask falloff, not just interior deformation.
- Compression/foldover: finite forward evaluation, marked invalid distance where needed,
  no hidden clamp or identity substitution.
- All four Strata outputs align; signed relief is retained; source changes invalidate cached
  consumers. Compare complete outputs, not just a beauty preview.

Known carried-over lamina, fold-hinge, PixelT > 1 seam and joint-prominence issues remain.
Do not claim this design fixes them, or that a heightfield becomes volumetric geology.
