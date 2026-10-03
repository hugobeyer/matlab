Now let me read the core runtime model files that anchor all five sections.

Now let me ground myself on the core model before delegating.

Large surface (220 source files, ~60 shaders). I'll delegate the five sections to parallel agents with source-verification instructions, then verify their key claims myself.

Both sections returned strong results. Now the remaining three in parallel.

Let me verify the most load-bearing claims myself before writing the plan.

Verified `bSkipNormalization` is dead and the scoped filter is hard-coded. Let me check two more claims.

All claims verified against source. Here's the audit.

---

# Mixtormat Cleanup Audit — main branch

**Method:** source-only verification. Every `file:line` below was read directly. Where AgentDocs conflicts, source wins and is flagged.

**Three doc-vs-source conflicts found up front:**
- `AgentDocs/code_docs/surface_mask_rework_plan.md:83-84` claims `FMixtormatMaskShaping` was removed from the five producer structs and became a "Levels child". **Source contradicts both** — it is still embedded in all five (`MixtormatMaterial.h:486,617,1768,1865,2200`), and no Levels child type exists. Stale; do not design against it.
- `Docs/Audit-2026-10-02.md:225` (M23) flags sRGB white at `MixtormatGpuMaskPasses.cpp:527`. Current source uses `AddClearUAVPass(..., FVector4f(1.0f))`. Already fixed — stale.
- Three different documented stage orderings exist (`MaskShaping.h:8-9`, `MixtormatMaskOps.ush:76-85`, priorities doc). `MixtormatMaskOps.ush` is authoritative: **contrast+offset → balance → invert**.

---

## 1. Scoped / gated mask producers

**Current architecture.** `FGuid ScopeOwnerChildId` (`MixtormatMaterial.h:3356-3360`) is the only authoring field: invalid = normal ordered chain, valid = scoped beneath another child. The single type-pair rule is `CanKeepScopedPlacement` (`MixtormatLayerChildren.cpp:543-564`), reused by menus and drops so they cannot drift — **this is the right shape and should be extended, not replaced.**

**Confirmed: only `EMixtormatLayerChildType::Mask` can be scoped** under Effect or Generator. `CanOwnScopedMasks` (`:43-51`) admits Effect|Generator owners; but `CanKeepScopedPlacement:555` hard-codes `Child.Type == Mask`, `AssignScopedMaskToChild` (`MixtormatLayerActions.cpp:485`) constructs `Type = Mask`, and `CanPasteAsGatingMask` (`MixtormatLayerClipboard.cpp:456-459`) rejects anything else.

**Every site assuming scoped == Mask** (verified):

| Kind | Site |
|---|---|
| Selection filter | `MixtormatGpuMaskPasses.cpp:526-527` — `ScopedChild.Type != Mask → continue` |
| Union read | `MixtormatGpuMaskPasses.cpp:531` — `ScopedChild.Mask` |
| "do I have a gate?" ×5 | `GradePasses.cpp:21-26`, `BreakupPasses.cpp:198-201`, `ErosionPasses.cpp:84-87`, `LayerBlurPasses.cpp:65-68`, `GpuGeneratorPasses.cpp:71-73` — identical `ContainsByPredicate` |
| Skip guard | `MixtormatGpuMaskPasses.cpp:856-859` — only `AddTextureMaskPass` refuses scoped children; Generated/ColorId/Craquelure/RandomId do **not**, so a scoped instance would *also* write `CombinedMask` |
| Gather scope resolution | `MixtormatMaskGather.cpp:71-92` — inside the `Mask` branch only; `Generated` (`:162-197`) and `Craquelure` (`:199-330`) never read `ScopeOwnerChildId` and unconditionally set `bHasMask = true` |
| Layer mask flag | `MixtormatMaskGather.cpp:155-158` |
| Pattern Gap lookup | `MixtormatGpuPatternPasses.cpp:1003` |
| Editor | `LayerChildren.cpp:555`, `LayerClipboard.cpp:458`, `LayerActions.cpp:485` |

