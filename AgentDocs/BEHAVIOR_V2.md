# Behavior System — migration in progress

Last source update: October 10, 2026. Branch: `feature/behavior-system-v2`; review PR #5 remains draft. Unreal 5.8, UHT, shader compilation, GPU runtime, save/load and performance validation have **not** run. Do not merge without authorized validation.

This document describes the target architecture and the currently implemented Behavior paths. **Legacy effects remain live in the source**; do not treat the removal sections below as implementation evidence.

> **P0 field contract (supersedes conflicting architecture below).**
> `AgentDocs/FINAL_BEHAVIOR_PLAN.md` and `AgentDocs/FIELD_CONTRACT_P0.md` are the source of
> truth for the final architecture. Where this document conflicts, they win:
>
> 1. Flow is a **Generator preset** of the Noise V2 implementation, not a Behavior. The
>    `FlowField` Behavior type and the flow-solve parameters on `FMixtormatBehaviorFlowSettings`
>    are legacy paths, removed in P3 — not part of the final architecture.
> 2. Typed field sockets with published references (`Direction` / `Height` / `Influence`
>    source routing) are replaced by **automatic field inheritance**: children read the
>    working fields of preceding siblings. No Inspector target/source routing survives;
>    socket and picker removal is P3/P4 work.
> 3. The Behavior flow float4 packing (direction, signed distance, influence) is superseded
>    by the canonical Flow layout in `FIELD_CONTRACT_P0.md` section 1 (VectorXY with
>    magnitude in RG, influence in A, validity in a separate texture, signed distance a
>    separate field). Migration happens in P2; existing channels are not silently
>    reinterpreted.
> 4. FlowDirection publication after post-generation Behaviors (section 8 below) is the
>    known evaluation-order defect; the required order evaluates intrinsic Flow **before**
>    scoped children (P2).
>
> Sections 1–9 below remain an accurate description of the *currently implemented* paths.

## 1. Target: one structural authoring path

> *Superseded in part:* under the final architecture a Flow preset child of a Generator is a
> field producer (a second authoring path that produces rather than rewrites), and Behaviors
> consume working fields automatically instead of through typed socket references. The
> description below is the current implementation; migration assignments are in
> `FIELD_CONTRACT_P0.md` section 7.

A **Behavior** (`EMixtormatLayerChildType::Behavior`, payload `FMixtormatBehavior`) is the only way to rewrite a Generator's own output. It is an ordinary entry in the flat `Layer.Children` array, scoped to an **earlier Generator child** through `ScopeOwnerChildId`. There is no separate target edge and no module that lives at its own authored position in the child chain.

The following legacy modules are **targeted for removal**, but the legacy flow effects and their dispatch are still live as of this source review:

| Removed | Replacement |
|---|---|
| `HeightPush` child + `FMixtormatGeneratorHeightPush` | Behavior, `Type = Push`, `Stage = PreGeneration` |
| `StructuralWarp` child + `FMixtormatGeneratorStructuralWarp` | Behavior, `Type = Warp`, either stage |
| `ShapeDeform` effect | Behavior `Carve` (boundary offset) or `Warp` (bulge displacement) |
| `GeneratorFlow` effect | Behavior `Warp` |
| `FlowCarve` effect | Behavior `Carve` |
| `GravityFlow` effect | Behavior `Warp` with an authored direction field |

`EMixtormatLayerChildType::HeightPush` and `::StructuralWarp` still exist **as enum values**, marked `Deprecated`. They are append-only serialized values: renumbering or removing them would silently retype every child in every saved asset. Their deprecation and removal must be verified per call site; enum metadata alone does not establish that all authoring and execution paths have been removed.

The same applies to `EMixtormatParameterOwnerType::HeightPush` (23), `::StructuralWarp` (24) and `::StructuralWarpFlow` (26). Those indices are held back so the surviving `MaskNoise` (25), `Behavior` (27) and `BehaviorFlow` (28) keep matching what saved bindings stored. Nothing resolves to the dead owners any more.

