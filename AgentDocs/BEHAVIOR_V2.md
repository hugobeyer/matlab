# Behavior System V2 — implementation contract

Last source update: October 10, 2026. Branch: `feature/behavior-system-v2`; review PR #5 remains draft. Unreal 5.8, UHT, shader, GPU, runtime, save/load and performance validation have **not** run. Do not merge without authorized validation.

## Ownership and compatibility
- The flat `Layer.Children` array owns `EMixtormatLayerChildType::Behavior`. `ScopeOwnerChildId` points directly to an **earlier Generator child**, never a layer-wide mask or Effect. Ownership/instance and expanded-group checks must fail closed; no implicit source guessing.
- Existing `StructuralWarp` and `HeightPush` children retain their serialized payload and historical ordering. Saved legacy material data is not migrated or repurposed. No enum values were reordered; the new `CarveWidth` float is an appended Behavior property.
- Generator native height is signed; the zero-preserving max-abs Normalization ON / raw un-clamped OFF contract is unchanged. All PostGeneration Behavior passes run **after legacy Flow and before normalization/Height Scale**.
- `PostGeneration Warp` moves the generator's completed native bundle (including declared transportable IDs, coverage and companions). `PostGeneration Deform` resamples only its signed native-height texture; IDs, coverage, named fields and material region identity remain in their original generator frame. This deliberate distinction must not be erased in future refactors.

## Implemented operation paths (source-level; unvalidated)
| Operation | Allowed inputs | Output rule |
| --- | --- | --- |
| Warp / PostGeneration | Published `Flow` or `UVMap`; or `OwnNativeHeight` explicit height-gradient-to-UV transform | Scale displacement by driven signed Strength, scoped masks and optional published `Scalar01` Influence; remap completed generator bundle |
| Push / PostGeneration | Published `ScalarSigned` Height or other typed signed output, Shelf Height; `OwnNativeHeight`; `PreviousRunningHeight` | Add signed Height delta times driven Strength and mask/Influence to native module height; no implicit SDF conversion |
| Carve / Deposit / PostGeneration | Published `SDF` from an ordered earlier layer, or `OwnBoundary` where the owning generator has a valid signed-distance+validity field | Inside negative distance: subtract `Strength * saturate(-distance / CarveWidth)`, modulated by independent scope mask and Influence. Positive Strength carves; negative deposits. Changes height only. `CarveWidth` is positive distance in UV units |
| Deform / PostGeneration | Published `Flow` / `UVMap`; or `OwnNativeHeight` explicit height-gradient-to-UV transform | Same directional transport/Strength calculation as Warp; remap native height only, leaving companions fixed |

Each of these operations has generator-menu creation, a kind-specific Inspector, source-status UI, enable checkbox, Strength and appropriate source controls. Flow Amount/Trace Length/Steps apply to Flow directions; Gradient Reach applies only to local height-gradient directions; CarveWidth only to Carve.

**Execution constraints:** `Direction` is only for Warp/Deform and must be Flow/UVMap or explicit native-height gradient. `Height` is only for Push/Carve and must retain its kind. `Influence` is an independently connected published `Scalar01` from a completed earlier layer. A connected but incomplete/missing field fails closed; it never becomes full strength. No `ScalarSigned`→SDF, `Vector2`→Flow, `SDF`→Height or Noise Value→coverage coercion is authorized.

The shared evaluator runs all scoped PostGeneration Behavior entries in authored child order at their owning generator's stage, before that generator's normalization. Push's `PreviousRunningHeight` is the already-completed earlier generator chain; `OwnNativeHeight` is the current native snapshot (including earlier owned operations).

## Drivers and ordered field demand
- Per-pixel scalar driver slots for Behavior `Strength` and `GradientReach`, and Behavior Flow `FlowAmount` and `FlowTraceLength`, are gathered and bound through the shared GPU contracts.
- **Current supported driver signal:** enabled `CombinedMask` from an earlier layer. Unsupported source kinds do not acquire a signal. Neutral authored zero may become active through a valid Strength or Gradient Reach driver; an actually evaluated zero produces the identity UV/no native-height delta.
- Demand scheduling includes published Warp and Deform Direction, published Push and Carve Height/SDF, all connected Influence fields and their ordered mask driver snapshots. Shelf scheduling supports Warp/Deform Flow/UVMap and Push ScalarSigned. The current shelf classifier does not authorize SDF or Scalar01 shelf sources.
- Source/Influence references should resolve at the **owning generator's evaluation point**. A later producer may not be used merely because it precedes the Behavior row in the editor.

## Not yet complete
1. **PreGeneration:** declared enum stage, no GPU execution. It must transform generator sampling coordinates before generation, not disguise a post-generation bundle resample as pre-generation. Implement a declared capability and shared coordinate binding across all six generator families, with correct lifted UV winding and cache invalidation before exposing UI.
2. **Full driver source-kind parity:** current Behavior scalar slots support earlier CombinedMask, not all published-field/region/gate/local parameter driver sources.
3. **Expanded-group and instance authoring parity:** existing stale-ownership checks are partial, not a substitute for exhaustive source remapping, duplicate/paste, template and instance verification.
4. **Field authoring/composition:** no universal field graph or automatic conversion between SDF, height, mask, flow, vector or ID semantics.
5. **Unreal validation:** C++/UHT build, shader compilation, signed-height/flow visual checks, UV seam/winding, six-family behavior output checks, masking/Influence/drivers, undo, save/load, clipboard/group/instance migration, and performance profiling have not been run.

## Safe handoff and merge gate
- Keep `main` unchanged and PR #5 in draft.
- Preserve `MixtormatRuntime`→`MixtormatShaders`→`MixtormatEditor` module layering.
- Preserve Noise V2 merged in main, legacy Structural Warp/Height Push, the signed-height contract, source field typing, authoring serialization and shipped JSON settings.
- For every new shader resource, keep the `FGlobalShader::FParameters` declaration, .usf declarations, `ClearUnusedGraphResources` and RDG lifetime in sync.
- Repository instruction: no builds/tests/Unreal launches without explicit authorization. Static source review and code edits here do not establish compile or GPU correctness.