**Grade → Generated Mask: surface inputs are identical.** `MixtormatGeneratedMask.usf` binds `PreviousMask, SurfaceNormal, SurfaceRAM, SurfaceHeight, SurfaceRidge, OutputMask`; only `PreviousMask` comes from the accumulator. Everything else is layer-level ping-pong, independent of the mask chain. A scoped Mask child (`FMixtormatMaskCS`) simply never touches the surface slots. So the port needs exactly two changes: `PreviousMask = FeatureMask` and `Initialize = 0` unconditionally (top-level uses `MaskPassIndex == 0 ? 1u : 0u`).

**Two decisions, not mechanics** — flagging rather than silently copying:
- `SurfaceValid = LayerIndex > 0` (`MixtormatGeneratedMask.usf:63-67`) makes a scoped Generated Mask on **layer 0** a pass-through. Needs an explicit call.
- Craquelure is scopable for the mask half only (binds no surface inputs); its relief half reads `OutputHeight`/`OutputN` post-composite and must stay there.

**Already good, keep:** `AddScopedFeatureMask` never touches `MaskTargets`/`CombinedMask`; `bIndependentScope` (`MixtormatEffect.h:80-95`) is well-argued; the "no mask" vs "a white mask" distinction documented at `GpuGeneratorPasses.cpp:60-66`; `ResolveMaskSourceTexture:295-323` as the one mask-source choke point; serialization risk is zero (`ScopeOwnerChildId` untouched — this only opens paths already representable in the data model).

**Smallest boundary:** extract one `IsScopedGate(Child, OwnerIndex)` helper into `MixtormatGpuCompositorInternal.h` (kills 5 duplicate probes); turn the `!= Mask` filter into a `switch` dispatching to per-producer emitters inside `AddScopedFeatureMask` (4 of 5 producers already have a standalone `AddXxxPass` to wrap); hoist the gather scope-resolution block out of the `Mask` branch and guard `bHasMask`; add the `ScopeOwnerSourceChildIndex != INDEX_NONE` early-out to the four other `AddXxxPass` functions; one arm in `CanKeepScopedPlacement` + relax `CanPasteAsGatingMask`.

**"Mask producer evaluated into a destination accumulator":** the abstraction already exists at the shader level — every producer ends on `MixtormatBlendShapedMask(Previous, Incoming, BlendMode, Weight)` with the same `PreviousMask`/`Initialize` contract. What's missing is the C++ pass-emission side. Parameterize two accumulator-dependent bits (the surface-bind block, and the `Initialize`/`Weight==0` policy) so a pass can be handed a local destination texture instead of `MaskTargets`.

**Deps / risk / order:** interacts with #4 (both touch `FMaskRenderData` bind blocks — do not run concurrently). Risk **medium**: pass-emission semantics + the layer-0 decision. **After the ramp** — the ramp lands uniformly either way, and doing this first means it's added once to a closed set of emitters.

---

## 2. Shared ramp module

**Current architecture — and this is the strongest finding in the audit: the ramp's path has zero duplication.**

```
FMixtormatMaskShaping.h:25-62   (1 declaration)
   → 5 runtime payloads (MixtormatMaterial.h:486,617,1768,1865,2200)
   → 5 render payloads, all by inheritance (GpuCompositorInternal.h:259,289,535,561,706)
   → 5 gather sites, all WHOLE-STRUCT assignment (MaskGather.cpp:125,193,243; IdGather.cpp:264,507)
   → MIXTORMAT_MASK_SHAPING_PARAMETERS (GpuMaskShaping.h:11-17)
   → BindMaskShaping (GpuMaskShaping.h:21-30)
   → MixtormatShapeMaskInput (.ush:31-41)  ← the single funnel, 7 callers
   → MixtormatShapeMask (MaskOps.ush:69-86)
   → AddMaskShapingRows (InspectorMasks.cpp:1180-1212), 5 call sites
   → 0 lines in the hasher (ComposeHash.cpp is fully reflection-driven; FArrayProperty recurses at :127-135)
```

