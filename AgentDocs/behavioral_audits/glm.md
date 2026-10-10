# Mixtormat — Behavior System V2 Architecture Audit

**Repository:** `hugobeyer/matlab` @ `main`, commit `9c3b3b16` — *"Preserve shelf mask sources and all generator module links when cloning a layer"* (2026-10-10).
**Mode:** Source-grounded audit only. No builds, tests, patches, branches, or file modifications were made. `AGENTS.md` routing rules were followed (exact-symbol search, line-range reads, `old_docs/` excluded, one subsystem doc per topic, all claims verified in source rather than docs).
**Method:** Three parallel source-tracing passes (UI/Slate, flow/deformation inventory, infrastructure) plus direct first-hand reads of `MixtormatGeneratorTypes.h`, `MixtormatOutputReference.h`, `MixtormatLayerTypes.h`, `MixtormatParameterTypes.h`, `MixtormatHeightTypes.h`, `MixtormatChildScope.h`, `MixtormatGeneratorStructuralWarp.usf`, `MixtormatGeneratorFlow.usf`, `MixtormatGpuGeneratorPasses.cpp` (module walk), `MixtormatGpuCompositorInternal.h` (registry/bundle/context), both `AgentDocs/Prototypes/Wrinkling/*.cl` files in full, and `AgentDocs/GENERATORS.md` / `COMPOSITION.md`.

**Evidence language.** Throughout this report:

- **CONFIRMED** — verified by direct source read; cited as `path:line-range`.
- **INFERENCE** — a conclusion drawn from confirmed facts; always labeled.
- **PROPOSED** — part of the Behavior System V2 evaluation/design, not existing code.

---

## 1. Executive verdict

**The Behavior System V2 architecture is directionally right but needs three structural corrections before it is safe to build.**

What V2 gets right, and what the codebase already secretly agrees with:

1. **A typed field model already half-exists and V2 should finish it, not invent it.** `EMixtormatPublishedFieldKind` (RegionIds, Flow, UVMap, Color, Scalar01, ScalarSigned, SDF, Vector2) with per-kind GPU format contracts, completeness validation, and an explicit prohibition on collapsing semantically distinct kinds (`MixtormatOutputReference.h:19-36`, `MixtormatGpuCompositorInternal.h:237-284`) is exactly the typed-field spine V2 needs. The `FGeneratorBundle` `EFieldSemantic`/`EFieldUnits` descriptors (`MixtormatGpuCompositorInternal.h:1124-1189`) are a second, internal half of the same idea. V2's real job is **unifying these two halves under one ownership model**, not adding a third.
2. **Nested ownership is feasible without changing the serialized data model.** The flat child array + `ScopeOwnerChildId` chains already support arbitrary depth (the runtime dependency machinery is depth-agnostic with a 128-deep guard, `MixtormatOutputReference.cpp:300-345`; `IdGroup` already owns scoped child rows, `MixtormatIdTypes.h:277-281`). Behaviors and Fields should be **GUID-identified children in the same flat arrays**, not new nested sub-arrays — that single decision preserves addresses, cache keys, cycle detection, clipboard, and undo.
3. **Ordering, cycle-safety, and caching infrastructure is genuinely reusable.** Strictly-earlier producer reads, active-path DFS cycle detection, prefix/node caches, and the published-field registry can all host Behavior/Field evaluation unchanged (`MixtormatOutputReference.cpp:411-428`, `MixtormatGpuCompositorInternal.h:2340-2428`).

Where V2 as proposed is wrong or over-specified:

1. **"Behaviors are actual children of generators" collides with the plugin's own earlier design conclusion.** The shipped interaction design doc explicitly evaluated "modules as scoped children of target" (Model A) and rejected it, choosing a display projection instead (`AgentDocs/code_docs/generator_interaction_ux_design.md` §2–3). The rejection was about **authored-order semantics**, not feasibility: today a Structural Warp executes at *its own* authored position and composes warp state *for a later target* (`MixtormatGpuGeneratorPasses.cpp:2702-2753`). If Behaviors become children *of the target*, execution position changes meaning — the warp would run at the target's position, which is **semantically different** (Push→Warp vs Warp→Push do not commute; `AgentDocs/code_docs/strata_structural_warp_design.md` §3). V2 must either (a) make "position = owner" explicit and re-define composition order, or (b) keep execution position authored and treat nesting as ownership-only. This is the single most important unresolved semantic and it must be settled before any code.
2. **The catalog conflates vector generation with the operations that use vectors** — the exact failure mode the brief warned against. Warp/Distort/Flow-Warp overlap almost totally with the existing generator flow tools and `FlowWarp` effect; Push has four proposed variants but only one (Strata bedding) has any executable semantics today; Carve/Deposit already exist as `FlowCarve` modes. The genuinely *new* implementations are: Wrinkle (no shipped equivalent), surface-space Fold (Strata's Bend is an in-frame bedding shear, not a surface fold), curl/phasor field producers (curl exists only inside effects; phasor only as `Bars`), and the composition ops between fields. V2 should define **fewer Behaviors + more field producers**.
3. **"Previous Field" and "self-input snapshots" are different problems.** The codebase strictly forbids self/forward reads (five independent cycle guards; `MixtormatSourceGather.cpp:34-35`, `MixtormatOutputReference.cpp:326-329, 523-529`). "Previous field" (read the accumulated chain state at your position) is already a proven pattern — `GeneratorModuleHeights` and the mask-chain ping-pong (`MixtormatGpuCompositorInternal.h:1662-1664`, `GpuPatternPasses.cpp:669-674`). A true self-snapshot (a node consuming its own output) has no precedent and should be **rejected** in V2 in favor of explicit "Previous" producers. Do not design for cycles.

What I would change in the proposal (summary): model Behaviors/Fields as scoped children in the existing flat arrays; unify the field-type halves; define an explicit **behavior-chain state** (running height + warp state + derived fields) that Behaviors read/write in authored order, with "Previous" as the only legal way to look backward; make Structural Warp/Height Push the *first* Behaviors (they already have ordered, target-explicit semantics); and treat the UI projection code as scaffolding to retire, not extend.

**The smallest complete slice that proves the thesis** is specified in §11: one new `Behavior` child type owning one `Field` child, evaluated inside `AddGeneratorLayerPasses`, publishing a typed output, gated by a scoped mask — reusing Warp as the pilot behavior. Everything else is expansion.

---

## 2. Current architecture map (confirmed data ownership & GPU execution)

### 2.1 Data ownership (runtime)

- A material is `UMixtormatMaterial` holding three **flat authored containers**: `Layers` (each `FMixtormatLayer` owns `TArray<FMixtormatLayerChild> Children`), `LayerGroups` (one shared child stack broadcast to member layers, one level deep), and `Sources` (the Sources shelf — standalone field producers that are **document data beside the layers, never stack members**) (`MixtormatMaterial.h:101-106`; `MixtormatLayerTypes.h:590-591, 322-323, 334-356`).
- Every child is one flat entry: `ChildId` GUID, `ScopeOwnerChildId` (valid ⇒ nested under an **earlier** child in the same array), instance pair, per-child `ParameterBindings`, a single `Type` from `EMixtormatLayerChildType` (24 values, append-only), and one payload slot (`MixtormatLayerTypes.h:167-279, 104-165`).
- Nesting is **one mechanism, one depth rule**: owner must exist and be strictly earlier (`MixtormatChildScope.cpp:17-36`); legality is a hardcoded type-pair table (`MixtormatCanOwnGeneratorFlow` `MixtormatGeneratorTypes.h:36-44`, `CanKeepScopedPlacement` in `MixtormatLayerChildren.cpp:693-714`, `CanOwnScopedMasks` in `MixtormatChildScope.cpp:38-57`); authoring depth ceiling `MaximumScopeDepth = 4` (`MixtormatLayersPrivate.h:12`).
- Generators are a **payload-union wrapper**: `FMixtormatGenerator` holds `Type` + six payload structs + dormant `HeightSource`/`WarpSource` sockets + deprecated serialized-only `HeightBlend` (`MixtormatGeneratorTypes.h:941-1007`). Structural modules (HeightPush, StructuralWarp) and height sublayers (HeightBlend/Curve/ColorRamp) are **sibling children** of the same layer, not generator children.

### 2.2 GPU execution (render thread, per compose)

- Gather (game thread): group expansion → per-layer `ApplyDirectReferences` into a transient copy → child dispatch into `FLayerRenderData` (generator/mask/ID/effect gatherers) → shelf producers collected in dependency order (`MixtormatGpuCompositor.cpp:1412-2012`; `MixtormatSourceGather.cpp:54-228`).
- Render (`MixtormatGpuComposePipeline.cpp:865-1249`): shelf producers run **ahead of the stack** (`:844-863`, uncached, publish-only); then per layer: region-ID passes → **generator module chain** → mask chain (ping-pong) → `AddLayerCompositePass` → deferred structure passes (Erosion/Breakup/WornEdges queued at child, dispatched after composite, `:1139-1170`) → height snapshot + publication.
- **Generator module chain** (`AddGeneratorLayerPasses`, `MixtormatGpuGeneratorPasses.cpp:2685-3043`) — the de-facto behavior system, currently implicit:
  1. `SignedRunningHeight` R32F cleared to 0 ("modules compose in child order from signed zero", `:2694-2699`).
  2. Children walked in authored order; per child: **StructuralWarp** composes per-target displacement/shift state (`:2703-2753`, state maps `MixtormatGpuCompositorInternal.h:1664-1668`); **HeightPush** accumulates per-target bedding shift (`:2754-2787`); **HeightBlend/Curve/ColorRamp** rewrite or publish on the running chain (`:2789-2862`); **Generator** runs its family pipeline (native fields → owned flow tools inside the module → shared signed normalization → structural pullback for pending warp/push → publication) (`:2863-3031`).
  3. Combine is a **plain add with no coverage gating** (`MixtormatGeneratorBundle.usf:167-170`); the layer's default IDs are "the last module that produced any" (`MixtormatGpuGeneratorPasses.cpp:2999-3028`).
- **Publication:** producers publish typed fields into a graph-local registry keyed `{LayerId|SourceId, ChildIndex, Output, OwnerKind}` (`FPublishedFieldKey`, `MixtormatGpuCompositorInternal.h:204-231`) — note **ChildIndex is an array index, not a GUID**. Consumers resolve through `MixtormatOutputReferences::Resolve*` with strictly-earlier ordering and five layers of cycle protection.
- **Caching:** content-hash prefix chain (lowest changed layer = snapshot point, `MixtormatGpuCompositor.cpp:1578-1587, 1955-1976`), prefix cache carrying typed published fields (`MixtormatGpuCompositorInternal.h:2160-2338`), per-producer node cache (`:2340-2428`), and a network cache. Re-composition is **pull-based** from editor `SetPreviewLayers`, throttled; there is no UObject delegate chain in Runtime.
- **Undo:** plugin-local snapshot history (`FEditHistoryState` deep-copies layers/groups/sources), 0.3 s coalescing only while child structure is unchanged, forced discrete steps for structural edits (`SMixtormat.cpp:286-332`; `MixtormatStructuralConnections.cpp:961-967`).

### 2.3 The structural projection (how "nested" looks today)

A Structural Warp/Height Push visually appears **under its target generator** via a display-only projection rebuilt per frame: `FMixtormatProjectedChildRow` with `Kind ∈ {Ordinary, IncomingConnection, AuthoredRepair}` — *"Display-only: indices into this array never address an authored child array"* (`MixtormatStructuralConnectionProjection.h:14-34`), tooltip: *"Authored execution position: {N}. Visual nesting does not change order or ownership"* (`MixtormatStructuralConnections.cpp:284-286`). The projection performs unusually defensive validation (identity uniqueness, scope safety, subtree contiguity, instance-chain uniqueness, target kind/scope/order checks — `MixtormatStructuralConnectionProjection.cpp:87-379`) that would become **data-model invariants** if nesting became real.

---

## 3. Inventory: every field/flow/deformation feature, its status, and its disposition

Status legend: ✅ shipped & active · 🟡 shipped but limited/dormant · 📐 prototype/docs only. Disposition: **Reuse** (becomes shared infra as-is) · **Adapt** (reworked into V2) · **Retain** (kept separately, not absorbed) · **Replace** (superseded).

### 3.1 Structural modules (generator-layer children)

| Feature | Status | Implementation (runtime → gather → GPU → shader) | Contract highlights | Disposition |
|---|---|---|---|---|
| **Structural Warp** | ✅ | `FMixtormatGeneratorStructuralWarp` (`GeneratorTypes.h:713-735`) → `GatherGeneratorHeightModuleChild` (`GeneratorGather.cpp:377-447`) → per-target state compose (`GeneratorPasses.cpp:2703-2753`) → `MixtormatGeneratorStructuralWarp.usf:30-54` + `MixtormatGeneratorWarp.ush` | Source = completed Flow (`FlowDirection`) or lifted UVMap; **Vector2 explicitly not a coordinate contract** (`GeneratorTypes.h:728`). Writes `D_new = M·d + sample(D_old, ψ)`, `B_new = sample(B_old, ψ)`, mask gates displacement once (`usf:36-53`). Strata target regenerates in the structural frame (`MixtormatStrataCarver.usf:246-359`); all other targets get a completed-bundle pullback (`GeneratorPasses.cpp:2938-2975`). Publishes nothing itself (`ChildCapabilities.cpp:317-319`). | **Adapt** — becomes the pilot V2 Behavior. Its ordered, target-explicit, state-composing semantics are exactly a Behavior's; only ownership changes. |
| **Height Push** | ✅ | `FMixtormatGeneratorHeightPush` (`GeneratorTypes.h:685-711`) → gather `:358-376` → `:2754-2787` → `MixtormatGeneratorHeightPush.usf:17-24` | Source = ScalarSigned `"Height"`; `OutShift = Previous + Amount·Gate·SourceHeight`. Target **hardcoded to first eligible later StrataCarver** (`MixtormatOutputReference.cpp:636-643, 652`; `GeneratorTypes.h:704-706` "Other target semantics are not enabled yet"). Applies before folds; destination-space gradient chained into bed filtering. | **Adapt** — Push Behavior; generalize target kinds in V2 (this is a V2 feature, not a regression risk). |
| **Height Blend (generator sublayer)** | ✅ | `FMixtormatGeneratorHeightBlend` (`GeneratorTypes.h:637-683`) → gather `:448-483` → `AddGeneratorHeightBlendPass` (`:2789-2808`, `:955-982`) → `MixtormatGeneratorHeightBlend.usf:24-71` | 7 ops (Add/Subtract/Min/Max/Difference/Multiply-Scale/HeightBlend) against an **earlier** module's height (`GeneratorModuleHeights`); neutral operands per op; result `lerp(A, Combined, Amount)`. **No mask input** (no ScopedMask param, `usf:10-22`). | **Adapt** — this is a *field composition op* in V2 terms; generalize into Field combiners. |
| **Height Remap / Curve** | ✅ | `FMixtormatGeneratorHeightCurve` (`GeneratorTypes.h:737-794`) → gather `:484-506` → `:2810-2814, 985-1016` → `MixtormatGeneratorHeightCurve.usf:36-85` | Locked order: normalize → signed input range → signed ramp → balance → contrast → offset → invert → Amount. Always reads the running chain. | **Adapt** — becomes a Field transform / Behavior parameter curve. |
| **Height Color Ramp** | ✅ | `FMixtormatGeneratorHeightColorRamp` (`GeneratorTypes.h:811-848`) → gather `:507-539` → `:2815-2862` → `MixtormatGeneratorHeightColorRamp.usf:23-33` | 4 scalar sources incl. `CompositeBelow` (cross-layer read, `:2845-2849`); publishes typed `Color` (`:2850-2854`); becomes layer Base Color when enabled (`:3036-3040`). Mask gates RGB only. | **Adapt** — Color producer field; `CompositeBelow` is the precedent for cross-container field reads. |
| **Generator HeightSource / WarpSource sockets** | 🟡 dormant | `FMixtormatGenerator::HeightSource/WarpSource` (`GeneratorTypes.h:965-982`), gathered (`GeneratorGather.cpp:33-77`), validated (`MixtormatOutputReference.cpp:612-619`), demand-scheduled (`SourceGather.cpp:27-51`), flow-traced, stored in `GeneratorInputs` (`GeneratorPasses.cpp:2864-2886`) — **then never read** (declaration `MixtormatGpuCompositorInternal.h:1665`; no consumer anywhere; grep-verified). Design docs say intentionally inactive (`strata_structural_warp_design.md:21-22`), yet the inspector still offers them (`MixtormatInspectorSources.cpp:107-167`). | Users can author edges that do nothing. | **Adapt** — V2 Field-Source nodes are the natural completion of this half-built feature; decide activate-or-remove (§12). |

### 3.2 Flow & deformation

| Feature | Status | Implementation | Contract highlights | Disposition |
|---|---|---|---|---|
| **Shape Deform** | ✅ | Effect type 9 (`MixtormatEffect.h:58, 1137-1143`); dispatched by `AddGeneratorFlowToolPasses` (`GeneratorPasses.cpp:1573-1875`) inside the module; `MixtormatGeneratorFlow.usf` stage 3 Mode 0 (`:528-557`) | Shared solve: Seed (stage 0, `:184-221`) + tile-aware JFA (stage 1, `:224-267`) + Resolve (stage 2, `:311-349`) + apply. One displaced sample; bulge/pinch along direction; expand/erode across boundary; neutral at zero. Publishes `WarpedUV`; bundle companions remapped (`:1847-1871`). | **Adapt** — a V2 Behavior whose field input is the owner's BoundaryField. |
| **Generator Flow** | ✅ | Effect type 10 (`MixtormatEffect.h:59, 1084-1154`); stage 3 Mode 1 RK2 trace (`usf:561-574`); publishes `FlowDirection` (Flow kind) + `WarpedUV` (UVMap kind) on demand (`:1792-1800`) | Flow field float4 = (direction.xy, signed distance, influence) (`usf:346`); influence = reach/feather × amount × mask (`:342-345`); direction blur = separable Bartlett (stage 7, `:694-719`). | **Adapt** — the *flow field producer* (Seed/JFA/Resolve) becomes shared field infrastructure; the apply becomes a Behavior. |
| **Flow Carve** | ✅ | Effect type 11; Groove/Deposit (`MixtormatEffect.h:75-82`); stage 3 Mode 2 (`usf:576-616`) | Distance-biased min/max along RK2 trace; publishes unclamped `CarveMask`; Deposit extends Coverage (`:612-615`). **Deliberately keeps IDs/masks/boundary in place** — no `WarpedUV`, no bundle remap (`:25-27`, `:1795-1800`, `:1851`). | **Adapt** — Carve/Deposit Behavior; the ID/height divergence must become an explicit, documented V2 semantic. |
| **Gravity Flow** | ✅ | Effect type 12 (`MixtormatEffect.h:61-62, 1097-1103`); stage 9 gravity resolve (`usf:352-409`), apply Mode 3 (`:561-574`); blocked-outline segment check ≤32 texels (`:447-468`) | Height steering is local (no JFA, `:1673-1676`); SDF steering deflection is approximate ("not a scene collision solver", `GENERATORS.md:80-83`). | **Adapt** — Behavior; gravity = field producer (height-gradient direction). |
| **Runoff** | ✅ | Effect type 8 (`MixtormatEffect.h:47-54`); `MixtormatGpuRunoffPasses.cpp:245-449`, 4 entry points, fixed 1024 analysis grid | Smears one prepared field along gravity with directional Gaussian on a 1024 grid; writes into the **mask chain**, not height (`RunoffPasses.cpp:415`). Not iterative; does not use `MixtormatEikonal.ush`. | **Retain** — an effect-layer op; distinct role from generator behaviors. |
| **Flow Warp** | ✅ | Effect type 4? — `EMixtormatEffectType::FlowWarp` (`MixtormatEffect.h:42, 1044-1078`); `Effects/MixtormatFlowWarpPasses.cpp:12-126`; `MixtormatFlowWarp.usf:161-273` | Layer-level warp: periodic curl field (`MixtormatPeriodicCurlField`, `MixtormatGully.ush:71-86`) rotated + slope-steered; warps surface/mask/effect channels; normal re-transformed via per-pixel warp Jacobian (`usf:140-152`). Runs pre-composite in the child loop (`ComposePipeline.cpp:1099-1101`). | **Retain** — its curl machinery is the seed of a V2 **Curl field producer**; the effect itself stays. |
| **Erosion** | ✅ | Effect type 2; `MixtormatErosion.usf` ("min-plus eikonal relaxation", `usf:3-9`); `ErosionPasses.cpp:353-459`, iterations 1..128 | Post-composite height filter with tangent horizon direction. | **Retain** (adapt later if V2 wants it as a Behavior). |
| **Breakup** | ✅ | Effect type 4 (`MixtormatEffect.h:35-39`); `MixtormatBreakup.usf` (751 lines) incl. curl-mixed direction (`usf:237-238`) and terrace `fold` (`usf:544-548, 572-580, 632-643`) | Multi-scale tileable SDF cell families + CSG; post-composite; produces Height/RegionIds/masks/shade. | **Retain**. Its `fold` is cell-terracing, **not** a wrinkle primitive. |
| **Stain** | ✅ | `MixtormatGpuSimulationPasses.cpp:5-9` header: ping-pong state pair over StainIterations | The real iterative transport solve. | **Reuse** — its ping-pong pattern is the precedent for Wrinkle's diffusion loop. |
| **Existing Fold / Bend** | 🟡 partial | Strata's geological Bend: shear-composed chevron fold of bedding *in the structural frame* (`MixtormatStrataCarver.usf:289-308`; `FMixtormatStrataCarver.Bend/BendScale` `GeneratorTypes.h:126-130`). Breakup terrace fold (above). Flow tools' `Bend` = periodic direction noise (`GeneratorFlow.usf:51, 306, 362`). | **No surface-space Fold/Bend/Wrinkle generator module exists.** | **Adapt** (concept) — a true surface Fold is V2-new; Strata Bend stays Strata-internal. |
| **Curl noise deformation** | 🟡 | `MixtormatPeriodicCurlField/Warp` (`MixtormatGully.ush:71-86`, analytic-derivative, divergence-free, tileable) consumed by FlowWarp (`FlowWarp.usf:95`), Craquelure family (`Craquelure.usf:60-64`, `CraquelureRelief.usf:77`, `CraquelureGrow.usf:185-188, 464`); PeelField has its own curl (`usf:324-331, 1284-1355`). **No generator module is curl-warped.** | | **Adapt** — lift into a Curl field producer. |
| **Phasor noise** | 🟡 | `MixtormatNoiseBars` ("a phasor on an integer wave vector", `MixtormatNoise.ush:226-235`, dispatched `GpuNoisePasses.cpp:95-130`) | Only the Bars/Stripes family. | **Adapt** — generalize into a Phasor field producer. |
| **Procedural noise families** | ✅ | `EMixtormatNoiseType` (9 families, `GeneratorTypes.h:856-868`); `MixtormatNoise.ush` + `MixtormatNoise.usf`; single-dispatch multi-output (Value/Height/Gradient/IDs) (`GpuNoisePasses.cpp:27-28`) | Integer-period lattice = tileable at any scale; family-typed Value contract (`NoiseValueKind`, `MixtormatOutputReference.cpp:44-57`); FlowDirection derived from completed height (`:216-238`). Also reusable inline as mask source (`GENERATORS.md:29-67`). | **Reuse** — the canonical V2 procedural field producer, as-is. |

### 3.3 Cross-cutting infrastructure

| Capability | Status | Where | Disposition |
|---|---|---|---|
| Typed field kinds + format contracts | ✅ | `MixtormatOutputReference.h:19-36`; `MixtormatGpuCompositorInternal.h:237-284` | **Reuse** (V2 spine). |
| Published-field registry `{LayerId, ChildIndex, Output, OwnerKind}` | ✅ | `MixtormatGpuCompositorInternal.h:204-231, 1295-1306, 1592-1596` | **Reuse** (works unchanged if nesting stays in the flat array; ChildIndex already is the flat-array index). |
| Demand-scheduled shelf producer DAG | ✅ | `MixtormatSourceGather.cpp:54-228` | **Reuse** — template for any new non-layer producer family. |
| Dependency validation, scope forest, active-path DFS | ✅ | `MixtormatOutputReference.cpp:300-364, 411-428` | **Reuse** (already depth-agnostic). |
| Parameter Follow/Link/Driver | ✅ | `MixtormatParameterTypes.h:123-152, 154-221`; `MixtormatParameterBinding.cpp:1380-1447` | **Reuse** semantics; **Adapt** addressing (§7.3). |
| Instance system (whole-child mirroring) | ✅ | `LayerTypes.h:181-193`; `ParameterBinding.cpp:1090-1146` | **Reuse.** |
| Group expansion (one level, broadcast) | ✅ | `MixtormatLayerGroups.cpp:232-377`; `MakeEffectiveChildId` MD5-derived IDs `.h:40-49` | **Reuse**; structural modules force-disabled in shared children today (`:349-357`) — V2 must decide group semantics. |
| Clipboard/clone reference remapping | ✅ | `MixtormatLayerClipboard.cpp:16-82`; `MixtormatParameterBinding.cpp:724-841` (latest commit = shelf-mask preservation `:831-839`) | **Reuse**; every new reference field must be hand-added (see §6 P9). |
| Prefix/node/network caches | ✅ | `MixtormatGpuCompositorInternal.h:73-181, 2160-2428` | **Reuse** unchanged. |
| Signed height / normals / coverage / IDs semantics | ✅ | Signed height about 0 (`GeneratorTypes.h:878-884`); normals from height delta (`MixtormatHeightDeltaNormal.usf:6-17, 43-86`); de-facto SDF = internal `BoundaryField` (negative-inside + validity, `MixtormatGpuCompositorInternal.h:1174-1176`) | **Reuse**; the public `SDF` kind needs a producer (§6 P6). |
| Tileability machinery | ✅ | Every flow solve torus-wrapped (`GeneratorFlow.usf:29-30` "Travel distance is never wrapped with fmod"); warp lift contract `W(x+k)=W(x)+k` (`GeneratorTypes.h:728` context, `MixtormatGeneratorWarp.ush`); noise integer periods (`GeneratorTypes.h:885-888`) | **Reuse** — non-negotiable invariant for all new fields. |

---

## 4. Wrinkling prototypes — analysis and adaptation plan (mandatory)

Both files read in full. They are **Houdini-style point-kernel OpenCL prototypes operating on a mesh with explicit topology** (`#bind point neighs int[] name=topo:neighbours`), displaced along 3D normals — **not** texture-space, and **not** an implemented Mixtormat tool.

### 4.1 wrinkling01.cl — curvature seed + curvature-dependent diffusion

- **Iteration 0** (`wrinkling01.cl:36-110`): over each vertex's neighbors, edge-length-weighted **Laplacian of the height axis** (`w = avg_edge/edge_len`, `lap += w·(pn−p0)`, normalized by Σw, `:76-91`) and **normal-variation curvature** `k = Σ w·|Nn−N0|·(avg/edge) / Σw` (`:86-94`). Curvature is compressed `curv = 1 − exp(−max(curv_gain,0)·k)`, clamped 0..1, power-shaped (`:96-98`). The Laplacian seed is scaled by `lap_gain`, biased, clamped to `[mask_min, mask_max]` (`:100-105`).
- **Iterations ≥1** (`:112-145`): **curvature-gated diffusion** of the shadow/fold mask: per-neighbor speed `speed = 0.5·(ci + cj)` where `ci = max(0, base_blur_speed + curv·curv_blur_speed)`, Gaussian edge weights `w = exp(−(edge/avg)²/(2·radius²))`, update `out = center + rate·Σ speed·w·(nb−center)/Σw`, clamped every iteration. Then `@WRITEBACK` swaps tmp→mask (`:147-150`).
- **Reading:** this is a two-stage algorithm — (1) estimate where folds are (Laplacian seed = broad concavity/convexity; normal variation = crease intensity), (2) relax the fold mask anisotropically, diffusing fast on creases and slowly on flat areas, so crease "valleys" connect into continuous wrinkles.

### 4.2 wrinkling02.cl — fold-side selection, mask composition, blended-direction displacement

- **Fold-side selection** (`:47-61`): Both sides `|s|`; Upper folds `max(s,0)`; Lower `max(−s,0)`; Soft signed `0.5+0.5·s`.
- **Mask composition** (`:63-89`): five modes (product, shadow-only, curv-only, max, add-clamped), power contrast, gain, then **AO and thickness mask modulation** `mask *= 1 + (ao−1)·ao_weight` (`:84-87`).
- **Displacement** (`:92-134`): `signed_mask = lerp(signed_low, signed_high, mask)` (optionally squared by the mask), `disp = amount_edges · avg_edge · signed_mask`, clamped to `±max_disp_edges·avg_edge`; direction `dir = normalize(G·gravity_weight + N·normal_weight·|signed_mask|)`; vertex displaced `P += dir·disp`; optionally the blended direction is **written back into `_grav`** (`:131-132`) so a downstream op inherits the blended flow — a primitive form of field chaining.

### 4.3 Which math is reusable in texture space, and what replaces mesh neighborhoods

| Mesh concept | Texture-space (heightfield) replacement | Precedent in repo |
|---|---|---|
| Topology neighbor list | 8-neighborhood (or two ring taps) on the wrapped grid; all samples torus-wrapped | every flow solve (`GeneratorFlow.usf` "Every sample wraps" `:29-30`) |
| `avg_edge_length` scale | texel size `1/OutputSize` (per-axis for non-square) | non-square caveat already documented (`flow_generation_core.md` limitations) |
| Weighted Laplacian of height | 5-tap/9-tap finite-difference Laplacian on height, wrap-sampled | `MixtormatCurvature.ush`; curvature mask filters |
| Normal-variation curvature | sample the **normal map** at 4/8 neighbors and average length of differences; or reuse an existing curvature estimate | normals recomputed from height via `MixtormatHeightDeltaNormal.usf` / `MixtormatHeightNormal.ush:15-43` (units convention) |
| Curvature-dependent diffusion (iterative) | ping-pong RDG pair, N iterations, per-texel speed texture | **Stain** (`GpuSimulationPasses.cpp:226-231, 507`), Cracks arrival ping-pong (`GeneratorPasses.cpp:2317-2341`), CraquelureGrow |
| Fold-side / mask composition / contrast | per-pixel math, portable verbatim | — |
| AO / thickness masks | texture-space stand-ins: coverage/height fields, or the final AO pass output **if** published as a field (it currently is not) | AO exists as a final pass (`ComposePipeline.cpp:1253-1312`), not a published field |
| Displace along `normalize(N·w + G·w)` | **two components:** vertical height shift `H += disp` (the normal-along-up part), plus optional lateral advection along the gravity-projected tangent `−∇H/|∇H|` reusing the existing RK2 trace | RK2 trace + `WarpedUV` remap machinery (`GeneratorFlow.usf:440-473`) |
| Write blended direction back (`write_grav`) | publish the blended direction as a typed **Flow** output | publication plumbing `:1792-1800` |

### 4.4 Required intermediate fields and iterations

**PROPOSED field plan:** (1) `WrinkleCurvature` (R16F, from normal variation or Laplacian-of-height), (2) `WrinkleSeed` (signed Laplacian), (3) `WrinkleMask` (ping-pong pair, diffusion iterations — expose as `Iterations` 1..~32 with the clamp-per-iteration semantics of the prototype), (4) optional `WrinkleDirection` (Flow) if normal/gravity blending is published. Only stages 1–3 are required for the core effect.

### 4.5 How Wrinkle differs from existing Fold, Breakup, and Shape Deform

- **Strata Bend** folds *bedding coordinates* by shear inside the structural frame before the surface exists (`StrataCarver.usf:289-308`) — a coordinate-space operation, not a curvature response.
- **Breakup terrace `fold`** is a cell/CSG terrace feature of the breakup SDF (`Breakup.usf:544-643`).
- **Shape Deform** moves reads by boundary distance (JFA solve) — global silhouette-scale, not local creases.
- **Wrinkle** is *local, curvature-gated, diffusion-connected* creasing driven by the surface's own geometry — **no existing module implements any part of this math**. It is genuinely new implementation.

### 4.6 Behavior, field generator, or both?

**Behavior, with field byproducts.** Wrinkle consumes height (+normals) and transforms height; it does not independently produce a reusable field as its primary purpose — that is the definition of a Behavior in V2 terms. However it should optionally **publish** its fold mask (like Flow Carve publishes `CarveMask`, `usf:609-610`) and optionally its blended direction (like `write_grav`), so downstream Behaviors/masks can consume them. A separate "Curvature" *field producer* (curvature-from-height as a standalone typed output) is worth extracting regardless — masks and other behaviors can use it, and it is half of the wrinkle math anyway.

### 4.7 Parameters and previews (from the prototypes, PROPOSED)

- **Seed:** height axis is fixed (Y/height); expose `lap_gain`, `lap_bias`, `lap_abs`, `mask_min/max`; `curv_gain`, `curv_power`.
- **Diffusion:** `diffusion_rate`, `blur_radius_edges`, `base_blur_speed`, `curv_blur_speed`, `Iterations`.
- **Composition:** `fold_side` (4 modes), `mask_mode` (5 modes), `contrast`, `mask_gain`, AO/thickness weights (0 = off by default), `signed_low/high`, `gate_signed`.
- **Displacement:** `amount`, `max_disp`, `normal_weight`, `gravity_weight`, `normalize_direction`, `mask_normal`, plus a texture-only "lateral advect" toggle/amount.
- **Previews:** fold mask (R), curvature field, seed sign (upper/lower tint), blended direction (reuse Flow Direction debug, `GeneratorFlow.usf:619-638`), and a Jacobian-style stretch tint on the warped height (precedent `:641-676`).

---

## 5. Runtime & shader architecture audit — the ten evaluation points

1. **Typed field model.** Exists as two parallel halves: public `EMixtormatPublishedFieldKind` (8 kinds, append-only, format-contracted, validated; `MixtormatOutputReference.h:19-36`) and internal `FGeneratorBundle` semantics (named masks with `EFieldSemantic`×`EFieldUnits` descriptors; `MixtormatGpuCompositorInternal.h:1124-1189`, "Internal producer contract, not new public field kinds" `:1126-1127`). **Gap:** no single ownership model; `SDF` and generic `Scalar01` have no producers ("no destination consumer yet", `GeneratorPasses.cpp:2671-2675`); nothing publishes UVMap from a generator (`MixtormatOutputReference.cpp:59-80` — only `WarpedUV` via flow tools). **V2 verdict:** extend, don't replace.
2. **Explicit conversions.** Today conversions are ad-hoc per consumer: signed Value→coverage `saturate(0.5·v+0.5)` for Noise masks (`GENERATORS.md:37-39`); Noise `Gradient` transport-sampled as `SourceFrameVector` rather than transformed (`MixtormatGpuCompositorInternal.h:1137, 1177-1179`); UV→displacement lift in Warp helpers. **V2 verdict:** the bundle-descriptor system already encodes "how this field moves under warp" — make that the conversion contract.
3. **Ordered field evaluation & composition.** Proven in two places: the module chain (running height + `GeneratorModuleHeights` cross-reads, `GeneratorPasses.cpp:2992-2993`, `MixtormatGpuCompositorInternal.h:1662-1664`) and the mask chain ping-pong. HeightBlend's neutral-operand rule ("a missing source falls back to neutral semantics, not Running+A", `GeneratorPasses.cpp:2789-2808`) is the correct composition failure semantic — keep it.
4. **Behavioral execution order.** Currently: authored child order within the layer; structural state composed per target at the module's own position (`:2702-2753`); push/warp non-commutativity documented (`strata_structural_warp_design.md` §3). **V2 must decide** owner-position vs target-position semantics (§12 D1).
5. **Self-input snapshots vs cycles.** Self/forward reads are rejected at five independent layers (shelf DAG `SourceGather.cpp:34-35, 89-136`; dependency DFS `OutputReference.cpp:326-329, 523-529, 411-428`; binding `Visiting` sets `ParameterBinding.cpp:466-471, 1407-1417`; instance chains `:1105-1118`; asset composition refs `MixtormatMaterial.cpp:53-127`). Cross-layer state copies exclude self (`ComposePipeline.cpp:511-516`). **V2 verdict:** "Previous Field" = ordered read of the behavior-chain state (maps to `GeneratorModuleHeights` pattern); **true self-reference stays illegal**; iterative self-modification = explicit iteration loops inside one Behavior (Stain/Wrinkle pattern).
6. **Local field instances vs external Sources.** Both exist: inline payloads (`Mask.Noise` with its own parameter owner `MaskNoise=25`, `ParameterTypes.h:45-46`; `ParameterBinding.cpp:176`) and external shelf producers (`SourceGather.cpp` demand DAG; `OwnerKind` discriminator prevents collisions, `MixtormatGpuCompositorInternal.h:209-212`). **V2 verdict:** Field-Source = shelf reference; Field-Instance = local payload; the two-GUID reference struct works if Fields are children (§7).
7. **GPU resource lifetimes, RDG, caching.** All RDG; graph-local registries reset per layer via `BeginLayer` (`MixtormatGpuCompositorInternal.h:1703-1740`); node cache keys are "exactly [the producer's] own settings and of what they read" (`:2340-2345`); prefix cache already carries typed published fields (`:2160-2338`). **V2 verdict:** Behavior/Field results slot into the node cache by hashing their own settings + input keys; no new cache machinery.
8. **Height/normal/gradient/ID consistency after deformation.** Weakest point of the current architecture. Non-Strata targets remap height/coverage **bilinearly** but IDs **nearest-anchor** in the pullback (`MixtormatGeneratorBundle.usf:202-205`) — a confirmed filter split (sub-texel height/ID disagreement at warp discontinuities is **INFERENCE** from the differing filters; the split itself is CONFIRMED). Distance fields use a Jacobian-ratio approximation with an explicit caveat ("existing local distances are not exact unit-gradient SDFs", `Bundle.usf:90-105`). FlowCarve intentionally leaves IDs unmoved (`usf:25-27`). **V2 verdict:** make remap policy a **declared per-field attribute** (the descriptor system) rather than hardcoded per stage.
9. **Tileability & resolution independence.** Strong: torus-wrapped solves, never-fmod travel (`GeneratorFlow.usf:29-30`), integer-period noise lattices (`GeneratorTypes.h:885-888`), lift-contract UVMaps, resolution-capped solves documented (`DistanceSolveMaxSize=1024`, `GeneratorPasses.cpp:41-61`). **V2 invariant:** every new field producer must state its wrap contract and solve-size cap up front.
10. **Compatibility with all six generator families.** Family list = `EMixtormatGeneratorType` (there is no separate family enum; grep: 0 matches for `EMixtormatGeneratorFamily`). Flow-tool ownership: all six (`MixtormatCanOwnGeneratorFlow`, `GeneratorTypes.h:36-44`); signed boundary field only for Strata/Rock/Cracks/Pebbles (`MixtormatGeneratorHasFlowBoundary` excludes Noise + CliffStrata, `:46-51`). Structural Warp works on all six but with two different semantics (Strata regenerates; others pullback — `GENERATORS.md:137-140`). **V2 verdict:** behaviors must declare capability requirements (e.g., "requires BoundaryField") instead of the current per-family hardcoded branching.

**Shared infrastructure that becomes V2 foundation instead of a rewrite:** published-field kinds/registry/validators; the flow solve (Seed/JFA/Resolve) as the universal vector-field producer; the RK2 trace + bundle remap as the universal consumer of warp fields; `FGeneratorBundle` descriptors as the semantic transform contract; the shelf producer DAG as the field-source scheduler; prefix/node caches; cycle-protection stack; the scoped-mask machinery (`AddScopedFeatureMask(..., bIndependentScope=true)`, `MixtormatGpuMaskPasses.cpp:555-582`).

---

## 6. Confirmed architectural problems (evidence register)

1. **P1 — Structural operations are siblings with display-only nesting.** Projection is explicitly "Display-only" (`MixtormatStructuralConnectionProjection.h:14-34`); execution is positional in the layer's child chain (`GeneratorPasses.cpp:2700-2753`); cross-owner moves refused (`MixtormatLayerDragDrop.cpp:893-908`); shared-group authoring unsupported (`MixtormatStructuralConnections.cpp:386-390`). *Consequence:* the UI performs per-frame defensive validation of invariants (identity, contiguity, order) that a real hierarchy would enforce once in data (`Projection.cpp:87-379`).
2. **P2 — Two execution semantics for one node type, per target family.** Strata regenerates in the warped structural frame (`StrataCarver.usf:246-359`); everyone else gets a post-hoc completed-bundle pullback (`GeneratorPasses.cpp:2938-2975, 1207-1220`). Height bilinear vs IDs nearest (`Bundle.usf:202-205`) — height/ID sub-texel desync risk (**INFERENCE** from filter split).
3. **P3 — HeightPush target hardcoded to StrataCarver** (`MixtormatOutputReference.cpp:636-643, 652`; "Other target semantics are not enabled yet", `GeneratorTypes.h:704-706`).
4. **P4 — Dormant explicit inputs.** `HeightSource`/`WarpSource` gathered, validated, scheduled, flow-traced, stored in `GeneratorInputs` — never read (`GeneratorPasses.cpp:2864-2886` + `MixtormatGpuCompositorInternal.h:1665`; grep-verified no consumer). UI still offers the sockets (`MixtormatInspectorSources.cpp:107-167`). Users can author dead edges.
5. **P5 — Dead StrataCarver gates.** `MaskInfluence`/`IDInfluence` UPROPERTYs exist (`GeneratorTypes.h:155-164`), shader gates exist (`StrataCarver.usf:468-481`), but dispatch hardcodes `bHasScopedMask=false; bHasRegionIds=false` ("modules do not read each other until Region inputs land", `GeneratorPasses.cpp:1310-1315`).
6. **P6 — Declared-but-unproduced field kinds.** `SDF` has format + validator but no producer; generic OutputReference children for other kinds have "no destination consumer yet" (`GeneratorPasses.cpp:2671-2675`). The real SDF data path is the internal `BoundaryField` pair (`MixtormatGpuCompositorInternal.h:1174-1176`).
7. **P7 — Duplicated math.** Jacobian implemented twice with different inputs (`MixtormatGeneratorWarp.ush:69-77` over absolute coordinates vs inline difference-Jacobian `StrataCarver.usf:253-262`); lattice-snap duplicated (`MakeStrataLattice` `GeneratorPasses.cpp:1241-1286` vs `ResolveNoiseWave` `GpuNoisePasses.cpp:91-130`, "kept local here because the original is file-private").
8. **P8 — Stale/hazardous comments.** `MixtormatEffect.h:55-57` claims flow tools are "valid only scoped under a Rock Formation generator" while all six families may own them (`GeneratorTypes.h:36-44`); "How a pebble's cut planes are oriented" sits above `MixtormatCanOwnGeneratorFlow` (`:32-33`); `AddGeneratorFlowToolPasses` header says published masks/IDs "stay undeformed" while the same function remaps the bundle for non-carve tools (`GeneratorPasses.cpp:1562-1572` vs `:1847-1871`).
9. **P9 — Hand-maintained remap lists.** Every reference-bearing field must be enumerated in ~4 places (clipboard `MixtormatLayerClipboard.cpp:16-82`; layer-clone `MixtormatParameterBinding.cpp:724-841`; group expansion `MixtormatLayerGroups.cpp:289-325`; shelf paste `SMixtormat_Layers.cpp:655-694`). The git history shows the cost: the latest commit is exactly a missed-case fix ("Preserve shelf mask sources…").
10. **P10 — Combine ignores coverage; coverage is height-decoupled.** Module combine is a bare add (`GeneratorBundle.usf:167-170`); Deposit extends coverage but nothing gates height by it (`GeneratorFlow.usf:612-615`; `flow_generation_core.md` owners bullet).
11. **P11 — FlowCarve asymmetry.** Publishes FlowDirection but not WarpedUV; performs no companion remap — carved height and unmoved IDs coexist by design but undocumented as a general rule (`GeneratorPasses.cpp:1795-1800, 1851`).
12. **P12 — Ordering constraints are positional and implicit.** Strictly-earlier source / strictly-later target rules (`OutputReference.cpp:523-529, 650-657`); issues only surface as editor diagnostics ("Before module"/"After module", `MixtormatStructuralConnections.cpp:71-102`); nothing in the data model names execution position as a first-class concept.
13. **P13 — UI selection is two integer lanes** (`SelectedMaskIndex`/`SelectedEffectIndex`), index-based, one subject (`MixtormatLayerChildren.cpp:985-1033`) — adequate for depth-2, inadequate for deep nesting (UI agent finding; §9).

---

## 7. Proposed V2 data model (PROPOSED unless marked)

### 7.1 Ownership: Behaviors and Fields are children in the flat arrays

```text
FMixtormatLayer.Children (unchanged flat array, append-only enum additions):
  [Generator child]                    ← Type = Generator (unchanged)
  [Behavior child]  ScopeOwnerChildId → Generator.ChildId
  [Field child]     ScopeOwnerChildId → Behavior.ChildId
  [Mask child]      ScopeOwnerChildId → Field.ChildId   (or → Behavior.ChildId directly)
```

- New enum values appended: `EMixtormatLayerChildType::Behavior`, `::Field` (append-only rule: `MixtormatLayerTypes.h:112-119`).
- New payloads: `FMixtormatBehavior { bool bEnabled; EMixtormatBehaviorType Type; per-type payload structs }` and `FMixtormatFieldNode { EMixtormatFieldProducer Kind; FMixtormatOutputReference Source (for Source/Instance); params; composition inputs }` — mirroring the `FMixtormatGenerator` wrapper pattern ("one child type, one owner type, one dispatch", `GeneratorTypes.h:941-953`), which avoids a per-behavior child-type explosion (the documented trade-off that filters chose differently, `LayerTypes.h:115-119`).
- **Why flat arrays and not nested sub-arrays (key decision):** the runtime dependency machinery is already depth-agnostic over `ScopeOwnerChildId` chains (`OutputReference.cpp:300-345`); `IdGroup` already owns scoped input rows (`MixtormatIdTypes.h:277-281`); the published-field registry key uses `ChildIndex` into the flat array (`MixtormatGpuCompositorInternal.h:204-231`); clipboard/clone/groups all operate on contiguous `FindSubtreeEnd` blocks that survive at any depth (`MixtormatLayerChildren.cpp:214-255`). Nested sub-arrays would break the registry key, the address structs, and every dispatch switch, for no gain. **Precedent + zero-struct-change beats model purity.**
- The existing structural modules can then be *reinterpreted* as Behaviors (`StructuralWarp` → Behavior[Warp], `HeightPush` → Behavior[Push]) either by migration-on-load or by adapter (§10) — their gather code (`GeneratorGather.cpp:358-447`) and GPU dispatch (`GeneratorPasses.cpp:2703-2787`) move almost verbatim into the behavior dispatcher.

### 7.2 Distinguishing generator-original vs accumulated fields

Two explicit field producers, both backed by existing patterns:

- **`Source`** — the owning generator's native outputs (its bundle slots: Height, BoundaryField, Coverage, RegionIds, named masks; the flow tools already read exactly these, `GeneratorFlow.usf:44-46`).
- **`Previous`** — the accumulated behavior-chain state at this position (running height, current warp state `D/B`, previously published fields). Backed by `GeneratorModuleHeights` (`MixtormatGpuCompositorInternal.h:1662-1664`) and the per-target state maps (`:1666-1668`). **No self-reference**: `Previous` is always "the state as of the previous chain step", matching the strictly-earlier rule everywhere (`OutputReference.cpp:346-364`). Iteration inside one behavior is that behavior's own loop (Stain precedent).

### 7.3 Parameter schema (the one real struct change)

Today `FMixtormatParameterAddress = {LayerId, ChildId, Owner-enum, Parameter}` — two GUID levels plus a **one-hop payload dispatch** (`ParameterTypes.h:93-121`; nested payloads handled by appending enum values: `MaskNoise=25` → `Child.Mask.Noise`, `StructuralWarpFlow=26` → `Child.StructuralWarp.Source`, `ParameterBinding.cpp:176, 194`). Because Behaviors/Fields are children (each with a GUID), most addressing needs **no change**: a behavior parameter is `{LayerId, BehaviorChildId, BehaviorOwner, Param}`. What needs a decision is parameters of a *nested payload* (e.g., a per-field curve inside a behavior): either continue the proven one-hop pattern (append owner values per nested payload — works, grows linearly, proven to depth 2 only) or extend the address with an optional intermediate-GUID array (**INFERENCE:** cleaner at depth 3+, but touches the binding resolver and every remap list — schedule it as P1, not P0). `EMixtormatParameterValueType::Invalid` already gives a safe "matches nothing" for unknown addresses (`ParameterTypes.h:57-61`), so old bindings never reinterpret.

### 7.4 Output contract

Every Behavior publishes its transformed chain state **and** optionally named typed fields into the existing registry under its own `ChildIndex` (publication point precedent: `GeneratorPasses.cpp:2994-3028`). Field nodes publish typed outputs the same way. Consumption rules, completeness validation, and caching require no new machinery (§5.7).

---

## 8. Proposed execution graph (PROPOSED)

Per Generator layer (extends today's `AddGeneratorLayerPasses` walk):

```text
BehaviorChainState = { RunningHeight := layer input height (or 0 for pure generator layers),
                       WarpD/B := identity,                     // per-target keyed maps, as today
                       Bundle := owner generator's native fields }
for each child in authored order:                                  // unchanged outer walk
  Behavior child:
    1. evaluate owned Field children in authored order             // typed outputs, node-cached
       (Source → native bundle slot; Noise/Curl/Phasor → procedural pass;
        Previous → read chain state; Composite/Blend → combine inputs;
        Instance/Source refs → published-field registry read, demand-scheduled)
    2. apply scoped mask gate (independent-scope masks, as structural modules do today
       — MixtormatGpuMaskPasses.cpp:555-582)
    3. dispatch behavior shader(s) consuming typed field inputs    // one pass per behavior,
                                                                   // fusion only where proven
    4. write back chain state; publish named outputs under ChildIndex
  Generator child: family pipeline as today (native fields, normalization, pullback)
  legacy structural/height children: dispatch as today (adapter path)
```

- **Field combination** reuses the HeightBlend op set + neutral-operand rule as the generic combiner (§3.1).
- **Caching:** field nodes cache under the node cache keyed on (own settings + input keys + resolution) — the exact pattern craquelure/peel already use (`MixtormatGpuCompositorInternal.h:2340-2428`; `GpuSimulationPasses.cpp:611-640`); behavior results participate in the prefix chain like any layer content; demand collection must add behavior-owned published fields to `PublishedFieldDemand` before prefix reuse (`ComposePipeline.cpp:679-743`).
- **Publication & cross-generator reads** unchanged: registry + strictly-earlier validation; shelf producers remain the external-source scheduler.
- **Tileability invariant:** every new producer declares wrap + solve cap (§5.9).

---

## 9. UI/UX audit and three concepts

### 9.1 What exists (confirmed)

- Painted flat list, not an `STreeView`: `RebuildLayerList` rebuilds an `SVerticalBox`; indentation is a paint-only wrapper `SMixtormatLayerHierarchy` whose `FMixtormatLayerHierarchyPaint.AncestorIndents` is already an array — **arbitrary depth is supported by the painter** (`SMixtormat_Layers.cpp:31-211`; `SMixtormatLayerHierarchy.h:10-38`; `MixtormatLayerHierarchy.cpp:108-208`). The projection already renders a 4-level visual tree via `VisualParentRowIndex` (`Projection.cpp:359-377`).
- Data nesting: contiguous-subtree invariant + `FindSubtreeEnd` splices; moves re-parent only the root's `ScopeOwnerChildId` (`MixtormatLayerChildren.cpp:649-652, 214-227`; drag handlers `MixtormatLayerDragDrop.cpp:407-556`) — depth-generic.
- Collapse: `CollapsedGeneratorAddresses` + generic ancestor-hiding (`MixtormatLayerHierarchy.cpp:286-311`) — but disclosure is currently **generator-only** (`:358-375`).
- Selection: single subject, two integer lanes + group lane; robust GUID-based row resolution (`ResolveRow`, `:1410-1440`); no child multi-select.
- Clipboard: subtree copy with explicit per-family reference remapping; instances, published outputs, scoped-row snapshots (`MixtormatLayerClipboard.cpp:16-82, 101-127, 651-746`).
- Undo: snapshot history; structural edits force discrete steps (`SMixtormat.cpp:286-332`; `MixtormatStructuralConnections.cpp:961-967`).
- Inspector: per-leaf-type panels; structural modules get a Source→Operation→Target relationship header with "Go to source" (`MixtormatInspectorGenerators.cpp:891-1023`; `MixtormatStructuralConnections.cpp:521-554`); parameter Follow/Link/Driver rows with driver popover (`SMixtormat_Parameters.cpp:659-726`).

### 9.2 The critical requirement

The brief forbids "merely visually projecting Structural Warp under its target again". The audit agrees with the direction but records the counter-evidence: the prior design doc rejected real nesting (Model A) for ordering-semantics reasons (`generator_interaction_ux_design.md` §2–3) — so **V2's execution semantics (§12 D1) must be settled first**, otherwise the UI will encode an ambiguity. Once settled, the change surface is: `CanKeepScopedPlacement` legality table, creation menus/gates (`CanAddGeneratorModule` "root of a Generator layer only", `MixtormatLayerChildren.cpp:1636-1642`), disclosure permission (generator-only today), inspector panel composition, and the retirement of projection repair logic for genuinely-nested nodes. Badges/icons/rows survive unchanged (fixed badge column, one glyph per type; deep trees consume indent linearly — acceptable at Behavior/Field depths, matching the depth-4 ceiling `MixtormatLayersPrivate.h:12`).

### 9.3 Three UI concepts (for later annotated mockups; all reuse existing dark compact tokens, `MixtormatDesignTokens.h` recipes)

**A. Compact nested stack** — Behaviors and Fields appear as ordinary indented rows under the generator in the existing stack (paint rails, chevrons enabled on Behavior rows, existing badges). Fields render as single-line chips (`FX Noise · 0.5 · ⌄`) with inline enable/preview. Editing happens in the Inspector as today.
*Advantages:* smallest delta; reuses every row primitive, drag target, badge, clipboard path; the projection already proved the visual model. *Weaknesses:* deep chains push parameter editing fully into the Inspector (context switching); field-level comparisons are hard; a generator with 8 behaviors × 3 fields reads tall and uniform — hierarchy is typographic only.

**B. Nested Behavior cards with field rows** — each Behavior is a self-contained card (header: icon, name, enable, collapse; body: its Fields as mini-rows with type glyph, key param slider inline, mask chip; footer: publish toggles). Cards nest one level (Field rows never nest further; masks attach as chips). Drop targets accept fields onto cards.
*Advantages:* ownership is unmistakable; per-behavior mental model matches "Behavior = transformation, Fields = inputs"; inline field editing reduces Inspector trips; collapse hides whole behaviors cheaply. *Weaknesses:* new widget family (card layout, measured widths) in a column already budgeted at ~20px rows; card nesting re-implements container logic the flat list gives for free; drag/drop and clipboard need card-aware rules; most divergence from existing recipes (risk against `AGENTS.md` UI rules).

**C. Hybrid stack with Inspector field editor** — the stack shows Behaviors as indented rows (concept A), but each Behavior row carries a **field summary strip** (compact inline chips listing its fields: `◌ Flow ● Noise ◧ Mask`), and selecting a Behavior switches the Inspector to a two-pane editor: behavior parameters + a field list where each field is edited in place (type picker, source picker reusing `SMixtormatStructuralSourcePicker`, per-field mask assignment, preview toggle). Creation: generator row menu → "Add Behavior ▸ Warp/Fold/Push/Carve/Wrinkle", which inserts the Behavior + a default Field child in one edit (the `ApplyChildCreationDefaults` pattern, `MixtormatLayerChildren.cpp:771-843`).
*Advantages:* keeps the proven flat-stack mechanics (drag, clipboard, undo) while making ownership visible (summary strips answer "what feeds this behavior?" without expanding); Inspector already routes per-child; cheapest path to retire the projection code; scales (strips collapse when narrow). *Weaknesses:* two surfaces for the same object (row vs Inspector pane) must stay synchronized; summary strips add a per-row widget cost; less visually exciting than B.

**Recommendation: C (hybrid).** It achieves genuine nested ownership with the smallest new widget surface, directly reuses the depth-generic primitives the audit confirmed (paint rails, subtree splices, address-keyed collapse, subtree clipboard), and it retires the projection rather than extending it. B's card system is the right *second step* if behaviors become the dominant authoring object; A alone under-communicates composition.

---

## 10. Migration & compatibility

### 10.1 Strategy recommendation: **adapters first, one-time storage migration second, legacy path for effects**

- **Adapters (P0–P1):** legacy `StructuralWarp`/`HeightPush`/height-sublayer children keep their child types and gather/dispatch paths untouched; the V2 behavior dispatcher calls the same `Add*` helpers when it encounters them. Zero serialized risk; V2 proves itself beside the legacy path. Precedent: deprecated-but-kept fields (`FMixtormatGenerator.HeightBlend` "Kept only so existing assets load; it is never read", `GeneratorTypes.h:984-988`).
- **One-time migration (P2, opt-in per asset or on load with version guard):** translate legacy structural children into Behavior+Field children **only after** V2 demonstrates equivalent behavior (the brief's own gate). Translation is data-only: `StructuralWarp{Source,Target}` → `Behavior[Warp]{ Field[Source:Flow], TargetChildId }`; the enum append rule keeps old values valid even untranslated (`LayerTypes.h:112-119`); `UMixtormatMaterial::PostLoad` is the established normalization hook (`MixtormatMaterial.cpp:225-267`).
- **Legacy execution path (indefinite for effects):** generator flow tools are `EMixtormatEffectType` values — leave them; expose Behaviors as the new authoring surface and let effects remain a compatibility family. Do **not** delete anything until V2 shows equivalent behavior (brief requirement; matches `AGENTS.md` "do not remove… merely look unused").

### 10.2 Migration matrix

| Existing feature (serialized) | V2 equivalent | Compatibility strategy | Preserved guarantees |
|---|---|---|---|
| `StructuralWarp` child (`LayerTypes.h:164, 275-276`) | Behavior[Warp] + Field[Source: Flow/UVMap] | Adapter at dispatch; optional P2 on-load migration; enum value retained | Child GUID, bindings, `Source`/`TargetChildId` remap lists (`ParameterBinding.cpp:825-829`), clipboard (`LayerClipboard.cpp:55-79`), projection until retired |
| `HeightPush` child | Behavior[Push] + Field[Source: Height] | Adapter; Strata-only target retained until V2 generalizes targets deliberately | GUID, Amount, per-target shift accumulation semantics (`GeneratorPasses.cpp:2754-2787`) |
| Height Blend/Curve/ColorRamp sublayers | Field combiners / transform fields | Adapter (they *are* field ops); migrate last — they have no target semantics | Op enums (`GeneratorTypes.h:624-635`), neutral-operand behavior (`GeneratorPasses.cpp:2789-2808`) |
| Generator flow tools (ShapeDeform/Flow/FlowCarve/GravityFlow as Effects, `MixtormatEffect.h:58-62`) | Behaviors (same math) | **Legacy path** — keep effects; V2 behaviors wrap/reuse the same `AddGeneratorFlowToolPasses` helpers; new authoring prefers Behaviors | Effect enum values, all params (`:1084-1170`), per-tool mask semantics |
| `Generator.HeightSource/WarpSource` sockets (dormant) | Field[Source] nodes | Decide: activate as Field-Source or deprecate UI (P0 decision, §12 D6); serialized fields retained regardless | Disabled-by-default defaults (`GeneratorTypes.h:974-982`) |
| `OutputReference` children / mask published sources | Field[Source] with owner-kind | Unchanged (already typed) | OwnerKind discriminator, shelf addressing (`OutputReference.h:38-96`) |
| Sources shelf entries | Unchanged (external field producers) | Reuse demand DAG as-is (`SourceGather.cpp:54-228`) | `SourceId` identity; shelf-mask preservation from latest commit (`ParameterBinding.cpp:831-839`) |
| Parameter bindings to legacy children | Same addresses | No change while adapters live; P1 address extension only for new nested payloads | `EMixtormatParameterOwnerType` values append-only (`ParameterTypes.h:25-47`) |
| Groups (one-level broadcast) | Same; behaviors inside shared children initially prohibited | Preserve current force-disable rule (`LayerGroups.cpp:349-357`) until group semantics decided (§12 D5) | `MakeEffectiveChildId` derivation |
| Undo snapshots | Structure compare already depth-correct (`SMixtormat.cpp:239-258`) | No change | 0.3 s coalescing rule |
| Enum values / redirects | — | Append-only + `Config/DefaultMixtormat.ini` `[CoreRedirects]` (today exactly two redirects: Chipping→Breakup, Fracture→Cracks) | Serialized-by-value stability |
| Shader functionality | — | All `.usf` retained; V2 adds passes, does not edit existing ones in P0/P1 | Byte-identical outputs for legacy paths |

---

## 11. Smallest complete V2 implementation slice (the decision gate)

**Goal:** prove real nested ownership + typed field composition + GPU execution with the minimum new surface. Everything below is PROPOSED, with the files likely touched.

**Slice definition — "One Behavior, Three Fields, One Mask":**

1. **`EMixtormatLayerChildType::Behavior`** (appended) + `FMixtormatBehavior { bEnabled; EMixtormatBehaviorType Type; FMixtormatGeneratorStructuralWarp Warp; }` — the pilot behavior **is** the existing Structural Warp payload, so the math is proven and the comparison against the legacy path is bit-for-bit meaningful.
2. **Field children under it** (new `::Field` type + `FMixtormatFieldNode`): `Source` (reads the generator's published Flow/UVMap — reuses `ResolveGeneratorInputSource`), `Noise` (reuse `FMixtormatNoise` + `GpuNoisePasses` publication), `Previous` (reads the behavior-chain warp state). One combiner field (add/lerp of two inputs with the HeightBlend neutral rule) proves composition.
3. **Scoped Mask under the Behavior** — proves 3-level nesting with gating (machinery already exists: `CanOwnScopedMasks` extension + `AddScopedFeatureMask` independent scope).
4. **Execution:** inside `AddGeneratorLayerPasses`, the Behavior child evaluates its fields then calls the existing `AddReferencedFlowUVPass` + `FMixtormatGeneratorStructuralWarpCS` composition, writing into `GeneratorStructuralDisplacements[target]` — i.e., the legacy target pullback consumes the result **unchanged**.
5. **Publication:** the Behavior publishes its composed displacement as a typed field under its own `ChildIndex` (registry reuse).
6. **UI (concept C, minimal):** Behavior row nested under generator (paint rails already support it), disclosure enabled for Behavior rows, Inspector panel with Source→Target header + field list. Creation via generator context menu.
7. **Serialization/compat:** adapter rule — a legacy `StructuralWarp` child dispatches through the same helper; nothing migrates yet.

**Exit criteria:** a saved project with legacy StructuralWarp and a new Behavior[Warp] on different targets renders identically to intent; behavior-owned mask gates only its own contribution; clipboard copy/paste of a Behavior+Fields+Mask subtree re-resolves all references; undo round-trips; prefix cache invalidates on field-parameter edits; no new address structs required.

**Likely files (P0):**
- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h` (enum + payloads), new `MixtormatBehaviorTypes.h`
- `Source/MixtormatRuntime/Private/MixtormatChildScope.cpp` + `Source/MixtormatEditor/Private/Widgets/MixtormatLayerChildren.cpp` (placement legality)
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp` + `MixtormatLayerClipboard.cpp` + `MixtormatLayerGroups.cpp` (new reference fields in remap lists — P9 discipline)
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp` (behavior gather), `MixtormatGpuGeneratorPasses.cpp` (behavior dispatch in the walk), `MixtormatGpuCompositorInternal.h` (render data + chain-state maps)
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`, `MixtormatLayerActions.cpp`, `MixtormatLayerHierarchy.cpp` (rows/disclosure), `Widgets/Inspector/MixtormatInspectorGenerators.cpp` (panel)
- New `Shaders/Private/MixtormatGeneratorBehaviorField.usf` only if the Noise-field variant needs a dedicated pass; otherwise reuse `MixtormatGpuNoisePasses.cpp` publication.

---

## 12. Unresolved design decisions (must be settled before implementation)

- **D1 — Execution position semantics (the blocking one).** When a Behavior is a child of its target generator, does it execute at the target's position (true ownership; changes Warp/Push composition order and saved-project results) or does authored position remain authoritative (ownership = gating/editing only)? The prior design doc rejected Model A precisely over this (`generator_interaction_ux_design.md` §3). Options: (a) owner-position execution with a defined state model; (b) keep positional execution, nesting = ownership/gating; (c) per-behavior flag. **Recommended:** (a) for new Behaviors, with legacy adapters keeping positional semantics; but this changes results for migrated assets — needs an explicit compatibility decision.
- **D2 — Pullback vs regeneration unification.** Should non-Strata targets ever regenerate under warp (fixing the bilinear/nearest ID split, P2), or is pullback-with-declared-remap-policies acceptable long-term?
- **D3 — Parameter address extension.** One-hop owner-enum pattern vs optional intermediate-GUID array in `FMixtormatParameterAddress` (§7.3).
- **D4 — Maximum scope depth.** Behavior→Field→Mask under a generator hits exactly 3; raise `MaximumScopeDepth` (`MixtormatLayersPrivate.h:12`) to 5 now or audit all load-bearing uses first (it gates `CanAddScopedChild` and ID-group subtree checks, `MixtormatLayerChildren.cpp:620-624`, `MixtormatLayerDragDrop.cpp:990-1000`)?
- **D5 — Group semantics for Behaviors.** Shared-stack Behaviors: prohibited like structural modules today (`MixtormatLayerGroups.cpp:349-357`), or enabled via `MakeEffectiveChildId` remapping (which already handles member-local references)?
- **D6 — Dormant sockets.** Activate `HeightSource`/`WarpSource` as Field[Source] (completing P4) or deprecate their UI and keep them serialized-only?
- **D7 — Behavior catalog breadth at V1.** Which of Warp/Distort/Push variants/Carve/Deposit/Wrinkle/Fold ship in the first behavior set? (Audit recommendation: Warp, Push (generalized targets), Carve/Deposit, Wrinkle. Defer Distort and surface Fold until curl/phasor producers land.)
- **D8 — SDF producer.** Who produces the declared `SDF` kind (a dedicated distance-solve field producer vs promoting `BoundaryField` pairs to public kind with validity aux)?
- **D9 — Wrinkle AO/thickness inputs.** Publish the AO pass output as a field (new producer + demand wiring) or restrict Wrinkle to height/coverage stand-ins initially?
- **D10 — Noise `Gradient` semantics.** Keep `SourceFrameVector` transport-only semantics under warp (current, conservative — `MixtormatGpuCompositorInternal.h:1137, 1177-1179`) or define a directional contract (breaks "not a Flow" boundary, `OutputReference.h:33-35`)?
- **D11 — Legacy duration.** How long do adapters live before on-load migration becomes default (asset-version gate design)?
- **D12 — Preview model for Behaviors.** Per-behavior preview targets (like child previews today, `MixtormatGpuCompositor.h` preview enums) vs published-field-only previews?

---

## 13. Implementation roadmap

**P0 — The proof slice (§11).** Nested Behavior+Field+Mask, Warp pilot, adapter dispatch, minimal UI, publication, clipboard/undo coverage. *Deliverable:* decision-gate demo. No legacy behavior change.

**P1 — Catalog & composition.**
- Generalize Push targets beyond StrataCarver (removes P3; `MixtormatOutputReference.cpp:636-657`).
- Carve/Deposit behaviors wrapping the flow-tool apply path; document the ID-preservation semantic (P11) as a behavior capability flag.
- Field producers: Curl (lift `MixtormatGully.ush:71-86`), Phasor (generalize `MixtormatNoise.ush:226-235`), Curvature (from §4), SDF (resolve D8).
- Field combiner set (HeightBlend op set generalized; masks on fields).
- Parameter address extension per D3; retire structural projection for migrated/legacy nodes; activate-or-remove dormant sockets per D6.
- Files: as P0 plus `MixtormatGpuNoisePasses.cpp`, `Effects/MixtormatFlowWarpPasses.cpp` (curl lift), `MixtormatParameterTypes.h`, projection files.

**P2 — Wrinkle & migration.**
- Wrinkle behavior per §4 (ping-pong diffusion pass in `MixtormatGpuSimulationPasses.cpp` style; fold-mask publication; previews).
- On-load migration (D11) translating legacy structural children; group semantics per D5; StrataCarver dead gates decision (P5); remap-policy descriptors per §5.8.
- Surface Fold/Distort only after curl/phasor proven.

---

## 14. Documentation-claim cross-check (required by the brief)

- `GENERATORS.md` and `COMPOSITION.md` were **accurate against source** on every point this audit verified (structural gather paths, pullback vs regeneration, shelf scheduling, caching, typed kinds). Two doc-drift items found elsewhere: `AgentDocs/UI.md:132-135` describes the stack row as "an editable source chip" while source renders the fuller `ConnectionContent` (`MixtormatStructuralConnections.cpp:309-380`); `flow_generation_core.md`'s owner list predates CliffStrata/Noise ownership. The stale in-code comments of P8 remain the largest doc-vs-source hazard.
- Prior plans consulted and explicitly **not** treated as implemented: `generator_interaction_ux_design.md` (proposal; its Model A rejection is load-bearing for V2, its 1.7 clipboard defect is now fixed in source), `Concepts/mixtormat-generator-ux/AGENT_HANDOFF.md` (HTML mockups only), `wrinkle_curvature_pinch_plan.md` (Phase 1 step 1 storage only; no evaluation), `strata_structural_warp_design.md` (design contracts, matches source where checked).
