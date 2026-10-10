# Mixtormat — Behavior System V2 Architecture Audit

**Repository:** https://github.com/hugobeyer/matlab  
**Branch / commit inspected:** `main` @ `9c3b3b1` (“Preserve shelf mask sources and all generator module links when cloning a layer”)  
**Scope:** AUDIT ONLY — no implementation, builds, tests, or file changes.  
**Method:** Source inspection of Runtime / Shaders / Editor + AgentDocs; wrinkling prototypes read in full. Prior plan docs used only as orientation, never as proof of current behavior.

Explicit separation throughout:
- **Confirmed** = observed in source / shaders
- **Proposed** = Behavior System V2 idea under evaluation
- **Inference** = reasoned conclusion from confirmed facts

---

## 1. Executive verdict

**Is Behavior System V2 the right architecture?**

**Partially — directionally right, structurally over-specified for a first cut.**

**What is right**
- Treating deformations as *owned by* a generator (not free-floating siblings) matches how the GPU already runs flow tools: scoped under a generator, applied to that generator’s native height *before* combine (`MixtormatGeneratorFlow.usf` header; `MixtormatIsGeneratorFlowEffect` in `MixtormatEffect.h` ~L149–154).
- Separating **field producers** (Flow, Curl, Noise, Gravity, Source/Instance) from **operations that consume them** (Warp, Push, Carve, Wrinkle) is already partially present (`EMixtormatPublishedFieldKind`, `FMixtormatOutputReference`) and is the correct long-term model.
- Ordered execution, masks on fields/behaviors, and distinguishing generator-original vs accumulated/previous fields are real needs the current system solves only awkwardly.

**What is wrong or risky**
1. **True nested ownership as the primary data model** conflicts with the canonical flat `Layer.Children` order, enum-by-value serialization, and the entire ScopeOwner / Instance / clipboard / group expansion machinery (`FMixtormatLayerChild` in `MixtormatLayerTypes.h` ~L105–276; AGENTS.md: “enum values are serialized by value: append, never reorder”).
2. **“Behavior” as a new first-class child type** duplicates three existing mechanisms:
   - Generator modules (`HeightBlend`, `HeightCurve`, `HeightColorRamp`, `HeightPush`, `StructuralWarp`)
   - Scoped effects (`ShapeDeform`, `GeneratorFlow`, `FlowCarve`, `GravityFlow`)
   - Visual structural projection (reference edges, not reparenting)
3. **Fields as children of Behaviors** is heavier than the existing published-field + OutputReference graph, which already supports Source/Shelf/Instance and typed kinds without a second hierarchy.
4. **GPU execution** is already a linear walk of children with RDG textures keyed by child index (`AddGeneratorLayerPasses` in `MixtormatGpuGeneratorPasses.cpp` ~L2685+). Nested ownership would require either expand-to-flat at gather time (keeping execution flat) or recursive passes — the former preserves GPU reality; the latter adds complexity without better results.
5. **Wrinkle** is not an implemented tool. The OpenCL prototypes are mesh-neighbor algorithms; promoting them to a Behavior without a texture-space Laplacian/curvature plan is premature.

**Recommended change to the proposal**
- Keep **flat serialized children** and **ScopeOwnerChildId** as the ownership mechanism.
- Introduce a **semantic Behavior catalog** and **typed field graph** on top of existing modules/effects, not a parallel nested tree.
- Use **true nesting only in the UI model** (collapse/expand under generator) with execution still ordered by the flat chain — the opposite of the current “project Structural Warp under target” approach, and also the opposite of rewriting ownership into deep trees.
- Prove V2 with one slice: **Structural Warp + Flow field as owned behavior under Rock Formation**, with typed field composition and one nested UI presentation — before inventing Warp/Fold/Push/Wrinkle/Carve as a full tree.

**Verdict summary:** Pursue the *ideas* (owned behaviors, typed fields, ordered composition). Reject deep nested ownership as the serialized data model until a minimal owned-behavior slice proves value. Prefer adapters + UI nesting over a greenfield Behavior hierarchy.

---

## 2. Current architecture map

### Data ownership (confirmed)

```
UMixtormatMaterial
  Layers[] / Groups[]
    FMixtormatLayer
      Children[]                    // flat, ordered, enum-typed
        FMixtormatLayerChild
          ChildId, ScopeOwnerChildId, SourceLayerId/SourceChildId
          Type → payload union:
            Generator | HeightBlend | HeightCurve | HeightColorRamp
            | HeightPush | StructuralWarp | Effect | Mask | …
```

