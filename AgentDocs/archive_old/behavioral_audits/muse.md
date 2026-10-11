Audit-only — mapping the current deformation/field/GPU systems to pressure-test the Behavior V2 proposal.

# Mixtormat — Behavior System V2 Architecture Audit (read-only, no changes made)

> Scope: `main` as present locally at `C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat`. I did **not** fetch/pull, run builds/tests/Unreal, or modify anything per AGENTS.md §5 + plan-mode constraint. All assertions below are from static reads by three parallel explore passes. Section 4 marks **Confirmed** (file:line read) vs **Inferred** (docs/comments/headers only).

---

## 1. Executive verdict

**Yes to V2 direction, no to the proposal as drawn.** The fragmentation is real and the nested `Generator > Behavior > Field > Mask` ownership is the correct fix — but the proposed tree in the brief is simultaneously **too deep** (Field-under-Behavior-under-Generator as authored `FMixtormatLayerChild` nesting) and **too vague** (untyped "Flow", no conversion rules, no snapshot semantics, no order/caching contract).

What I would change:

1. **Two levels, not three.** Authored nesting should be `Generator > Behavior` (scoped child, existing `ScopeOwnerChildId` mechanism) + `Behavior > Field-slot reference`, **not** `Field` as a full child type. Fields are already references (`FMixtormatOutputReference`), not children — making them children explodes `EMixtormatLayerChildType`, scope-depth, drag/drop, clipboard, and gather for no execution benefit. Masks already scope under owners via `CanOwnScopedMasks`; reuse that for `Behavior`-owned masks.
2. **Typed field registry first, Behaviors second.** The highest-value slice is: `EMixtormatPublishedFieldKind` (already exists: `MixtormatOutputReference.h:20-36`) + explicit conversions + snapshot discipline + bundle remap validation (`MixtormatGpuGeneratorPasses.cpp:1100-1171`). Without this, Warp/Fold/Push/Wrinkle will each re-invent ScalarSigned-vs-SDF-vs-Flow-vs-UVMap handling — which is exactly today's bug farm (`GeneratorGather.cpp:38-41` kind gates).
3. **Keep the effect-vs-generator split, unify the *executor*.** Generator-owned flow tools (`ShapeDeform/GeneratorFlow/FlowCarve/GravityFlow`, `MixtormatEffect.h:28-63`) and layer-level structural modules (`HeightPush/StructuralWarp`) must converge on **one** dispatch path (`AddGeneratorFlowToolPasses` stages 0/1/2/7/3 + `RemapGeneratorBundle`), not two. Do not rewrite shelves, binding, groups, or clipboard — they already work and are the compatibility load-bearing walls.
4. **Wrinkle = Behavior consuming composed fields, not a field.** The two OpenCL prototypes are mesh-iteration algorithms (Laplacian/curvature diffusion + fold-side masked displacement); they map to 2–3 RDG passes over height/normal/curvature textures, reusing existing `FieldRange/normalize`, `HeightDerivedNormal (Strength=8.0)`, Bartlett smooth, and FlowCarve-style masked displace. New work is ~1 Behavior + 1–2 field producers (curvature, fold-mask), not a new system.

**Smallest complete V2 slice (§10 detail):** one new scoped `Behavior: Warp` under one generator family (Rock Formation) + two field slots (Flow-or-UVMap source, Mask gate) + snapshot-in/snapshot-out + GPU execution through the existing flow-tool stages + inspector + real nesting in hierarchy (no projection). That proves ownership, typed composition, and GPU execution without touching the other five families or Wrinkle.

---

## 2. Current architecture map (actual, not proposed)

