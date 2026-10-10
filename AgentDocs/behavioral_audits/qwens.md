# Mixtormat — Behavior System V2 Architecture Audit

## 1. Executive Verdict
**Verdict**: The proposed Behavior System V2 architecture is conceptually sound for improving authoring clarity and typed data flow, but **it is incompatible with the current GPU execution and serialization model in its proposed form**. 

**What to change**: 
- Do not implement deep, arbitrary nesting (e.g., `Generator -> Behavior -> Field -> Mask -> Field`). The current flat `TArray<FMixtormatLayerChild>` serialization model and index-based GPU state tracking (`TargetChildIndex`) will break.
- Instead, adopt a **shallow nested model**: `Generator` owns `Behaviors`, and `Behaviors` own a flat list of `Fields` (which can include Masks). This preserves the existing child-array serialization pattern while achieving the desired logical grouping.
- Explicitly define field composition rules (e.g., how multiple displacement fields from different behaviors are combined: additive, max, or lerp) before implementing the GPU passes.

---

## 2. Current Architecture Map
- **Data Ownership**: `FMixtormatLayer` owns a flat `TArray<FMixtormatLayerChild>`. Children are differentiated by `EMixtormatLayerChildType`. Scoped ownership is handled via `ScopeOwnerChildId` (a flat reference, not a nested array).
- **GPU Execution**: `MixtormatGpuGeneratorPasses.cpp` iterates through children in authored order. Structural modules (`StructuralWarp`, `HeightPush`) do not execute immediately; they register their `TargetChildIndex` and accumulate state in `LayerCtx.GeneratorStructuralDisplacements` and `LayerCtx.GeneratorHeightPushFields` maps. The target generator later pulls this state.
- **Field Typing**: Currently handled via `EMixtormatPublishedFieldKind` (RegionIds, Flow, UVMap, Color, Scalar01, ScalarSigned, SDF, Vector2). There is no explicit "Field" object; fields are published outputs keyed by `(LayerId, ChildIndex, OutputName)`.

---

## 3. Inventory of Existing Features & Implementation Status

| Feature | Implementation Status | Shader / C++ Location | Recommendation for V2 |
|---|---|---|---|
| **Structural Warp** | Implemented. Accumulates RG32F displacement and R32F shift per target. | `MixtormatGeneratorStructuralWarp.usf`, `MixtormatGpuGeneratorPasses.cpp` | Adapt to `Behavior — Warp`. Reuse existing displacement accumulation logic. |
| **Height Push** | Implemented. Accumulates R32F shift per target. | `MixtormatGeneratorHeightPush.usf` | Adapt to `Behavior — Push`. Reuse existing shift accumulation. |
| **Height Blend / Curve / Color Ramp** | Implemented as sublayers. | `MixtormatGeneratorHeightModules.ush` | Retain as sublayers or adapt to `Behavior` with `Previous Field` input. |
| **Generator Flow / Gravity Flow** | Implemented. Trace-based RK2 solve. | `MixtormatGeneratorFlow.usf` | Adapt to `Field — Flow` consumed by a `Behavior`. |
| **Flow Carve / Shape Deform** | Implemented. Modifies boundary/trace. | `MixtormatGeneratorFlow.usf` | Retain as parameters on the `Flow` field or `Behavior`. |
| **Breakup (Fold/Crease/Push)** | Implemented. SDF-based multi-pass. | `MixtormatRockFormation.usf` (integrated) | Keep integrated. Do not split into separate V2 behaviors yet; too tightly coupled to the SDF generation. |
| **Sources Shelf / Published Fields** | Implemented. `EMixtormatPublishedFieldKind`. | `MixtormatOutputReference.h` | **Retain and expand**. This is the foundation for V2 typed fields. |
| **Parameter Follow/Link/Driver** | Implemented. | `MixtormatParameterBinding.cpp` | Retain. Extend `EMixtormatParameterOwnerType` to include `Behavior` and `Field`. |

---

## 4. Confirmed Architectural Problems

1. **Index-Based State Tracking Breaks with Nesting**  
   - **File**: `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` (Lines ~2400-2500)  
   - **Evidence**: `LayerCtx.GeneratorStructuralDisplacements.Find(Warp.TargetChildIndex)` assumes `TargetChildIndex` is a flat index in the layer's child array. If a Behavior is a child, and its Field is the target, this index lookup fails or becomes ambiguous.
   - **Impact**: V2 cannot use nested children as structural targets without a new GUID-based or flattened execution graph resolver.

