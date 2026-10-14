# Generators

A generator rewrites a layer's **input height** before anything reads it, so the
layer composites as though the carved surface were authored. It is neither an
effect (post-composite filter) nor a mask (0..1 coverage).

## Registration / taxonomy

| Thing | File |
|---|---|
| `EMixtormatGeneratorType` | `Runtime/Public/MixtormatGeneratorTypes.h` (~L15) |
| `FMixtormatGenerator` (payload union) | `MixtormatGeneratorTypes.h` (~L853) |
| Per-generator structs | `FMixtormatStrataCarver`, `FMixtormatCracks`, `FMixtormatRockFormation`, `FMixtormatPebbles`, `FMixtormatCliffStrata`, `FMixtormatNoise` — all in `MixtormatGeneratorTypes.h` |
| Generator height sublayers | `FMixtormatGeneratorHeightBlend`, `FMixtormatGeneratorHeightCurve`, `FMixtormatGeneratorHeightColorRamp`, `FMixtormatGeneratorHeightPush` — `MixtormatGeneratorTypes.h` |
| `MixtormatCanOwnGeneratorFlow` | `MixtormatGeneratorTypes.h` (~L34) |

Current types: `StrataCarver`, `Cracks`, `RockFormation`, `Pebbles`,
`CliffStrata`, `Noise`, `BooleanOps`. Enum is serialized by value — **append, never reorder**.

Strata Carver is frozen and excluded from Mixtormat 1.0 authoring and shipping requirements.
Its creation entries are removed from layer, child and Sources menus. Types, shaders,
serialization and existing-asset editing/rendering remain intact. Cliff Strata is unaffected.

Generator-owned flow tools (`ShapeDeform`, `GeneratorFlow`, `FlowCarve`, `GravityFlow`) live in
`EMixtormatEffectType` (`MixtormatEffect.h`), not here. They are valid only
scoped under a generator that can own them (`MixtormatCanOwnGeneratorFlow`:
Strata Carver, Rock Formation, Pebbles, Cracks, Cliff Strata, Noise) and rewrite its height before
combine. Noise and Cliff Strata support Height steering only: neither publishes a signed boundary
field, so Signed Distance stays unavailable there (`MixtormatGeneratorHasFlowBoundary` names that
distinction for the inspector and authoring defaults). Every other eligible generator keeps both
source modes. Historical baseline: `old_docs/generator_flow_interaction_audit.md` (archived).

## Boolean Ops — Scatter Carve and field combining

> [!NOTE]
> The four-mode `BooleanOps` generator authoring is superseded by the dedicated **Boolean stack container** child (`EMixtormatLayerChildType::Boolean`, see `AgentDocs/BOOLEAN_STACK.md`). In the stack architecture, Target and Source operands are authored as scoped children with independent UV and height offsets. The finite-volume **Scatter Carve** compute path is preserved and reserved for separate reclassification.

`BooleanOps` originally authored four modes: **Scatter Carve**, **Boolean**, **Round** and
**Bevel**. Scatter Carve preserves its existing finite-volume implementation.
The other modes combine the current signed height surface with the signed Height
output of an explicitly chosen **earlier generator module in the same layer**.
The operand is selected by stable child GUID. Member-specific group expansion
and multi-child clipboard duplication remap Boolean operand GUIDs along with
Height Blend and Color Ramp references; moving a lone Boolean to another
owner clears its now-invalid local source. Missing, disabled, nested,
heightless Noise and forward references are an exact zero-delta identity.
The inspector uses the canonical child-container resolver for either a
Generator layer or a shared layer group, and lists only earlier, height-writing
generator modules. A source selected in a group is remapped separately for
each member during expansion. Invalid selection displays Unavailable instead
of a misleading valid source name.

- **Boolean:** hard union (max) and intersection (min) of single-valued vertical
  surfaces; subtract removes positive authored source relief from the running
  height (a *relief cut*, not a general 3D solid difference).
- **Round:** polynomial rounded surface joins and a C1 eased one-sided cut
  shoulder; beyond Join Width it is exactly the original hard cut.
- **Bevel:** analytic chamfered joins and piecewise-planar cutting shoulders;
  outside the band it is exactly the original hard cut, with no permanent
  subtraction depth offset.
- Parameters: source chip, operation, signed Source Scale (negative inverts
  source relief), raw signed Height Bias and Amount; Round and Bevel also use
  Join Width in signed height units.
