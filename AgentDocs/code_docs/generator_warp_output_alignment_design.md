# Generator warp output alignment — step 6 design for Sol

Status: steps 5–6 implementation present, 2026-10-07; targeted source review only.
Read [step 5](strata_structural_warp_design.md) first. Its coordinate, matrix and ordered
Height Push/Warp contracts apply here. Other structural targets and Noise remain gated
for step 7. Only the user's earlier step-2 compile is confirmed; the gather missing-header
issue is fixed, but the newest build is unconfirmed. Broken StructuralWarp tests were removed
at user request; no agent compile, build, runtime, visual or test results are claimed.

## 1. Scope and invariants

Step 5 generates Strata's geometry and outputs in the final structural frame. Step 6
implements typed handling when existing post-generation Shape Deform/Generator Flow
resamples a completed bundle, and the output contract future targets must satisfy.

- One coordinate map and one immutable source revision per operation.
- Preserve signed height, IDs, invalid-ID sentinels, ID hashing semantics, UV winding,
  bed-position meaning, per-region random values, coverage and mask/ID influence.
- No generic bilinear treatment merely because a field is stored as a float texture.
- Do not warp Strata's newly generated outputs again for the same structural operation.
- Flow Carve modifies relief/support, not coordinates. Do not remap its IDs/bed labels.
- Preserve existing layer source sampling and legacy external-reference behavior.
- No changes to enum values, published names or storage formats without an explicit need.

## 2. Current source evidence

`FGeneratorBundle` in `MixtormatGpuCompositorInternal.h` contains Height, Coverage, RegionIds,
CentreUV, Orientation, NamedMasks, BoundaryField and bHashedIds. Producer-owned
`RegisterNamedMask` now attaches `NamedMaskDescriptors` (semantic, units, invalid distance),
without a second texture registry. `FPublishedField::IsComplete` checks formats/payload presence,
not vector meaning, coordinate frame, per-pixel validity or source revision.

`MixtormatGpuGeneratorPasses.cpp::RemapGeneratorBundle` currently remaps IDs, centre/orientation,
named masks and boundaries. It does not remap Height/Coverage. The flow apply updates those
separately, and repoints PebbleCoverage to the moved coverage. This is confirmed code behavior.

`MixtormatGeneratorBundle.usf` now uses:
- Stage 0 for declared continuous attributes; stage 10 copies random at the ID anchor.
- Stage 1 with a shared finite, safely bounded wrapped anchor; stage 11 uses owner/phase-aware T.
- Stages 2/7 with validity-aware distance stencils and `length(g_source)/length(g_source*J)`.
  Named UV distances use their own gradient; old BoundaryField supplies validity only.
- Stages 5/6 retaining legacy local inverse centre/orientation approximations, not exact inverses.
- Stage 8 for inactive structural Strata; active structural Strata supplies direct RG32F
  negative-inside destination distance + validity, with no second publication correction.

`RemapGeneratorBundle` captures an immutable source snapshot, builds moved companions from
old IDs/boundaries and commits together. Apply owns Height/Coverage; `PebbleCoverage` aliases
moved Coverage. `CrackDistance` remains a positive crack-cell attribute, not a UV SDF.

Noise publishes Value/Gradient separately in `MixtormatGpuNoisePasses.cpp`. They are not
members of its bundle and would be missed by a bundle-only generic warp.

## 3. Internal output descriptors, not a second reference system

Implemented: small shader-side descriptors on producer-owned outputs via `RegisterNamedMask`.
Reuse existing published addresses and `EMixtormatPublishedFieldKind`; these are not new
Runtime sockets, public field kinds or a parallel registry.

The producer must state, where relevant:
- Sampling semantic: continuous scalar, discrete per-region scalar, bed coordinate,
  IDs, lifted coordinate map, centre, orientation, covector, vector or distance.
- Coordinate frame/units; associated ID map; validity source.
- Scalar's relation to its geometry: transported material attribute versus derived quantity.

`NamedMaskDescriptors` is the producer-registered policy table alongside existing NamedMasks;
it contains policies only, not another mutable name->texture registry. The remap loop selects
stages by declared semantic/units and rejects unsupported combinations before companion writes.

Keep `NamedMasks`' published names and legacy consumers intact. Audit each current named
output while attaching descriptors. Unknown semantic combinations are unsupported; never
quietly use the scalar or Vector2 shader as a default for new producers.

No new user parameters are needed for these correctness rules. Future controls must still
trace Runtime/defaults -> gather -> GPU declaration/binding -> shader -> inspector metadata.

## 4. Sampling/transform table

Let y=W(x), J=dW/dx, using step 5's destination-to-source convention.