**Data ownership (authored truth):**
- `FMixtormatLayerChild` (`MixtormatLayerTypes.h:168-279`): `ChildId`, `ScopeOwnerChildId` (invalid = chain, valid = scoped under same-layer child), `SourceLayerId/SourceChildId` (instance), `ParameterBindings`, `Type`, one payload per kind. Only nesting primitive in the system.
- Allowed scope pairs (`Widgets/Layers/MixtormatLayerChildren.cpp:693-714`): GeneratorFlow-effect under capable Generator; Mask under capable owner; Blur/Curvature under Mask; FlowWarp-effect under Mask|Surface-effect. **No Generator>Behavior>Field chain exists.** Depth capped by `MaximumScopeDepth` (`:620-624`, `GetScopeDepth :152-183`, drag check `MixtormatLayerDragDrop.cpp:990-999`).
- `FMixtormatGenerator` (`MixtormatGeneratorTypes.h:955-1007`): wrapper + `Type` discriminator + `HeightSource` (default ScalarSigned/"Height") + `WarpSource` (default Flow/"FlowDirection") + one payload per family + serialized-only deprecated `HeightBlend`. Rationale documented `:941-953`.
- Shelf: `FMixtormatSourceEntry` = standard unscoped generator child + future `OwnedChildren`; never composites; order organisational only (`UI.md:65-66`, `SMixtormatSourcesShelf.h:1-53`).

**GPU execution (render thread, RDG):**
- Order: shelf producers first (uncached, `MixtormatGpuComposePipeline.cpp:844-863`) → per-layer `AddOutputReferencePasses` + `AddRegionProducerPasses` (`:887-889`) → `AddLayerInputPass` (`:893-896`) → `AddGeneratorLayerPasses` (`:898-903`) → bundle→`LayerInputHeight` (`:904-908`) → region/UV IDs (`:911-913`) → generator normal rebuild (`ZeroHeight` + `AddHeightDerivedNormalPass Strength=8.0`, `:928-947`, constant `Internal.h:185`) → child mask/effect loop **skipping** already-run Generator/Height-modules/flow-tools (`:1006-1075`).
- Generator position canon: `Layer source height → AddLayerInputPass → AddLayerHeightSmoothPasses → AddGeneratorLayerPasses → masks/effects/composite` (`MixtormatGpuGeneratorPasses.cpp:16-35`).
- Structural Push/Warp execute as **unscoped layer-level ops** ordered source-before-module-before-target, but *display* as nested via `MixtormatStructuralConnectionProjection.cpp:87-379` (display-only; tooltip: "Authored execution position: N. Visual nesting does not change order or ownership." `MixtormatStructuralConnections.cpp:284-286`). Pass 1 authored scopes, Pass 2 promotes only `HeightPush|StructuralWarp` (`IsModule :37-41`) to `IncomingConnection` under strict contiguity/unique-mapping gates (`:245-340`), Pass 3 emits (`:343-377`). No write-back to `ScopeOwnerChildId`.
- Lifetimes: all flow/generator intermediates RDG-transient within one `EnqueueCompose` (`CreateTexture :1594-1598,1728-1775`); cross-composite persistence only via `FMixtormatNodeCache` (Rock 10 slots + 3 gates `:1932-1996`; Cracks 7 `:2223-2255`), craquelure network cache (512MB LRU, `Internal.h:57-181`), prefix snapshots (`SavePrefixSnapshot :140-169`, resume `:749-842`).

---

## 3. Inventory: every field/flow/deformation feature