- Outputs: copyable/previewable Height (signed module delta), copyable
  FieldMask and FieldBoundary, and signed FieldDifference. The inspector's
  primary preview is Height Delta in all four modes; the other outputs remain
  secondary. No separate Contribution texture is allocated because it would
  exactly duplicate Height. FieldMask carries the effective continuous operation weight, not a binary
  threshold; the boundary respects the same scope/amount. Scoped masks blend
  the result.
Hard Boolean joins use pixel-neighbor crossings for a visible boundary
diagnostic; Round/Bevel show a height-width band.
- These field modes use one compute dispatch. They do not normalize or clamp
  signed height or presume height is a metric SDF.

Heightfields cannot represent arbitrary 3D undercuts, and these Round/Bevel
operations are heightfield joins rather than Euclidean solid SDF operations.
The existing Height Blend sublayer remains independent.

Scatter Carve: Finite
ellipsoid or polygon-prism cutters are seeded periodically in UV; each cutter
samples the original signed height at its seed to stay rigid. Scatter XY uses
the Generator layer's tile/flip/quarter-turn/offset placement and the existing
pre-generation UV mapping. Affine placement reconstructs the nearest cutter
anchor in destination UV space. Nonlinear pre-generation warps affect XY but
do not currently invert the warp when sampling cutter anchor height.
Subtract removes
the union of intersecting depth intervals, revisiting overlaps; Union projects
the frontmost cutter. `CenterDepth < 0` also selects Union as in the prototype.
Chamfer uses the root-refined finite-volume distance method, not morphological
erosion, Eikonal propagation or a height-stamp approximation. Scoped masks blend
the finished result.

The compute pass evaluates `CompositeBelow + RunningHeight` as its raw target,
outputs `resolved - target`, then uses the standard additive running-height
chain. No normalization or additional Height Scale is applied to this delta.
Outputs: copyable/previewable Height (signed delta), CutDepth (signed removal), GeometryFront,
GeometryBack (unclipped original cutter envelope), and copyable GeometryMask.
The footprint includes hard cutter intervals and chamfer-only influence, and
respects the cutter's scoped mask (rather than showing masked-off cutters).
Normals remain on the standard final-height path. In this first iteration
Boolean Ops does not publish a combined-surface SDF, IDs, or native Flow.
The full seed-count range is available but expensive; default 16, UI range
0–256, and a hard cap of 4096. The source prototype is
`AgentDocs/Prototypes/ScatterCarve/scatter_carve.cl`.

Implementation: `FMixtormatBooleanOps` →
`GatherGeneratorChild` → `AddBooleanCarvePasses` / `AddBooleanFieldPasses` →
`MixtormatBooleanCarve.usf` / `MixtormatBooleanField.usf` →
`BuildBooleanOpsControls`. Previews are mode-specific. Boolean field
diagnostics and Scatter Carve depth/front/back use the signed-height blit
rather than a UV-metric signed-distance renderer. Boolean Ops does not advertise
an Accumulated Flow eye, since it publishes no such field.
Static source review only; no Unreal build or runtime validation performed.

## Shared native Flow foundation (Stage 1)

- `MixtormatGpuNativeFlowPasses.*` / `MixtormatNativeFlow.usf` own the extracted Noise
  intrinsic derivative pass. `AddNativeFlowPass` accepts explicitly signed destination-space
  height (R32), signed boundary distance (R32), or the existing negative-inside UV-distance +
  validity boundary contract (RG32). Packed boundaries require trusted centre and derivative
  taps; arbitrary Gradient, unsigned coverage and ID fields are not converted implicitly.
- Wrapped per-UV derivatives retain unit negative-gradient transport: downhill for height,
  inward for signed boundary. Flow/FlowSmooth use RGBA16F (the same texture without authored
  smoothing); reserved B stays zero, A carries influence, and validity remains separate R16F.
- Scoped consumers seed working Flow before post-generation Behaviors. Sculpt (`Carve` backing
  enum), traced Deform and traced Warp demand that seed without an additional Flow child.
  The intrinsic field is adopted once; only explicit Noise MODE contributions use Add/Mix.
- Noise retains its completed signed-height FlowDirection diagnostic, height-less scratch-field
  diagnostic, weighted GeneratedFlow magnitude, normalization/gating and Feather ordering.
  Native Flow publications reuse the existing FlowDirection address and preview renderer.
- Strata Carver is excluded from the universal native Flow integration. Boundary support is
  an explicit derivation primitive, not SDF steering for Sculpt.

## Universal native Flow integration

