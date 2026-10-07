# Mixtormat — Generator Flow Interaction (audit and plan)

Status: static audit, verified against source by direct reads (2026-10-07), plus one
read-only sub-audit of Runtime/Editor. No build, GPU capture, or Unreal session was run.
Line numbers are from this revision and may drift. This file records findings and a
proposed direction; it is not the canonical owner of current behavior — source wins.

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
- Comment vs code, unresolved statically: L1282–1284 says published masks/IDs stay
  undeformed, but `RemapGeneratorBundle` (L950–967) remaps `NamedMasks` and `RegionIds`
  after a non-carve apply (L1552–1560).

## 3. Per-generator status

| Generator | Own flow tools | Publishes flow/UV | Consumes another generator's flow |
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
2. In `MixtormatStrataCarver.usf`, warp the destination UV by the flow displacement
   (toroidal) before `MixtormatGeneratorUV`; transform the displacement through placement
   (`MixtormatGeneratorSourceVector`, `MixtormatGeneratorPlacement.ush` L24–30) so the
   integer bedding lattice stays periodic.
3. Chain the flow Jacobian into `StrikeGradient`/`SGradient`, the bend terms, and
   Height Follow (including its source taps).
4. Keep mask/ID influence sampling at the unwarped destination UV (flow-tool semantics:
   the mask gates where the module acts).

Caveats: sampled flow and boundary distances remain approximations; "structurally exact"
is too strong.

Recommendation: B for Strata Carver (one shader change, all four outputs stay mutually
consistent by construction); A remains the right tool for generators whose bundle is
already flow-native (Rock, Pebbles).

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
- `flow_generation_core.md` — status (owner list, dispatch name), owners bullet and
  limitations bullet corrected; the mask/ID/boundary remap claim is now marked disputed
  (see §2).

Still open (text inside code files, not edited):

- `MixtormatEffect.h` L55–57 comment says Rock-only.
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

## 9. Requires Unreal (not verified statically)

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
