# Behavior System V2 — implementation contract

Status: Shared PostGeneration Warp gather and GPU execution are implemented on this branch for any generator family. Only typed published Flow/UVMap Direction input is executed; UI authoring, scoped masks, other Behavior kinds/stages, and runtime/shader validation remain outstanding.

## Ownership
- A Behavior is an appended `EMixtormatLayerChildType::Behavior` in the existing flat `Layer.Children` array.
- `ScopeOwnerChildId` must resolve **directly** to an earlier Generator child. The runtime helper `ResolveBehaviorGeneratorIndex` checks this when called; generic owner sanitization does not automatically enforce Behavior-specific ownership. A future evaluator must reject invalid owners.
- Field inputs are serializable typed sockets on `FMixtormatBehavior`, not nested child arrays.
- Existing legacy child kinds, enum values, generator module order, source references and saved materials remain untouched.
- Invalid/unowned Behaviors are skipped by the dedicated V2 gather and never fall into the legacy Effect path.

## Evaluation stages (V2 only)
1. **PreGeneration**: operate on generator sampling coordinates before the owning generator evaluates. Only field sources available before native generation are legal. A self-native-height reference here is invalid.
2. **PostGeneration**: implemented for **Warp using a published Flow/UVMap Direction**. This shared executor runs after legacy generator-owned Flow tools but before normalization/output scale. It remaps native signed height, coverage, IDs and supported companions. Other Behavior kinds and input types are stored but do not execute.
3. PreGeneration is a declared but not yet implemented stage. Existing Structural Warp/Height Push continue executing at authored sibling positions with their historical order-dependent accumulation and Strata/non-Strata distinctions. Do **not** reparent/migrate those operators without explicit compatibility handling.

## Field semantics
- `OwnNativeHeight`: signed scalar in native generator units; available only after generation.
- `OwnBoundary`: generator-defined signed boundary, if the generator family publishes one. Noise and Cliff Strata do not.
- `PreviousRunningHeight`: snapshot of completed earlier authored generator chain, never the owning generator's future output.
- `PublishedOutput`: explicit `FMixtormatOutputReference` (Layer/Shelf), validated for kind, endpoint and dependency order.
- `None`: unconnected; execution must not guess a source.
- `ScalarSigned` is not automatically SDF. `Vector2` is not automatically Flow or UVMap. Direction/Height/Influence sockets need per-behavior kind eligibility and neutral handling.
- A Warp Direction input cannot consume `OwnNativeHeight` as a vector without an **explicit** scalar-to-direction operation, such as a height-gradient field producer. That operation is not implemented. Do not silently reinterpret scalar textures as Flow.

## Current executable Warp subset
- Any generator module (Strata Carver, Rock Formation, Cracks, Pebbles, Cliff Strata, Noise) may own V2 Warp via a direct `ScopeOwnerChildId`.
- A Warp is gathered only when enabled, scoped correctly and at `PostGeneration`, with `Direction.Origin = PublishedOutput`, `Direction.Published.Kind = Flow | UVMap`, `Height.Origin = None`, `Influence.Origin = None`, and finite nonzero Strength.
- Only completed, typed earlier source fields are consumed. Shelf Flow/UVMap dependencies are demanded before the material stack; flow tracing uses the existing flow-to-UV pipeline. UVMap Strength uses a separate tiny shader that scales lifted displacement without discarding winding.
- All new Warp operations for a generator execute in their child order via the same `ApplyGeneratorPostWarpBehaviors` GPU function. No family-specific branch or duplicate remap shader is introduced.
- Both legacy completed Structural Warp and V2 Warp reuse `RemapGeneratorModuleOutputs`; Noise Value/Gradient publication retains the existing dedicated transport semantics.
- Legacy Generator Flow tools run before V2 post-generation Behaviors. Legacy Structural Warp for non-Strata remains applied after signed normalization. This is an explicit transitional ordering, not yet an arbitrary interleavable operation chain.
- **Not yet authorable in the UI.** A dedicated row/menu/Inspector and parameter binding registration are required before a user can create and configure this from the editor. Scoped Behavior masks, Own Height Gradient, PreGeneration, other Behavior kinds, and distinct per-socket render source identity are not implemented.
- No build, shader compile, Unreal launch or GPU validation was performed.

## Required next code increments
1. Add Behavior add menu, row, and Inspector controls using the existing themed layer UI and parameter metadata.
2. Extend explicit field contracts to local Own Height Gradient/Boundary, independent masking and per-socket sources without changing serialized legacy operations.
3. Define the next execution phase for PreGeneration and interleaved generator-owned tools while preserving legacy order.
4. Review group / clipboard / instances / source scheduling under effective projections, and examine typed-transport limits for every generator bundle.
5. Validate Unreal shader compilation and real material output when explicitly authorized.

## Parallel development
Noise V2 owns noise-specific shaders/passes on `feature/noise-v2`. Behavior V2 does not change noise-specific files or enums without coordination.

Repository policy: source/static review only by default; build, test and Unreal launches require user approval.