| Feature | Where (confirmed) | I/O contract | Status / verdict |
|---|---|---|---|
| Structural Warp | `Shaders/Private/MixtormatGeneratorStructuralWarp.usf:30-54`, `SHADERS.md:81-101`, gather `GeneratorGather.cpp:381-385,439-445` | `Mask`-gated `D_new=d+sample(D_old,Psi)`, `B_new=sample(B_old,Psi)`; Flow stage-8 trace vs UVMap periodic lift | **Adapt** → canonical Behavior-Warp executor; keep `.usf` core |
| Height Push | `FMixtormatGeneratorHeightPushCS → HeightPush.usf::MainCS (:682-705 decl)`, gather `:362-373` | `Amount, HasPrevious/HasMask, SourceHeight/PreviousShift/ScopedMask → OutShift` (body not re-read = inferred shift-accumulate) | **Adapt** → Behavior-Push (height/boundary/bedding/directional variants share executor) |
| Height Blend / Curve / ColorRamp | Single `MainCS` each → new `R32F` (color → `FloatRGBA`), `GpuGeneratorPasses.cpp:755-847,953-1047`; gather `:470-481,520-531` earlier-child-only | Running-height transform | **Retain separately** — not Behaviors; keep as height-module utils |
| Generator HeightSource/WarpSource | `GeneratorTypes.h:974-982`, gather kind gate `:38-41` (Height=ScalarSigned; Warp=Flow‖UVMap) | Reference, not child | **Reuse** → V2 field-slot type (this *is* the field model seed) |
| Generator Flow / Gravity Flow / Flow Carve / ShapeDeform | `MixtormatGeneratorFlow.usf` stages 0/1/2/7/3/8/6/4/5; `AddGeneratorFlowToolPasses :1573-1875`; `MixtormatEffect.h:66-82` (flow source, carve mode) | Seed→Jump→Resolve(9 gravity)→Smooth→Apply; carve keeps IDs in place; ShapeDeform single displaced sample + Bulge | **Reuse executor** → Behaviors Warp/Push/Carve/Deposit/ShapeDeform are modes, not new passes |
| FlowWarp effect | `MixtormatEffect.h:28-63` (type 6), scope `CanOwnFlowWarp :56-62` | Under Mask\|Surface-effect | **Retain separately** (effect-domain warp) |
| Breakup Fold/FoldWidth | `FEffectRenderData.BreakupFold/FoldWidth (Internal.h:494-495)` | Effect-side only; no generator Fold tool found | **Replace** with Behavior-Fold (new); keep Breakup effect intact |
| Existing curl/noise deform | Via Noise generator payloads (`EMixtormatNoiseType :857-868`) + mask Noise gate (`AddNoiseMaskPass`) | Signed lattice vs 0..1 magnitudes | **Reuse** as field producers (Curl, Procedural, Phasor new; lattice infra exists) |
| Heightfield warp/displace | `RemapGeneratorBundle :1100-1171`, bundle stages 10/11 ID-anchored/bed-T (`SHADERS.md:92-99`) | `FGeneratorBundle` (`Internal.h:1124-1189`): Height R32F, Coverage, RegionIds UINT, Boundary G32R32F neg-inside, CentreUV, Orientation + `EFieldSemantic/EFieldUnits` | **Reuse** → V2 typed bundle *is* the field store |
| Masks/fields/vectors | Kinds `OutputReference.h:20-36`; storage `Internal.h:233-284` (table in subagent report) | Exact-format `IsComplete :271-283` | **Reuse** — do not redefine kinds |
| Sources shelf | `COMPOSITION.md:20-55`, `GENERATORS.md:117-131` | Only ScalarSigned/Flow/UVMap consumable; shelf→shelf only; demanded producers in dependency order | **Retain** as external-source tier; local field instances new |
| Published refs / Source-Instance | `MixtormatOutputReferences :99-281`, `ResolveChildInstances :1090-1146`, `ClassifyInstancePlacement :1148-1258` | Cycle-guarded chains; mask instances keep Blend/Invert | **Reuse** — V2 Source/Instance rides this |
| Follow/Link/Driver | `MixtormatParameterTypes.h:8-48,50-91,93-245`, `MixtormatParameterBinding.cpp:42-61,104-216,455-541,1380-1394` | Typed compat, cycle guard, instances-first apply | **Reuse untouched** |
| Group expansion | `FMixtormatLayerGroup :293-324`, `BuildEffectiveLayers` (broadcast, no write-back) | Render-only `bEnabled` | **Reuse** |
| Ordering/scopes/clipboard | `MixtormatLayerChildren.cpp`, `MixtormatLayerDragDrop.cpp:407-482,484-556,564-779,781-967`, `MixtormatLayerClipboard.cpp:16-82` | Subtree-contiguity invariant; structural locks; GUID-fresh copy w/ owner-matched remap | **Extend** (new scope pairs + new field remaps), don't rewrite |
| Caching/scheduling | Node/network/prefix caches above; `DistanceSolveMaxSize=1024` (`:48-61`), Cracks ≤256 (`:2181-2187`) | RDG-transient + extracted entries | **Reuse** |
| Signed height/normals/coverage/UV/ID/SDF | Delta-around-0 convention (`GeneratorTypes.h:71-77`); normal from `Previous-Current` w/ zero previous (`ComposePipeline:931-946`); final normal/AO once (`:29-74`); gains `8.0/1.0/1.0` (`Internal.h:184-192`); flow-smooth unnormalized + renormalize fallback (`Flow.usf:19-21,411-423`) | — | **Preserve semantics**; V2 must re-derive normals after every deforming Behavior |