- **Generators** are layer children of type `Generator` (`EMixtormatLayerChildType::Generator`, `MixtormatLayerTypes.h` ~L147).
- **Height modules** (`HeightBlend`, `HeightCurve`/`Height Remap`, `HeightColorRamp`, `HeightPush`, `StructuralWarp`) are *sibling* child types in the same array (`MixtormatLayerTypes.h` ~L157–164, payloads ~L261–276).
- **Flow tools** are `Effect` children with types `ShapeDeform` / `GeneratorFlow` / `FlowCarve` / `GravityFlow`, valid only when scoped under a flow-capable generator (`MixtormatEffect.h` ~L58–62, ~L149–154; `MixtormatCanOwnGeneratorFlow` in `MixtormatGeneratorTypes.h` ~L36–44).
- **Ownership of flow tools** = `ScopeOwnerChildId` → generator’s `ChildId` (same pattern as masks under masks/effects). Not a nested tree.
- **Structural connections** (Push/Warp → target generator) are **reference edges** (`TargetChildId` + source `FMixtormatOutputReference`), projected in the UI (`MixtormatStructuralConnectionProjection.cpp`). They do **not** reparent children or change flat order (prior plan + projection code: modules remain unscoped siblings).

### GPU execution (confirmed)

Gather (game thread) → RDG passes (render thread):

1. Demanded Sources shelf producers → `PublishedFieldOutputs`
2. Per layer: masks, IDs, then if `bGenerator` → `AddGeneratorLayerPasses`
3. Walk children in order; maintain **signed running height** from zero
4. StructuralWarp / HeightPush run at authored position; accumulate displacement/shift keyed by **target** child index
5. Generator modules resolve `HeightSource` / `WarpSource`, run family passes, then scoped flow effects on that module’s height, then combine into running height
6. HeightBlend / HeightCurve / ColorRamp update running height or publish Color
7. Layer composite uses final generator height as layer input height

Key files:
- `MixtormatGpuGeneratorPasses.cpp` — `AddGeneratorLayerPasses` ~L2685+, StructuralWarp handling ~L2703+, flow effect selection ~L1462+
- `MixtormatGeneratorFlow.usf` — multi-stage seed / jump-flood / resolve / apply / gravity
- `MixtormatGeneratorStructuralWarp.usf` — displacement composition + coordinate lift
- `Compositing/MixtormatGeneratorGather.cpp` — gather of modules; rejects scoped Push/Warp (~L361, ~L380)

### Published fields (confirmed)

`EMixtormatPublishedFieldKind` (`MixtormatOutputReference.h` ~L20–36):  
`RegionIds`, `Flow`, `UVMap`, `Color`, `Scalar01`, `ScalarSigned`, `SDF`, `Vector2`.

References carry owner kind (Layer / Shelf), GUIDs, output name, and flow-specific amount/trace/steps. This is already a typed field model with Source / Instance / Shelf — incomplete relative to a full “field children of behaviors” graph, but real infrastructure.

---

## 3. Inventory — existing field / flow / deformation features