Cracks, Rock Formation and Pebbles automatically derive a native `FlowDirection`
from their existing packed signed boundary, with local validity/Region ID guards.
Cliff Strata derives from its signed native height because its internal vector output
does not satisfy the canonical Flow contract. All four publish the typed Flow
before post-generation Behaviors and expose the standard output-preview control.

`FMixtormatGenerator::NativeFlow` holds shared Amount, Angle, Slope Influence,
Boundary Influence, Gravity Influence and Gravity Angle. Gather validates these
settings independently of the cached generator solve; the shared Flow pass steers
boundary/height directions without mutating Height, IDs or masks. Where no signed
boundary exists, the geometry weight applies to signed-height gradient instead.
Additional Flow-producing children still accumulate in authored order.

Noise retains its own Flow modes, `GeneratedFlow`, diagnostic and controls, and
Strata Carver is excluded from this integration.

## Cracks native Flow (Stage 2)

- Cracks automatically publishes `FlowDirection` from `FGeneratorBundle::BoundaryField` via
  the shared `AddNativeFlowPass(..., ENativeFlowSource::SignedBoundary)` before Behaviors,
  even without a Flow child or consumer. Its signed Height is not a substitute boundary source.
- Existing piece Region IDs optionally constrain the shared packed-boundary derivative stencil.
  Invalid/non-finite taps, flat slopes, cross-piece taps and sampled derivative reversals produce
  zero vector, influence and validity. All taps wrap across the tile; IDs are not modified.
- The ordered working Flow adopts this native seed once. Sculpt, automatic Warp and traced
  Deform consume it; explicit Flow children still contribute in order through existing Add/Mix.
  Warp transports working Flow, while Deform changes Height only. `FlowDirection` remains the
  original native geometry snapshot, not a second solve from the modified Height.
- Runtime field capability/reference validation and the existing child-output preview metadata
  expose Cracks Flow without new inspector controls or a separate preview/shader implementation.
  Height, IDs, masks, chamfer, Noise and Strata Carver keep their existing behavior.
- Validity is a conservative local-stencil guard, not subpixel boundary reconstruction or a
  physics/collision guarantee.

## Noise as a mask source (inline or live)

Every Mask child can now read noise directly; the same producer family is reused rather than
re-implemented:

- **Inline (`EMixtormatMaskSource::Noise`, appended value 2).** `FMixtormatMaskLayer` carries a
  local `FMixtormatNoise Noise` (`UsesNoise()` is true only while no published source is set, the
  same precedence Layer Values uses). GPU-side, `AddNoiseMaskPass` dispatches the shared
  `MixtormatNoise.usf` families with identity placement and produces R32 coverage: signed Value
  maps through `saturate(0.5 * Value + 0.5)`, unsigned families clamp unchanged
  (`AddNoiseCoveragePass`). Mask placement/shaping/blur run afterwards in the ordinary mask
  shader, so Tiling/UV/Rotation stay meaningful and cost no extra pass when untouched.
- **Live (`Noise Value from…`).** A mask can instead reference an existing Noise generator's
  completed published `Value` through the normal `PublishedSourceLayerId/ChildId/Output` fields.
  `MixtormatOutputReferences::ResolvePublishedMaskSource` is the canonical same-layer ordering
  check (completed earlier scope, no self/owner feedback); GPU resolution converts the typed
  `ScalarSigned`/`Scalar01` field to coverage (`Ctx.NoiseMaskSources` graph-local cache). The mask
  resolver checks typed Value before any legacy `PublishedMaskOutputs` alias, so raw signed
  Value is never consumed directly as coverage.
- **Sources shelf (`Noise Value from…`).** A Mask can now select a shelf Noise root
  directly. Its appended `PublishedSourceOwnerKind = Shelf` / `PublishedSourceShelfId`
  preserves the distinction from legacy layer addresses. Demand scheduling evaluates
  the valid Noise root before the stack and the mask uses the same typed Value-to-coverage
  converter, shaped locally by its own UV, blend, filters and scope. This consumer
  supports Noise Value only; shelf color, vector and generic ID-to-mask conversion
  are not inferred.
- The raw typed `Value` publication is unchanged, so Height Push/Warp/reference consumers keep
  full precision. The Noise Gate and mask-source menu/form live in
  `Widgets/Layers/MixtormatMaskSources.cpp`; the inline controls reuse the generator inspector's
  PATTERN/PLACEMENT rows (`BuildNoisePatternPlacementControls`). Height-only generator settings
  (`NoiseHeightScale`, `bNoiseNormalizeHeight`) intentionally do not appear for mask noise,
  because they shape module Height, not the Value coverage a mask reads.
