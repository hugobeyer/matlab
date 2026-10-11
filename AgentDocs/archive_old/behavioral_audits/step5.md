I'll begin by mapping the repository structure and reading the core runtime/shader files in parallel.

This is a large codebase. Let me create a task list and begin systematic reading.

Reading core runtime types now.

Now the layer types, effect types, and parameter types.

Now Effect types, HeightTypes, and ParameterTypes.

Let me read the rest of Effect.h, ParameterTypes, and the ParameterBinding files.

Excellent detail on the runtime model. Now the GPU gather and compositor core.

Now the compositor internal header — the render data contract.

Now the key generator shaders and the AgentDocs. Reading in parallel.

Now the AgentDocs and the mandatory Wrinkling prototypes.

Now the subsystem docs and structural connection UI files.

Now the UI hierarchy and structural connection files.

Let me map the big generator passes file by locating its key functions.

Let me read the main `AddGeneratorLayerPasses` walk and its surrounding context.

Let me examine the flow-tool dispatch and structural-warp helpers in the generator passes.

Let me confirm the pipeline ordering of generators, effects, and composite in the compose pipeline.

I have enough to finalize. Let me verify the shelf source gather and clipboard/instance specifics quickly before writing.

# Mixtormat — Behavior System V2 Architecture Audit

**Audit only.** No files changed, no build, no branch. All claims below are tagged **[CONFIRMED]** (read in source, with file:line), **[INFERENCE]** (reasoned from confirmed facts), or **[PROPOSED]** (new architecture). The Wrinkling prototypes are OpenCL mesh-space algorithms — *not* a Mixtormat tool.

---

## 1. Executive verdict

Behavior System V2 is **the right directional model but the wrong framing as a replacement**. The proposal's core insight — "Behaviors are actual children of generators, not projected sibling operations; Fields are typed children of Behaviors" — is correct, and it *already partially exists* in two incompatible forms. V2 should be framed as **unification of two existing, real mechanisms**, not a green-field rewrite.

Key findings that must shape the design:

1. **A typed field model already exists** ([CONFIRMED], `MixtormatOutputReference.h:20-36` `EMixtormatPublishedFieldKind`; `MixtormatGpuCompositorInternal.h:237-284` per-kind storage + `FPublishedField`; `:1128-1158` `FGeneratorBundle::EFieldSemantic`/`EFieldUnits`). V2 must **adopt and extend** this, not invent a parallel type system.
2. **Genuine nested ownership already exists for flow tools** ([CONFIRMED], `MixtormatGpuGeneratorPasses.cpp:1459-1464` — Shape Deform / Generator Flow / Flow Carve / Gravity Flow are `EMixtormatEffectType 9-12` scoped under a generator via `ScopeOwnerChildId`, executed on the owner's own height in `AddGeneratorFlowToolPasses` `:1573-1875`). The prompt's own example tree ("Behavior — Warp → Flow — Own Height") is almost a literal description of Generator Flow / Gravity Flow.
3. **The only genuinely "projected sibling" mechanism is Structural Warp + Height Push** ([CONFIRMED], `:2703-2787`). These are top-level children that write per-target-index side-channel maps (`GeneratorStructuralDisplacements`, `GeneratorHeightPushFields`) consumed by a *later* generator. This is the fragmentation the proposal is right to attack.
4. The **CPU-side structural connection projection** (`MixtormatStructuralConnectionProjection.cpp`, 363 lines) exists purely to **visually re-order the flat child list so warp/push appears nested under its target** — a display engine built to compensate for the lack of real nesting. This is exactly what the proposal says not to rebuild. It should be retired, not ported.

**What I would change in the proposal:** Do not make "Behavior" a brand-new child node type that re-implements gather/dispatch/inspector/clipboard from scratch. Instead:
- Introduce **one** new child type, `FMixtormatBehavior` (Generator's own subtree, real nesting), that **reuses the existing flow-tool stage machinery** (`MixtormatGeneratorFlow.usf` STAGE 0–9) and the existing `FPublishedField`/`FGeneratorBundle` typed-field contract.
- Promote "Field" from an implicit concept to a **first-class typed node** that owns a `FMixtormatOutputReference` producer but is *evaluated locally* (not a global published dependency).
- Keep Height Blend / Height Curve / Color Ramp / Height Push / Structural Warp exactly where they are as a **legacy execution path** until V2 proves parity (see §7).

---

## 2. Current architecture map

### Data ownership (CPU)
| Concept | Owner | File |
|---|---|---|
| Generator layer → flat `Children` array | `FMixtormatLayer` | `MixtormatLayerTypes.h:590-591` |
| Generator child (kind + payload union) | `FMixtormatLayerChild.Generator` | `MixtormatLayerTypes.h:243`, `MixtormatGeneratorTypes.h:955-1007` |
| Generator explicit input sockets | `FMixtormatGenerator.HeightSource/WarpSource` | `MixtormatGeneratorTypes.h:969-982` |
| Generator-flow **scoped effects** | child of `Type==Generator`, via `ScopeOwnerChildId` | `MixtormatEffect.h:58-62`; scope resolve `MixtormatChildScope.cpp:19-36` |
| Structural **projected modules** | top-level children `HeightPush`/`StructuralWarp` | `MixtormatLayerTypes.h:159-164`, `:272-276` |
| Typed field handle | `FMixtormatOutputReference` + `EMixtormatPublishedFieldKind` | `MixtormatOutputReference.h:48-97` |
| Field values (RG) | `FGeneratorBundle` + `FPublishedField` | `MixtormatGpuCompositorInternal.h:1124-1189,261-284` |
| Sources shelf | `FMixtormatSourceEntry` (root generator + `OwnedChildren`) | `MixtormatLayerTypes.h:334-356` |
| Parameter address / reference / driver | `FMixtormatParameterAddress`, `FMixtormatParameterDriver` | `MixtormatParameterTypes.h:93-221` |

### GPU execution (render thread)
`AddGeneratorLayerPasses` (`MixtormatGpuGeneratorPasses.cpp:2685`) walks `Layer.Children` **in authored order** and does, per child:

- **StructuralWarp** (`:2703-2753`): trace source Flow/UV → destination displacement `D`; `D_new = d + sample(D_old, psi)`, `B_new = sample(B_old, psi)`, `psi = x + d` (`MixtormatGeneratorStructuralWarp.usf:49-53`); stored in `GeneratorStructuralDisplacements[TargetChildIndex]`.
- **HeightPush** (`:2754-2786`): `OutShift = Previous + Amount*Gate*SourceHeight` (`MixtormatGeneratorHeightPush.usf:23`), stored in `GeneratorHeightPushFields[TargetChildIndex]`.
- **HeightBlend/Curve/ColorRamp** (`:2789-2861`): rewrite `RunningHeight` in place / publish `Color`.
- **Generator** (`:2863-3030`): native height → owned flow tools (`AddGeneratorFlowToolPasses`) → signed normalize + HeightScale → structural-warp pullback (`RemapCompletedGeneratorBundle`, `:2940-2975`) → publish `Height`/`RegionIds`/`NamedMasks` → `AddGeneratorModuleCombine`.
- Flow Warp (Filter) and other effects run **after** `AddLayerCompositePass` (`MixtormatGpuComposePipeline.cpp:1156,1183`; generator modules skipped in the post-composite effect loop `Compositing… .cpp:1006-1022`).

**Caching:** generator fields are node-cached on *field-shaping settings only* (e.g. Rock `:212` skips `RockHeightScale`/`RockNormalizeHeight`); `FMixtormatNetworkCache` (byte-bounded, `:73-181`), `FMixtormatPrefixCache`, `FMixtormatNodeCache`. Shelf producers run **uncached**, folded into the prefix seed.

**The critical structural fact:** the "later target" mechanism is real execution, not projection. Structural Warp/Push create forward-looking state that a subsequent generator consumes when it evaluates. The *visual* nesting is UI-only compensation.

---

## 3. Inventory (confirmed behavior + reuse verdict)

| Feature | Implementation | Where | Status |
|---|---|---|---|
| **Structural Warp** | child → Flow/UV trace → per-target displacement map, consumed by target | `MixtormatGeneratorStructuralWarp.usf`; `:2703` | **CONFIRMED** — Retain as legacy path; the projection is what V2 replaces |
| **Height Push** | child → signed source height → per-target bedding shift (Strata regenerates) | `MixtormatGeneratorHeightPush.usf`; gather `MixtormatGeneratorGather.cpp:358-376`; `:1361-1363` | **CONFIRMED** — maps cleanly to a "Push (bedding)" Behavior |
| **Height Blend** | sublayer combines running height with another module's | `MixtormatGeneratorTypes.h:640-683`; `GatherGeneratorHeightModuleChild:448-483`; `AddGeneratorHeightBlendPass:955` | **CONFIRMED** — already "field composition" spec |
| **Height Remap** (Height Curve) | signed ramp remap of running height | `MixtormatGeneratorTypes.h:741-794`; `:985` | **CONFIRMED** — reusable as a scalar field transform |
| **Height Color Ramp** | signed-height → color, publishes `Color` | `MixtormatGeneratorTypes.h:814-848`; `:1021`; `:2815-2861` | **CONFIRMED** — reusable as a color field producer |
| **Generator Flow** | seed→JFA→smooth→resolve→RK2 trace advection | `MixtormatGeneratorFlow.usf` STAGE 0/1/7/2/3; `AddGeneratorFlowToolPasses:1573` | **CONFIRMED** — **this is the field-solve + warp skeleton to reuse** |
| **Gravity Flow** | texture-space gravity, height/boundary steered, back-trace | `MixtormatGeneratorFlow.usf` STAGE 9/3 mode 3; mode `3u` `:561-574` | **CONFIRMED** — reusable as "Gravity" field producer |
| **Flow Carve** | distance-biased min/max along trace (Groove/Deposit) | `MixtormatGeneratorFlow.usf:503-616` | **CONFIRMED** — maps to "Carve / Deposit" Behavior |
| **Shape Deform** | boundary-normal expand/erode + bulge | STAGE 3 mode 0 `:528-557` | **CONFIRMED** — maps to "Distort" Behavior |
| **Flow Warp** (effect) | curl-noise surface-channel + mask distortion, post-composite | `MixtormatFlowWarpPasses.cpp:132-336`; class Filter `MixtormatEffect.h:126-147` | **CONFIRMED** — "Curl" field producer already exists but runs post-composite; reuse the curl generator, not the insertion point |
| **Fold / Bend** | Strata `Bend`/`BendScale` + Breakup `Fold` | `MixtormatGeneratorTypes.h:126-130,893`; Erosion/Breakup `MixtormatBreakupPasses.cpp:371` | **CONFIRMED** — fragmented across 3 systems; candidate for consolidation, not rewrite |
| **Breakup** | multi-scale SDF spent 4 ways (relief/fold/crease/push) | `MixtormatBreakupPasses.cpp:371`; params `MixtormatEffect.h:800-946` | **CONFIRMED** — "Fold + Crease + Push on a field" already; the closest existing analogue to a Behavior |
| **Wrinkle** | none | `AgentDocs/Prototypes/Wrinkling/*.cl` | **ABSENT** — must be built (see §5 of deliverable / §5 below) |
| **Generator flow source inference** | Signed Distance or Height | `MixtormatGeneratorFlow.usf:160-182,197-218` | **CONFIRMED** |
| **Distance solve grid** | capped at 1024 | `MixtormatGpuGeneratorPasses.cpp:48` `DistanceSolveMaxSize` | **CONFIRMED** — JFA cost ceiling |
| **Node caching** | settings-hash per generator | `MixtormatComposeHash`, `:149-346` | **CONFIRMED** — reusable, must extend to field/behavior keys |
| **Sources shelf** | topological demand, DAG, cycle-exclusion | `MixtormatSourceGather.cpp:54-229` | **CONFIRMED** — instance/external-source pattern to reuse for Behavior "Source/Instance" |

---

## 4. Confirmed architectural problems (with evidence)

**P1 — Two incompatible nested-ownership mechanisms.** Flow tools are scoped children (real nesting, `ScopeOwnerChildId`), while Structural Warp/Push are flat children referencing a *forward* target by GUID. `MixtormatLayerTypes.h:159-164` vs `MixtormatEffect.h:58-62`; dispatch `MixtormatGpuGeneratorPasses.cpp:2703` vs `:1607-1612`.

**P2 — A 363-line UI projection engine exists solely to fake nesting.** `MixtormatStructuralConnectionProjection.cpp:87-379` recomputes group/instance/visual-parent mappings so a warp/push *appears* under its target. Its own header comments say "not merely visually projecting… again" (`GENERATORS.md:158-172`). This is dead weight the moment real nesting exists.

**P3 — Field types exist but are per-producer, not composable.** `FGeneratorBundle` carries typed `NamedMasks` with descriptors (`:1171-1189`), but each producer hard-codes its own `Remap*Field(…, Stage, …)` path (`:1049-1220`). There is no generic "convert field type A→B" or "blend two fields" pass — Height Blend is the only true field-combine (`AddGeneratorHeightBlendPass:955`).

**P4 — "Curl/vector generation" is separated from "the operation using the vector."** Flow Warp generates curl and applies it to channels in one Effect (`MixtormatFlowWarpPasses.cpp`); Generator Flow generates a direction field and applies it in one pipeline. V2's "a Behavior consumes a Field" split is *correct and new* — currently no vector field is reusable across two Behaviors.

**P5 — Generator."Height Blend" is gone but generators were always "plain signed field producers."** `MixtormatGeneratorTypes.h:984-988` (deprecated), `GENERATORS.md:620-635`. Combination lives only in layer sublayers — the model is *almost* ready for typed field composition but never generalized beyond height.

**P6 — Self-reference is only resolvable backward.** `ResolveGeneratorInputSource` requires strictly-decreasing order (`MixtormatOutputReference.h:253-257`); shelf producers exclude self-cycles by exclusion (`MixtormatSourceGather.cpp:81-88`). "Behavior reads Generator-original vs Previous Behavior Output" is not representable yet — both would be a plain backward reference.

---

## 5. Deliverable sections 5–10 (V2 model, graph, UI, migration, roadmap, open questions)

### 5. Proposed V2 data model
```
FMixtormatLayerChild (Type == Behavior)          // NEW, appended to EMixtormatLayerChildType
  FMixtormatBehavior
    EBehaviorKind   Warp|Distort|Fold|Push|Carve|Wrinkle|ShapeDeform   // appended enum
    FGuid           Scope? / SourceRef / InstanceSource
    ECombineMode    Blend|Replace|Max|Min|Add|Chain                    // how fields combine
    TArray<FMixtormatBehaviorField>  Fields         // NEW typed field children
    FMixtormatGeneratorHeightBlend  Blend           // reuse existing struct
    FMixtormatParameterBinding xN (Driver/Ref)      // reuse existing
```
Fields reuse `FMixtormatOutputReference` producers but resolve **locally within the Behavior/Generator subtree**, not through `Ctx.PublishedFieldOutputs`. Field *kinds* = existing `EMixtormatPublishedFieldKind` (add nothing; `SDF` and `Vector2` already cover distance/vector). Reuse `FGeneratorBundle::EFieldSemantic` (`:1128-1158`) as the conversion matrix.

- **Self-input snapshot:** introduce `EFieldOrigin { GeneratorOriginal, PreviousBehavior, FieldInstance }` on each field — resolved to a *snapshot texture* of the running bundle at that point, not a forward edge. **[PROPOSED]**
- **Masks gate fields and Behaviors** via the existing `CanOwnScopedMasks` (`MixtormatChildScope.cpp:38-57`) — extend to cover `Behavior`/field types.

### 6. Execution graph
- **Field evaluation:** each Behavior evaluates its Fields into a local typed struct mirroring `FGeneratorBundle`; conversions via `EFieldSemantic`; blend via existing `MixtormatGeneratorHeightBlend` math (`MixtormatHeightTypes.h:41-77`).
- **Behavior order:** authored order inside the generator's own subtree, evaluated by extending `AddGeneratorFlowToolPasses` (`:1573`) — each Behavior's "apply" is a `FLOW_STAGE 3` variant or a new compute pass.
- **Caching:** reuse NodeCache keyed on the new behavior/field structs (`MixtormatComposeHash`); field producers (Noise/Gravity/Height-Gradient/Contour/Curl/Procedural/Phasor) get their own tiny caches like `MixtormatNoiseRenderStore` (`MixtormatGeneratorGather.cpp:307`).
- **Publication:** unchanged — Behavior still publishes `Height`/`RegionIds`/`NamedMasks` at the *generator* index. **[CONFIRMED → reuse]**

### 7. Three UI concepts (all fit the existing compact dark Shell style)

| Approach | Description | Strengths | Weaknesses |
|---|---|---|---|
| **A. Compact nested stack** | A flat tree-view with indent depth; Field rows as single-line leaves under a Behavior row (like `SMixtormatLayerGroupRow` + `SMixtormatLayerChildRow`) | Minimal new widgets; matches existing hierarchy rows; natural collapse/reorder/drag | Deep Behaviors get visually thin; field-type richness under-shown |
| **B. Nested Behavior cards with field rows** | Each Behavior is a foldout card; fields are editable rows with a kind chip + mask gate button | Clear field *type* semantics; mirrors Inspector cards; best for the "Behaviors own Fields" mental model | More vertical space; needs new card recipe from Design tokens |
| **C. Hybrid stack + Inspector field editor** *(recommended)* | Left column = compact nested stack (A) for structure/collapse/drag/instance; right Inspector edits the selected Field/Behavior in place (like `MixtormatInspectorGenerators`) | Reuses both existing systems almost verbatim; separation of tree vs detail; strongest for parameters/masks/drivers | Two placement rules to keep consistent; selection state must sync |

**Recommended: C.** It reuses `SMixtormatLayerHierarchy`/`SMixtormatLayerChildRow` for the stack and the existing Inspector panel pattern for editing, adds one new row icon + one Field inspector section, and is the least likely to drift from the left-column visual contract in `AgentDocs/UI.md`.

### 8. Migration matrix (old → V2 → strategy)

| Existing | V2 equivalent | Strategy |
|---|---|---|
| Shape Deform (scoped effect) | Behavior: Distort | **Adapter** — wrap in `FMixtormatBehavior` shim; same `AddGeneratorFlowToolPasses` |
| Generator Flow (scoped) | Behavior: Warp + Field: Flow(Own Height) | Adapter; reuse STAGE 0–3 |
| Flow Carve (scoped) | Behavior: Carve + Field: Flow | Adapter |
| Gravity Flow (scoped) | Behavior + Field: Gravity | Adapter |
| Flow Warp (Filter) | Field: Curl + Behavior: Warp (pre-composite) | **New path** + keep legacy post-composite until parity |
| Height Push (module) | Behavior: Push (bedding) | Adapter via `GeneratorHeightPushFields` |
| Structural Warp (module) | Behavior: Warp (target = next behavior/generator) | Adapter; retire the projection engine |
| Height Blend / Curve / Color Ramp | Field transforms / color field producer | Retain; become first Behaviors |
| Wrinkle | NEW Behavior (curvature/Laplacian + diffusion) | New implementation (§9) |
| All enums (`EMixtormatLayerChildType`, `EMixtormatEffectType`, `EMixtormatPublishedFieldKind`) | append-only | Never reorder ([CONFIRMED] serialized-by-value, `MixtormatOutputReference.h:15-19`) |

**Compatibility recommendation:** **adapters + explicit legacy path.** Do not one-time-migrate serialized `HeightPush`/`StructuralWarp`/flow-effect children; the editor should construct `FMixtormatBehavior` nodes whose execute branch reads the same gather structs. Old assets keep their exact child structures and execute through the existing passes; V2 nodes are additive. Delete old paths only after V2 parity is demonstrated per generator family.

### 9. Implementation roadmap (P0/P1/P2)

- **P0 — smallest proving slice:** A single generator (Strata Carver) with a **hand-authored** `FMixtormatBehavior` subtree: Behavior "Warp" containing Field "Flow — Own Height" (reusing `MixtormatGeneratorFlow.usf` STAGE 0/7/2/3 and the existing `AddGeneratorFlowToolPasses`), executing *before* combine, producing a preview. Touches: `EMixtormatLayerChildType` (+`Behavior`), `MixtormatGeneratorTypes.h` (add `FMixtormatBehavior`), `MixtormatGeneratorGather.cpp`, `MixtormatGpuGeneratorPasses.cpp` (extend `AddGeneratorFlowToolPasses`), `MixtormatChildCapabilities.cpp`, one Inspector file, one row icon. This proves real nesting + typed field + GPU without touching projection, clipboard, or any existing effect.
- **P1 — generalize:** Field kinds + `EFieldSemantic` conversion matrix; Behavior combine modes; mask gating of fields/Behaviors (extend `CanOwnScopedMasks`); clipboard/instance remap for the new child (`MixtormatLayerClipboard.cpp`, `MixtormatParameterBinding::CopyChildPayload`); retire Structural Warp projection UI behind a feature flag; Push/Fold/Carve/Distort Behaviors reusing existing passes.
- **P2 — new math:** **Wrinkle** Behavior (see below), Phasor Noise field, boundary/bedding Push variants; parameter Follow/Link/Driver coverage for Behavior fields (`EMixtormatParameterOwnerType` append).

### Wrinkle (mandatory analysis) — new implementation required
Neither prototype is implemented. Reusable math **(texture-space)**:

- **Weighted Laplacian** `wrinkling01.cl:76-94` → wrapped heightfield second-difference (finite-difference Laplacian, `avg/edge_len` weight → texel-size normalization; drop, since texels are uniform).
- **Normal-variation curvature** `wrinkling01.cl:86` (`length(Nn−N0)`) → wrapped difference of gradients reconstructed from height (`MixtormatHeightNormal.ush`), with `curv = 1-exp(-gain*k)` remap `:96-98`.
- **Curvature-dependent diffusion** `wrinkling01.cl:112-141` → **ping-pong Jacobi blur** (already the plugin's idiom — e.g. Peeling `MixtormatGpuSimulationPasses.cpp:833-862`), speed = base + curv · rate. This **needs iterations**, i.e. a solve loop with ping-pong targets, so it is a generation-stage Behavior, not a single-pass effect.
- **Fold-side + mask composition** `wrinkling02.cl:52-89` → single pass (mask_mode 0–4, contrast/gain, AO/thickness modulation) — maps to a scalar mask field.
- **Displacement** `wrinkling02.cl:96-134`: `disp = amount·avg·signed_mask` clamped by max, direction `= normalize(gravity·g + normal·n·|mask|)`, height-write `P += dir·disp`. In texture space: gravity is a constant `(0,-1)`-style vector (as Gravity Flow uses, `MixtormatGeneratorFlow.usf:363`); "normal" is the height gradient; `avg_edge` becomes a texel-height scale factor.

**Mesh→texture adaptation:** `topo:neighbours` → wrapped 4/8-neighbour taps; `avg_edge_length` → texel size in height units; the running `_grav` field `wrinkling02.cl:131-132` → either recompute per pass from the (already displaced) gradient or carry a vector render-target texture (plugin already carries `PF_G32R32F` vectors, e.g. BoundaryField/flow). Differs from Breakup (pure multi-scale SDF, no iteration), Fold/Bend (analytic sinusoidal), and Shape Deform (boundary-normal advection): Wrinkle is **curvature-driven, iteratively diffused displacement** — genuinely new.

**Wrinkle should be a Behavior with two Fields** (a Curvature/Laplacian field + a diffusion/composite field), not a bare field generator — because it *consumes* the generator's running height and *writes* new height, which is Behavior-shaped. **Parameters to expose:** curv gain/power, diffusion rate, iterations, blur radius, mask mode, contrast/gain, amount (edges), max displacement, normal:gravity weight. **Previews:** curvature field, wrinkle mask, displacement, final shaded normal.

### 10. Unresolved design decisions (must be settled first)
1. **Behavior vs Field taxonomy boundary:** is "Curl" a Field (pure vector producer) with Warp a Behavior, or one fused "Wrinkle/Curl" node? (Affects whether a single curl source can drive two Behaviors — currently impossible.)
2. **Origin semantics:** canonical way to address "Generator-original snapshot" vs "previous Behavior output" vs "own earlier Field" without creating forward edges or ambiguity.
3. **Iteration budgeting:** which Behaviors get ping-pong solves and their default cost (Wrinkle diffusion, Peeling, Stain already consume it).
4. **Retirement trigger for the projection engine** (`MixtormatStructuralConnectionProjection.cpp`): exact parity test per generator family before deletion.
5. **Scope on the Sources shelf:** `OwnedChildren` is persisted-but-unauthored (`MixtormatLayerTypes.h:350-355`) — do V2 Behaviors live on shelf producers too, or layer-only for the slice?
6. **Parameter owner for Fields:** append a `BehaviorField` value to `EMixtormatParameterOwnerType` vs reuse the Behavior owner.
7. **Boundary of Height Blend/Curve/Ramp:** keep as standalone sublayers or fold into the Behavior/Field model (currently they are the only true field-combine).

---

**Bottom line for the review conversation:** V2 is worth building as a **unification and extension** of (a) the existing generator-owned flow-tool pipeline and (b) `FGeneratorBundle`/`EMixtormatPublishedFieldKind` typed fields — starting with the P0 slice that reuses `MixtormatGeneratorFlow.usf` under a real `FMixtormatBehavior` child on one generator. The single biggest architectural win is *deleting* the structural-projection engine by making nesting real. Wrinkle is the only wholly new math and should be P2.