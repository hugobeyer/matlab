# Ghost Params / Dupes Audit — targeted progress check

- Updated: 2026-10-24. Read `AGENTS.md` and `AgentDocs/COMPOSITION.md` first.
- Stable follow-up: driver source restrictions and missing authoring-key resolution checked.
  Moss and GPU-pass internals are excluded while their rewrites are in progress.
- Method: targeted static source reads/searches only; no commands, git, tests or diagnostics. Hard-excluded directories were not inspected.
- Scope: driver support/UI, retained fields, child-owner mapping, mask tiling metadata and selected Flow paths. This is not an exhaustive enum, reader or JSON audit.
- Evidence paths below are relative to the plugin root; `Runtime/`, `Shaders/` and `Editor/` shorthand under Source means `Source/MixtormatRuntime/`, `Source/MixtormatShaders/` and `Source/MixtormatEditor/`. Shader files use `Shaders/Private/` explicitly where stated.

## DRIVERS — confirmed status / remaining decisions

- **ChildMask: guarded, not implemented.** The enum remains (`Runtime/Public/MixtormatParameterTypes.h:95`), but `SourceSupported` rejects child-mask snapshots with an explicit error (`Runtime/Private/MixtormatParameterDrivers.cpp:52-74`). The menu calls that check and disables unsupported entries (`Editor/Private/Widgets/SMixtormat_Parameters.cpp:1820-1851`); `SetDriverSource` also rejects them (`1716-1719`). The earlier claim that normal UI authoring silently no-ops is incorrect. Owner decision (2026-10-23): defer ChildMask drivers for future work, retain the contract and disabled UI, and exclude implementation from v1 scope. No removal approved.
- **IdMapping: unresolved contract cleanup.** `IdMapping`, `SpecificId`, `IdRangeMin/Max` remain editable runtime fields (`Runtime/Public/MixtormatParameterTypes.h:240-253`). The inspected output menu offers only Random Per ID and writes both `IdMapping` and `SourceOutput` (`Editor/Private/Widgets/SMixtormat_Parameters.cpp:1879-1892`). Targeted Runtime/Shaders searches found no readers of those mapping fields; this is not proof against every reflective access. No Specific ID/ID Range choice exists in this inspected menu. Decide implementation versus approved removal after checking reflection/serialization consumers.

## PROPOSAL — normalized per-ID controls (2026-10-24)

- **Status:** owner-requested design note, not implemented or committed to v1.
  This does not approve removal of `IdMapping` fields or change the audit findings above.
- Prefer artist-facing normalized weights over typing Specific ID / ID Range numbers.
  Start from one stable seeded random value in 0–1 per valid region ID, not the
  numeric ID divided by the current region count. Keep the signal constant within each region.
- **Scalar controls:** remap the signal to an output min/max range, shape it with a
  scalar ramp, and use it as a value or multiplier on a supported parameter.
  Example: vary roughness per brick without editing individual brick IDs.
- **Color controls:** map the same signal through a color ramp for per-region color
  variation where color driving is supported. Color and scalar outputs need distinct consumers.
- **Range mask:** separately select an interval of the random signal, not an interval
  of numeric IDs. Selecting 0–0.3 includes roughly 30% of regions statistically,
  not necessarily 30% of surface area or exactly 30% of a small region set.
- Adjusting ranges or ramps must not reshuffle random values for unchanged IDs and seed.
  Changing the seed chooses another distribution; topology/ID changes may change assignments.
- **Height safety:** soft random thresholds soften activation while dragging, but do
  not smooth spatial borders between regions. Spatial edge feathering is a separate
  concern. For masked height effects, blend the effect delta onto the existing substrate
  rather than fading the substrate toward zero. Preserve intentional piece tilt/slip
  and discontinuities; do not impose universal smoothing on Cracks or Pebbles.
- **Before implementation:** inspect existing Random Per ID, driver remapping/multiply,
  ID masks and shared scalar/color ramps; reuse their canonical mechanisms where suitable.
  Establish supported parameter readers, source scope, invalid-region behavior and preview
  feedback before extending the contract. Do not assume every parameter supports spatial driving.