- Right-click an eligible scoped-mask owner (generators and effects, including Gravity Flow) →
  **Noise Gate** to create one scoped Mask child with Source=Noise in a single edit.
- Canonical nested parameter ownership is the appended `EMixtormatParameterOwnerType::MaskNoise`
  (25), with actual owner/child GUIDs. Reflected defaults, binding/introspection and instance
  inheritance share the normal parameter pipeline. Exact files and later-UI integration rules:
  `old_docs/noise_gate_flow_handoff.md` (archived delivery history).

Noise V2 extends this shared producer with an appended **Phasor** family, Worley metrics/jitter
and optional curl-driven domain distortion. Defaults preserve old Value/Height contracts.
Noise V2.1 adds opt-in octave layering to Gradient/Value/Worley/Bars/Phasor, a smooth
Worley cell-distance-depth control, Phasor Scale/Bias and independent jagged-domain warp.
Noise-specific numeric/enum authoring defaults are registered for Generator/MaskNoise in
`Config/MixtormatParameterAuthoring.json`. The normal right-click reference/dev-menu path
applies; pixel-based Noise GPU Driver evaluation is **not** implemented yet.
See [NOISE_V2.md](NOISE_V2.md) for parameters and constraints.

## Ordered Flow and post-generation Behaviors

- `ApplyGeneratorPostBehaviors` adopts native Flow once, then evaluates scoped Noise and
  Behaviors in authored order. Noise `GeneratedFlow` retains vector magnitude and composes as
  `lerp(FlowIn, Generated, saturate(Mix * Mask)) + Generated * Add * Mask`.
- Noise MODE combines weighted own-height downhill, preceding-height downhill, periodic Curl
  and angular Force with optional height-gravity steering. Opposing contributions can cancel.
- `MixtormatFlowTrace.usf` integrates canonical Flow with RK2 into lifted destination-to-source
  coordinates. It preserves magnitude; it does not solve or enforce SDF collisions.
- Scoped masks gate their owner only. Independent scopes start white and do not modify the
  layer CombinedMask. Warp masks scale traced displacement toward identity after tracing.
- Sculpt (`Carve` backing type) consumes current Height and accumulated Flow. Push composes
  signed Height without transport. Deform resamples Height only, leaving companion fields fixed.
- Warp remaps the generator bundle and transports accumulated Flow at the same coordinates.
  Feather filters Height or an explicitly scoped Flow publication according to its owner.
- Noise Value/Gradient remain raw and ungated; mask consumers convert signed Value to coverage
  before applying their own placement, shaping and filters.

## Height Push (structural module)

`HeightPush` is an independent, append-only child type, not Height Follow, Height Blend,
Flow Carve or a coordinate warp. Author it as **source generator → Height Push → target
Strata generator**. Sources may also be generators in earlier layers; the typed `Height`
output is the completed signed module result after its tools, normalization and Height Scale.
The target is a later, enabled Strata generator in the same layer. Group placements and
other target kinds are not enabled yet; unavailable choices remain disabled.

`Amount` maps signed source height to a bedding-coordinate shift (one unit = one bed period).
Zero amount, disabled modules or missing/invalid connections do nothing; there is no implicit
source or target. Scoped masks gate only the push. Multiple pushes accumulate at their own
ordered positions before the target runs. The Strata shader applies the shift before folds
and chains its destination-space gradient into filtering; bed IDs/position/random are
regenerated together with relief. Existing composite-below Height Follow is unchanged.

Path: Runtime child/payload → `GatherGeneratorHeightModuleChild` →
`MixtormatGeneratorHeightPush.usf` → `MixtormatStrataCarver.usf` →
`BuildHeightPushControls` (source, target, amount, enable). Implementation added.
Ordered Structural Warp integration is now present; see below.

## Structural Warp (steps 5–7 implemented)

`StructuralWarp` is an append-only Runtime child with its own enable flag and
`FMixtormatGeneratorStructuralWarp` payload. `Source` is an `FMixtormatOutputReference`:
Flow or UVMap only, never generic Vector2. Noise publishes an explicit `FlowDirection`
from its completed signed Height: a unit downhill direction in destination tile UV,
with zero influence/validity at flat or non-finite slopes. It does not reinterpret the
heterogeneous raw `Gradient`. Flow Amount and Trace Length control travel; Height Scale
zero yields no flow, and negative Height Scale reverses direction. It requires a completed earlier source and an
explicit later, enabled, unscoped same-layer generator target: Strata, Rock Formation, Pebbles,
Cracks, Cliff Strata, or Noise. Shared group Warp/Height Push modules remain disabled on expansion. Member-local structural modules and their parameter/input references now remap authored shared-child GUIDs to the member's effective child IDs; this does not expose new shared-module authoring.