**A ramp is a 5-file change and every producer inherits it automatically.** The seam is already reserved in prose at `MixtormatMaskShaping.ush:38` — *"Future curve remapping belongs in the shared shaping helper, not individual producers."*

**Where it slots:** shader, between `MixtormatRemapMaskInput` and `MixtormatShapeMask` (`.ush:39`) — which yields exactly the target `raw → normalize → input levels → ramp → contrast/offset/balance/invert → blend`. Struct, between `InputMax` (h:39) and `bInvert` (h:43). The `MaskRawOutput` bypass at `.ush:33-37` correctly sits *before* the ramp, so both the single-pass and the normalize→shape two-pass routes pick it up with no per-producer work.

**Hard constraint the ramp inherits — the parameter system is scalar-only.** `EMixtormatParameterValueType` is `Float|Int|Bool|Enum|Invalid` (`MixtormatMaterial.h:251-261`) and `Invalid` addresses are *explicitly refused* by every typed read/write. `FMixtormatParameterUiResolution` (`MixtormatParameterUiMeta.h:23-30`) is float-only. **Ramp points will therefore be authored-only** — not drivable, not bindable, not instanceable per-field. Accept this deliberately.

**Fixed-array precedent to copy exactly:** `FMixtormatColorIdMask::MaxColors` declared once on the struct (`GpuMaskPasses.cpp:129`, comment: *"Deferred to the struct rather than restated, so the array here cannot drift"*), `SHADER_PARAMETER_ARRAY(..., [MaxColors])`, and the tail **explicitly zero-filled** (`:771-781`, *"A shader parameter array is not zero initialised"*). A ramp without a cap would blow the constant budget on 8 shader structs.

**Interpolation: monotone cubic Hermite (Fritsch–Carlson).** For masks specifically:
- plain cubic / natural spline **routinely overshoots outside 0..1** — and `MixtormatShapeMask` saturates *after* the ramp's insertion point, so overshoot silently hard-clips mid-shaping. That is precisely the artifact `MaskOps.ush:62-68` documents removing a stage to avoid.
- clamped Catmull-Rom still overshoots interior; the clamp is a band-aid.
- per-segment smoothstep is monotone but forces zero slope at *every* knot → visibly scalloped at 3+ points. Good as a cheap third mode, not as "Spline".
- Fritsch–Carlson guarantees the interpolant never leaves its segment's local min/max, and forbids non-monotonic segments — a down-then-up segment between two points the user dragged up would punch a hole in coverage.

Compute tangents **CPU-side at gather time**, not in the shader: it keeps the shader cheap *and* makes CPU/UI/GPU evaluation provably identical — which matters because the widget and the GPU must not disagree.

**Data/evaluator boundary (widget deliberately not designed):**
- `MixtormatScalarRamp.h`: `FMixtormatRampPoint {X,Y}`, `FMixtormatScalarRamp { bEnabled, TArray<FMixtormatRampPoint> Points, EMixtormatRampInterpolation }`, `static constexpr int32 MaxPoints`.
- Default must be the **identity ramp** `{(0,0),(1,1)}` + Linear, so existing assets composite bit-identically.
- Pure free functions: `EvaluateMixtormatScalarRamp(Ramp, X)` and `BuildMixtormatScalarRampParameters(Ramp, float4* Out, ...)`.
- Contract: **widget owns** hit-testing, LMB add/drag, RMB remove, undo — and nothing else. **Evaluator owns** ordering, dedup, endpoint protection, X clamping, monotonicity limiting, capacity cap, identity fallback. Widget draws by sampling the evaluator; it never reimplements interpolation.
- Widget connects at exactly two places: one row inside `AddMaskShapingRows` (between the Input pair and the Balance pair) and a `TFunction<FMixtormatScalarRamp*()>` derived from the existing `TFunction<FMixtormatMaskShaping*()> Resolve` already threaded there. **It must not** go through `MakeMemberSlider`/`MixtormatParameterUi` — that path will refuse an array.
- Endpoint protection: protect X *and* Y of index 0 and N-1. A ramp whose endpoints are movable cannot express "pass-through below 0 / above 1", and the `saturate` semantics elsewhere assume a full 0..1 domain. No strong reason otherwise.

