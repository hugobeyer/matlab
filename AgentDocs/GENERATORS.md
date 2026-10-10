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
`CliffStrata`, `Noise`. Enum is serialized by value — **append, never reorder**.

Generator-owned flow tools (`ShapeDeform`, `GeneratorFlow`, `FlowCarve`, `GravityFlow`) live in
`EMixtormatEffectType` (`MixtormatEffect.h`), not here. They are valid only
scoped under a generator that can own them (`MixtormatCanOwnGeneratorFlow`:
Strata Carver, Rock Formation, Pebbles, Cracks, Cliff Strata, Noise) and rewrite its height before
combine. Noise and Cliff Strata support Height steering only: neither publishes a signed boundary
field, so Signed Distance stays unavailable there (`MixtormatGeneratorHasFlowBoundary` names that
distinction for the inspector and authoring defaults). Every other eligible generator keeps both
source modes. Historical baseline: `old_docs/generator_flow_interaction_audit.md` (archived).

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
- The raw typed `Value` publication is unchanged, so Height Push/Warp/reference consumers keep
  full precision. The Noise Gate and mask-source menu/form live in
  `Widgets/Layers/MixtormatMaskSources.cpp`; the inline controls reuse the generator inspector's
  PATTERN/PLACEMENT rows (`BuildNoisePatternPlacementControls`). Height-only generator settings
  (`NoiseHeightScale`, `bNoiseNormalizeHeight`) intentionally do not appear for mask noise,
  because they shape module Height, not the Value coverage a mask reads.
- Right-click an eligible scoped-mask owner (generators and effects, including Gravity Flow) →
  **Noise Gate** to create one scoped Mask child with Source=Noise in a single edit. Evidence is
  source review only; no build, shader compile, runtime or visual validation has been run.
- Canonical nested parameter ownership is the appended `EMixtormatParameterOwnerType::MaskNoise`
  (25), with actual owner/child GUIDs. Reflected defaults, binding/introspection and instance
  inheritance share the normal parameter pipeline. Exact files and later-UI integration rules:
  `old_docs/noise_gate_flow_handoff.md` (archived delivery history).

## Gravity Flow (texture-space generator child)

- Right-click an eligible generator → **Gravity Flow**. It uses the same ownership rules as
  existing flow tools: Strata Carver, Rock Formation, Pebbles, Cracks, Cliff Strata and Noise.
  Noise- and Cliff-scoped tools start with Height steering; Signed Distance is unavailable there
  because neither publishes a signed boundary field.
- `GravityFlow = 12` is appended to `EMixtormatEffectType`. New children start with Height
  steering. `GeneratorFlowAngle` rotates `(0,-1)`: 0 degrees = -V, 90 = +U, 180 = +V.
- Height steering is local, keeps gravity active on flat texels and avoids seed/JFA passes.
  `GravityFlowSurfaceFollow` controls bounded downhill steering; zero gives uniform gravity
  when Bend is also zero. Surface steering retains a positive component along gravity.
- Signed Distance steering optionally projects incoming gravity along trusted owner outlines.
  `GravityFlowDeflection`, Reach and Feather control this; known interiors remain unmoved.
  This is approximate owner-boundary steering, not an external obstacle-mask/scene collision
  solver. Head-on flows can stop; invalid or unsampled outlines cannot guarantee collision.
- The owned RK2 trace checks potentially crossed outlines with half-texel segment samples;
  near-boundary segments exceeding 32 output texels stop. Increase Steps or reduce Trace
  Length/Warp Strength. The nearest-seed distance limits checks to the boundary neighbourhood.
  Published Flow references use the existing generic trace and do not carry that boundary check.
- Stage 9 in `MixtormatGeneratorFlow.usf` resolves gravity; stage 3 mode 3 uses the existing
  RK2/apply and aligned bundle remap. FlowDirection, WarpedUV, Influence and Validity outputs
  remain available through existing publication/preview plumbing. Masks gate only this child.
- Existing flow types and serialized enum values are unchanged. New parameters use reflected
  runtime metadata and the shared gather/inspector path; layer-prefix hashing includes them.
- Source review only: no build, shader compile, tests or visual/performance validation run.

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
`BuildHeightPushControls` (source, target, amount, enable). Implementation added; build,
shader compilation and visual validation of Height Push have not been run by the agent.
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
Cracks, Cliff Strata, or Noise. Group targets remain gated.

Structural Warp Flow Amount and Trace Length now accept the existing scalar Driver chain from an earlier enabled layer's completed combined mask. Gather validates the producer order and snapshots the signal; stage-8 Flow tracing applies `MixtormatApplyDriver` per destination texel before RK2 integration. Missing or unavailable snapshots preserve authored scalar values. Self/later-layer, child-mask and Region-ID drivers are not offered for these wells. Direct numeric editing and Follow/Link remain supported; Flow Steps stays integer numeric/reference-only. Existing serialized bindings are preserved.

