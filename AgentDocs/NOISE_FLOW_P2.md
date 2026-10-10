# Mixtormat — P2 Ordered Flow Integration

Branch: `feature/behavior-system-v2`
Engine target: Unreal Engine 5.8 / PCD3D_SM6
Scope: P2 from `FINAL_BEHAVIOR_PLAN.md` and `FIELD_CONTRACT_P0.md`.

## Implemented in repository source

1. **Flow is generator-owned, not a material layer.** The standalone Flow-layer creation entry was removed. A parent Generator's context menu creates a scoped Noise V2 Generator with the Flow preset. Masks may be children of Flow; Behavior-under-Flow and Flow-under-Flow insertion are rejected.
2. **Ordered execution.** The gather records each Generator child's scoped owner. The parent's post-generation child walk now evaluates scoped Flow generators and ordinary Behaviors in authored order. Scoped Flow generators are excluded from the top-level module height chain.
3. **Working Flow.** Each parent starts with an RDG clear-to-zero float4 field, or the native unit downhill Flow snapshot derived from its height when an owned consumer exists. That intrinsic seed is adopted, not added to itself. Canonical generated contributions keep weighted vector magnitude.
4. **Composition.** Add/Mix are sanitized from the serialized Noise V2 `NoiseFlowAdd`/`NoiseFlowMix` settings, and `AddNoiseFlowComposePass` now dispatches against the actual inherited field. A direct scoped mask controls that Flow contribution only. A missing mask binds a valid neutral RDG scalar texture.
5. **Surface Slope.** The MODE Slope input receives the immediate preceding working Height. If Push modified Height earlier, that modified texture is sampled. The module's own Noise height is exclusively its separate Height basis.
6. **Distort and Deform.** Unconnected post-generation Warp (Distort) and Deform validate and gather; they automatically trace the accumulated Flow through the existing generator-flow RK2 kernel. Distort remaps the complete generator bundle; Deform remaps Height only. Per-operation masks and strength still apply separately.
7. **Canonical tracing.** A new Flow shader permutation (stage 10) uses vector magnitude directly, without the legacy stage-8 direction normalization or applying canonical influence twice. The legacy published-flow tracer remains unchanged for existing callers.
8. **After Distort.** A separate tile-wrapped RDG transport dispatch re-samples accumulated vectors and validity through the Distort coordinates. Deform leaves the Flow field and companion IDs/coverage fixed.
9. **Independent Height switch.** Flow-only Noise still renders the native Height scratch UAV required by MainCS/Flow MODE, but does not contribute relief when Write Height is OFF. If scoped Write Height is ON, its signed Noise relief can additionally compose into the parent's Height.
10. **Inspector.** Functional MODE Slope and COMPOSITION Add/Mix controls are now exposed. New unconnected Warp/Deform show their Flow trace settings rather than an empty direction-source picker.

## Preserved / deferred

- The finalized P0 native-height and Noise-specific normalization rules still apply.
- Existing named Noise Value, Gradient, FlowDirection, GeneratedFlow publications and source-reference algorithms are retained.
- The legacy Behavior `FlowField` and explicit source selector implementations remain in code pending P3 structural removal. They are no longer offered as the default Flow creation route.
- P5 arrow viewport visualization, broad P3 clipboard/group/instance cleanup, old socket deletion, and older Behavior menu cleanup are **not** claimed here.
- Scoped Flow's authored-order GPU integration is new and requires a real Unreal runtime smoke test; a source audit cannot establish visual or numerical equivalence.

## Validation evidence

Completed: source review and GitHub round-trip of changed files; checks for unmatched braces/parentheses, edit-conflict markers, shader entry-point names, shader parameter assignments, exported C++ function declarations, and the field ownership/composition wiring. No pull-request-triggered GitHub Actions run was available for the current commit. CodeRabbit status alone is not an Unreal test.

**NOT RUN**: Unreal 5.8 UHT/C++ compilation, PCD3D_SM6 shader compilation, RDG runtime, sample render, flat-height/tile seam comparisons, numeric two-Flow Add/Mix identities, order-sensitive Push/Slope comparisons, group/instance/clipboard, undo/redo, save/load or performance profiling. These must be evaluated in the Windows UE5.8 project before this branch is considered validated or merged.

## Runtime acceptance cases

- Parent Rock Formation > Flow Curl > Flow Constant > Flow Slope > Distort > Deform.
- Mask=0 exact unchanged Flow; Add=Mix=0 identity; Mix=1/Strength=0 attenuates previous vector toward zero.
- Slope after/before Push uses its actual preceding signed height.
- Distort moves Height/RegionIDs/coverage consistently; Deform moves only Height.
- Flow after Distort uses transported vectors with magnitude retained.
- Flat surfaces produce valid zero movement; seam wrapping remains periodic.
- Flow only output writes no phantom height; standalone flow-layer creation is unavailable.

## Important implementation locations

- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.cpp/.h`
- `Source/MixtormatShaders/Private/Compositing/MixtormatNoiseRender.h`
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
- `Shaders/Private/MixtormatNoise.usf`
- `Shaders/Private/MixtormatGeneratorFlow.usf`
- `Source/MixtormatRuntime/Private/MixtormatChildScope.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/{MixtormatLayerActions.cpp,MixtormatLayerChildren.cpp,MixtormatLayerMenus.cpp}`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`

Status: **P2 implementation integrated in source; compilation and runtime acceptance NOT VERIFIED**.