**Existing gradient primitives are not a ramp.** `MixtormatGradientPainter` (`FStop {Position, Color}`, premultiplied sRGB, `<2 stops draws nothing`, 12 samples/span) and `SMixtormatGradientBox` are colour-only — no scalar, no control point, no interaction. `SMixtormatGradientBox` must not be touched. But `MixtormatGradient::Paint` **is** reusable as the curve *painter*: convert sampled scalars to grey `FLinearColor(v,v,v,1)` stops at 12/span. **Trap:** that painter samples in sRGB, and `MixtormatMaskShaping.h:52-53` is explicit that masks are stored raw — so a ramp preview drawn through it will not match the composite unless handled.

**Three unknowns, in priority order:**
1. **Nested `TArray` default initialization on existing serialized assets** — the identity default must materialize on every saved asset or masks composite wrong on first load. *Unverified; highest risk; falsify this first, before any shader work.*
2. **Register pressure** — adding `float4[8] + 3 uints` to a macro used by **8** shader structs, some already register-tight. *Unverified.*
3. **Colour-space display trap** above — cosmetic, but it will be filed as a bug.

**Deps:** constrains only #3 (the ramp's position *is* #3's deliverable). Untouched by #1 and #4. #5 unaffected provided published outputs stay scalar. Risk **medium**. **Do first** — everything else is cheaper once it exists.

---

## 3. Finish mask-pipeline modularization

**Current architecture — the pipeline is genuinely unified.** All six producers call both `MixtormatShapeMaskInput` and `MixtormatBlendShapedMask`: texture mask (`Mask.usf:74,117`), Generated (`GeneratedMask.usf:171-172`), Craquelure ×2 (`Craquelure.usf:110-111`, `CraquelureGrow.usf:516-517`), Color ID ×2 (`ColorId.usf:84-85,115-116`), Random ID (`RandomId.usf:63-64`). Normalization is shared via one generic GPU min/max reduction, `AddNormalizeFieldPasses`, invoked from exactly one place: `AddNormalizedMaskShapingPass`.

**Confirmed duplication — exactly one instance.** `MixtormatMask.usf:100-101` (`MergeCS`) inlines `MixtormatBlendShapedMask`. It can't call it today only because `FMixtormatMaskMergeCS` doesn't declare the shaping macro — and `MixtormatBlendShapedMask` reads `MaskRawOutput`, so binding it would drag in an unrelated uniform. Identical arithmetic. ~6 lines to fix.

**Confirmed dead code:** `bSkipNormalization` (`GpuMaskShaping.h:53`, guarded `:56`) — **no call site passes it**. Its comment (`:43-44`) describes a policy no producer uses; Generated Mask's `SurfaceValid==0` case is handled in-shader instead.

**Legitimate behavior — do not "fix":**
- `MixtormatGeneratedMask.usf:169` `MixtormatBias01` before the funnel. This is signal-mixing on the *raw* field (weighted sum of curvature/direction/AO/height/ridge), not shaping. `MaskShaping.h:38-39` anticipates it.
- `MaskCurvature.usf:193-202` own `Invert`/`Weight` and `RangeLow/High` — `FMixtormatMaskCurvature` does not embed shaping. Different node.
- `Mask.usf:9-13` `PreShapedMask`/`UsePreShaped` — the raw second pass, by design.
- Effect/generator-side `MixtormatApplyMaskOperation` uses (`Runoff.usf:687`, `Stain.usf:610`, `RampIdRelief.usf:119`) — no shaping at all.