| Output | Operation | Important restriction |
|---|---|---|
| Signed Height | H(W(x)), continuous scalar | No saturate, 0.5 shift or implicit renormalization |
| Continuous masks/coverage | f(W(x)) | Respect producer range; coverage never gates shared height combine |
| RegionIds | One wrapped nearest integer load | Preserve invalid sentinel and bHashedIds |
| Per-region random | Load at the same anchor as IDs | Do not interpolate or regenerate with another hash |
| BedPosition | Owner-and-branch-aware interpolation (§5) | No average through the 1->0 reset |
| Lifted UV map U | U composed with W | Interpolate displacement, not wrapped coordinates |
| Spatial centre | Solve/approximate W inverse at centre | Not an ordinary UV texture sample (§7) |
| Orientation/tangent | Inverse-Jacobian direction transform | Not a scalar gradient transform |
| Scalar gradient | g(W(x))*J | Must first be expressed in the source-map UV frame |
| Generic Vector2 | Requires explicit semantics | Storage alone does not distinguish vectors/covectors/data |
| Signed distance | d(W(x))*metric ratio | Correct declared units, sign and validity (§8) |

Height may be bilinearly filtered while IDs remain discrete: alignment means the same map
and producer revision, not identical interpolation across a categorical boundary.
For significant minification these local samplers are not a complete antialiasing solution.
Do not blur IDs to disguise missing footprint filtering.

## 5. Coupled ID, random and bed-position sampling

### Shared anchor

For finite coordinates and positive source extent N:

    q = frac(W(x))
    anchor = min(floor(q*N), N-1)   // integer texel coordinates

The final bound handles rounding to N; it does not clamp authored displacement. Use the
same wrapped anchor for the ID and every associated discrete scalar. Preserve an invalid
anchor ID; do not search neighboring pixels for a different valid owner.

Classify StrataRandom, PebbleRandom and PieceRandom as discrete producer attributes.
Copy their existing values rather than deriving new ones from the output ID. This retains
producer seeds and existing published values.

### BedPosition policy: identity first, then interpolation

BedPosition is a coordinate within a selected bed, not an ordinary globally circular scalar.
A circular mean alone can choose the wrong side of a bed seam while the nearest ID still
names the other side. IDs also repeat modulo BedPeriod; equal IDs do not prove adjacency.

Use this deterministic raster policy for completed-field remapping:
1. Load anchor ID `id0` and anchor position `t0`.
2. Construct the usual four bilinear taps in wrapped source texel space.
3. Keep only taps with the same valid ID and the same local phase branch:
   `abs(t_i-t0) < 0.5`. Reject the exact half-cycle ambiguity.
4. Renormalize the surviving bilinear weights and interpolate those T values.
5. Invalid owner keeps its producer-defined invalid/default scalar value at the anchor.

The anchor is one of the bilinear taps and has positive weight, so a valid anchor always
supplies a defined value. This is one owner-guided sampling rule, not a cascade of fallback
systems. It deliberately reduces smoothing close to label/reset boundaries. Do not apply
`frac` to the result or switch the output ID after interpolating position.

This is seam-aware but approximate. If a source pixel spans multiple beds, the raster lacks
the information needed for an exact answer. Do not infer missing bed counts from hashed
IDs or repeatedly unwrap modulo IDs until a plausible result appears.

Do not differentiate this label-conditioned, bounded T field to recover a supposedly exact
boundary. Structural Strata uses step 5's direct interface gradients; completed-field tools
transform their existing boundary field with its own validity/metric.

This refines the earlier audit's terse “seam-unwrapped variant”: phase unwrapping does not
justify interpolation across different bed owners. Existing stage-8 boundary reconstruction
is a separate historical approximation, not the new scalar sampling policy.

## 6. Ownership of the Height remap — exactly once

Do NOT simply add Height to the current `RemapGeneratorBundle` call. Existing flow apply
already produces moved Height; doing so would warp it twice.

Keep explicit entry paths:
- Structural Warp: regenerate Strata outputs at final coordinates; no bundle remap.
- Existing Shape Deform/Generator Flow: apply writes Height/Coverage; remap only companion
  fields from the matching pre-operation snapshot using that apply's WarpedUV.
- Future completed-bundle warp (step 7): owns Height/Coverage and companion remapping as one
  operation, with no separate flow-apply height result being remapped again.
- Flow Carve: use its new Height/Coverage; preserve coordinate-owned companion fields.

Represent this ownership with a clear function boundary or explicit mode, not a default
boolean whose omission can double-remap data. Before changing any bundle members, capture
all inputs including old IDs and old BoundaryField. Random/T/distance passes must not
accidentally sample IDs or a metric already transformed by the current operation.

At commit, alias PebbleCoverage to the same moved Coverage texture. Do not remap that alias
independently, which can waste a pass and diverge under deposits. Preserve Carve's support
extension without turning it into layer opacity or signed-height gating.

## 7. UV maps, centres and orientation are different types

