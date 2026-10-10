# Behavior System V2 — implementation contract

Status: P0 serialization and direct-ownership validation are present on this branch; GPU evaluation and UI authoring are not implemented.

## Ownership
- A Behavior is an appended `EMixtormatLayerChildType::Behavior` in the existing flat `Layer.Children` array.
- `ScopeOwnerChildId` must resolve **directly** to an earlier Generator child. The runtime helper `ResolveBehaviorGeneratorIndex` enforces this condition.
- Field inputs are serializable typed sockets on `FMixtormatBehavior`, not nested child arrays.
- Existing legacy child kinds, enum values, generator module order, source references and saved materials remain untouched.
- Invalid/unowned Behaviors must be ignored by future GPU evaluation rather than interpreted as global effects.

## Evaluation stages (V2 only)
1. **PreGeneration**: operate on generator sampling coordinates before the owning generator evaluates. Only field sources available before native generation are legal. A self-native-height reference here is invalid.
2. **PostGeneration**: operate on the generated native height/companion fields before normalize/output-scale. `OwnNativeHeight` resolves the current stage snapshot, not a recursive request to re-evaluate the owner.
3. Both stages execute as generator-owned V2 semantics. Existing Structural Warp/Height Push continue executing at authored sibling positions with their historical order-dependent accumulation and Strata/non-Strata distinctions. Do **not** reparent/migrate those operators without explicit compatibility handling.

## Field semantics
- `OwnNativeHeight`: signed scalar in native generator units; available only after generation.
- `OwnBoundary`: generator-defined signed boundary, if the generator family publishes one. Noise and Cliff Strata do not.
- `PreviousRunningHeight`: snapshot of completed earlier authored generator chain, never the owning generator's future output.
- `PublishedOutput`: explicit `FMixtormatOutputReference` (Layer/Shelf), validated for kind, endpoint and dependency order.
- `None`: unconnected; execution must not guess a source.
- `ScalarSigned` is not automatically SDF. `Vector2` is not automatically Flow or UVMap. Direction/Height/Influence sockets need per-behavior kind eligibility and neutral handling.

## Required next code increments
1. Implement a non-mutating typed resolver for each Behavior input; report invalid/forward/missing/scope-incompatible sources. Respect source shelf scheduling and group-expanded identity.
2. Add a stage-aware gather representation, based on original child GUID rather than an ambiguous authored/effective index.
3. Implement the Rock Formation **PostGeneration Warp** prototype using existing native flow/UV transport code. Preserve height and companion field remapping contracts.
4. Opt-in dispatch only for valid, enabled scoped Behavior children. Explicitly define stage ordering between generator modules and post-generator child tools.
5. Add compact row and Inspector controls through shared UI recipes/tokens, parameter metadata, copy/paste, undo and instance support.
6. Verify saved-project parity, disable/enable semantics, mask scope, normalization and source publishing before expanding to other generators.

## Parallel development
Noise V2 owns noise-specific shaders/passes on `feature/noise-v2`. Behavior V2 does not change noise-specific files or enums without coordination.

Repository policy: source/static review only by default; build, test and Unreal launches require user approval.