**Ownership:** `MaskShaping.ush`, `GpuMaskShaping.h/.cpp` and the render payloads are **clean**. `MaskOps.ush` is **muddled** — it holds `MixtormatShapeMask` (only reachable via the funnel) *and* `MixtormatApplyMaskOperation` (used by effects with no shaping), so it serves two ownership claims; its `:6-12` header only describes the first. The `MaskShaping.usf` / `MaskShaping.ush` pair is a genuine naming hazard: same base name, and the `.usf` is a 3-line standalone compute pass that *consumes* the `.ush`. No comment in either file says so.

**Smallest boundary:** delete `bSkipNormalization`; add the macro to `FMixtormatMaskMergeCS` and use the shared blend (do this **before** the ramp so there is one blend site, not two); add one clarifying comment to `MaskShaping.usf`. Explicitly out of scope: `Bias01`, Curvature, effect-side blend uses.

**Deps / risk / order:** the ramp is a *blocker*, not a dependant — item 3's residual is pre-shaping. Touches `PublishedSource*` fields only in the sense that item 5 sits beside it. Risk **low**. **After the ramp**, but the MergeCS fix should land immediately after it.

---

## 4. Shared mask placement

**Current architecture.** Duplication is **entirely CPU-side** — the HLSL is already shared.

| Layer | Sites |
|---|---|
| Authored fields | `FMixtormatMaskLayer:490-514` and `FMixtormatColorIdMask:1843-1862` — 7 fields, byte-identical names/types/defaults/meta |
| Render payload | `FMaskRenderData:269-273`, `FColorIdRenderData:302-306` — identical |
| Gather copy | `MaskGather.cpp:118-123`, `IdGather.cpp:507-513` — identical text |
| Shader params | **6 copies** of the same 5-line block in `GpuMaskPasses.cpp` (decls `:22-32,60-70,141-149`; assigns `:371,550,788,899,952`) |
| Shader uniforms | `Mask.usf:29-33`, `ColorId.usf` |
| Inspector | `InspectorMasks.cpp:1084-1123` and `InspectorIds.cpp:632-661` — hand-written twice, different labels/tooltips, separate rotation menus |

All resolve through the **one** `MixtormatSourceUV` (`MixtormatUV.ush:123-142`), same gather clamp, same enum, same flip→rotate→tile→frac order. The only difference is the sampler (LinearWrap vs Point) — filtering, not placement.

**Genuinely identical: exactly two types** (`FMixtormatMaskLayer`, `FMixtormatColorIdMask`). **Must stay out, with reasons:**
- `FMixtormatGeneratedMask` — no placement fields at all; procedural field from the surface.
- `FMixtormatRandomIdMask` — hash per region ID; no texture, no UV.
- `FMixtormatCraquelure` — `Scale` is **cells per UV repeat**, consumed *inside* the periodic Voronoi solve (`Craquelure.usf:57`) before the warp resolves. Different pivot, order, units, wrap guarantee. Tiling it would rescale a lattice that is already periodic — not the same operation.
- `FMixtormatLayer` Adjustments — same transform, but fields differ (separate float `Tiling` × `UVScaleX/Y`) and it has its own `Placement` hash key (`LayerGather.cpp:127-140`) feeding generators. Folding it in changes the layer hash.
- `FMixtormatPatternFilter`/`FMixtormatUvIdFilter` UV blocks — per-region *random draws* around a solved centre (`Composite.usf:514-533`). Min/max pairs + random draws are not a placement transform.

**`MixtormatGeneratorPlacement.ush` is not reusable** — it wraps *global* generator uniforms, adds Jacobian/tangent conversions, and carries no `frac()` wrap. Its shared core is `MixtormatSourceUV`, which already lives in `MixtormatUV.ush` (7 consumers). **There is nothing left to factor in HLSL. Do not touch `MixtormatUV.ush`** — its `:23-27` comment records that the position path must stay non-matrix because a 1-ULP drift flips a region ID.

