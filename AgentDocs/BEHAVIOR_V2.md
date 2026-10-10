# Behavior System V2 — implementation contract

Status: Generator-owned V2 PostGeneration Warp and signed Push now have shared GPU paths, typed source gathering, independent scoped masks, optional published Scalar01 Influence, and two per-pixel scalar drivers. Warp supports published Flow/UVMap or Own Native Height Gradient; Push accepts Own Native Height, Previous Running Height, or published ScalarSigned. Execution remains unvalidated by Unreal 5.8 builds or viewport inspection. PreGeneration, Carve / Deposit, and Deform are not implemented.

## Ownership
- A Behavior is an appended `EMixtormatLayerChildType::Behavior` in the existing flat `Layer.Children` array.
- `ScopeOwnerChildId` must resolve **directly** to an earlier Generator child. The runtime helper `ResolveBehaviorGeneratorIndex` checks this at evaluation. `SanitizeStaleOwners` disables Behaviors with missing, invalid, indirect or non-Generator owners without converting them into root operations. Group expansion also disables effective Behavior clones whose owner cannot resolve to an earlier Generator; authored templates remain unchanged.
- Field inputs are serializable typed sockets on `FMixtormatBehavior`, not nested child arrays.
- Existing legacy child kinds, enum values, generator module order, source references and saved materials remain untouched.
- Invalid/unowned Behaviors are skipped by the dedicated V2 gather and never fall into the legacy Effect path.

## Evaluation stages (V2 only)
1. **PreGeneration**: operate on generator sampling coordinates before the owning generator evaluates. Only field sources available before native generation are legal. A self-native-height reference here is invalid.
2. **PostGeneration**: implemented for **Warp using a published Flow/UVMap or an explicit OwnNativeHeight → Gradient direction**, and native signed-height Push. This shared executor runs after legacy generator-owned Flow tools but before normalization/output scale. It remaps native signed height, coverage, IDs and supported companions. Carve / Deposit and Deform kinds, and unsupported input types, are stored but do not execute.
3. PreGeneration is a declared but not yet implemented stage. Existing Structural Warp/Height Push continue executing at authored sibling positions with their historical order-dependent accumulation and Strata/non-Strata distinctions. Do **not** reparent/migrate those operators without explicit compatibility handling.

## Field semantics
- `OwnNativeHeight`: signed scalar in native generator units; available only after generation.
- `OwnBoundary`: generator-defined signed boundary, if the generator family publishes one. Noise and Cliff Strata do not.
- `PreviousRunningHeight`: snapshot of completed earlier authored generator chain, never the owning generator's future output.
- `PublishedOutput`: explicit `FMixtormatOutputReference` (Layer/Shelf), validated for kind, endpoint and dependency order.
- `None`: unconnected; execution must not guess a source.
- `ScalarSigned` is not automatically SDF. `Vector2` is not automatically Flow or UVMap. `Direction` accepts explicit Flow/UVMap or the local gradient conversion. `Influence` accepts only earlier-layer published Scalar01, with no signed Value-to-Coverage coercion. A connected but unavailable Influence causes the Warp to skip rather than operate at full strength.
- A Warp Direction can consume `OwnNativeHeight` **only by an explicit gradient-to-UV transform** in `MixtormatBehaviorWarp.usf` (`HeightGradientCS`); it reads the current native signed-height snapshot using wrapped central differences, normalizes the slope response and scales by authored `GradientReach`. It is not published as Flow or Vector2 and no scalar is silently retyped as a vector.