### Coordinate maps

For identity-winding lifted maps, write U(z)=z+D_U(z):

    U_new(x) = W(x) + sample_periodic(D_U, W(x))

This is coordinate-map composition, not displacement addition. If other winding matrices
are introduced, they need explicit metadata and a generalized lift—not automatic frac.
Step 5's shared periodic displacement helper should be reused.

### Centres

A centre point c in the source frame needs a destination preimage `W(x_c)=c`.
The existing local estimate is:

    x_c approximately x + inverse(J(x))*shortest_periodic(c-W(x))

It is affine-local only. A nonlinear warp can produce different centre estimates within
one region; a folded map may have multiple preimages. Never advertise this as an exact,
per-ID-constant centre or apply the inverse twice.

Preserve the existing approximation contract where already used, while labeling it in
internal metadata. Exact shared centres require a per-region inverse/branch policy and are
outside this step. Do not enable a future producer that demands exact centres under a
noninvertible warp. No generator CentreUV assignment was found in the inspected generator
pass registrations; do not synthesize centre outputs for Strata merely to fill this slot.

### Orientation

For the existing angle convention, form `v=(cos(a),-sin(a))`, transform with J inverse,
then encode `atan2(-v.y,v.x)`. Sample categorical orientations with their ID anchor;
never interpolate angles across +/-pi as ordinary scalars.

The inverse-based operations require a finite, sufficiently well-conditioned J. Define
validity using singular values/condition, not only exact `det != 0`. Uniform tiny scale and
anisotropic collapse are different conditions. For newly supported inverse-dependent
fields, wire validity through consumers or disable that unsupported operation; do not
invent a centre, zero direction or identity warp and call it valid. Changing legacy invalid
handling requires explicit consumer work, not silently adding an unconsumed validity flag.

## 8. Distances and derivative-derived outputs

For a distance field already expressed in the source-map UV metric, let g be its local
source gradient in those same units:

    g_dest = g*J
    d_dest approximately d_source * length(g)/length(g_dest)

For an exact SDF, length(g)=1. Keeping the numerator preserves identity for approximate
existing distance fields. This corrects the local metric, not global distance accuracy.
Negative-inside sign and units must be retained. Never apply this correction again to a
boundary regenerated directly in destination space by step 5.

Distance sampling must not bilinearly blend no-hit sentinels into valid distances. Decode
validity before interpolation, and require supported valid taps for metric/derivative reads.
For an invalid stencil or degenerate gradient, emit an invalid boundary, not a made-up SDF.
Keep any required legacy scalar sentinel on its named public output while retaining the
internal distance+validity pair. The sentinel itself is not transformed as a real distance.

Do not identify metric fields by names alone:
- RockEdgeDistance/PebbleEdgeDistance use UV-distance semantics and associated validity.
- CrackDistance is a positive distance in crack-cell units; its internal BoundaryField is
  a separate, placed negative-inside UV-distance estimate. Preserve that distinction.
  Transport CrackDistance as its declared source attribute unless a new metric contract is
  explicitly approved; don't replace it with -BoundaryField.x or label it an SDF.
- RockHeight is a normalized geometry output, not the final signed module Height.
- RockSlope currently measures intrinsic/source-frame slope. It is not automatically the
  slope of the final warped/carved relief. Preserve its contract as a transported attribute.
- A field explicitly defined as destination slope/curvature must be recomputed from its
  declared source scalar; scalar interpolation or J alone is not enough for curvature.

Existing seed validity/heuristics and source-distance approximations still apply.

## 9. Noise and separately published fields — gate step 7 correctly

Re-read `MixtormatNoise.usf` before supporting Noise as a warp target:
- Value has a family-dependent raw contract; signed Height is derived from it, then the
  shared normalization and scale are applied separately.
- Gradient/Value noise write analytic derivatives in generator-domain coordinates.
- FBM/Ridged/Billow use their respective derivative helpers.
- Worley writes `w.Direction`, including for F2 and F2-F1: it is not generally the derivative
  of the published clamped Value. Bars is documented as a directional output too.
- The shader writes Gradient directly; it does not apply generator placement to it.

Thus “Vector2 Gradient -> multiply by J” is insufficient. Descriptors need frame and
meaning. For an actual generator-domain derivative used as a destination derivative, the
chain is `g_domain*A*J`; for a geometric direction, determine vector versus covector meaning
from its producer, not its name. Normalizing a transformed vector is another semantic choice.

Do not change today's published Gradient meaning/frame silently. Step 6 records the
contract and registry coverage requirement; step 7 must settle per-family transformations
and compatibility before enabling Noise targets. No automatic Vector2-to-Flow conversion.

When supported, update Value, Gradient, any real cell IDs and Height under the same operation
revision. Do not leave old Value/Gradient addresses pointing into the unwarped node cache.
Do not mutate cached producer textures; publish transformed outputs for the consumer graph.

