# Mixtormat Behavior System V2 Architecture Audit

**Audit scope:** Existing generator deformation, flow, structural connections, and field systems; evaluation of a proposed unified nested Behavior System V2.  
**Repository:** `hugobeyer/matlab`, branch `main`.  
**Latest commit fetched:** `9c3b3b1` — “Preserve shelf mask sources and all generator module links when cloning a layer”.  
**Constraint compliance:** Read‑only audit. No builds, tests, Unreal launches, diagnostics, or automated validation were run, per `AGENTS.md` §5.

---

## 1. Repository guardrails and architectural direction

`AGENTS.md` is the canonical routing document. It establishes three modules with a one‑way dependency chain: **MixtormatRuntime ← MixtormatShaders ← MixtormatEditor**. Runtime owns the data model, Shaders owns GPU gather and RDG passes, Editor owns Slate UI and inspector. No Editor code may be included from Runtime or Shaders.

The agent‑routing rules emphasise symbol‑level searches, opening line ranges rather than whole files, and following the subsystem documentation instead of re‑deriving architecture. The audit therefore relies on the subsystem docs and the canonical type headers rather than attempting a full‑source scan.

---

## 2. Current generator architecture (as‑built)

### 2.1 Generator role and taxonomy

A **generator** rewrites a layer’s input height **before anything reads it**, so the layer composites as though the carved surface were authored. It is deliberately neither an effect (post‑composite filter) nor a mask (0..1 coverage). The serialized generator enum (`EMixtormatGeneratorType`) currently contains six values: **StrataCarver**, **Cracks**, **RockFormation**, **Pebbles**, **CliffStrata**, and **Noise**; it is append‑only.

### 2.2 Generator‑owned flow tools

Four flow tools — **ShapeDeform**, **GeneratorFlow**, **FlowCarve**, and **GravityFlow** — live in `EMixtormatEffectType`, not in the generator enum. They are valid only when scoped under a generator that can own them. The eligible owners are StrataCarver, RockFormation, Pebbles, Cracks, CliffStrata, and Noise.

A critical distinction exists between **Height steering** and **Signed Distance steering**. Noise and CliffStrata support **Height steering only** because neither publishes a signed boundary field; Signed Distance is therefore unavailable for those generators. Every other eligible generator supports both source modes. This distinction is encoded in `MixtormatGeneratorHasFlowBoundary` and is surfaced in the inspector.

### 2.3 Structural modules

Two independent child types are classified as **structural modules**:

- **HeightPush** — consumes completed signed Height from an eligible generator and targets a later unscoped StrataCarver in the same layer. It maps signed source height to a bedding‑coordinate shift (one unit = one bed period). Scoped masks gate only the push.
- **StructuralWarp** — consumes **Flow** or **UVMap** from a compatible earlier completed scoped effect and targets a later enabled unscoped generator (Strata, RockFormation, Pebbles, Cracks, CliffStrata, or Noise). It rewrites destination coordinates, regenerating in the target’s structural frame.

Both are appended child types in `EMixtormatLayerChildType`. Their payloads are referenced on `FMixtormatLayerChild` as `FMixtormatGeneratorHeightPush` and `FMixtormatGeneratorStructuralWarp`.

### 2.4 Field system and published outputs

The compositor publishes typed fields — **Flow**, **UVMap**, **ScalarSigned** — through an output‑reference system. StructuralWarp, for example, requires a source of type Flow or UVMap; it never accepts a generic Vector2. Noise publishes an explicit **FlowDirection** from its completed signed Height (unit downhill direction in destination tile UV), and does not reinterpret the heterogeneous raw Gradient.

A **Sources shelf** allows generators to publish fields for other operations. Only demanded sources are evaluated; demand is seeded from generator HeightSource/WarpSource inputs and enabled masks bound to shelf Noise Value. Shelf producers run ahead of the layer loop and publish into `Ctx.PublishedFieldOutputs`. Consumer input keys resolve through `ClassifyShelfSourceReference`, which rejects unsupported output kinds.

---

## 3. Fragmentation points in the current system

The existing architecture is **fragmented across multiple parallel child types and dispatch paths**:

| Layer | Fragmentation |
|-------|---------------|
| **Data model** | Generator payload (`FMixtormatGenerator`), HeightBlend, HeightCurve, HeightColorRamp, HeightPush, and StructuralWarp are separate union members on `FMixtormatLayerChild`. Flow tools live in `EMixtormatEffectType` despite being generator‑scoped. |
| **Gather** | `GatherGeneratorChild` switches on `Generator.Type`; `GatherGeneratorHeightModuleChild` handles HeightBlend/Curve/ColorRamp/Push/StructuralWarp as a separate path. |
| **GPU dispatch** | `AddGeneratorLayerPasses` walks module children in authored order; separate pass functions exist for each generator (`AddStrataCarverPasses`, `AddRockFormationPasses`, etc.). |
| **Shader files** | One `.usf` per generator family (`MixtormatStrataCarver.usf`, `MixtormatRockFormation.usf`, etc.) plus dedicated files for HeightPush and StructuralWarp. |
| **Inspector** | Separate control builders for each generator and each structural module (`BuildHeightPushControls`, structural connection adapter). |
| **Serialization** | Every new capability requires a new append‑only enum value and a new payload struct, because the child `Type` is serialized by value and cannot be reordered. |

The proposed **Behavior System V2** hierarchy — Generator → Behavior → Warp → {Flow, Curl Noise, Noise, Source} — would collapse these parallel types into a **nested, composable behavior model**. The audit below evaluates that direction against the existing constraints.