| Feature | Implementation status | Primary files | I/O contract | Reuse vs replace |
|--------|------------------------|---------------|--------------|------------------|
| **Structural Warp** | Implemented | Runtime: `FMixtormatGeneratorStructuralWarp` (`GeneratorTypes.h` ~L715–747); GPU: `FMixtormatGeneratorStructuralWarpCS` + CoordinateCS; shader `MixtormatGeneratorStructuralWarp.usf`; UI projection | Source: Flow or UVMap via OutputReference; Target: later generator; accumulates D + transports B; scoped mask | **Reuse** as core Warp behavior; do not rewrite |
| **Height Push** | Implemented | `FMixtormatGeneratorHeightPush` (~L688–711); `MixtormatGeneratorHeightPush.usf` | Source: ScalarSigned Height; Target: later StrataCarver only; bedding shift B | **Reuse**; extend target types carefully |
| **Height Blend** | Implemented | `FMixtormatGeneratorHeightBlend` (~L640+); GPU combine pass | Running height ⊕ referenced module height; ops Add/Sub/Min/Max/… | **Retain** as field composition op |
| **Height Remap / Curve** | Implemented | `FMixtormatGeneratorHeightCurve` (~L750+) | Signed remap + scalar ramp; Amount | **Retain** |
| **Height Color Ramp** | Implemented | `FMixtormatGeneratorHeightColorRamp` | Reads running / module / layer / composite-below; publishes Color | **Retain** as field→Color producer |
| **HeightSource / WarpSource** | Implemented | `FMixtormatGenerator` inputs (~L969+) | Optional OutputReference into generator | **Retain**; foundation for field wiring |
| **Generator Flow** | Implemented (scoped effect) | Effect params ~L1084+; `MixtormatGeneratorFlow.usf` stages 0–3,7 | Source: SDF or Height; outputs deformed height / flow direction | **Adapt** into Warp/Distort field consumers |
| **Gravity Flow** | Implemented | Stage 9 + apply mode 3 | Gravity ± height/boundary steering | **Reuse** as field producer + optional apply |
| **Flow Carve** | Implemented | Stages + carve modes | Trace + depth/width/falloff | **Reuse** as Carve behavior |
| **Shape Deform** | Implemented | Apply mode 0 | Boundary-aware deform | **Reuse** / fold into Distort or Warp |
| **Flow Warp** (layer effect) | Implemented | `EMixtormatEffectType::FlowWarp` = 6; post-composite style | Layer-level warp, not generator-owned | **Keep separate** from generator behaviors |
| **Fold / Bend** | Partial | `GeneratorFlowBend`, Breakup Fold params; no standalone Fold tool | Bend noise on flow; Breakup fold | **Not** a full Fold behavior yet |
| **Curl / noise deformation** | Partial | Noise gradient publish; FlowWarp noise; no dedicated Curl field | Vector from noise | **New field producer** needed for Curl |
| **Masks on modules** | Implemented | ScopeOwner under Push/Warp; `HasScopedMasks` | Gate displacement/shift | **Reuse** for field/behavior gates |
| **Sources shelf** | Implemented | `EMixtormatOutputReferenceOwnerKind::Shelf`; demand-driven gather | Producers publish before layer loop | **Reuse** for external field sources |
| **Published output refs** | Implemented | `FMixtormatOutputReference` | Typed, enabled, shelf/layer | **Shared infrastructure** for V2 fields |
| **Parameter Follow/Link/Driver** | Implemented | `MixtormatParameterBinding`, drivers on flow amount etc. | Bindings on children | **Retain** unchanged |
| **Group expansion** | Implemented | `BuildEffectiveLayers` | Flat effective list | **Retain**; nesting must expand |
| **Clipboard remapping** | Implemented | `MixtormatLayerClipboard.cpp` | GUID remaps | **Must** preserve for any migration |
| **Signed height / SDF / normals / IDs** | Implemented | Bundle fields; flow remaps UV after apply | Consistency after warp is explicit in flow shader comments | **Preserve** contracts |
| **Wrinkle** | **Prototype only** | `AgentDocs/Prototypes/Wrinkling/wrinkling01.cl`, `wrinkling02.cl` | Mesh Laplacian + curvature diffusion + fold displacement | **New** if adopted; not present in product |

---

## 4. Confirmed architectural problems (with evidence)

1. **Modules are siblings, not children of generators**  
   `EMixtormatLayerChildType` lists `HeightPush` / `StructuralWarp` as peer types to `Generator` (`MixtormatLayerTypes.h` ~L157–164). Gather rejects scoped Push/Warp (`MixtormatGeneratorGather.cpp` ~L361, ~L380). Ownership is “same layer + target GUID,” not parent pointer.

2. **Structural connection UI is projection, not nesting**  
   Projection code builds display ownership from reference edges without changing `Children` order (`MixtormatStructuralConnectionProjection.cpp`; prior plan: “Hierarchy indentation remains unimplemented”). User proposal correctly rejects “merely projecting again.”

3. **Three parallel deformation channels**  
   - Module-level Structural Warp / Height Push  
   - Scoped generator flow effects  
   - Layer-level Flow Warp  
   Same conceptual “move height with a vector field,” three type systems and three pass entry points.

4. **Target restrictions are asymmetric**  
   Height Push: StrataCarver only (`GeneratorTypes.h` ~L704–706). Structural Warp: any later generator. Inconsistent for a unified Behavior model.

5. **No first-class “previous field” / “previous behavior output”**  
   Running height exists; Color Ramp can read `GeneratorRunning` / `ModuleRef` / `LayerHeight` / `CompositeBelow` (`EMixtormatColorRampSource` ~L790+). Flow tools do not expose a general “previous behavior output” as a field kind.

6. **Wrinkle is not in the product**  
   Only OpenCL mesh prototypes; no USF, no child type, no gather path.

7. **Serialization rigidity**  
   Enum-by-value, append-only (comments throughout LayerTypes / Effect / GeneratorTypes; AGENTS.md §5). A new `Behavior` child type is possible only by **appending**; reordering or collapsing old types would break assets.

---

## 5. Proposed V2 data model (evaluation + recommendation)

### Proposed (from user)

```
Generator
  Behavior
    Field
      Mask
```

Behaviors execute in order; fields produce typed data; behaviors consume/transform.

### Critical evaluation vs GPU / current code