**CRITICAL — serialization hazard.** `FMixtormatMaskLayer`/`FMixtormatColorIdMask` are embedded **by value** in `FMixtormatLayerChild`. Moving `TilingX` into a nested struct changes the tagged path from `Mask.TilingX` to `Mask.Placement.TilingX`. **Unreal's tagged serializer does not nest-forward** — an existing asset would load the C++ default and silently lose every authored value. There is **no `CustomVersion`/`Serialize`/`PostLoad` migration anywhere in MixtormatRuntime** (the only `PostLoad`s are material ID/group validation and a surface provenance flag). So either ship a custom-version migration, or keep the 7 fields flat and share only the render payload + inspector rows. Three non-inspector sites write these fields directly (`LayerActions.cpp:570`, `LayerChildren.cpp:753`) plus two tests use `Mask.TilingX` as a probe value.

**Already good:** `MixtormatUV.ush`; `FMixtormatMaskShaping` + `BindMaskShaping` + the macro + `AddMaskShapingRows` — **this is the exact template to copy, and its header comment (`:12-16`) documents the very drift this refactor could re-create: "the ranges did drift, and ended up at 0-2, 0-16 and unclamped for the same parameter."** Also keep `UsesLayerValues()` + the inspector's `Visibility_Lambda` (hides placement for the one mode where it's inert) and the ColorId `ExactId` equivalent (`InspectorIds.cpp:520-524`).

**Latent inconsistency flagged:** "Layer Values Mask has no placement controls" is **UI-only** — the fields still exist, are still gathered unconditionally (`MaskGather.cpp:118-123`), and the shader applies them if ever set programmatically.

**Smallest boundary:** authored `FMixtormatSourcePlacement` + render `FMixtormatSourcePlacementRenderData` beside `MixtormatMaskShaping.h`, plus `MIXTORMAT_SOURCE_PLACEMENT_PARAMETERS`/`BindSourcePlacement` beside `BindMaskShaping`, plus one shared inspector row block taking a `TFunction<FMixtormatSourcePlacement*()>` — **two types only**, and only if the authored-struct half ships with a migration.

**Deps / risk / order:** must not run concurrently with #1 (both touch `AddScopedFeatureMask` bind blocks) — #4 first, then #1, since Generated/Craquelure/RandomId have no placement and generalizing scoping changes what "placement" means. **Risk medium-high, and the risk is 100% serialization, not shader behaviour** (shader behaviour is provably identical). **After the ramp** — the ramp's serialization decision is still open and touches the same structs.

---

## 5. Typed output publication

**Current architecture — 7 layers, all mapped:**

| # | Layer | Owner |
|---|---|---|
| 1 | Authored descriptors | `FMixtormatPublishedOutputDesc` / `GetChildCapabilities` — `MixtormatChildCapabilities.cpp:35-287` |
| 2 | Authored references | `FMixtormatOutputReference {RegionIds\|Flow\|UVMap}` — `MixtormatOutputReference.h:20-54` |
| 3 | Gather/render | `FOutputReferenceRenderData` — `GpuCompositorInternal.h:222-259` |
| 4 | Registries | `Ctx.PublishedMaskOutputs`, `Ctx.PublishedFieldOutputs`, `Ctx.PublishedFieldDemand` — `:1342-1344` |
| 5 | Preview routing | `FMixtormatChildPreviewOutputSet` → `FMixtormatChildPreviewTarget` — `SMixtormat_Preview.cpp:23-63` |
| 6 | Copy Output | `CopyChildOutput` — `MixtormatLayerClipboard.cpp:122-170` |
| 7 | Dependency validation | `ValidateDependency` — `MixtormatOutputReference.cpp:88-263` |

Layer 1 is genuinely single-sourced, and the `bPreviewable` / `bCopyableAsMask` / `bCopyableAsField` split is documented and load-bearing (the Breakup Gap case proves it). **This is the part that is working — leave it alone.**

**Parallel systems — confirmed, but they are not redundant.** Two registries share one key type: `using FPublishedFieldKey = FPublishedMaskKey;` (`GpuCompositorInternal.h:223`) — the intent to converge is *already recorded in code*. But `FPublishedField` carries a typed bundle (`FlowSmooth`/`Validity`/`bHashedIds`) with an `IsComplete()` format check (`FPublishedField::IsComplete()`, `:233-249`) that is meaningless for a scalar mask. **So: don't merge the registries.** They *are* the same job at the address/dispatch/validation layer though.