- Manual per-ID weight tables, visual region picking and generator subdivision are outside
  this proposal. Existing features/fields remain untouched pending a separate scope decision.

## DEFERRED — Child Mask parameter drivers

- Owner hold: future work only; preserve documentation, existing code and disabled source choice.
- Purpose: use an individual child's mask to modulate supported parameters per pixel.
  This is separate from group controls and ordinary module mask gates.
- Existing driver math supports remapping and combining signals with parameter values
  (`Shaders/Private/MixtormatDriver.ush`, `MixtormatApplyDriver`). Multiply can act as a gate.
- Missing work: provide child-mask snapshots and bind them to supported GPU parameter readers.
- Before implementation: define source ordering/scope, reject self/forward dependencies,
  and establish snapshot lifetime and invalidation. Not every float has a spatial reader.
- Revisit only when requested with a concrete use case; no new implementation in this update.

## DONE / RETAINED — confirmed declaration state

- **Done, scoped confirmation:** `HeightPush` / `StructuralWarp` are absent as enum entries in `Runtime/Public/MixtormatLayerTypes.h`; remaining mentions are comments. `Behavior=24`, `Boolean=25`, `Surfacing=26` are pinned (`129-136`). A targeted Editor `*Recipe*` search found no old names; this does not establish repository-wide removal.
- **Retained:** six deprecated stain asset defaults remain without `EditAnywhere` (`Runtime/Public/MixtormatEffect.h:114-139`): Color, Roughness, HeightInfluence, HeightWarp, HeightBias and HeightContrast. Comments describe them as unread and retained for asset loading; end-to-end reader absence was not re-audited.
- **Retained:** `RampShape` remains a deprecated serialized property; its message points to `LedgeWidth` (`Runtime/Public/MixtormatGeneratorTypes.h:120-122`).
- **Retained, comment-sourced retirement:** `RockHeightMode` (`308-311`), color `OutputName` (`1256-1260`), and generator `HeightBlend` (`1852-1856`) remain in that header. Do not confuse the retired generator member with live height-blend modules.
- These compatibility comments do not override the unshipped-development rule. Removal still needs approval and a targeted consumer check; no fields were removed in this review.

## PREVIOUS AUDIT NOTES — not revalidated in this progress check

The following are historical symbol-trace findings, not current exhaustive coverage or runtime validation. Counts and line references may be stale.

- NoiseV2 is a live helper lib (`Shaders/Private/MixtormatNoiseV2.ush`), consumed by `MixtormatNoise.usf`. Not a dupe.
- `CombineId`: readers in `Compositing/MixtormatGeneratorGather.cpp:1242`, `Runtime/Private/MixtormatOutputReference.cpp:317,507`, `Runtime/Private/MixtormatParameterBinding.cpp:85,224,669`.
- `ProceduralType` (authoring) → `Type` (render) is one pipeline (`Shaders/Private/MixtormatGpuCompositor.cpp:2134`).
- `EdgeWear`: live (`Shaders/Private/Effects/MixtormatWornEdgesPasses.cpp`, `Shaders/Private/MixtormatEdgeWear.usf`).
- SSS/Fuzz, ViewScale/CameraPitch/Quarter, TilingX/Y, TipRoundness/CrackSlip/CrackTilt: struct + gather + inspector all present.
- Coverage: generators 11/11, effects 8/8, MaskSource 3/3, ValueType 7/7, CombineMode applied GPU-side numerically (`Shaders/Private/MixtormatGpuGeneratorPasses.cpp:1478,1946,2024,2155`), RefMode Link/Follow consumed (`Runtime/Private/MixtormatGroupControls.cpp:133`, `MixtormatParameterBinding.cpp:1549`), `MixtormatShaderParamScanner::Scan` wired (`Widgets/SMixtormat_Parameters.cpp:987`).
- Stain/erosion/relief roughness+AO all live end-to-end (Stain.usf:112/248, CarveShade.usf:34, ReliefIdPasses, SurfaceIds.usf); the 6 stain asset fields are already out of the inspector (no EditAnywhere).
- No header dupes: `MixtormatGroupControls.h`, `MixtormatGroupParameterTypes.h`, `MixtormatMask.h`, `MixtormatSurface.h`, `MixtormatScalarRemap.h` all referenced.
- `EMixtormatNoiseType` fwd-decl in `MixtormatOutputReference.h:11` is legal, not a dupe.
- The previous audit reported no other `DEPRECATED` markers. No repository-wide absence claim is made here.