---

## 4. Confirmed architectural problems (with evidence)

1. **Projection masquerades as ownership.** Push/Warp look nested but `ScopeOwnerChildId` unchanged; execution order = authored array order. Evidence: `MixtormatStructuralConnectionProjection.cpp:87-379` + tooltip contract `MixtormatStructuralConnections.cpp:284-286`. V2 must not repeat this — genuine scoped execution required.
2. **Two warp executors, one concept.** Structural Warp (coordinate-lift `UV+D`, compose `D_new/B_new`, `.usf:38-53`) vs flow-tool Apply (RK2 trace / displaced sample / min-max carve). Same math family, separate paths. Evidence: `MixtormatGeneratorStructuralWarp.usf:21-54` vs `MixtormatGeneratorFlow.usf:425-473,528-616` + `AddGeneratorFlowToolPasses:1573-1875`.
3. **No Fold in generator domain.** Only `BreakupFold/FoldWidth` effect-side (`Internal.h:494-495`); proposal's "Behavior — Fold / Previous Field" has no generator executor to inherit. New implementation genuinely required.
4. **Kind gates are ad-hoc, conversions implicit.** HeightSource=ScalarSigned-only, WarpSource=Flow‖UVMap-only (`GeneratorGather.cpp:38-41`); SDF-vs-signed distinction exists in enum (`OutputReference.h:20-36`) but no explicit conversion nodes. Proposal's "typed data" claim needs conversion table or it re-creates this.
5. **Self-reference has no snapshot discipline.** "Own Height" / "Previous Field" / "Previous Behavior Output" are indistinguishable today from cyclic refs; gather only knows earlier-child-index (`:470-481,520-531`) and binding cycle-guard (`MixtormatParameterBinding.cpp:455-493`). Proposal must define snapshot-in (frozen input) vs live accumulation.
6. **Scope depth + container walls block V2 nesting.** `MaximumScopeDepth`, `GetScopeDepth/FindSubtreeEnd/FindSiblingRoot/InsertScopedChild` (`MixtormatLayerChildren.cpp:152-227,620-672`), `ResolveContainer==nullptr` for Source (`:1125-1133`), structural-module cross-owner lock (`MixtormatLayerDragDrop.cpp:895-909`). Three-deep `Generator>Behavior>Field>Mask` will trip all three; two-deep + slots avoids it.
7. **Shelf/layer kind asymmetry.** Shelf consumes only ScalarSigned/Flow/UVMap; structural modules layer-only (`COMPOSITION.md:20-55`, `GENERATORS.md:117-131`). Proposal's "Fields support Source, Instance, self-ref, procedural, composition" must respect this or break shelf isolation.

---

## 5. Proposed V2 data model (delta, not rewrite)