**The real asymmetry — three things the mask path lacks:**
1. **No gather-time dependency validation.** `MixtormatIdGather.cpp:101-120` calls `ValidateDependency` via `ResolveSource`. `MixtormatMaskGather.cpp:29-44` just does `IndexOfByPredicate` — no ordering, enable, or cycle check. A forward/disabled published source gathers fine and silently resolves to `Ctx.EmptyDriverSignal` (`GpuMaskPasses.cpp:320`). Validation exists only editor-side.
2. **No completeness contract.**
3. **No demand/cache participation** — `PublishedFieldDemand` has no mask counterpart; the mask path reads `PublishedMaskOutputs` unconditionally.

**RegionIds is the genuinely special case** — a third consumption path, `AddRegionIdReferencePass` (`GpuPatternPasses.cpp:733-789`), reading local `RegionIdMaps` directly with a `bHashedIds` override and only falling back to the registry cross-layer. That's *why* `ValidateDependency` is 175 lines with scope-completion ordering: RegionIds is the only kind readable within its own layer before the layer ends (`OutputReference.h:70` — *"Flow/UV retain their existing earlier-layer-only contract"*). Legitimate; do not flatten it.

**Four address representations, four hand-maintained remap blocks — the strongest consolidation argument:**
- `FMixtormatChildAddress` (editor: owner-type discriminator) vs `FMixtormatOutputReference` (runtime: no discriminator) vs `FPublishedMaskKey` (LayerId+int32+FName) vs `FMaskRenderData::PublishedSource*` (`:264-266`).
- Remap sites, all confirmed by hand: `MixtormatLayerGroups.cpp:287-297`, `MixtormatParameterBinding.cpp:665-673,742-752,1100-1105`, `MixtormatLayerClipboard.cpp:39-49`. **No shared helper exists.**
- Also duplicated: `ProducesRegionIds` listed in three places (`OutputReference.cpp:43-73`, `LayerChildren.cpp:330-340`, `GetChildCapabilities`) — but note this **crosses a module boundary** (Runtime cannot call editor capabilities), so consolidating it needs the capability table moved to Runtime or a deliberately-duplicated table. **That's a decision for you, not something I'd do unilaterally.**

**Height should NOT join the typed system.** Height isn't a per-child published output at all — it's a **layer-level accumulation** (`Layer.HeightSource` → uint mode at `LayerGather.cpp:238-254` → `HeightOp`/`HeightSource` shader params), and the cross-layer read is `HeightReferenceLayerIndex`, an **int32 layer index** requiring four dedicated remap functions in `SMixtormatInternal.h:191-302` — not a child address. Admitting it would add an enum value with no consumer using the address machinery. **Explicitly not proposing a migration, because it would not simplify anything.** (The one thing resembling it, `RockHeight`, is a preview scalar ramp in `PublishedMaskOutputs` — unrelated.)

**Stale/wrong:** `MixtormatLayerMenus.cpp:240` applies the label *"Copy Gate · {0}"* to **every** `bCopyableAsMask` output (Edge, Pieces, Wear, CrackMask, RockTop…) when only Breakup Gap/Edge/Pieces are gates. Cosmetic but user-visible wrong on ~15 outputs. **No dead compatibility paths found** — no legacy flag, no dead descriptor field.

**Tests:** `MixtormatChildOutputPreviewTests.cpp` is **healthy** — never names registry internals. `MixtormatCompositorTests.cpp:832-859` hand-writes `PublishedSource*` deliberately to test the mask path in isolation — **do not "modernise" it**; that would stop testing the mask path. But `MixtormatLayerGroupTests.cpp:182-195,383-399,409-438` assert the four remap sites **by field name** and will break under consolidation — that's the reason to do it with the tests in hand.