2. **Lack of Explicit Field Composition Rules**  
   - **File**: `Shaders/Private/MixtormatGeneratorStructuralWarp.usf`  
   - **Evidence**: The shader performs `D_new = d + sample(D_old, psi)`. It assumes a single accumulated displacement. If V2 allows multiple `Warp` behaviors, there is no defined composition mode (e.g., `Max`, `Add`, `Lerp`) for their outputs.
   - **Impact**: Unpredictable results if multiple behaviors modify the same field type.

3. **Massive Generator Payload Union**  
   - **File**: `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h` (Line ~853)  
   - **Evidence**: `FMixtormatGenerator` is a large struct containing payloads for all generator types. Wrapping this in a `Behavior` struct without refactoring will lead to bloated serialization and confusing parameter ownership.

---

## 5. Proposed V2 Data Model

```cpp
// New shallow-nested structure to preserve serialization compatibility
UENUM(BlueprintType)
enum class EMixtormatLayerChildType : uint8
{
    // ... existing types ...
    Behavior UMETA(DisplayName = "Behavior") // NEW
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatBehavior
{
    GENERATED_BODY()

    UPROPERTY()
    EMixtormatBehaviorType Type; // Warp, Fold, Push, Wrinkle, Carve

    UPROPERTY()
    TArray<FMixtormatLayerChild> Fields; // Flat list of Fields (Noise, Source, Previous, Mask)

    UPROPERTY()
    TArray<FMixtormatParameterBinding> ParameterBindings;
};

// Extend FMixtormatLayerChild
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayerChild
{
    // ... existing fields ...
    
    // NEW: For Behavior type, holds the behavior data. For Field type, holds field data.
    UPROPERTY()
    FMixtormatBehavior BehaviorData; 
    
    UPROPERTY()
    FMixtormatFieldData FieldData; // NEW struct for typed field production
};
```
- **Typed Field Model**: Expand `EMixtormatPublishedFieldKind` to include `Height`, `Normal`, `Curvature`, `Mask`, `ID`.
- **Explicit Conversions**: Introduce a `FieldConversion` pass in the compositor to handle `ScalarSigned` -> `Height` or `Vector2` -> `Flow` when required by a Behavior.

---

## 6. Proposed Execution Graph

1. **Gather & Flattening**: The Gather phase (`MixtormatGeneratorGather.cpp`) recursively traverses the shallow hierarchy, resolving all `Source` and `Instance` references. It produces a **linear execution list** of `FResolvedField` and `FResolvedBehavior` operations, preserving the authored order.
2. **Field Evaluation**: Leaf fields (e.g., `Noise`, `Source`, `Previous Field`) are evaluated first. Results are cached in `Ctx.PublishedFieldOutputs` using a hash of the field parameters and input dependencies.
3. **Behavior Execution**: Behaviors are executed in the flattened order. Each behavior requests its required typed fields from the cache. If a field is missing or mismatched, an explicit conversion pass is inserted.
4. **Composition**: If multiple behaviors output the same field type (e.g., `Displacement`), they are composed using a predefined rule (e.g., `Add` for Push, `Max` for SDF, `Lerp` for Masks).
5. **Publication**: The final accumulated fields are published to the layer's output bundle, maintaining backward compatibility with existing `RegionIds` and `Flow` consumers.

---

## 7. Three UI Concepts

1. **Compact Nested Stack**  
   - **Description**: Indented rows in the main layer stack. Behaviors are collapsible headers; Fields are indented rows beneath them.  
   - **Pros**: Familiar, minimal screen real estate, leverages existing drag-and-drop infrastructure.  
   - **Cons**: Can become visually cluttered if nesting goes beyond two levels.  

2. **Nested Behavior Cards with Field Rows**  
   - **Description**: Behaviors are distinct, bordered cards in the stack. Fields are listed in a dedicated, compact panel within the card.  
   - **Pros**: Clear visual separation, easy to reorder entire behaviors.  
   - **Cons**: Takes up significant vertical space; conflicts with Mixtormat's existing compact row design.  

3. **Hybrid Stack with Inspector Field Editor (Recommended)**  
   - **Description**: The main stack shows only Behaviors as flat rows (with a small badge indicating the number of fields). Selecting a Behavior opens its Fields in the existing Inspector panel.  
   - **Pros**: Keeps the main stack clean and compact. Leverages the existing, robust Inspector infrastructure for parameter editing and field management.  
   - **Cons**: Requires context switching (clicking the Behavior, then looking at the Inspector) to edit fields.  

---

## 8. Migration Matrix