Structural Warp Flow Amount and Trace Length now accept the existing scalar Driver chain from an earlier enabled layer's completed combined mask. Gather validates the producer order and snapshots the signal; stage-8 Flow tracing applies `MixtormatApplyDriver` per destination texel before RK2 integration. Missing or unavailable snapshots preserve authored scalar values. Self/later-layer, child-mask and Region-ID drivers are not offered for these wells. Direct numeric editing and Follow/Link remain supported; Flow Steps stays integer numeric/reference-only. Existing serialized bindings are preserved.

`GatherGeneratorHeightModuleChild` fills `FGeneratorStructuralWarpRenderData.Source` and
`TargetChildIndex`. Published source demand is registered before prefix reuse. GPU state
is per target: `GeneratorStructuralDisplacements` (RG32F) and the existing
`GeneratorHeightPushFields` (R32F). `MixtormatGeneratorStructuralWarp.usf` writes fresh
resources: `D_new = d + sample(D_old, psi)`, `B_new = sample(B_old, psi)`, `psi = x + d`.
Flow uses the canonical RK2 reference trace, applies amount once, then masks displacement.
Strata regenerates in its structural frame. Rock Formation, Pebbles, Cracks, Cliff Strata and
Noise use one completed-bundle pullback after native generation, existing flow and signed
normalization. That operation owns Height/Coverage and companion remapping together, so neither
is double-warped. Named coverage outputs must remain aligned with bundle Coverage.

Strata evaluates warped coordinates with placement once and gradients `g*A*J`; final B's
already-destination gradient is not multiplied by J again. Active warp emits direct RG32F
negative-inside boundary distance + validity; inactive warp retains stage-8 reconstruction.
Step-6 producer descriptors (`RegisterNamedMask`) drive immutable-snapshot companion remaps:
	safe wrapped ID/random anchors, owner/phase-aware bed T, lifted-map composition, and
validity-aware distance metrics with the source-gradient numerator retained. `CrackDistance`
remains a crack-cell attribute, not the internal UV boundary. Noise Value is moved as its raw
scalar; its heterogeneous generator-domain Gradient is explicitly transport-sampled, not
blindly transformed. See both structural/output-alignment design docs.

Implementation plus targeted source review. The gather missing-header issue is fixed. Raster
composition/filtering and legacy local inverse centre/orientation approximations remain; no
geological fixes are included.

## Structural connection authoring (interaction v1, D3)

Inspector Push/Warp menus now delegate to the address-based
`Widgets/Layers/MixtormatStructuralConnections.cpp` adapter. It checks Runtime status
against Gather's mixed effective/resolved projection, lists eligible choices first,
and explains unavailable choices. One setter revalidates the live GUID address before
writing, preserves module enable/trace settings, and records a discrete undo step.
`None` clears only the selected edge's GUIDs; dangling sources are never auto-rebound.
Readable labels retain existing sources even for disabled references or wrong output kinds.

D1 adds compact row chips. D2 adds independent structural source/target highlights,
active-valid incoming counts, and transient markers for collapsed source layers while preserving
instance-source glow. E1 adds target-row actions that insert an unscoped module immediately
before the explicit target, set only its target GUID, and leave source GUIDs unset. Existing
layer-level creation remains unconnected; no source is inferred or auto-rebound.

## Defaults / parameter metadata

- Compiled defaults: the struct initializers in `MixtormatGeneratorTypes.h`.
- UI ranges: `meta = (UIMin/UIMax/Delta)` on the UPROPERTYs, read by
  `Editor/Private/UI/Parameters/MixtormatParameterUiMeta.*`.
- Authoring overrides: `Config/MixtormatParameterAuthoring.json` via
  `MixtormatParameterAuthoring.*`.
- Hard bounds / sanitize: `Runtime/Public/MixtormatParameterDefinition.h`.

## Gather

`Shaders/Private/Compositing/MixtormatGeneratorGather.cpp`

- `GatherGeneratorChild` — switch on `Generator.Type`; resolves settings into
  `FGeneratorRenderData`; applies `PlacementKey` to `FieldKey` for cached types.