- **Keep:** `FMixtormatLayer`, `FMixtormatLayerChild` + `ScopeOwnerChildId`, `FMixtormatGenerator` + payloads, `FMixtormatOutputReference` + `EMixtormatPublishedFieldKind`, `FMixtormatParameterBinding` (+Follow/Link/Driver), groups, shelf entries, enum values/serialization.
- **Add (minimal):**
  - `EMixtormatBehaviorType`: Warp, Fold, Push, Carve, Deposit (or Carve+mode), Wrinkle, ShapeDeform (+ Distort only if proven distinct from Warp — default: Distort = Warp preset, not a type).
  - `FMixtormatBehavior` scoped child payload: `BehaviorType`, ordered `FieldSlots[]` (each = `FMixtormatOutputReference` + combine mode + mask ref), `ExecutionIndex` (implicit = scope order; no separate order field), snapshot flags (`bSnapshotInput` — frozen "own height" vs live).
  - `EMixtormatFieldCombine`: Replace/Multiply/Add/Min/Max/Lerp (mirror `EMixtormatDriverCombineMode :64-74`).
  - Explicit conversion table: ScalarSigned↔SDF (offset/bias), Flow↔UVMap (integrate/trace via stage-8 `AddReferencedFlowUVPass :861-912`), Vector2↔Flow (validity attach/drop), Scalar01↔ScalarSigned (remap, zero-preserving per `:71-77`).
  - Field producers needed new: Gravity, Height-gradient Flow, Tangential/contour Flow, Curl, Phasor Noise (Procedural Noise/Curl partially exist via Noise payloads + mask Noise; Source/Instance/Previous = reference kinds, not producers).
- **Ownership:** `Generator > Behavior` (real `ScopeOwnerChildId`) + `Behavior > field slots` (references) + `Behavior > Mask` (reuse `CanOwnScopedMasks`). No `Field` child type.

---

## 6. Proposed execution graph

1. **Field evaluation (per Behavior, in scope order):** resolve each slot → kind-check (`:38-41` pattern generalized) → convert → combine (ordered) → mask-gate (Behavior mask, then per-field mask) → snapshot if flagged. Shelf producers evaluated on demand ahead of stack (existing `:844-863`); local slots evaluate inline (no new scheduler).
2. **Behavior order:** authored scope order under the Generator (= execution order — this is what makes ownership genuine, unlike projection). Each deforming Behavior outputs updated `(Height, BoundaryField, WarpedUV, Coverage)` bundle + validity; `RemapGeneratorBundle (:1100-1171)` validates.
3. **Caching:** reuse node cache keyed on (BehaviorType + field keys + resolution) with same skip-normalization convention (`:149-158,208-216,249-255,288`); prefix snapshots at Behavior boundaries (reuse `SavePrefixSnapshot`); RDG-transients freed per graph.
4. **Publication:** Behavior output publishes as `Height` (signed delta) + optional `FlowDirection`/`BoundaryField` under existing `GeneratorPublishesField` contract; downstream HeightSource/WarpSource consume without change.
5. **Consistency:** after each deforming Behavior, re-derive normals/boundary (`:928-947` pattern); FlowCarve exception (IDs stay) preserved.

---

## 7. Three UI concepts (compact dark UI compatible)

1. **Compact nested stack.** Behaviors as indented rows under Generator; field slots as single-line chips (icon + name + kind dot) expanding inline. *Pro:* densest, matches current stack; least code (reuse rails/indent `MixtormatLayerHierarchy.cpp:156-308`). *Con:* field editing cramped; masks at 3rd indent get noisy. Best for small graphs.
2. **Nested Behavior cards with field rows (RECOMMENDED).** Generator row → Behavior card (header: type icon, enable, amount, mask badge, overflow menu) → field rows (source picker, combine combo, mask dot, preview eye). Masks nest inside card via existing scoped-mask UI. *Pro:* real ownership visible; per-field combine/mask/preview has room; maps 1:1 to execution order; drag = subtree move (existing `FindSubtreeEnd` machinery). *Con:* taller than today; needs card recipe from Shell foldout/card/well/row (per AGENTS.md §5 — no new palette). **Recommend this:** it is the only concept where what you see (card order) *is* execution order.
3. **Hybrid stack + Inspector field editor.** Stack shows Generator > Behavior rows only; selecting a Behavior loads a dedicated Inspector field-slot editor (list + kind badges + conversion warnings). *Pro:* smallest stack churn; inspector has room for curves/ramps/previews. *Con:* composition invisible in stack; two-place editing; weakest proof of "nested authoring".