| Proposal element | Fit | Notes |
|------------------|-----|--------|
| Behaviors as actual children of generators | **Poor fit as serialized nesting** | Flat array + ScopeOwner already encodes ownership for effects; modules deliberately unscoped. Deep nesting breaks clipboard, groups, instance resolution, and gather without a full expand step. |
| Fields as children of Behaviors | **Overkill initially** | `FMixtormatOutputReference` + published map already wires producers→consumers. Local “inline field” can be a struct on the behavior, not a child node. |
| Typed fields | **Strong fit** | Extend `EMixtormatPublishedFieldKind` if needed; keep explicit conversions. |
| Ordered behavior execution | **Already true** | Child order in `AddGeneratorLayerPasses`. |
| Generator-original vs accumulated fields | **Needed** | ColorRamp sources show the pattern; generalize. |
| Masks on fields/behaviors | **Exists** | ScopeOwner masks under modules/effects. |
| Loadable old projects | **Hard constraint** | Adapters / dual path required. |

### Recommended data model (inference)

Keep serialized form flat; introduce **semantic roles**:

```text
FMixtormatLayerChild (unchanged enum append strategy)
  Type = Generator | … | HeightPush | StructuralWarp | Effect(…)
  // NEW optional (appended, default empty):
  BehaviorRole / FieldBindings   // or reuse existing structs

Ownership:
  ScopeOwnerChildId → Generator   // for flow-like behaviors
  TargetChildId     → later Generator  // for structural modules (unchanged)

Fields:
  Prefer OutputReference + published map
  Optional local field producer struct on the behavior (Noise, Curl, Gravity)
  “PreviousField” = explicit ref to prior module/behavior output name
```

Runtime types to **share**, not rewrite:
- `EMixtormatPublishedFieldKind`, `FMixtormatOutputReference`
- `FMixtormatGeneratorHeightPush` / `StructuralWarp` payloads
- Flow effect parameter block on `FMixtormatLayerEffect`
- RDG published field cache

New types only when necessary:
- Curl / Phasor as field producers (if not expressible via Noise + existing Flow)
- Wrinkle parameters + intermediate curvature/Laplacian textures
- Optional `EMixtormatBehaviorKind` as **UI/catalog** enum mapping onto existing child types (not a new serialized child type until proven)

---

## 6. Proposed execution graph

**Confirmed baseline (keep):**

```
Shelf producers (demand order)
→ for each Generator layer:
     clear RunningHeight = 0
     for child in Children order:
       StructuralWarp → update Displacement[Target], Shift
       HeightPush     → update Shift[Target]
       Generator:
         resolve HeightSource / WarpSource
         family passes → Module height (+ boundary, IDs, …)
         scoped flow effects (ShapeDeform / GeneratorFlow / FlowCarve / GravityFlow)
         remap bundle through WarpedUV where required
         combine Module → RunningHeight
       HeightBlend / HeightCurve → RunningHeight
       HeightColorRamp → publish Color
     LayerInputHeight = RunningHeight
→ composite / masks / effects …
```

**V2 execution (recommended):** same linear graph. Behaviors are **labels + parameter bundles** on existing child slots, not recursive subgraphs. Field evaluation = resolve OutputReference or local producer → texture in `PublishedFieldOutputs`. Combination = existing HeightBlend ops + explicit vector blend helpers if needed.

**Caching / RDG:** continue keying by `(LayerIndex, ChildIndex, OutputName)`. Self-input snapshots already happen by consuming previously published complete fields only (incomplete refs skip). Avoid cyclic self-references by the same earlier-only rule Structural Warp uses.

**Publication:** generator modules and behaviors publish named fields; downstream behaviors read by reference. Do not invent a second publication system.

---

## 7. Three UI concepts

All must stay compatible with the compact dark dock-only Layers stack (AGENTS.md / UI.md constraints: no floating Layers overlay recreation, no native tooltips).

### A — Compact nested stack
- Generator row expands to show owned behaviors as indented rows (true visual children).
- Fields appear as sub-rows or inline chips under each behavior.
- **Pros:** Matches mental model of ownership; selection/reorder under parent is natural.  
- **Cons:** Deep indentation in a dense stack; reorder across generators is harder; must still map to flat `Children` order.  
- **Risk:** Looks like hierarchy while serialization remains flat — documentation must be clear.

### B — Nested Behavior cards with field rows
- Selecting a generator opens a card panel (Inspector or inline) listing behaviors as mini-cards; each card has field rows (Source, Noise, Curl, …).
- Stack stays flatter.  
- **Pros:** Richer field editing; less stack clutter.  
- **Cons:** Two places to edit (stack vs card); weaker at-a-glance order; more Inspector work.

### C — Hybrid stack + Inspector field editor **(recommended)**
- Stack: Generator with **one-level** indented owned behaviors (Warp, Push, Flow, Carve, …) — genuine authoring under the generator, not projection under the *target*.
- Structural links still show target chips/highlights (existing connection UI).
- Inspector: when a behavior is selected, full field list (producers, masks, blend, previous-field refs).
- **Pros:** Ownership visible without deep trees; reuses connection UI; Inspector already owns dense parameters; matches “do not merely project under target.”  
- **Cons:** Needs clear creation menus (“Add Warp under this Generator”) and order controls among siblings under the same owner.