- `GatherGeneratorHeightModuleChild` — Height Blend / Height Curve / Height
  Color Ramp / Height Push / Structural Warp sublayers.

## GPU dispatch

`Shaders/Private/MixtormatGpuGeneratorPasses.cpp`

- `AddGeneratorLayerPasses` — once per Generator layer; walks module children in
  authored order. Each generator builds native height, runs its flow tools, then applies
  shared signed normalization / Height Scale. Shared bundle stage 9 adds module height
  to running height without coverage gating; explicit Height Blend sublayers are separate.
  The final running height becomes `LayerCtx.LayerInputHeight`.
- Dispatch switch (~L2378): `AddStrataCarverPasses`, `AddRockFormationPasses`,
  `AddPebblesPasses`, `AddCracksPasses`, `AddCliffStrataPasses`, Noise.
- `AddGeneratorModuleCombine` (~L780).
- Distance solves run on a capped grid (`DistanceSolveMaxSize = 1024`).

## Shaders

`Shaders/Private/`: `MixtormatStrataCarver.usf`, `MixtormatCracks.usf`,
`MixtormatRockFormation.usf`, `MixtormatPebbles.usf`, `MixtormatCliffStrata.usf`,
`MixtormatNoise.usf`, `MixtormatGeneratorBundle.usf`, `MixtormatGeneratorFlow.usf`,
`MixtormatGeneratorHeightBlend.usf`, `MixtormatGeneratorHeightCurve.usf`,
`MixtormatGeneratorHeightColorRamp.usf`, `MixtormatGeneratorHeightPush.usf`,
`MixtormatGeneratorStructuralWarp.usf`, `MixtormatGeneratorWarp.ush`,
`MixtormatGeneratorHeightModules.ush`, `MixtormatGeneratorPlacement.ush`.

## Inspector

`Editor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` —
`SMixtormat::Build*Panel` and `BuildGeneratorFlowControls`.

## Published outputs

`Editor/Private/Widgets/MixtormatChildCapabilities.cpp` — `GetChildCapabilities`
is the single place a generator's previewable/copyable outputs are declared.
Noise has Value (primary), Gradient (Vector2), Flow and Worley-only IDs. Copy preserves
typed Value/Gradient payloads. Value's kind follows the family (signed lattice/Bars;
0..1 Ridged/Billow/Worley). Value and Gradient retain full precision; field validation
accepts those formats alongside default half precision. Scalar previews map signed
values to display gray only; vector previews show direction. Scoped coordinate tools
and Structural Warp remap Value, Gradient and IDs together; Flow is derived afterwards.
Flow Carve changes Height, not the raw Value/Gradient field.

## Sol review checklist — steps 1–7

Implementation claims below and in the linked shader/design docs are based on targeted source
review. `HeightSource`/`WarpSource` remain serialized, disabled-by-default fields; they are not
the ordered Height Push/Structural Warp sockets. Runtime header comments still say consumers are
implemented separately and need a source-truth review; no code change is made here.

- [ ] **Sources/order:** direct completed signed Height or eligible Flow/UVMap; reject self,
  forward, disabled, invalid and wrong-kind sources/targets. Check cache demand and same-layer order.
- [ ] **Parameter trace:** Runtime defaults/serialization → gather/render data → GPU uniforms,
  dispatch and resource lifetime → shader use → inspector rows/enable state. Include reference
  Flow Amount/Trace Length/Steps and Height Push/Warp source, target and enable controls.
- [ ] **Separate modules:** Height Push changes Strata bedding; Structural Warp changes the
  structural coordinate map. Confirm authored order and non-commuting Push/Warp composition.
- [ ] **Jacobian:** row-gradient `g*J`, placement exactly once, `grad(B)` not transformed
  twice, non-square texel differences, masks, degeneracy and boundary validity.
- [ ] **Tile/identity:** preserve signed Height, invalid IDs/hash semantics, UV winding,
  ID-anchored per-region random, owner/phase-aware bed position, and destination mask/ID influence.
- [ ] **Single ownership:** no double warp of Height/Coverage; coverage aliases share one result;
  Noise Value/Gradient and each producer's named fields move under the same source revision.
- [ ] **Six producers:** Strata regeneration; Rock, Pebbles, Cracks, Cliff and Noise typed bundle
  policies. Preserve Cliff identity inventory and Noise family-specific Gradient contract.
- [ ] **Known limits:** local centre/orientation inverse approximations, raster filtering/distance
  approximations and carried-over Strata defects remain.
