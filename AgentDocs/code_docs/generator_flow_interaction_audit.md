# Mixtormat — Generator Flow Interaction (audit and plan)

Status: baseline static audit (2026-10-07), captured before the explicit generator-input,
Height Push, Structural Warp and typed bundle-warp work. Sections 1–5 and 7–9 describe that
baseline or historical proposals; do not read their "missing"/"not implemented" statements as
current status. Steps 1–7 are now described in `GENERATORS.md`, `SHADERS.md`, and the two
warp design docs, based on targeted source review only. No current build, shader compile,
Unreal/runtime or visual validation is confirmed. This remains a historical audit; source wins.

Scope: can one generator's flow tools (Shape Deform / Generator Flow / Flow Carve) deform
another generator's output, and what is missing to make that possible. Companion to
`flow_generation_core.md` (the tools themselves) and `GENERATOR_SHADER_IMPROVEMENT_PLAN.md`
§C1 (Strata redesign).

## 0. Plan recap (from docs)

- `flow_generation_core.md`: three tools, one shared solve (seed → jump flood → resolve →
  apply), scoped under a generator, per-item mask. Milestones: publish a generator distance
  field → Shape Deform → RK2 Generator Flow → Flow Carve.
- `heightmap_fixing_needs.md` §20/§23: flow ordering vs normalize was an open question. The
  code now runs flow on the native field before signed normalization
  (`MixtormatGpuGeneratorPasses.cpp` L2447–2453), matching the doc's proposed order.
- Doc status: "implemented, unvalidated in Unreal".

## 1. What works today

- Flow tools are effect types 9–11: `ShapeDeform`, `GeneratorFlow`, `FlowCarve`
  (`MixtormatEffect.h` L56–62); `MixtormatIsGeneratorFlowEffect` L146–150.
- Eligible owners: `MixtormatCanOwnGeneratorFlow` (`MixtormatGeneratorTypes.h` L34–40) =
  Strata Carver, Rock Formation, Pebbles, Cracks.
- Solve: `MixtormatGeneratorFlow.usf`, stages 0–8 — seed, jump flood, resolve/feather,
  apply (Shape Deform / RK2 trace / carve), previews 4–5, pack 6, smooth 7, trace-into-UV 8.
- Dispatch: `AddGeneratorFlowToolPasses` (`MixtormatGpuGeneratorPasses.cpp` L1288) runs each
  tool in authored order on the owner's native field, before the shared signed normalization
  (L2447–2453).
- Publish: `FlowDirection` (Flow kind + `FlowSmooth` + `Validity`) and `WarpedUV` (UVMap;
  not published by Flow Carve) at L1497–1505.
- Reference path: OutputReference child → gather (`MixtormatIdGather.cpp` L101–126; earlier
  layers only) → `AddOutputReferencePasses` (L2251–2322). Kind Flow traces stage 8 into
  `LayerCtx.ReferencedUV` (L2317); Kind UVMap copies the texture (L2270–2274).
- Consumers of `ReferencedUV`: `AddLayerInputPass` (L655–660) and `AddLayerCompositePass`
  (L1180–1185) only — they warp layer source sampling in `MixtormatComposite.usf`.
- Ordering/cycles: `ValidateDependency` (`MixtormatOutputReference.cpp` L116–133);
  prefix-cache demand tracking (`MixtormatGpuComposePipeline.cpp` L310–314, L688–691,
  L733–737, L1163–1168).

## 2. What is missing

- No generator reads `LayerCtx.ReferencedUV`; zero references in `AddGeneratorLayerPasses`
  or any generator dispatch.
- Strata Carver's height source is the raw composite below
  (`Ctx.OutputHeight[1-(LayerIndex&1)]`, L2425–2426), not the flow-warped
  `LayerCtx.LayerInputHeight`.
- `RemapGeneratorBundle` (L950–967) never remaps `Bundle.Height`.
- Bundle stage gaps for a generic warp: stage 0 bilinear blends BedPosition across bed seams;
  BedRandom is a per-bed constant and needs a nearest-scalar stage (only stage 1, IDs, is
  nearest).
