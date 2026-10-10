# Mixtormat — Unified Sources, Geometry, Colorize, Color Ramp and Grade

**Consolidated architecture and minimal execution plan** — 2026-10-09. Audit only; no repository edits, tests or builds.

**Input reviews:** Sonnet's 91-file local snapshot audit, GLM's `AUDIT-COLOR-GRADE.md`, Grok's Colorize/Grade/composition discussion, and targeted read-only GitHub verification. **Latest verified committed main HEAD:** `d38dfaf3a6d81439421b2fa356643c8171265ab6` ([commit](https://github.com/hugobeyer/matlab/commit/d38dfaf3a6d81439421b2fa356643c8171265ab6)). GLM audited `eee6df1` before the newer shelf commit; Sonnet's manifest did not establish a commit, but the shelf changes it reported are now committed in `d38dfaf`. Uncommitted local changes beyond that commit remain unknown.

## 1. Unified verdict

**Keep one Sources collection, one GPU/CPU Color Ramp implementation, one layer-owned Colorize consumer, and a separate existing Grade filter.** Wrinkle and Curvature Pinch remain separate geometry operators and eventually publish explicitly described, completed scalar fields. Avoid a new rendering engine, a second ramp evaluator, a second masks system or a separate generator-owned Colorize implementation.

### Status by subsystem

| Area | Confirmed at d38dfaf | Not implemented / not established |
|---|---|---|
| Sources shelf | Document-backed `UMixtormatMaterial::Sources`, source rows, generator Add/Delete/Rename, inspector, save/load and undo | Field evaluation/publication, source-owned tools/masks, cross-source links, Paste as Instance, Influence Only; verify all paths at runtime |
| Structural authoring | Target-first Warp/Height Push pickers and `Choose source later`; Advanced/Add unconnected removed | Proposed source-first Ctrl/Cmd pair workflow |
| Color Ramp | `FMixtormatColorRamp` 8 stops, Constant/Linear/Smooth, CPU `Evaluate` + GPU `MixtormatEvalColorRamp`, 8-bit-sRGB presets converted to linear | Named shared ramp objects, links/invalid-ref repair, HDR stops, spline or OKLab |
| Generator Height Color Ramp | Height -> published Color; optional last valid Color to `LayerInputBC` when `bGeneratorAlbedo` | No explicit blending among old generator Color results; preserve as legacy |
| HSV From IDs | 8-color even-spacing palette, seeded per-ID selection and HSV jitter; invalid IDs remain untinted | Shared Color Ramp palette adapter |
| Grade | Per-layer incoming color adjustment, sequential up to 8, before layer blend | Grade v2 controls; HDR-correct neutral Levels |
| Wrinkle / Curvature Pinch | Prototype/plan only; shared curvature and structural warp machinery exists | Both operators, final fields, full synchronized bundle pullback for Pinch |

## 2. Confirmed defects and integration blockers