**Smallest boundary:** the four remap blocks only → one helper. Touches `MixtormatLayerGroups.cpp`, `MixtormatParameterBinding.cpp`, `MixtormatLayerClipboard.cpp` + three test cases. **Not** registries, **not** `ValidateDependency`, **not** `GetChildCapabilities`, **no shader changes**. The gather-time mask validation is a separate decision (behaviour change — previously-silent masks would start dropping).

**Deps / risk / order:** collides with #4 over the scope-unaware published-source index resolution at `MaskGather.cpp:37-41` (not scope-aware, unlike `ResolveBoundarySource` at `IdGather.cpp:31`) — **a latent divergence I flagged but did not fully chase.** Risk **low** for the remap helper. **After the ramp.**

---

# Ordered implementation plan

Ordering principle: the ramp first (smallest surface, unblocks everything), then the micro-cleanups that must land before the ramp's insertion point is finalized, then the two structural widenings, then the consolidation. #4 before #1 — they touch the same bind blocks, and widening scoping changes what "placement" means.

| # | Step | Item | Risk |
|---|---|---|---|
| **0** | **Falsify the nested-`TArray` default-on-existing-assets assumption** (load a saved material, read back an unset ramp). One check; if it fails, the whole ramp default model changes. Do this before writing any code. | 2 | — |
| **1** | Delete dead `bSkipNormalization`; add the `.usf`/`.ush` relationship comment. Zero behaviour change. | 3 | low |
| **2** | Close the one duplicated blend: declare `MIXTORMAT_MASK_SHAPING_PARAMETERS` on `FMixtormatMaskMergeCS`, replace `Mask.usf:100-101` with `MixtormatBlendShapedMask`. Land **before** the ramp so there is one blend site. | 3 | low |
| **3** | Add the `.usf`/`.ush` relationship comment; settle the `MixtormatMaskOps.ush` ownership split in comments (shaping-only vs shared blend). Comment-only. | 3 | low |
| **4** | **The ramp itself.** New `MixtormatScalarRamp.h` + evaluator; embed in `FMixtormatMaskShaping`; macro + `BindMaskShaping`; shader slot at `.ush:39`; one row in `AddMaskShapingRows`. Five files. **Widget explicitly out of scope.** | 2 | medium |
| **5** | Extract `IsScopedGate` / `GateWeight` helpers; replace the five duplicate `ContainsByPredicate` probes. | 1 | low |
| **6** | Placement: render payload + `BindSourcePlacement` + shared inspector rows for `FMixtormatMaskLayer` / `FMixtormatColorIdMask` **only**. Keep the 7 authored fields flat unless you also ship the custom-version migration. Two types, no others. | 4 | medium |
| **7** | Widen scoping: gather scope-resolution hoist, per-producer scoped emitters inside `AddScopedFeatureMask`, `ScopeOwnerSourceChildIndex != INDEX_NONE` guards on the other four `AddXxxPass`, one arm in `CanKeepScopedPlacement`, relaxed `CanPasteAsGatingMask`. **Resolve the layer-0 `SurfaceValid` question first** — it is a decision, not a port. | 1 | medium |
| **8** | Consolidate the four address remap blocks into one helper, updating the three `MixtormatLayerGroupTests.cpp` cases that name fields. | 5 | low |
| **9** | Optional/separate decisions — each its own change: gather-time mask dependency validation (behaviour change); `ProducesRegionIds` consolidation (needs a module-boundary call I won't make for you); the *"Copy Gate"* mislabel on ~15 outputs. | 5 | med |

**Two things worth saying plainly.** First, the ramp is dramatically cheaper than the framing suggests — five files, zero gather edits, zero render-payload edits, zero hasher edits, and every producer inherits it through a path that is already clean. Second, the two *structurally risky* items (6 and 7) are also the two where the risk is concentrated in a single identifiable mechanism: item 6's entire risk is the absence of any `CustomVersion` migration in MixtormatRuntime, and doing step 6's low-risk half (render payload + inspector rows) while leaving the authored fields flat would let you land most of the value at low risk.