## Current executable Warp subset
- Any generator module (Strata Carver, Rock Formation, Cracks, Pebbles, Cliff Strata, Noise) may own V2 Warp via a direct `ScopeOwnerChildId`.
- A Warp is gathered only when enabled, scoped correctly and at `PostGeneration`, with `Direction.Origin = PublishedOutput` and `Kind = Flow | UVMap` **or** `Direction.Origin = OwnNativeHeight`; `Height.Origin = None`, optional `Influence.Origin = None | PublishedOutput(Scalar01)`, and finite nonzero Strength. `GradientReach = 0` is neutral for the local gradient mode.
- Only completed, typed earlier source fields are consumed. Shelf Flow/UVMap dependencies are demanded before the material stack; flow tracing uses the existing flow-to-UV pipeline. UVMap Strength uses a separate tiny shader that scales lifted displacement without discarding winding.
- All new Warp operations for a generator execute in their child order via the same `ApplyGeneratorPostWarpBehaviors` GPU function. No family-specific branch or duplicate remap shader is introduced.
- Both legacy completed Structural Warp and V2 Warp reuse `RemapGeneratorModuleOutputs`; Noise Value/Gradient publication retains the existing dedicated transport semantics.
- Legacy Generator Flow tools run before V2 post-generation Behaviors. Legacy Structural Warp for non-Strata remains applied after signed normalization. This is an explicit transitional ordering, not yet an arbitrary interleavable operation chain.
- **Initial UI authoring is present:** right-click Generator → **Add Warp Behavior (V2)** → choose **Own Height Gradient**, a compatible typed Flow/UV published field, or **Choose source later**. Gradient Reach is enabled only in local mode. **Influence Field** accepts an ordered, published Scalar01 from an earlier layer. Existing scoped-mask authoring (including Noise Gate) also targets Behavior and combines multiplicatively with Influence. Missing/invalid Direction or Influence sources are indicated as unavailable in the chips; the Warp skips unavailable fields instead of silently using full strength. The picker does not yet offer expanded group producers.
- **Not yet complete:** general field composition, PreGeneration, other Behavior kinds, Strength/Gradient Reach GPU drivers, per-socket render source identities and full instance/group authoring. Post-generation published Flow Warp now reuses the existing Flow Amount/Trace Length scalar-driver shader, with enabled earlier-layer CombinedMask snapshots, unchanged authored fallback for unresolved signal textures, and prefix-cache demand propagation. Strength and Gradient Reach now use the same ordered mask-driver signaling and may activate zero-authored values; actual runtime semantics are unverified. This is a partial driver implementation, not general Behavior driver parity. These must not be inferred from the presence of a Warp row.
- Shader field demand includes V2 Warp Direction and optional Scalar01 Influence references, preventing neutral or cached producers from silently dropping a required field.
- Scoped masks are gathered with the canonical `AddScopedFeatureMask` path and blend displacement toward identity through `MixtormatBehaviorWarp.usf`, preserving neutral behavior outside the mask. Additional `Influence` multiplies displacement with that mask for either published Flow/UV or Own Height Gradient. Shader bindings reject incomplete or unsupported formats instead of dropping the influence.
- No build, shader compile, Unreal launch or GPU validation was performed.

## Required next code increments
1. Extend the initial Warp UI with typed source/status messages, reusable mask authoring from gallery/Noise/field sources, remaining Behavior Strength/Gradient Reach drivers, driver source-kind parity, and editable expanded-group sources.
2. Extend field contracts beyond the implemented local gradient transform and Scalar01 Influence to Own Boundary and general field composition, without changing serialized legacy operations.
3. Define the next execution phase for PreGeneration and interleaved generator-owned tools while preserving legacy order.
4. Review group / clipboard / instances / source scheduling under effective projections, and examine typed-transport limits for every generator bundle.
5. Validate Unreal shader compilation and real material output when explicitly authorized.

## Parallel development
Noise V2 is merged into `main` through PR #1 (`fb3ebbd`). Behavior V2 does not alter Noise algorithms, serialized enums or GPU producer passes. Its shared Inspector file retains the Noise V2 controls. `main` was merged into this feature branch through PR #2 (`40cb6f6`) and PR #4 (`2883362`), leaving `main` unchanged. The branch incorporated `main` through PR #4, but as of the latest October 10 comparison is two commits behind current `main` (Shell and unrelated audit/roadmap files); synchronization remains required before merge.

Repository policy: source/static review only by default; build, test and Unreal launches require user approval.

## Unvalidated October 10 continuation
- Scope sanitation and group-member clone expansion now fail closed for invalid Behavior owners; existing authored group template data remains intact.
- Published Flow Warp gathers two existing `BehaviorFlow` scalar-driver slots for `FlowAmount` and `FlowTraceLength`, allows enabled earlier-layer CombinedMask sources, demands their snapshots in composition, and forwards both slots to the existing shared Flow tracing shader. Driver semantics for other source kinds/Behavior parameters remain unimplemented.
- The Warp Inspector hides Gradient Reach outside Own Height Gradient mode and Flow controls outside published Flow mode.
- These changes were source-reviewed only. Unreal 5.8 C++/UHT, shader compilation, viewport behavior, GPU masking, undo/redo, and saved-project compatibility have **not** been validated. Keep PR #5 draft; do not merge before authorization and completion.

## October 10 current branch continuation
- Corrected authored-zero driver gating for Strength and Gradient Reach, including exact identity for evaluated zero-weight Warp coordinates. Both drivers retain authored fallback when no completed signal texture exists.
- Implemented signed-height PostGeneration Push in the shared generator operation executor before normalization. Its source is typed ScalarSigned, Own Native Height, or Previous Running Height; SDF is not implicitly converted. Each Push has its own scope mask and optional Scalar01 Influence. A GPU scalar Strength driver uses the same ordered snapshots as Warp.
- Added generator context-menu Push creation, typed signed-height source picker, Inspector Strength/Influence/enable controls, source availability indication, and per-kind child labels. The legacy Height Push remains unchanged.
- Updated Behavior-branch ancestry with the two latest main commits, retaining unrelated Shell UI and audit documentation; main is unchanged.
- Implementation is not complete. Remaining: explicit pre-generation generator capabilities and sampling, typed Carve/Deposit SDF/boundary rules and GPU executor, distinct Deform semantics, broadened driver sources/dependency handling, group and instance parity, general source/mask authoring, saved-material compatibility verification, and Unreal C++/UHT/HLSL/runtime checks. No builds/tests were authorized or run.