1. **Sources GUID integrity missing:** `Source/MixtormatRuntime/Private/MixtormatMaterial.cpp:225-245` `PostLoad()` validates and repairs Layers/Groups, but does not visit `Sources`. Need duplicate/invalid SourceId repair, root/owned-child ID stabilization, naming-independent addressing, and copy/paste GUID remaps.
2. **Sources lack owned children:** `Runtime/Public/MixtormatLayerTypes.h:326-349` has `SourceId`, `DisplayName`, `Child` only. Add an owned child collection for tools, scoped masks, Noise Gates without silently changing the existing generator payload/serialized field. Source row hierarchy is presentation, not layer composition.
3. **Existing output-reference address excludes shelf:** `Runtime/Public/MixtormatOutputReference.h:66-69` requires `SourceLayerId`+`SourceChildId`. Introduce an explicit backwards-compatible owner/source-kind variant (Layer vs Shelf), stable GUID, output name and kind; do not fake a LayerId or repurpose Structural Warp/Height Push meaning. Reuse one validator and cycle/availability classification for both target-first and later source-first authoring.
4. **No shelf evaluation yet:** Material owns `Sources`, editor exposes generator settings, but current compositor RequestCompose/field routing does not accept or evaluate shelf sources. Add non-compositing producer scheduling and typed published outputs, with no direct writes to Height, Normal, RAM, BaseColor or occupancy.
5. **Ramp HDR and invalid-value contract:** `Runtime/Private/MixtormatColorRampMath.cpp:12-25` `GetClamped()` restricts every RGBA stop to [0,1], `TArray::Sort` does not promise tie stability and >8 stops truncate after sorting. CPU `Evaluate` re-sanitizes per sample (`:28-31`) and does not specify nonfinite X behavior; validate CPU/GPU parity for NaN/Inf and equal-position knots.
6. **Legacy Height Ramp masking makes RGB black:** `Shaders/Private/MixtormatGeneratorHeightColorRamp.usf:29-32` computes `float4(ramp.rgb * gate, ramp.a)`. **Preserve the existing effect contract**; **do not copy it for new Colorize blending**, where gate zero must preserve prior albedo. The alpha channel remains in the published Color field; currently alpha is not the module's blend weight.
7. **Grade default Levels clamps HDR:** `Shaders/Private/MixtormatComposite.usf:845-888` always runs saturating input remap, even with input/output 0..1 identity. Also first clamps negatives. Add an identity-path bypass for neutral Levels; assess backwards-compatibility for previously authored HDR materials, and version behavior if strict visual preservation is required. `bGradeInvertMask` is implemented in the shader but reported absent from Inspector (`Runtime/Public/MixtormatEffect.h:798`, GPU compositor `:1146`).
8. **Grade scope, cap and ordering:** Shader uses incoming layer color, not fully accumulated material; source comment at `MixtormatEffect.h:732` is misleading. Up to eight Grade children; `MixtormatGpuCompositor.cpp:1735-1752` rejects a 9th during gather rather than silently dropping it. Grade masks act inside Grade, and the whole layer can be independently covered again at layer blending (`MixtormatComposite.usf:1194-1238`); this is **two distinct stages**, not automatically a coding defect. New Colorize must gate its own color contribution once, independently of the layer's final coverage.
9. **Available vs missing fields:** Current typed publications include generator Height, Noise Value/Gradient/FlowDirection and named producer fields. `MixtormatMaskCurvature.usf` outputs shaped mask coverage, not raw signed curvature. Erosion carve/deposit are computed internally and their typed publication is not established. Publishing a temporary field may require preservation across scheduling, a new output texture/UAV and cache handling, **not merely adding a descriptor**.
10. **ID color is a separate legacy palette evaluator:** `MixtormatComposite.usf:338-380` uses evenly spaced RGB palette interpolation and seeded `RegionId`, with invalid ID passthrough and subsequent HSV jitter. It is not a second implementation of `FMixtormatColorRamp`, because its authored data model is different. New shared-ramp ID mapping can call the existing ramp evaluator through an adapter, **without replacing or changing the serialized legacy palette path**.
11. **Cache invalidation:** `MixtormatGpuCompositor.cpp:1532-1559` hashes resolved layer content for prefix reuse, while `MixtormatComposeHash.cpp` hashes reflection arrays in order. A new naive hash of `Sources` would invalidate on reorder but could miss shared-ramp changes when only references are hashed. Compute per-source content revisions / canonical dependency keys; exclude cosmetic names, foldout state and shelf order from evaluated content identity. Include changed ramp data and all downstream consumers.

**GLM nuance correction:** smoothstep has zero derivatives on both sides of an interior stop; it is not a first-derivative (`C1`) discontinuity. It can create flat transitions at every knot and discontinuous higher derivatives. Spline is an optional quality extension, **not a blocker for MVP**. Scalar Constant knot CPU/GPU equality is a distinct fix (`MixtormatScalarRampMath.cpp`, `MixtormatGeneratorHeightModules.ush`).

## 3. Single source and color contract (proposed)

**Document-owned shelf:** Store reusable source generators, shared Color Ramp data and later Global Floats in an array of independent GUID-identified records. Shelf order is purely organizational; opening/closing the shelf never changes material evaluation. A generator source publishes typed completed fields only. Shared ramp is stops/interpolation (and ramp domain if explicitly authored as ramp space), **not** a texture or material channel contribution.

**References:** `{OwnerKind: Layer|Shelf, OwnerGuid, ChildGuid (where applicable), OutputName, PublishedFieldKind}`. Preserve legacy Layer references with append-only serialized fields / well-defined defaults, and maintain producer completion, owner and scope checks. Invalid links have explicit unresolved/repair state, not a guessed fallback. Source/target placement and instance identity must stay separate from shared authored parameter payload.

**Suggested minimal shelf data extension:** Keep `FMixtormatSourceEntry.Child` as the current root generator, append `OwnedChildren[]` containing scoped tools/masks by child GUID. Do not replace old serialized `Child` or create fake stack layers. On load and copy/paste repair both source and child IDs and check owner scopes. Evaluate producer-only generator passes without changing layer normals/albedo/composites; add placement-specific `Influence Only` for generators *inside material layers* separately.