**Recommendation:** **C**. It delivers nested *authoring* without forcing nested *serialization* or abandoning the connection model that already works for multi-target structural modules.

---

## 8. Migration matrix

| Old feature | V2 equivalent | Compatibility strategy |
|-------------|---------------|----------------------|
| Structural Warp child | Behavior Warp (owned by generator via ScopeOwner *or* keep as module + UI nest under owner) | **Adapter:** same struct, same GUIDs; UI parents under source-owner or stay flat with indent by owner |
| Height Push | Behavior Push (bedding) | Same |
| Height Blend / Curve / Color Ramp | Field composition / remap / Color producer | **Retain as-is** |
| ShapeDeform / GeneratorFlow / FlowCarve / GravityFlow | Behaviors Distort / Warp / Carve + field Gravity/Flow | **Keep effect types and values**; map catalog names in UI; no enum renumber |
| Flow Warp (layer) | Outside generator Behavior system | **Retain separately** |
| HeightSource / WarpSource | Field Source / Instance on generator | Unchanged |
| Scoped masks under modules | Mask on behavior | Unchanged ScopeOwner |
| Sources shelf | Field Source (Shelf) | Unchanged |
| Instances / clipboard / groups | Same GUID rules | **No change** to remapping |
| Breakup Fold params | Not Wrinkle | Leave until Wrinkle exists |
| Wrinkle prototypes | Future Behavior Wrinkle | New child type **appended** only when ready |

**Strategy recommendation:**  
**Adapters + UI nesting first**, not one-time migration of assets, not dual GPU paths. Old serialized children load unchanged; V2 is interpretive layer + optional new appended types. Delete old implementations only after behavioral parity is proven.

Preserve: enum values, reflected parameters, Source GUIDs, external refs, clipboard, groups, undo/redo, existing shaders.

---

## 9. Implementation roadmap (files likely to change)

### P0 — Smallest complete V2 proof slice
**Goal:** Prove real *owned* behavior authoring, typed field composition, and GPU execution without rewriting the stack.

1. **Ownership semantics for one behavior**  
   - Choose Structural Warp *or* GeneratorFlow as the pilot.  
   - Enforce `ScopeOwnerChildId` → flow-capable generator for the pilot if using effect path; or UI-only nest of Structural Warp under its *source* generator (not target).  
   - Files: `MixtormatLayerTypes.h` (if any flag), `MixtormatChildScope.*`, `MixtormatGeneratorGather.cpp`, `MixtormatStructuralConnectionProjection.*`, layer hierarchy widgets.

2. **Typed field wiring visible**  
   - Flow field from scoped GeneratorFlow → Structural Warp Source already works; surface it as “Field: FlowDirection” in Inspector.  
   - Files: Inspector generators, `MixtormatOutputReference` UI.

3. **GPU path untouched for parity**  
   - No shader changes in P0 if existing passes already execute the pilot.  
   - Verify order: owned behavior runs with generator height pipeline as today (`MixtormatGpuGeneratorPasses.cpp`).

4. **UI hybrid (C)**  
   - Indent owned behaviors under generator; creation menu “Add under Generator.”  
   - Files: `MixtormatLayerHierarchy.cpp`, `MixtormatLayerMenus.cpp`, `MixtormatLayerDragDrop.cpp`, structural projection.

**Exit criteria:** Create Rock Formation → add Warp under it → wire Flow field → deform later generator → save/load → same pixels as pre-V2 equivalent setup.

### P1
- Catalog mapping (Warp / Push / Carve / Distort) over existing modules/effects  
- Previous-field ref generalization  
- Curl / Gravity as first-class field producers if missing  
- Height Push target expansion (if required)

### P2
- Wrinkle (texture-space Laplacian/curvature from prototypes)  
- Full Behavior child type only if P0/P1 show ScopeOwner + modules insufficient  
- Deprecate duplicate entry points after parity

**Files most likely touched overall:**  
Runtime: `MixtormatLayerTypes.h`, `MixtormatGeneratorTypes.h`, `MixtormatEffect.h`, `MixtormatOutputReference.*`, `MixtormatChildScope.*`  
Shaders: `MixtormatGpuGeneratorPasses.cpp`, `MixtormatGeneratorGather.cpp`, optionally new USF for Wrinkle  
Editor: Layers widgets, structural projection, Inspector generators, menus, drag/drop

---

## 10. Unresolved design decisions (must settle before implementation)