## OPEN QUESTIONS / CORRECTED FINDINGS

- **OutputReference owner mapping remains open.** `ChildTypeMatchesOwner` still has no OutputReference case (`Runtime/Private/MixtormatParameterBinding.cpp:63-108`). However, the child DOES carry `FMixtormatOutputReference OutputReference` (`Runtime/Public/MixtormatLayerTypes.h:276-277`); the former “no payload” rationale is unsupported. Decide whether generic parameter bindings should address this payload or intentionally exclude it; mapping absence alone does not prove a broken reference feature.
- **Surfacing premise corrected.** `EMixtormatSurfacingType` now contains Skin, Moss and Rust (`Runtime/Public/MixtormatSurfacingTypes.h:13-19`). Owner matching accepts non-Rust for Surfacing and Rust for the Rust owner (`Runtime/Private/MixtormatParameterBinding.cpp:96-99`). The old Skin-only/vacuous-gate question is obsolete; this is not a full surfacing binding audit.
- **Mask.TilingY metadata asymmetry confirmed; intent open.** `Config/MixtormatParameterAuthoring.json:535` contains `Mask.TilingX:Int`; targeted search found no `Mask.TilingY` key. Y is not a ghost: it defaults to 1 (`Runtime/Public/MixtormatMaskTypes.h:147-148`), is exposed alongside X (`Editor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp:1658-1665`), and reaches `MaskData.Tiling` (`Shaders/Private/Compositing/MixtormatMaskGather.cpp:142-145`). Missing-default resolution is now traced: `MixtormatParameterAuthoring::ResolveAuthoringDefault` delegates to `MixtormatParameterUi::ResolveUiDefault`, which returns the reflected property default when found, otherwise the caller's fallback. Missing UI bounds delegate to `ResolveUiBound`. Thus a missing JSON entry alone does not prove a broken parameter; whether Y needs an explicit authoring override remains an owner decision.
- **Flow distinctions partly resolved, not an exhaustive dupe finding.** NativeFlow produces/publishes a FlowDirection field from signed height/boundary inputs, using `MixtormatNativeFlow.usf` (`Shaders/Private/MixtormatGpuNativeFlowPasses.cpp:48-108`, Source module). FlowWarp instead warps layer input channels or owned masks using `MixtormatFlowWarp.usf` (`Shaders/Private/Effects/MixtormatFlowWarpPasses.cpp:171-268`, Source module). Behavior FlowField is accepted by gather (`Shaders/Private/Compositing/MixtormatGeneratorGather.cpp:548-557`) and recognized as a Flow-feather parent (`Shaders/Private/MixtormatGpuGeneratorPasses.cpp:2702-2706`); its full producer path remains to trace. Root `Shaders/Private/MixtormatFlow.usf` exists, but a targeted Source shader-module `.cpp` search found no exact filename registration. Its include/dispatch relationship remains open; do not label it dead or duplicate from that result.

## PRIORITIZED NEXT STEPS

1. **P1 — driver contract:** decide IdMapping support/removal, informed by the normalized per-ID controls proposal above; the proposal is not a v1 commitment or removal approval. ChildMask is deferred, not an active v1 task. Trace reflective consumers before removing fields.
2. **P2 — ownership:** establish whether OutputReference payload parameters belong in generic bindings; document intent before changing the owner map.
3. **P2 — authoring metadata:** missing-key handling is traced; decide whether Mask.TilingY needs an explicit JSON override for parity with X.
4. **P2 — Flow:** finish the Behavior FlowField producer trace and follow MixtormatFlow.usf includes/dispatch before proposing consolidation or deletion.
5. **P3 — approved retirement:** remove retained stain/generator fields only after consumer checks and approval; no historical-asset migration scaffolding.
6. **P3 — broader coverage:** a full JSON-to-struct/reader audit remains outstanding. Historical coverage counts above are not a completion criterion; any automated work needs separate permission.