| Old Feature | V2 Equivalent | Compatibility Strategy |
|---|---|---|
| `Generator` (e.g., Rock Formation) | `Behavior — Rock Formation` (Wrapper) | **Adapter**: Wrap existing `FMixtormatGenerator` payload in a `Behavior` struct. No data loss. |
| `Structural Warp` child | `Behavior — Warp` | **Adapter**: Map `StructuralWarp` child to a `Warp` behavior. Its target remains a GUID reference. |
| `Height Push` child | `Behavior — Push` | **Adapter**: Map `HeightPush` child to a `Push` behavior. Reuse existing R32F shift accumulation. |
| `Generator Flow` params | `Field — Flow` (under Behavior) | **Adapter**: Convert flow parameters into a `Flow` field child consumed by the generator behavior. |
| Scoped Masks | `Field — Mask` (under Behavior) | **Direct Mapping**: Existing scoped masks become children of the Behavior. `ScopeOwnerChildId` remains valid. |
| `Height Blend` / `Curve` / `Color Ramp` | `Behavior — Remap` / `Color` | **Adapter**: Retain as sublayers for now, or map to new Behaviors with a `Previous Field` input. |

---

## 9. Implementation Roadmap

**Smallest Complete V2 Implementation Slice**:  
Prove nested ownership, typed field composition, and GPU execution with **one** Behavior before rewriting the rest.
1. **P0 (Data Model)**: Introduce `EMixtormatLayerChildType::Behavior` and `FMixtormatBehavior` struct. Add `Behavior` and `Field` to `EMixtormatParameterOwnerType`.
2. **P0 (Single Behavior)**: Implement **Behavior — Push**. It owns a single `Field — Signed Height` (which can be a `Source` or `Noise`).
3. **P1 (Gather)**: Update `MixtormatGeneratorGather.cpp` to resolve the `Push` behavior and its field, mapping it to the existing `FGeneratorHeightPushRenderData`.
4. **P1 (GPU)**: Update `MixtormatGpuGeneratorPasses.cpp` to execute the `Push` behavior using the existing `MixtormatGeneratorHeightPush.usf` logic, driven by the new field resolution.
5. **P2 (UI)**: Add UI support in `MixtormatInspectorGenerators.cpp` and `MixtormatLayerDragDrop.cpp` for adding a Behavior and nesting a Field under it (using the Hybrid Stack concept).

---

## 10. Wrinkling Prototypes Analysis

- **wrinkling01.cl**: Computes a weighted Laplacian and normal-variation curvature, then applies curvature-dependent diffusion to a shadow mask.  
  - *Reusable*: Laplacian calculation, curvature-based mask generation.  
  - *Requirements*: Needs intermediate fields for `Curvature` and `Shadow Mask`. Requires iterative evaluation (diffusion).  
- **wrinkling02.cl**: Uses the curvature/shadow mask to drive displacement along a blended normal/gravity direction.  
  - *Reusable*: Fold-side selection, mask composition, displacement calculation.  
- **Differences from Existing**: Existing `Fold`/`Breakup` are SDF-based or procedural. Wrinkling is **curvature-driven and iterative**, relying on mesh-neighbor operations.  
- **V2 Placement**: Wrinkle should be a **Behavior**, not a field generator. It should consume a `Curvature` field and a `Height` field, and produce a `Displacement` field.  
- **Mesh to Heightfield Adaptation**: The `#bind point neighs int[]` (mesh neighbors) must be replaced with a texture-space 3x3 or 5x3 kernel sampling the heightfield, using wrapped UV coordinates for tileability.  
- **Parameters**: `Fold Side` (Upper/Lower/Both), `Mask Mode` (Shadow/Curvature/Combined), `Amount`, `Normal Weight`, `Gravity Weight`.

---

## 11. Unresolved Design Decisions (Must be settled before implementation)

1. **Cyclic Dependencies**: How should the system handle a `Field` that references its own `Behavior`'s output (e.g., for iterative diffusion like in `wrinkling01.cl`)? The execution graph must detect and either allow controlled iteration or reject the cycle.
2. **"Previous Field" Semantics**: Does "Previous Field" refer to the previous field *within* the same Behavior, or the accumulated output of the *previous Behavior*? (Recommendation: Previous Behavior, to enable chaining).
3. **Field Composition Rules**: When two `Warp` behaviors target the same generator, how are their displacement fields combined? (Recommendation: Additive for vectors, Max for SDFs, Lerp for Masks, with an explicit `Composition Mode` parameter on the Behavior).
4. **Performance of Flattening**: If a layer has many nested behaviors and fields, the gather-phase flattening and dependency resolution could become a bottleneck. Caching the flattened execution graph per-material is required.

---
*Audit completed based on source inspection of `main` branch as of October 10, 2026. No builds, tests, or runtime validations were performed, per AGENTS.md constraints.*