All three must: support create/nest/collapse/select/reorder/duplicate/copy-paste (extend `CopyChildSubtree :16-82` + `CanKeepScopedPlacement :693-714`), Source/Instance pickers (reuse shelf UI), per-Behavior preview eye (reuse preview ownership), deletion with structural-link veto (`StructuralLinksPreserved :781-878`). **Never reintroduce projection for Behaviors** — projection file stays for legacy Push/Warp only until migrated.

---

## 8. Migration matrix

| Old | V2 equivalent | Strategy |
|---|---|---|
| Structural Warp module | Behavior-Warp (coordinate path) | **Adapter**: legacy module executes via new executor; authored payload untouched; one-time migration offered later, never forced |
| Height Push module | Behavior-Push (height variant; boundary/bedding/directional = modes) | Adapter, same as above |
| Height Blend/Curve/ColorRamp | Unchanged utils (not Behaviors) | **Retain**; no migration |
| GeneratorFlow / GravityFlow effect | Behavior-Warp (flow-trace mode) / Behavior w/ Gravity field | Adapter: effect payload → Behavior slots; keep effect type for compat |
| FlowCarve (Groove/Deposit) | Behavior-Carve + `EMixtormatFlowCarveMode` preserved | Adapter |
| ShapeDeform | Behavior-ShapeDeform (same executor mode) | Adapter |
| FlowWarp effect | Unchanged | Retain separately |
| Breakup Fold params | Behavior-Fold (new executor) | **Legacy path**: Breakup keeps working; Fold new; no auto-migration until parity proven |
| HeightSource/WarpSource sockets | Behavior field slots (same `FMixtormatOutputReference`) | **Reuse** — GUIDs, OwnerKind, SourceIds preserved |
| Parameter Follow/Link/Driver, groups, clipboard, undo/redo, Source GUID identity, external refs | Unchanged | Preserve: no enum reorder (`GeneratorTypes.h:13-15`, `LayerTypes.h:112-113`, `ParameterTypes.h:8-48`), no payload removal, `EnsureStableIds :634-722` + `RemapChildParent` intact |

Rule: **no old implementation deleted until V2 adapter demonstrates equivalent behavior** (per brief §7). Compatibility = adapters + legacy execution path, not one-time destructive migration.

---

## 9. Implementation roadmap (files likely to change)

- **P0 — proof slice (nested Warp on Rock Formation):** new `EMixtormatBehaviorType` + `FMixtormatBehavior` in `MixtormatGeneratorTypes.h` (append-only); scope pairs in `MixtormatLayerChildren.cpp:693-714` + `MixtormatChildScope`; gather path in `MixtormatGeneratorGather.cpp` (new `GatherBehaviorChild`, reuse `:38-74` gates); executor reuse `AddGeneratorFlowToolPasses` + `RemapGeneratorBundle` in `MixtormatGpuGeneratorPasses.cpp`; inspector panel (`Widgets/Inspector/`, `MixtormatInspectorGenerators.cpp`); hierarchy rows/menus/drag (`MixtormatLayerHierarchy.cpp`, `MixtormatLayerDragDrop.cpp`, `MixtormatLayerMenus.cpp`); `CopyChildSubtree` remap (`MixtormatLayerClipboard.cpp:16-82`). Exit: real nesting + typed slots + GPU output + preview.
- **P1 — catalog + conversions:** Fold (new), Push/Carve/Deposit/ShapeDeform adapters, Wrinkle (see below); conversion table + snapshot flags; Behavior-mask gating; per-field combine; cache keys; all six families (`MixtormatCanOwnGeneratorFlow :36-51` gates respected — Noise/CliffStrata SDF limits stay).
- **P2 — migration + polish:** adapters for legacy Push/Warp/flow-tools, legacy execution path flag, shelf/local-slot unification docs, previews per field, undo/redo verification, annotated mockups from §7 concept 2.

---

## 10. Unresolved design decisions (must settle before implementation)