**Colorize:** one *layer-owned* operation available on material/fill/generator layers and able to consume completed local or shelf fields. Shared/embedded ramp resolve to the same evaluator; source refs, normalization, range, projection, scope masks, gates, strength, blend mode, target are **consumer-local**.

- ScalarSigned: raw signed field, default input [-1,1]; Scalar01: raw [0,1]; SDF: raw signed distance with authored scale/units. Never infer current display/preview normalization as computation.
- Vector2/Flow: explicit projection (component, magnitude or `dot(v,dir(theta))`), frame and units; Flow validity controls unmapped contribution. No automatic direction-to-color guessing.
- RegionIds: separate deterministic integer-ID mapping `hash(ID, seed) -> t -> ramp`; invalid-ID sentinel preserves existing albedo. Integer IDs nearest-sampled under UV warp, never bilinear. Palette edits recolor regions deterministically but do not change ID membership.
- Out-of-range: Clamp by default, opt-in Wrap or Mirror; require finite, nondegenerate input range. Non-finite or missing input: no Colorize contribution, not forced min/max color. Keep authoring status visible.
- Colorize per-node weight `w=saturate(strength * scopedMask * NoiseGate * validity)`. Use exactly one consumer-local gate, and let the existing layer-placement blending apply separately downstream. Blend `C_next=lerp(C_previous, BlendMode(C_previous, Ramp(field)), w)` in authored child order. Amount=0/Mask=0 bit-identical bypass. Alpha remains authored data; any decision to use it as opacity must be explicit and separate.
- A Colorize may publish named `Color` only when needed as another typed output. One GPU implementation (dedicated pass or ordered ping-pong into prepared layer albedo) must support all layer types; **do not** duplicate a generator-stage and composite-stage Colorize subsystem.

**Agreed color order recommendation:**

```
Generator fields + legacy Height Color Ramp publication
  -> completed IDs, scoped masks and other source fields
  -> Layer input / legacy generator-selected color
  -> ordered Colorize contributions (before HSV)
  -> HSV From IDs and layer HSV
  -> sequential Grade filters
  -> existing layer blend with lower material
```

Why Colorize before HSV: the existing region variation remains visible after a strong Colorize blend; Grade adjusts the completed mapped color. Grok proposed Colorize after HSV; this is **a proposed ordering choice**, not an implemented constraint. Generator-owned extra Colorize is unnecessary; source-specific scalar selection lives in a generic field reference.

**Generator `bGeneratorAlbedo` rule:** preserve old last-published-Color selection. A new Colorize has its own explicit BaseColor destination/enable state and must not accidentally be suppressed by the old `GeneratorAlbedo==0` guard in `MixtormatComposite.usf:1345-1348`. Wire a distinct `HasExplicitColorizeContribution` gate / prepared-color input if needed. Decide whether generator Colorize is allowed to contribute with `bGeneratorAlbedo=false` (recommend yes when explicitly enabled), and define its base color when there is no legacy generator albedo (recommend previous composited color or explicit layer input, never unintended white).

**Geometry:** Wrinkle modifies signed Height at fixed pre-op snapshot and recomputes height-derived fields; Curvature Pinch modifies coordinate placement via bounded pullback and transports Height, Coverage, RegionIds and source-declared companion fields together, using nearest integer sampling and appropriate continuous sampling/derivative recomputation. Existing Structural Warp and Height Push remain distinct. Local tool first, external-source variant only after shelf reference/scheduler contracts. Raw curvature is a new publishable typed signed field if actually materialized and scheduled; Flow stable scalar extraction lives in Colorize.

## 4. Implementation sequence — one recommended path