- Strata mask/ID influence inputs are disconnected: `bHasScopedMask`/`bHasRegionIds`
  hardcoded false (L1040–1043); `MaskInfluence`/`IDInfluence` are dead paths
  (`MixtormatStrataCarver.usf` L387–397).
- Missing/incomplete reference sources fail silently (L2264–2265).
- Generic scalar/vector reference kinds (Scalar01, ScalarSigned, SDF, Vector2) have no
  destination consumer (L2290–2294).
- Confirmed remap path: after an active non-carve apply, `RemapGeneratorBundle`
  (L950–967, L1545–1564) remaps IDs, named masks, centre UV, orientation and boundary
  distances. Height and coverage are updated separately by the apply. The contrary
  masks/IDs/original-boundary comment at L1282–1284 is stale, not unresolved behavior.
- Pebbles coverage is moved/published, but does not gate shared height combine:
  `AddGeneratorModuleCombine` binds only RunningHeight/ModuleHeight (L788–812), and
  `MixtormatGeneratorBundle.usf` stage 9 adds them (L69–72).

## 3. Per-generator status

| Generator | Own flow tools | Generator-owned flow/UV outputs (excludes children) | Consumes another generator's flow |
|---|---|---|---|
| Strata Carver | Yes | No (publishes bed IDs/position/random; boundary field is internal) | No |
| Rock Formation | Yes | No | No |
| Pebbles | Yes | No | No |
| Cracks | Yes | No | No |
| Cliff Strata | No (menu omitted; gate excludes it) | No (internal `OutFlow` only) | No |
| Noise | No | No (publishes Value + Gradient; Gradient is Vector2, not Flow) | No |

Notes:

- "Own flow tools" = can host Shape Deform / Generator Flow / Flow Carve children
  (`MixtormatCanOwnGeneratorFlow`).
- FlowDirection/WarpedUV are published by the flow-tool children themselves (L1497–1505),
  not by the generators.
- Cliff Strata has internal directional shaping (`OutFlow` in `MixtormatCliffStrata.usf`)
  but is deliberately excluded from the flow gate until it publishes a valid signed
  boundary field (`Prototypes/CliffStrata/GENERATOR_AUDIT.md`).
- Noise publishes `Value`/`Gradient` outside its bundle (`MixtormatGpuNoisePasses.cpp`
  L261–266); a generic bundle warp would miss them.

## 4. Options

### A. Warp the completed output bundle (generator-agnostic)

1. After the module switch (~L2446), if `LayerCtx.ReferencedUV` and the module bundle
   exists, call `RemapGeneratorBundle(Ctx, Module, LayerCtx.ReferencedUV)`.
2. Add a height remap (`Bundle->Height = RemapBundleField(..., 0)`).
3. Add a nearest-scalar bundle stage for BedRandom and a seam-unwrapped variant for
   BedPosition.

Caveats: two new bundle stages; bed-position seams stay approximate; published Noise
Value/Gradient and any out-of-bundle fields are not covered.

### B. Warp the structural coordinates before generation (Strata)

1. Pass `LayerCtx.ReferencedUV` into `AddStrataCarverPasses` as new shader uniforms
   (`ReferencedUVEnabled`, `ReferencedUVField`); no new UPROPERTY.
2. In `MixtormatStrataCarver.usf`, apply a toroidal displacement before placement/bedding,
   folds and joints. Choose one coordinate contract: warp destination UV before
   `MixtormatGeneratorUV`, or transform displacement into generator space once; never both.
   Preserve the periodic integer bedding lattice.
3. Chain the flow Jacobian into `StrikeGradient`/`SGradient`, the bend terms, and
   Height Follow (including its source taps).
4. Keep mask/ID influence sampling at the unwarped destination UV (flow-tool semantics:
   the mask gates where the module acts).

Caveats: sampled flow and boundary distances remain approximations; "structurally exact"
is too strong.

Candidate direction: B for Strata Carver to move geological structure and derive aligned
outputs, subject to Jacobian/filtering correctness. It requires shader parameter declarations,
GPU bindings and dispatch changes, not just one shader edit. A is a completed-field option
for other generators, but needs typed output handling. Neither is approved or implemented;
layer-wide ReferencedUV is not an explicit per-generator input socket.

## 5. Inspector / authoring findings