`GatherGeneratorHeightModuleChild` fills `FGeneratorStructuralWarpRenderData.Source` and
`TargetChildIndex`. Published source demand is registered before prefix reuse. GPU state
is per target: `GeneratorStructuralDisplacements` (RG32F) and the existing
`GeneratorHeightPushFields` (R32F). `MixtormatGeneratorStructuralWarp.usf` writes fresh
resources: `D_new = d + sample(D_old, psi)`, `B_new = sample(B_old, psi)`, `psi = x + d`.
Flow reuses the stage-8 reference trace helper, applies amount once, then masks displacement.
Strata regenerates in its structural frame. Rock Formation, Pebbles, Cracks, Cliff Strata and
Noise use one completed-bundle pullback after native generation, existing flow and signed
normalization. That operation owns Height/Coverage and companion remapping together, so neither
is double-warped. `PebbleCoverage`/`CliffCoverage` remain aliases of the single moved coverage.

Strata evaluates warped coordinates with placement once and gradients `g*A*J`; final B's
already-destination gradient is not multiplied by J again. Active warp emits direct RG32F
negative-inside boundary distance + validity; inactive warp retains stage-8 reconstruction.
Step-6 producer descriptors (`RegisterNamedMask`) drive immutable-snapshot companion remaps:
	safe wrapped ID/random anchors, owner/phase-aware bed T, lifted-map composition, and
validity-aware distance metrics with the source-gradient numerator retained. `CrackDistance`
remains a crack-cell attribute, not the internal UV boundary. Noise Value is moved as its raw
scalar; its heterogeneous generator-domain Gradient is explicitly transport-sampled, not
blindly transformed. See both structural/output-alignment design docs.

Evidence is implementation plus targeted source review only. Only the user's earlier step-2
compile is confirmed; the gather missing-header issue is fixed, but the newest build is
unconfirmed. Broken StructuralWarp tests were removed at user request; no agent compile,
build, runtime or test results are claimed. Raster composition/filtering and legacy local
inverse centre/orientation approximations remain; no geological fixes are included.

## Structural connection authoring (interaction v1, D3)

Inspector Push/Warp menus now delegate to the address-based
`Widgets/Layers/MixtormatStructuralConnections.cpp` adapter. It checks Runtime status
against Gather's mixed effective/resolved projection, lists eligible choices first,
and explains unavailable choices. One setter revalidates the live GUID address before
writing, preserves module enable/trace settings, and records a discrete undo step.
`None` clears only the selected edge's GUIDs; dangling sources are never auto-rebound.
Readable labels retain existing sources even for disabled references or wrong output kinds.
D3/D1/D2/E1 evidence is targeted source inspection only; no build, tests or runtime validation.
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
Flow Carve changes Height, not the raw Value/Gradient field. Source review only; builds,
shader compilation and runtime preview/copy/warp checks have not been run.

## Sol review checklist — steps 1–7

Implementation claims below and in the linked shader/design docs are based on targeted source
review. Only the earlier step-2 compile is confirmed; do not treat source presence as a passing
build, shader compile, test, runtime or visual result. `HeightSource`/`WarpSource` remain
serialized, disabled-by-default fields; they are not the ordered Height Push/Structural Warp
sockets. Runtime header comments still say consumers are implemented separately and need a
source-truth review; no code change is made here.

- [ ] **Sources/order:** direct completed signed Height or eligible Flow/UVMap; reject self,
  forward, disabled, invalid and wrong-kind sources/targets. Check cache demand and same-layer order.
- [ ] **Parameter trace:** Runtime defaults/serialization → gather/render data → GPU uniforms,
  dispatch and resource lifetime → shader use → inspector rows/enable state. Include reference
  Flow Amount/Trace Length/Steps and Height Push/Warp source, target and enable controls.
- [ ] **Separate modules:** Height Push changes Strata bedding; Structural Warp changes the
  structural coordinate map. Confirm authored order and non-commuting Push/Warp composition.
- [ ] **Jacobian:** verify row-gradient `g*J`, placement exactly once, `grad(B)` not transformed
  twice, non-square texel differences, masks, degeneracy and boundary validity.
- [ ] **Tile/identity:** preserve signed Height, invalid IDs/hash semantics, UV winding,
  ID-anchored per-region random, owner/phase-aware bed position, and destination mask/ID influence.
- [ ] **Single ownership:** no double warp of Height/Coverage; coverage aliases share one result;
  Noise Value/Gradient and each producer's named fields move under the same source revision.
- [ ] **Six producers:** Strata regeneration; Rock, Pebbles, Cracks, Cliff and Noise typed bundle
  policies. Verify Cliff identity inventory and Noise family-specific Gradient contract.
- [ ] **Known limits:** local centre/orientation inverse approximations, raster filtering/distance
  approximations and carried-over Strata defects remain. Record runtime tests still needed.