1. Is **Distort** distinct from Warp, or a Warp preset? (Default: preset.)
2. Is **Deposit** a Carve mode (existing `EMixtormatFlowCarveMode :75-82`) or its own Behavior?
3. Snapshot semantics: does "Own Height" freeze at Behavior entry, and does "Previous Field" mean previous *Behavior* output or previous *field slot*? Frozen-copy vs live-reference changes RDG texture count.
4. Field-slot combine order: fixed slot order vs explicit priority value?
5. Conversion failures: hard drop (current `:42-62` behavior) vs fallback with warning?
6. Tileability contract: wrapped heightfield neighborhoods for Wrinkle Laplacian (see below) — wrap vs clamp vs mirror per family?
7. Resolution independence: curvature/diffusion params in texel vs world units?
8. Do Behavior masks compose (AND/OR/weighted) with field masks, or gate only at Behavior level?
9. Shelf generations: can V2 slots consume *any* shelf kind, or keep ScalarSigned/Flow/UVMap-only restriction?
10. Branch strategy for the follow-up review (per brief: decided next conversation, not here).

---

## Appendix A. Wrinkling prototypes (mandatory read — both files read fully)

- `wrinkling01.cl` (150 lines): iter-0 seeds `_curv` (neighbour normal divergence, `curv_gain=8`, `curv_power=1`) + `_shadow_tmp` (edge-weighted height-Laplacian/avg_edge, clamped); later iters curvature-modulated Gaussian diffusion (`base_blur_speed=0.05`, `curv_blur_speed=1`, `diffusion_rate=0.25`, `blur_radius_edges=2`, ping-pong `_shadow_tmp→_shadow_mask`). **Reusable in texture space:** Laplacian via 3×3 height taps, normal-variation curvature via Sobel-normal divergence (matches existing `HeightDerivedNormalStrength=8.0` path), curvature-gated blur (reuse Bartlett smooth `Flow.usf:694-722` + `FieldRange` normalize). **Needs:** height + normal + curvature + ping-pong mask textures (2–3 RDG transients), 2–8 iterations (draft 2, upres later).
- `wrinkling02.cl` (135 lines): fold-side select (`fold_side` 0 both/abs, 1 upper, 2 lower, 3 soft-signed) + mask compose (`mask_mode` product/shadow/curv/max/add) + contrast/gain + AO/thickness weights → `_fold_mask`; signed remap + edge-scaled displace (`amount_edges=0.2`, `max_disp_edges=0.35×avg_edge`) along gravity+normal blend (`gravity_weight=0.25`, `normal_weight=1`). **Reusable:** side/mode/compose math is resolution-independent; displacement along blended normal/gravity mirrors existing ShapeDeform bulge + FlowCarve displace.
- **Differs from Fold/Breakup/ShapeDeform:** Breakup-Fold is single-pass effect params, no iteration/diffusion; ShapeDeform is single displaced sample along flow, no curvature gating; Wrinkle is *iterative curvature-diffusion + masked anisotropic displace* — genuinely new executor core, but plumbed through existing bundle/normal/caching.
- **Mesh→heightfield adaptation:** neighbour-edge ops → wrapped 3×3/5×5 taps (toroidal, matching jump-flood toroidal distance `:224-267`); `avg_edge` → texel size uniform; vertex `P` displace → height-delta + normal-rederive (never move UVs). Tileability requires wrap sampling; resolution independence requiresgain-per-texel normalization.
- **Verdict: Wrinkle = Behavior (with 2 field producers: Curvature field, Fold-mask field).** Previews: curvature, shadow-mask per iteration, fold-mask, final delta. Params: `curv_gain/power`, `lap_gain/bias`, `diffusion_rate`, `blur_radius`, `fold_side`, `mask_mode`, `contrast/gain`, `amount`, `gravity_weight`, `normal_weight`, iterations.

---

## Appendix B. Confirmation ledger

- All file:line citations in §2–§4 verified by direct read in this session. Shader *bodies* beyond the two named `.usf` files, per-generator payload bodies beyond headers, and `FMixtormatLayerEffect` fields beyond `MixtormatEffect.h:387` are **inferred from declarations/docs**, not re-read — flagged inline. No runtime/GPU validation performed.