- Copy path works: child context menu → Outputs (`MixtormatLayerMenus.cpp` L209–227,
  L263–272; `MixtormatLayerClipboard.cpp` L122–167).
- Capabilities (`MixtormatChildCapabilities.cpp` L237–283, L342–356): FlowDirection from
  all three modules; WarpedUV from Shape Deform and Generator Flow only (not Flow Carve).
- Reference rows expose Source / Go to Source only (`MixtormatInspectorIds.cpp`
  L2091–2144). `FlowAmount` (default 1, −4..4), `FlowTraceLength` (0.05, 0..1) and
  `FlowSteps` (16, 1..64) exist in Runtime (`MixtormatOutputReference.h` L54–63) but have
  no inspector rows.
- Flow/UV references require an earlier layer; same-owner references are rejected
  (`MixtormatLayerChildren.cpp` L431–471; `MixtormatParameterBinding.cpp` L1036–1062;
  `MixtormatOutputReference.cpp` L208–229).
- Enable checks disagree: `IsPublishedSourceEnabled` checks disabled IdGroup ancestors but
  not disabled Generator ancestors (`MixtormatLayerClipboard.cpp` L102–119, L316–318;
  `MixtormatLayerChildren.cpp` L411–426; `MixtormatLayerMenus.cpp` L852–865).
- `BuildUvIdControls` (`MixtormatInspectorIds.cpp` L1548–1619) has no ID-availability gate
  and no WarpedUV publisher; Strata `IDInfluence` defaults to 0 with no inspector row
  (`MixtormatGeneratorTypes.h` L149–153).

## 6. Documentation discrepancies (verified)

Fixed in this pass:

- `GENERATORS.md` L20–22 — Rock-only claim corrected to the four eligible owners.
- `flow_generation_core.md` — current owners/dispatch, confirmed remapping and ungated
  shared height combine corrected; original integration/milestone text marked historical.
- This audit — generator-owned vs child-owned outputs clarified; option B includes GPU
  bindings/dispatch and a single placement transform, not a shader-only change.
- Scope is these routed docs only; no repository-wide documentation consistency audit.

Still open (text inside code files, not edited):

- `MixtormatEffect.h` L55–57 comment says Rock-only.
- `MixtormatGpuGeneratorPasses.cpp` L1282–1284 claims undeformed masks/IDs/original boundary.
- Inspector tooltip "Generator layers support every kind" (`MixtormatInspectorGenerators.cpp`
  L39) and the paste hint (`MixtormatLayerClipboard.cpp` L401–405) are misleading —
  `CanAddGeneratorFlow` requires an eligible generator child (`MixtormatLayerChildren.cpp`
  L2207–2214).

## 7. Height push vs coordinate warp (terminology and proposal)

Existing, distinct mechanisms:

- Height Follow (Strata): shifts bedding with the upstream composite height
  (`MixtormatStrataCarver.usf` L249–263; inspector row `MixtormatInspectorGenerators.cpp`
  L276–278). Not a chosen module reference.
- Height Blend (generator-layer sublayer): combines signed relief
  (`MixtormatGeneratorHeightBlend.usf`).
- Flow Carve: cuts/raises height along the flow (`MixtormatGeneratorFlow.usf` stage 3).
- Breakup's Push / Push Relief / Push Width are unrelated
  (`MixtormatInspectorEffects.cpp` L754–760, L805–807).

Proposal (not implemented): keep height pushing and coordinate warping as separate,
independently ordered modules with an explicit source/target connection, instead of
folding deformation into height scaling. Warping must carry IDs/UVs with it (section 8).

## 8. IDs, UVs, gradients — what a warp must carry

- IDs: nearest-load, never interpolated (bundle stage 1 pattern).
- Bed position: seam-aware unwrap before differentiating (bundle stage 8 pattern).
- Bed random: per-bed constant; needs a nearest-scalar stage (does not exist yet).
- Gradients: chain the warp Jacobian (row-vector `mul(grad, J)` convention, matching
  `MixtormatComposite.usf`).
- Distances: rescale by the destination metric (bundle stage 2/7 pattern).
- Coverage, centre UV, orientation: remap with the same warp (bundle stages 5/6).
- Noise Value/Gradient: published outside the bundle; needs its own remap if Noise is
  ever a warp target.