| Phase | Ship in this order | Acceptance / limit |
|---|---|---|
| **A — Sources integrity (now)** | Repair/uniquify source and child GUIDs in PostLoad; add scoped owned children; define shelf source address and reference validation; align save/load/undo/paste semantics | No materials change visually; names/reorder/collapse cannot affect evaluation |
| **B — Functional Sources generators** | Shared producer evaluation, typed field publication, instance resolver, dependency DAG, cycle/disabled/missing-source handling, cache keys; layer-generator Influence Only | Shelf sources never alter material by themselves; source list order irrelevant; existing layers preserve behavior |
| **C — Ramp/Grade correctness (small independent track)** | Finite-safe ramp colors/HDR policy; stable duplicate knot ordering; CPU/GPU invalid and Constant-knot parity; Grade neutral Levels no-op; Grade invert-mask UI; faster preview eval | Old SDR assets unchanged; no old enum removals/parameter range changes; explicitly track behavior change for HDR |
| **D — Local Colorize MVP** | Shared GPU ramp helper in neutral include; layer-owned field->Colorize; explicit range/projection, mask/NoiseGate, order, blend, amount; optional Color publish; integrate before HSV and Grade | 0 strength or gate preserves color; two Colorize children visibly compose; proper invalid-source repair; no implicit last wins |
| **E — Shared ramps + ID adapter** | Source-owned ramp GUID, reuse same editor and GPU payload, local/shared link mode, transaction/undo/relink, dependency hash; optional ID-ramp with deterministic position + legacy fallback | Edit shared ramp updates all consumers; layer order and shelf display don't affect cached values; legacy IDs identical |
| **F — Wrinkle / Pinch** | Separate numerically bounded local operators; Warp bundle coherency, signed Height, derivative publication, then external references | No double sampling/mask; no cycles; zero amount exact identity; rectangular/wrapped UV parity; no broken IDs |
| **G — Optional enhancements after MVP** | Grade v2 Exposure, Temperature, Tint (Saturation only if justified); Color Ramp monotone Spline then optional OKLab; source Global Floats | Append-only enums, neutral defaults and existing Grade parameter semantics; no LUT for 8-stop ramps |

Phase A/B can proceed without Colorize; Phase C is independently low scope. Do not intertwine all seven phases in one agent patch.

## 5. Explicit decisions needing approval (with defaults recommended)

1. **New HDR ramp semantics:** allow finite HDR/negative stops through shared ramp math, preserve historical values, consider compatibility version if necessary. Existing 8-bit bake remains display-quantized.
2. **Source ownership:** append owned child array to current root `Child`; avoid restructuring/migrating the root unnecessarily.
3. **Address variant:** explicit `Layer|Shelf` owner kind + stable SourceId, not faux Layer GUIDs.
4. **Colorize scope/order:** one layer-owned operation; order `Colorize -> HSV -> Grade`; Blend Normal, Strength=1 initial defaults; zero gate preserves preceding albedo.
5. **Generator flag:** keep legacy `bGeneratorAlbedo` controlling existing Height Color Ramp, but allow an explicitly enabled new Colorize to contribute independently with a clearly defined base.
6. **ID linking:** preserve 8-entry legacy evenly-spaced RGB palette, add an opt-in shared-ramp mode with salted deterministic per-ID positions and invalid-ID bypass.
7. **Grade v2:** fix existing neutral/HDR behavior and expose invert first; add Exposure/Temperature/Tint later, preserving old serialized effects. Spline and OKLab are separate optional requests.

## 6. Key verified locations (committed HEAD)

- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h:326-349` — Sources entry holds one generator child.
- `Source/MixtormatRuntime/Public/MixtormatMaterial.h:101-106` — persisted Sources.
- `Source/MixtormatRuntime/Private/MixtormatMaterial.cpp:225-245` — PostLoad omits Sources repairs.
- `Source/MixtormatRuntime/Public/MixtormatOutputReference.h:66-69` — source address requires layer+child.
- `Source/MixtormatRuntime/Private/MixtormatColorRampMath.cpp:5-65` — clamp, sorting, CPU eval and payload.
- `Shaders/Private/MixtormatGeneratorHeightModules.ush:20-71` — scalar+color GPU ramp math.
- `Shaders/Private/MixtormatGeneratorHeightColorRamp.usf:23-32` — legacy gate behavior.
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp:2785-2831,2953-3001` — generator publication, optional albedo.
- `Shaders/Private/MixtormatComposite.usf:338-389,845-889,1074-1090,1194-1238,1345-1348` — IDs, Grade, layer color blend, generator gate.
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp:1117-1148,1532-1559,1735-1752` — Grade packing, prefix cache, 9th-grade error.
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp:1042-1080` — scoped feature mask and Grade queueing.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp:441+` — functional shelf list UI.
- `Source/MixtormatEditor/Private/UI/Layers/SMixtormatSourceRow.*` and `Widgets/Inspector/MixtormatInspectorSources.cpp` — shelf row + Inspector.

**Limitations:** Source verified through read-only GitHub connector; no direct local worktree comparison. Sonnet/GLM reports contain additional assertions about files not rechecked here; all proposed remedies are architectural recommendations, not implemented changes. Runtime visuals and compilation remain unverified.