1. **Serialized ownership:** ScopeOwner-only vs new nested child array vs hybrid UI-only nesting?  
2. **Is Structural Warp owned by source generator or remains a free module with target edge?** (Affects multi-target and order.)  
3. **Single Behavior type with sub-kind vs many child types?** (Dispatch cost vs append-only enum.)  
4. **Field nodes as children vs OutputReference-only + local producer structs?**  
5. **Where does Height Blend live** — field combinator, behavior, or leave as module forever?  
6. **Wrinkle:** Behavior only, field generator only, or iterative multi-pass behavior with intermediate fields?  
7. **Compatibility path:** Adapter forever vs eventual one-time migration tool?  
8. **Group-shared structural modules** policy (called out in prior plan as open).  
9. **Noise / CliffStrata** boundary: height-only flow vs full SDF tools in V2 catalog.  
10. **Self-reference policy** for “Previous Behavior Output” when behaviors share a generator.

---

## Wrinkling prototypes — mandatory findings

**Files read in full:**  
`AgentDocs/Prototypes/Wrinkling/wrinkling01.cl` (150 lines), `wrinkling02.cl` (135 lines).

### Mathematics (reusable in texture space)

| Component | Prototype | GPU heightfield adaptation |
|-----------|-----------|----------------------------|
| Weighted Laplacian of height | wrinkling01, iteration 0 | 4/8-neighborhood finite differences on signed height; wrap for tileability |
| Normal-variation curvature | length(N_i − N_0) weighted | From height gradients: reconstruct N, compare to neighbors; or use existing curvature helpers (`MixtormatCurvature.ush`) |
| Curvature-dependent diffusion | later iterations | Jacobi/Gauss-Seidel style ping-pong on a scalar buffer; rate = base + k·curv |
| Fold-side selection | wrinkling02 | Sign of Laplacian / mask modes (both, upper, lower, soft signed) |
| Mask composition | shadow×curv, max, add, AO/thickness weights | Multiply with scoped Mask child; optional extra scalar fields |
| Displacement | along blend(N, gravity) | In heightfield: displace *height* (scalar) or UV (structural); full 3D P update is mesh-only |

### Intermediate fields / iterations
- `_curv`, `_shadow_mask` / `_shadow_tmp` (diffusion ping-pong), `_fold_mask`, `_fold_disp`, optional gravity direction.  
- Multiple iterations for diffusion (mesh neighbor loop). Texture space: fixed N passes or until residual threshold.

### Difference from existing Fold / Breakup / Shape Deform
- **Breakup Fold:** effect parameters on post-composite surface (`BreakupFold*`), not generator-owned iterative Laplacian.  
- **Shape Deform / Generator Flow:** boundary/height-seeded flow + RK2 trace; not curvature-weighted diffusion of a fold mask.  
- **Structural Warp:** consumes external Flow/UV; does not *generate* fold masks from Laplacian.

### Behavior vs field
- **Both:** field producers for Curvature / Laplacian / FoldMask; Behavior Wrinkle consumes them and applies displacement.  
- Do not ship as mesh OpenCL; reimplement neighborhood as wrapped texel neighborhoods.

### Parameters / previews (suggested)
- Fold side, mask mode, contrast, gain, diffusion rate, blur radius, normal vs gravity weights, max displacement, iteration count.  
- Previews: curvature, fold mask, displacement magnitude, optional gravity direction (align with existing flow debug stages).

---

## Smallest complete V2 implementation slice

**One sentence:**  
Under a single Rock Formation generator, author a **Warp behavior** that is clearly owned in the UI, consumes a **typed Flow field** (from existing GeneratorFlow or published Flow), writes through the **existing Structural Warp / flow GPU path**, saves and reloads with **unchanged GUIDs and shaders**, and does **not** depend on projecting under the target.

That slice proves:
1. Nested ownership *authoring* (hybrid UI)  
2. Typed field composition (Flow → Warp)  
3. GPU execution (current passes)

Only after that should Fold / Push catalog unification / Wrinkle / deep data nesting be considered.

---

## Closing

Behavior System V2’s **goals** align with real pain: fragmented generator interaction, weak ownership UX, and incomplete field composition. Its **literal hierarchy** is not required by the GPU and fights the serialization and ScopeOwner model that already encode ownership.  

Build on published fields, scoped effects, and structural modules; nest in the UI; keep execution flat; prove one owned Warp+Flow path end-to-end; then expand the catalog. That is the lowest-risk path to the same architectural destination without a premature rewrite.

Yes — with one important constraint.

**Package what you have and rewrite *from* those packages, not *around* a brand-new hierarchy.** The current confusion is real: three deformation channels (modules, scoped flow effects, layer Flow Warp), siblings pretending to be children, and structural links that only look nested in the UI. Cleaning that up by inventing Behavior/Field trees on top of the mess usually makes it worse.

What “catch modules → package → rewrite” should mean here:

### 1. Inventory packages (freeze contracts)
Treat each working unit as a black box with a stable I/O surface:

| Package | What it actually is today | Contract to keep |
|--------|---------------------------|------------------|
| **Structural Warp** | Module + displacement accumulation | Flow/UV in → D/B out for a later target |
| **Height Push** | Module | Signed height in → bedding shift for Strata target |
| **Generator flow set** | Scoped effects | Owner height/boundary in → deformed height + optional Flow out |
| **Height ops** | Blend / Remap / ColorRamp | Running signed height in → height or Color out |
| **Published fields** | OutputReference + kinds | Typed producer → consumer wiring |
| **Scope + masks** | ScopeOwnerChildId | Gate under owner |

Do not redesign those contracts in the first rewrite pass. Document them as the product’s real API.

### 2. Rewrite the *composition shell*, not every shader
The confusing part is mostly **ownership, ordering, and naming**, not the math in `MixtormatGeneratorFlow.usf` / StructuralWarp USF. A sensible rewrite:

- One **execution spine**: ordered list of “ops on a generator’s fields + running height.”
- Each package becomes an **op** with the same GPU path it has now.
- UI shows ops **under the generator that owns them**.
- Serialization stays flat (or migrates once after the spine is proven).

That is packaging + rewrite of the shell. It is *not* reimplementing jump-flood, RK2 carve, or strata resolve.

### 3. What not to do
- Don’t start with a full Behavior/Field tree and hope packages fall into it.
- Don’t delete HeightPush / StructuralWarp / flow effects until a new spine runs the same pixels.
- Don’t merge layer Flow Warp into generator ops in the same pass — different stage of the pipeline.

### 4. Practical sequence
1. **Name the packages** in code/docs (even if types stay as they are).  
2. **One spine** that runs only Rock Formation + Warp + Flow field end-to-end.  
3. **Map catalog** (Warp / Push / Carve / Distort) onto packages — UI names first.  
4. **Then** optional real data-model cleanup (append enums, adapters).  
5. **Wrinkle** only after the spine exists; it’s new math, not a package.

**Bottom line:** Yes — catch and package the working modules, then rewrite the interaction model *from those packages*. That is cleaner than Behavior System V2 as a greenfield hierarchy, and it matches how the GPU already works. The rewrite target is a single owned-op list under generators, not a second parallel type system.

Here’s the same idea drawn out: **current mess → packages → one spine**.

---

### 1. What it feels like today

```text
LAYER (flat Children[])
│
├─ Generator: Rock Formation          ← real producer
├─ Effect: Generator Flow             ← scoped under Rock? only via ScopeOwner
├─ Effect: Flow Carve                 ← same
├─ Structural Warp                    ← sibling, targets *another* generator
├─ Height Push                        ← sibling, Strata-only target
├─ Height Blend                       ← sibling
├─ Generator: Strata Carver           ← warp/push land *here* by GUID
└─ Effect: Flow Warp                  ← layer-level, different pipeline stage

Three channels, one flat list, ownership mostly invisible.
```

---

### 2. Packages (catch what already works)

```text
┌──────────────────────────┐  ┌──────────────────────────┐
│ PACKAGE: Field I/O       │  │ PACKAGE: Height ops      │
│ • OutputReference        │  │ • Height Blend           │
│ • Published kinds        │  │ • Height Remap           │
│ • Shelf / Instance       │  │ • Color Ramp → Color     │
└──────────────────────────┘  └──────────────────────────┘

┌──────────────────────────┐  ┌──────────────────────────┐
│ PACKAGE: Structural      │  │ PACKAGE: Generator flow  │
│ • Structural Warp        │  │ • Shape Deform           │
│ • Height Push            │  │ • Generator Flow         │
│ • D / B accumulation     │  │ • Flow Carve             │
│                          │  │ • Gravity Flow           │
└──────────────────────────┘  └──────────────────────────┘

┌──────────────────────────┐
│ PACKAGE: Scope / mask    │
│ • ScopeOwnerChildId      │
│ • Scoped mask gate       │
└──────────────────────────┘

Shaders stay. Contracts stay. Only the *shell* changes.
```

---

### 3. Target: one spine under the generator

```text
GENERATOR  (Rock Formation)
│
│  fields (typed, published or local)
│  ├─ Height          (own / Source)
│  ├─ Boundary / SDF  (if family has it)
│  ├─ Flow            (from flow package or Source)
│  └─ Previous        (prior op output)
│
└─ OPS  (ordered, owned — UI nested, exec still linear)
    ├─ [flow]   Generator Flow     → writes Flow + may deform Height
    ├─ [flow]   Flow Carve         → Height
    ├─ [struct] Warp               → D for later target (same package)
    ├─ [struct] Push               → B for later target
    └─ [height] Blend / Remap      → Running height contribution

Later GENERATOR (Strata) still receives Warp/Push via target link —
but Warp/Push are *authored under the source generator*, not floating siblings.
```

---