The target removal includes the structural-connection subsystem that existed to draw Height Push and Structural Warp source/target edges — `MixtormatStructuralConnections.cpp`, `MixtormatStructuralConnectionModel.*`, `MixtormatStructuralConnectionProjection.*`, `SMixtormatStructuralSourcePicker.*`, `SMixtormat::StructuralLinksPreserved`, `EvaluateStructuralLink`/`ForGather` and the row's `StructuralLink` / `StructuralCount` / `StructuralHighlightRole` / `bStructuralSource` slots — which must be verified and cleaned up in source. A Behavior names its generator by scope and its fields by typed socket, so it has one editable end rather than an authored source and target pair.

## 2. Operations

| Type | Stage | Inputs | Output rule |
|---|---|---|---|
| `Warp` | PreGeneration **and** PostGeneration | Published `Flow` or `UVMap` | PreGeneration moves the coordinates the generator samples; PostGeneration remaps the completed bundle |
| `Push` | PostGeneration | Published `ScalarSigned`, `OwnNativeHeight`, `PreviousRunningHeight` | Adds a signed native-height delta scaled by driven Strength, scope mask and Influence |
| `Carve` | PostGeneration | `OwnBoundary`, or a published `SDF` | `Height -= Strength * saturate(-distance / CarveWidth)` inside a negative distance field. Positive Strength carves, negative deposits. Height only |
| `Deform` | PostGeneration | Published `Flow` / `UVMap`; or `OwnNativeHeight` | Same directional transport as Warp, but resamples **native height only** |

Only Warp is meaningful at both stages. Push, Carve and Deform operate on produced relief, so Gather rejects them at PreGeneration rather than running them at an arbitrary point in the pass list.

**Why Deform survives consolidation.** It is the one operation that cannot be expressed through the other three, and the reason is structural rather than cosmetic. Warp remaps the whole bundle, so its Region IDs, coverage and named attributes travel with the height. Push and Carve add a scalar they do not resample. Only Deform resamples relief while deliberately leaving its companions at their authored generator-domain placement, which is what keeps a rock's region identity pinned to the rock rather than to the warped coordinate. `MixtormatGpuGeneratorPasses.cpp` implements this as `RemapBundleField(Height)` for Deform against `RemapGeneratorModuleOutputs` for Warp.

## 3. Fields

A field socket (`FMixtormatBehaviorFieldInput`) is either a **local semantic snapshot** (`EMixtormatBehaviorFieldOrigin`) or an **explicit typed reference** (`Origin = PublishedOutput`, an `FMixtormatOutputReference` naming a kind and an output name).

`FMixtormatBehaviorFieldInput::Kind` is the socket's own published-type contract — `Flow`, `UVMap`, `Vector2`, `SDF`, `ScalarSigned`, `Scalar01`, `Color`. It decides whether an authored reference is accepted. **No implicit conversion is performed between published types.** A coordinate socket consuming a `Vector2` output is an invalid authoring state that Gather rejects, not a value it reinterprets. There is no `ScalarSigned`→SDF, `Vector2`→Flow, `SDF`→Height or Noise Value→coverage coercion anywhere in the pipeline.

Socket rules, enforced in `MixtormatChildScope::ValidateBehaviorInputs`:

- `Direction` — Warp and Deform only, and only `Flow` or `UVMap`. Both are destination→source coordinate contracts, which is what makes a bundle remap a valid resampling.
- `Height` — Push and Carve only, and it retains its kind.
- `Influence` — an independently resolved `Scalar01`. It multiplies the final weight alongside the generator's own coverage and any scoped Mask child; it shapes where the Behavior acts, not whether the generator runs.

A connected but incomplete, wrong-kind or wrong-extent field **fails closed**. It never becomes full strength.

## 4. Execution