## 10. Producer manifest checklist

| Producer | Required companion policy |
|---|---|
| Strata | RegionIds + owner-guided StrataPosition + nearest StrataRandom + boundary validity |
| Rock | Typed continuous masks/ramps, intrinsic RockHeight/RockSlope, metric RockEdgeDistance |
| Pebbles | IDs + nearest PebbleRandom + one shared Coverage/PebbleCoverage result + valid distance |
| Cracks | IDs + nearest PieceRandom + source-unit CrackDistance distinct from BoundaryField |
| Cliff | Real block/row identities must be inventoried; named seams/cavity/coverage are not IDs |
| Noise | Family contract + frame metadata; separately published Value/Gradient and optional IDs |

The manifest is a registration checklist, not approval to add missing outputs or enable new
flow-tool owners. Preserve bHashedIds and never create IDs for Noise families without them.

## 11. Implemented scope (steps 5 + 6)

1. Step 5's ordered per-target RG32F D/R32F B compose and Strata coordinate/gradient chain
   are present; structural generation does not resample completed outputs.
2. Current bundle producers register semantics; shared safe wrapped anchors couple IDs/random,
   and stage 11 interpolates T only within the anchor's owner/local phase branch.
3. Companion remapping reads one immutable snapshot with old IDs/boundaries, then commits;
   apply owns Height/Coverage, and coverage aliases do not get a second warp.
4. Distance remapping excludes invalid/sentinel taps and retains the source-gradient numerator.
   Active structural Strata uses its direct boundary; inactive retains stage 8.
5. Exact centres, unknown Vector2/Noise-family semantics and other structural targets remain
   gated for step 7. Legacy inverse-based centre/orientation limitations remain unchanged.
6. Source review only: broken StructuralWarp tests were removed at user request. Acceptance
   cases below are unexecuted; no agent compile/build/runtime/test results are claimed.

Existing resampling corrections can change assets with active flow tools. That is an
intentional behavior correction, not a behavior-preserving refactor. Disabled/no-warp paths,
source seeds, published names and defaults must remain intact. No legacy toggle is requested.

Primary files, relative to plugin root:
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`: internal semantics.
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`: producer registration,
  apply/remap ownership, immutable source snapshots and publication.
- `Shaders/Private/MixtormatGeneratorBundle.usf`: typed sampling, wrapped ID loads, metrics.
- `Shaders/Private/MixtormatGeneratorFlow.usf`: retain Height/Coverage ownership.
- `Shaders/Private/MixtormatStrataCarver.usf`: aligned structural outputs/direct boundaries.
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`: output demand/cache scope.

Reference-only until step 7: `MixtormatGpuNoisePasses.cpp`, `MixtormatNoise.usf`, and the
individual Rock/Pebbles/Cracks/Cliff producer shaders. No broad inspector/layout work in step 6.

## 12. Acceptance matrix (not executed)

- ID seam: negative UV, exact 0/1, near-1 rounding, multiple tile offsets, non-square output.
  Every load stays in bounds; outputs are original uint IDs or the original invalid sentinel.
- Random: fractional warp between two beds/pieces/pebbles never creates intermediate random
  values, and always uses the same anchor as the output ID.
- Bed seam: T=0.98 versus 0.02 never becomes a false 0.5 stripe. Test different IDs, modulo-ID
  wrap, BedPeriod=1, repeated IDs and an underresolved source; report approximation honestly.
- Signed height: negative and over-one values survive; apply+companion remap matches one
  warp, not two. Flow Carve does not move categorical outputs.
- Coverage: moved Coverage equals PebbleCoverage; deposits extend support; combine remains
  signed height addition without coverage gating.
- UV maps: two noncommuting maps compose correctly; no wrapped absolute-UV seam or loss of
  winding. Centres/orientation are tested separately from coordinate-map composition.
- Gradients: use step 5's periodic shear, placement flips/nonuniform scale and mask falloff;
  compare with numerical differentiation within finite-resolution tolerance.
- Distances: affine local scale gives the expected metric ratio and sign; invalid/no-hit
  stencils never contaminate valid distances with sentinel magnitudes.
- Singular maps: scalar/ID pullback may remain defined; inverse-dependent outputs must not
  masquerade as valid. A placement flip is not a warp foldover.
- Cache: change source, source enablement, ordering, mask or warp settings; all demanded
  companion fields agree on the same new revision after prefix resume.
- Structural-only Strata: no completed-bundle resampling, no double Jacobian, all generated
  outputs align; legacy post-tools still apply their separate typed remap exactly once.

The lamina contact fade, hinge discontinuities, PixelT>1 shared-seam issue and oblique-joint
footprint defect remain separate work. Step 6 corrects resampling semantics, not those
geological shader defects or joint prominence.