---

## 4. Evaluation of the proposed Behavior System V2

### 4.1 Deformation and flow

**Current state:** Deformation is split between **HeightPush** (bedding‑coordinate shift) and **StructuralWarp** (UV displacement using Flow/UVMap). Flow itself is a scoped effect (`GravityFlow`, etc.) that can be Height‑steered or Signed‑Distance‑steered depending on the owning generator.

**V2 implication:** A “Warp” behavior containing “Flow — Own Height” would unify height‑based warping and flow‑based warping under one behavioral umbrella. This is conceptually coherent, but it must preserve the existing distinction between **Height steering** and **Signed Distance steering**. Noise and CliffStrata cannot support Signed Distance because they publish no signed boundary field. If V2 makes Signed Distance a default capability of the Warp behavior, those generators would either need new boundary outputs or the behavior would need a per‑generator capability mask.

### 4.2 Structural connections

**Current state:** Connections are **explicit and address‑based**. The inspector adapter (`MixtormatStructuralConnections.cpp`) validates type, order, and scope against Gather’s effective/resolved projection, lists eligible choices first, and explains unavailable ones. A setter revalidates the live GUID address before writing.

**V2 implication:** A nested hierarchy would make connections **implicit** (parent‑child within the behavior tree). This is a fundamental shift from explicit GUID references to structural containment. It would simplify authoring (no manual source/target assignment) but would require **serialization migration**: existing assets store explicit `TargetChildId` and source references; a nested model would need to reconstruct those relationships from the tree on load, or retain the explicit references as a compatibility layer.

### 4.3 Field systems and “Source / Ins”

**Current state:** Fields are published through a typed output‑reference system with **owner kinds** (Layer, Shelf) and **output kinds** (Flow, UVMap, ScalarSigned). Consumers resolve through `ClassifyShelfSourceReference`, which enforces the consumer contract and rejects unsupported kinds.

**V2 implication:** “Source / Ins” appears to generalise this into a unified input/output contract for behaviors. This is the most promising part of the proposal: it would replace the current proliferation of reference types with a single **field contract** that behaviors declare and consume. However, the contract must still express the existing ownership semantics (Shelf vs Layer) and output kinds, and must preserve the DAG scheduling and cycle‑rejection logic already implemented in `CollectDemandedShelfSources`.

### 4.4 “Curl Noise” and “Noise — Phasor”

Neither **Curl Noise** nor **Phasor** appears in the current source or documentation. The existing Noise generator publishes **Value**, **Gradient**, **IDs**, and **FlowDirection**. A “Curl Noise” sub‑behavior would be a new field producer; a “Noise — Phasor” would imply a phase‑based noise variant. These are additive capabilities, not replacements for existing enum values, so they would append to the current taxonomy rather than reorder it.

---

## 5. Feasibility assessment and risks

| Dimension | Assessment |
|-----------|------------|
| **Conceptual fit** | The nested behavior model aligns with the existing **generator‑owns‑flow** relationship. The current system already treats flow as scoped to a generator; V2 simply makes that scoping explicit in the type hierarchy. |
| **Serialization safety** | High risk if enum values are reordered. V2 must either **append** new behavior types or maintain a **migration mapping** from existing `EMixtormatLayerChildType` values to behavior nodes. The current rule is append‑only. |
| **Dependency direction** | Any new behavior types must live in **Runtime** (data model), with Shaders consuming them via gather and Editor rendering them via inspector. This preserves the Runtime ← Shaders ← Editor chain. |
| **Shader traceability** | The current pipeline requires every parameter to trace through **CPU declaration → gather → dispatch → defaults → inspector metadata → .usf/.ush**. A unified behavior system must maintain this trace, which may be easier if behaviors share a common parameter structure but harder if each behavior has unique parameters. |
| **Flow boundary distinction** | V2 must not silently grant Signed Distance to Noise or CliffStrata. The capability distinction must remain expressible in the behavior model, likely as a **capability flag** on the behavior or a **validation rule** in the inspector. |
| **Structural connection validation** | The current explicit‑reference model has extensive validation (type, order, scope, cycle rejection). An implicit nested model would need equivalent validation at authoring time (e.g., forbidding a Warp behavior from targeting a generator that does not support warping). |

**Gaps in this audit:** The raw `MixtormatGeneratorTypes.h` file was truncated at 430 lines in the fetched view, so the exact definitions of `FMixtormatGeneratorStructuralWarp` and `FMixtormatGeneratorHeightPush` were not fully inspected. Their existence and responsibilities are confirmed by `MixtormatLayerTypes.h` and `GENERATORS.md`, but a full payload‑level audit would require reading the complete header.

---

## 6. Summary

The current Mixtormat generator architecture is **functionally complete but structurally fragmented**: generators, flow tools, structural modules, and field producers are parallel child types with separate gather, dispatch, and inspector paths. The proposed **Behavior System V2** would unify these under a nested model, which is conceptually consistent with the existing generator‑owns‑flow relationship. The main risks are **serialization compatibility**, **preservation of the Height‑vs‑Signed‑Distance capability split**, and **maintaining the shader parameter trace**. The “Source / Ins” concept is a promising generalisation of the existing published‑field system, while “Curl Noise” and “Noise — Phasor” would be additive rather than substitutive.

No implementation, modification, or validation has been performed. This audit is source‑grounded and limited to the repository’s current `main` branch.