- **PreGeneration** (`BuildGeneratorPreCoordinates`): ordered PreGeneration Warps compose a lifted destination→source coordinate field, which the six native generator passes consume through the shared optional `UsePreGenerationUV` / `PreGenerationUV` contract. Dynamic pre-fields bypass geological node-cache lookups; the original path is unchanged when no pre-warp exists.
- **PostGeneration** (`ApplyGeneratorPostBehaviors`): ordered Push/Carve/Deform/Warp run on the generator's own native bundle, **before** the shared signed-height normalization and Height Scale, and before the Noise-only scoped height gate. Push's `PreviousRunningHeight` is the already-completed earlier generator chain; `OwnNativeHeight` is the current native snapshot including earlier owned operations.
- Strata Carver now publishes its own `(distance, validity)` boundary field unconditionally, so `Carve` with `OwnBoundary` works on it. Previously that field was only produced when a Structural Warp was present.

Shader entry points live in `Shaders/Private/MixtormatBehaviorWarp.usf`: `MainCS` (UV scale), `HeightGradientCS` (local gradient direction), `SignedPushCS`, `SignedCarveCS`.

## 5. Drivers

Per-pixel scalar driver slots exist for Behavior `Strength` and `GradientReach`, and for the reflected `FlowAmount` / `FlowTraceLength` on a Behavior's Direction reference (owner `EMixtormatParameterOwnerType::BehaviorFlow`).

The supported signal is an enabled `CombinedMask` from an earlier layer. An unsupported or unavailable signal leaves the authored scalar in place rather than substituting zero, and a neutral authored zero can still become active through a valid driver. An actually **evaluated** zero is exact identity — identity UV, or no native-height delta — even when the input field is unavailable at that texel.

## 6. Preserved contracts

- Generator native height is signed. The zero-preserving max-abs Normalization ON / raw un-clamped OFF contract is unchanged, and Noise keeps its distinct normalization and its scoped height gate. **Noise V2 is untouched by this consolidation.**
- Ownership, instance and expanded-group checks fail closed. `MixtormatLayerGroups::BuildEffectiveLayers` remaps Behavior sockets to member-local producers and disables a shared Behavior whose scope owner does not resolve on a member, rather than letting it become an unscoped operation.
- Clipboard, subtree copy and instance behaviour are preserved; Behavior typed sockets are remapped with the subtree.

## 7. Unvalidated

C++/UHT compilation, shader compilation, GPU runtime behaviour, six-family visual checks, UV seam/winding, mask/gate/driver behaviour, undo, save/load, clipboard/group/instance migration and performance profiling have **not** been run. Static source review is not compile or GPU evidence.

## 8. Critical migration status (source-reviewed)

- `MixtormatGpuGeneratorPasses.cpp` still calls `HasActiveFlowTools` and `AddGeneratorFlowToolPasses` before `ApplyGeneratorPostBehaviors`. This is a **parallel legacy execution path**.
- The old producer publishes `FlowDirection` and `WarpedUV` using the jump-flood / resolve / smooth / apply pipeline. Moving its useful producer controls and publication to generator-owned typed field production is Phase 2 and **has not been implemented**.
- Behavior `Warp`/`Deform` can consume published Flow/UVMap or derive a height-gradient UV map; this is **not equivalent** to migrating the legacy flow solve.
- Behavior `Carve` presently uses `SignedCarveCS` and does **not** preserve the full traced legacy Flow Carve groove/deposit algorithm.
- Do not remove legacy producers before a replacement publishes complete typed fields. Do not claim legacy removal, compilation, or runtime validation without evidence.

## 9. Known remaining work

1. PreGeneration currently accepts only a published `Flow`/`UVMap` Direction. `OwnNativeHeight` is necessarily post-stage and is rejected there.
2. Behavior scalar driver slots support earlier `CombinedMask` only, not the full published-field / region / gate / local-parameter driver source set.
3. Expanded-group and instance authoring parity is verified by construction, not exhaustively.
4. There is no universal field graph. Fields are typed references and local snapshots, composed by the Behavior's own operation, not authored as a free-form graph.