### 4. Rewrite vs reimplement

```text
KEEP (packages)              REWRITE (shell only)
─────────────────────────    ─────────────────────────────
USF / RDG passes             Flat list → “ops under generator”
OutputReference kinds        UI ownership (indent / create under)
ScopeOwner + masks           Catalog names: Warp Push Carve Distort
Structural D/B maps          One mental model for authors
Flow multi-stage solve       Optional later: cleaner serialization

        ┌─────────────┐
        │  old assets │── adapter / same GUIDs ──► same pixels
        └─────────────┘
```

---

### 5. P0 slice (the only thing to build first)

```text
                    ┌─ UI: nested under Rock
                    │
 Rock Formation ────┼─ Op: Warp
                    │     └─ Field: Flow  ← from Generator Flow (or Source)
                    │
                    └─ GPU: existing Structural Warp + Flow path

Save / load / same image as today’s equivalent setup.
If that works, map the rest of the catalog onto packages.
If it doesn’t, stop — don’t invent Behavior trees yet.
```

---

### 6. One-line picture

```text
NOW:   [Gen] [Flow?] [Warp?] [Push?] [Gen] [FlowWarp?]   ← scatter
WANT:  Gen{ Flow, Carve, Warp, Push, Blend } → Gen{ … } ← packages on a spine
```

That’s the illustration: **don’t redesign the engines; box them and hang them on one owned list under each generator.**

You can. Existing materials don’t need a greenfield rebuild. The point of **package → spine** is that an already-authored asset keeps its children, GUIDs, and pixels; you only change how those children are *interpreted and shown*.

### What an old material actually has

A saved layer is still a flat `Children[]` list, for example:

```text
[0] Generator  Rock Formation     ChildId = A
[1] Effect     Generator Flow     ScopeOwner → A
[2] StructuralWarp                Source = Flow from [1], Target = C
[3] HeightPush                    Source = …, Target = C
[4] Generator  Strata Carver      ChildId = C
[5] HeightBlend                   …
```

Nothing in that list is invalid under a spine model. It’s already the ops — just unordered in the UI and weakly owned.

### How “do the behavior” works on that asset

**1. Recognition (no write)**  
On load / gather, map each child to a package role:

| Existing child | Spine role |
|----------------|------------|
| Generator | Host |
| Effect with `MixtormatIsGeneratorFlowEffect` + `ScopeOwnerChildId` | Owned flow op under that generator |
| StructuralWarp / HeightPush | Structural op (owned by source side in UI; target link unchanged) |
| HeightBlend / HeightCurve / ColorRamp | Height op |
| Layer FlowWarp | Leave outside generator spine |

Same data, labeled.

**2. UI ownership (still no forced reserialize)**  
For Rock `A`, the stack shows:

```text
▼ Rock Formation
    Generator Flow     (was sibling effect, ScopeOwner=A)
    Warp               (StructuralWarp that sources from A’s scope)
    …
Strata Carver          (target chip still points here)
```

You did not move rows in the asset; you **parented them visually** by `ScopeOwner` / source resolution. Order in the flat array remains the execution order (already what the GPU uses).

**3. Authoring new behavior on that material**  
“Add Warp under Rock” still:

- Inserts a `StructuralWarp` (or scoped flow effect) in `Children` at the right index  
- Sets source / `ScopeOwner` / target GUIDs the same way as today  
- Saves like any other edit  

Old and new ops coexist in one list.

**4. When you actually rewrite the asset (optional, later)**  
Only if you introduce a new serialized shape (e.g. nested array or a Behavior child type):

- One-time migration: walk children → emit spine ops under each generator → keep `ChildId`s so references and drivers still resolve  
- Or dual-read: load old flat layout forever; save new layout only when the user edits that layer  

Until then, **adapters are enough**. No mass “convert project” required to start using the model.

### What you must not break on old materials

- `ChildId` / `SourceLayerId` / `SourceChildId` / `TargetChildId`  
- Enum values (`EMixtormatLayerChildType`, effect types)  
- ScopeOwner for flow + masks  
- Published field names (`Height`, `FlowDirection`, …)  
- Clipboard / instance remaps  

If those hold, the same material opens, composites the same, and can grow new owned ops.

### Practical answer

| Goal | On an already authored material |
|------|----------------------------------|
| See behaviors under generators | Yes — interpret + UI nest from existing ScopeOwner / structural sources |
| Add a new Warp/Flow under a generator | Yes — same child types, clearer create path |
| Change GPU result without editing | No — spine is organizational first; pixels stay until you change ops |
| Force-convert every asset day one | No — unnecessary; adapter first |

So: **open the old material, treat its modules as packages on a spine, author new behavior under generators the same way.** Migration is a display and catalog problem first; a data rewrite is optional and only after P0 proves parity on real projects.