- Constraints: keep tileable; preserve signed height, bed IDs, bed position, bed random,
  mask/ID influence.

## 9. Historical implementation plan (superseded)

The sequence below records the plan before steps 1–7 were implemented. It is not current
status or authorization. For current implementation evidence and Sol's review items, see
`GENERATORS.md` and `generator_warp_output_alignment_design.md`.

1. **Typed per-generator inputs and order.** Reuse typed reference identity/validation,
   adding explicit source/target connections rather than treating layer-wide ReferencedUV
   as a socket. Earlier layers and earlier same-layer generators only; reject self, forward
   and cyclic dependencies. Define whether each source is native or post-normalization
   signed height, and which ordered module revision its outputs represent. Track dependencies
   for cache invalidation; compose ordered warp maps, not layer-wide last-reference wins.
2. **Inspector and parameter contract.** Add kind-specific reference FlowAmount,
   FlowTraceLength and FlowSteps rows for the existing controls. For each new input/control,
   trace Runtime/defaults → gather/render data → GPU declaration/binding/dispatch → shader
   tags → inspector metadata/authoring. Expose source, target, stage and order; disable
   unavailable modes with a reason. Preserve existing flow-tool controls as distinct rows.
3. **Height Push, separate from Warp.** Define an independently ordered Height Push module
   with an explicit signed-height source and target. Strata's structural use shifts bedding,
   not Height Scale or blend; retain existing composite-below Height Follow behavior unless
   explicitly changed. Define target semantics for other generators before enabling them;
   never silently substitute height addition for structural pushing. Height Blend and Flow
   Carve remain separate operations. Restore Strata mask/ID bindings and its missing
   IDInfluence row without changing defaults.
4. **Warp across all six generators.** Strata: structural coordinate path before bedding,
   folds and joints, with one placement transform and chained Jacobians/footprints.
   Rock Formation, Pebbles and Cracks: typed completed-bundle path first.
   Cliff Strata: completed-bundle target without enabling unsupported flow ownership or
   treating internal OutFlow as a published Flow. Noise: include separately published
   Value/Gradient and any IDs. Gradient remains Vector2, not automatically Flow.
5. **Aligned typed outputs.** Warp signed Height, coverage, IDs, UV centres/orientation,
   named masks and boundary fields together. IDs/per-region random use discrete sampling;
   bed position uses seam-aware sampling tied to bed identity. Chain gradients/footprints
   through Jacobians and correct distance metrics. Preserve tiling, signed values, bed
   position/random and destination-space mask/ID influence. Declare distance sign/units
   and validity; do not promote approximate distances to exact SDFs.
6. **Validation, only with consent.** First static end-to-end parameter and ordering checks;
   then approved build/Unreal tests for all six targets, masks/IDs, source disablement,
   saved defaults, seam continuity, signed height, non-square filtering and cache updates.
   Structural warp does not by itself fix the carried-over seam defects or joint prominence.

Decision before implementation: approve the explicit input/module contract and Strata-first
slice. Do not introduce an interim layer-wide warp path that will later need replacing.

## 10. Requires Unreal (not verified statically)

- Stage-8 trace cost at 4K × Steps=16 per consuming layer.
- Prefix-cache invalidation when a generator newly depends on another layer's field.
- Visual size of the 1-texel bed-seam blend if option A is chosen.
- Known stage-1 `floor(frac(UV)*size)` out-of-range Load (`Docs/Audit-2026-10-02.md` M13)
  if IDs are remapped.
- Non-square outputs measure flow distance in UV, not texels (documented limitation,
  `flow_generation_core.md`).

## Appendix: carried-over Strata open items (from the earlier static review; not re-verified)

- Lamina contacts: visibility fades a nonzero contact height (−0.055); adjacent laminae
  can step.
- Fold hinges: derivative kinks feed PixelT → EdgeWidth; shoulder height can jump, not
  just slope.
- Shared bed seams: when PixelT > 1, UpperBlend < 1 at T = 0; seam continuity between beds
  can break.
- Joint footprint: `max(footprint(Q.x), footprint(Q.y))` underfilters oblique planes
  (needs ~sqrt(2)).
