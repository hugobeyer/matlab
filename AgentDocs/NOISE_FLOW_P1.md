# Mixtormat — P1 Noise / Flow Generator Presets

Branch: `feature/behavior-system-v2`. Engine: Unreal Engine 5.8.
Implements P1 of `AgentDocs/FINAL_BEHAVIOR_PLAN.md` under the contract in
`AgentDocs/FIELD_CONTRACT_P0.md`. Noise and Flow are two creation presets of the one
Noise V2 generator; no second engine, no new generator type, no source/target routing.

**Status: partially complete by design.** The independent P1 functionality is
implemented; ordered working-Flow accumulation is P2 and its controls are hidden
rather than exposed inert. See section 8.

Validation status: static source review only. No UHT/C++ build, shader compile, GPU
runtime, save/load or visual validation has run. See section 9.

---

## 1. Files changed

| File | Change |
|---|---|
| `Source/MixtormatShaders/Private/Compositing/MixtormatNoiseRender.h` | `FMixtormatNoiseRenderData` carries `bWriteHeight`, `bWriteFlow` and the MODE weights/angle/strength. |
| `Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.cpp` | `ResolveNoiseRenderData` resolves the new settings (clamped, non-finite → default). New `FMixtormatNoiseGeneratedFlowCS` and `FMixtormatNoiseFlowComposeCS`. `AddNoiseFieldPass` leaves `OutHeight` unbound when Write Height is off. `AddNoisePasses` gates `Bundle->Height`, publishes intrinsic `FlowDirection` from the native field for height-less modules, and dispatches `AddNoiseGeneratedFlowPass` when Write Flow is on. New `AddNoiseGeneratedFlowPass` and `AddNoiseFlowComposePass`. |
| `Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.h` | Declarations for the two new passes. |
| `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` | Noise branch: height-less modules skip post Behaviors, normalization, height publication, module-height retention and the running-height combine; Value/Gradient/FlowDirection/GeneratedFlow previews and Region IDs still run. Previews include `GeneratedFlow`. |
| `Shaders/Private/MixtormatNoise.usf` | `GeneratedFlowCS` (weighted MODE, canonical layout) and `FlowComposeCS` (reusable Add/Mix). |
| `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp` | `GeneratorPublishesField` accepts `GeneratedFlow` (kind Flow) for Noise with Write Flow on. |
| `Source/MixtormatEditor/Private/Widgets/SMixtormat.h` | `EMixtormatChildCreation::NoiseFlow`; `AddGeneratorLayerCreation` declaration. |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp` | `NoiseFlow` creation defaults (authoring defaults + Flow preset overrides); preset-aware `GetLayerChildName` ("Flow" persists independently of toggles). |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp` | ADD GENERATOR menu gains "Flow"; entries are creation kinds. |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp` | `AddGeneratorLayer` delegates to the new `AddGeneratorLayerCreation`. |
| `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp` | Noise outputs list gains `GeneratedFlow` when Write Flow is on. |
| `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` | OUTPUT card: Height/Flow toggles; Scale/Bias/Normalize collapse when Height is off. New MODE card (Height/Curl/Constant weights, Angle, Strength) when Flow is on. Heading reads FLOW for the Flow preset. |
| `Config/MixtormatParameterAuthoring.json` | Authoring entries for the new Noise parameters. |
| `AgentDocs/FIELD_CONTRACT_P0.md` | Section 10 implementation clarifications. |

## 2. Preset implementation

- Both presets are `EMixtormatGeneratorType::Noise` + `FMixtormatNoise`. The preset is
  the serialized `NoisePreset` (P0); creation only sets defaults.
- **Noise** (`EMixtormatChildCreation::Noise`): unchanged struct defaults — Height ON,
  Flow OFF, Height weight 1.
- **Flow** (`EMixtormatChildCreation::NoiseFlow`): authoring defaults applied first
  (same as a Noise prototype), then Write Height OFF, Write Flow ON, Height weight 0,
  Curl weight 1. Strength 1, Add 1, Mix 0 (serialized defaults).
- Identity persists: `GetLayerChildName` reads `NoisePreset`, so enabling Height on a
  Flow (or Flow on a Noise) never renames it. The inspector heading follows the preset.

## 3. Output behavior

- **Write Height ON**: unchanged path — post Behaviors, Noise centring/gate,
  normalization, Height Scale/Bias, publication, module combine. Existing assets are
  bit-identical: with both flags at their serialized defaults the new branches are
  inert.
- **Write Height OFF**: `OutHeight` UAV unbound (no height written); `Bundle->Height`
  null, so the compositor skips post Behaviors (they modify relief; there is none),
  normalization, "Height" publication, module-height retention and the running-height
  combine. No phantom relief through Height Blend, no normalization side effects.
  Value/Gradient/IDs publications, previews and Region IDs still run. The intrinsic
  `FlowDirection` diagnostic is published from the native field
  (FIELD_CONTRACT_P0.md section 10.2).
- **Write Flow ON**: `AddNoiseGeneratedFlowPass` computes and publishes `GeneratedFlow`
  (canonical layout: RG = VectorXY with magnitude, B = 0, A = influence; separate R16F
  validity). Published inside `AddNoisePasses`, i.e. *before* post-generation
  Behaviors — the generated field does not repeat the intrinsic publication's
  late-publication defect (FINAL_BEHAVIOR_PLAN section 10).
- **Write Flow OFF**: no generated-flow pass, no dispatch. Gradient/FlowDirection
  publications are untouched.

## 4. GPU calculations (`GeneratedFlowCS`)

```
GeneratedFlow = HeightWeight  * Downhill(OwnNativeHeight)     // per-UV, magnitude kept
              + SlopeWeight   * Downhill(SlopeHeight)         // P2 binds the snapshot
              + CurlWeight    * MixtormatNoiseV2Curl(...)     // shared helper, reused
              + ConstantWeight * float2(cos A, -sin A);       // V-down texture frame
GeneratedFlow *= Strength;
```

- Downhill = negative wrapped central difference, per-UV (resolution-independent),
  magnitude preserved; flat/non-finite slopes contribute zero.
- Curl uses the module's own base period (round(Scale)) and distortion octave stack;
  destination tile UV, so it tiles. No new serialized parameters, no second solver.
- Zero weights are exact zeros; the result is never normalized; non-finite results
  collapse to zero with validity 0.
- One extra dispatch only when Write Flow is on; no additional noise evaluation (the
  Height basis reads the native height MainCS already produced).

## 5. Add/Mix semantics

`FlowComposeCS` implements the P0 formula exactly, component-wise over the canonical
float4, with `MixWeight = saturate(Mix * Mask)`, `AddWeight = Add * Mask`, and
validity = either operand valid. `AddNoiseFlowComposePass` is the reusable dispatch.
**It is not enqueued anywhere in P1** — there is no temporary `FlowIn = 0`
approximation; P2 connects it to the ordered working-field accumulation.

## 6. Actual implemented functionality

- Flow creation entry (ADD GENERATOR → Flow) with the specified defaults; preset
  identity persists across output-toggle changes.
- Write Height / Write Flow toggles affect rendering (height contribution and
  generated-flow publication), not just the UI.
- Weighted Height/Curl/Constant generation, Strength, canonical publication,
  previewable through the existing output-key mechanism ("Generated Flow").
- `GeneratedFlow` is a valid typed reference target for shelf/consumer validation.
- Reusable Add/Mix GPU operation, compiled and callable, awaiting its P2 input.

## 7. Deferred P2 dependencies (explicit)

1. **Working-Flow accumulation**: `GeneratedFlow` is published but nothing consumes
   it yet. Distort/Deform still read the legacy paths. Connecting composition to the
   working field is P2.
2. **Slope basis**: shader input contract implemented (`SlopeHeight` +
   `UseSlopeHeight`); the preceding working-height snapshot is a P2 input. The Slope
   Inspector control is hidden until then.
3. **Add/Mix controls**: hidden until the composition op is connected to a real
   `FlowIn` (P2). The serialized parameters exist and are respected by the op.
4. **Intrinsic FlowDirection publication order**: unchanged (after post Behaviors for
   Write Height ON). P2 moves intrinsic publication earlier per
   FINAL_BEHAVIOR_PLAN section 10; the generated field already publishes early.
5. **Scoped Flow creation**: not implemented. A Flow beneath another Generator
   requires the P2 working-field evaluation and the P3 hierarchy support; creating it
   now would produce an inert node, so the creation entry exists only at the layer
   level.
6. **Viewport arrows**: P5.

## 8. Known limitations

- A Behavior scoped to a height-less Noise module has no effect (there is no relief
  to modify). Accepted per the P0 contract; P2 revisits with working fields.
- The intrinsic `FlowDirection` of a height-less module reflects the native rather
  than normalized field (documented deviation; the module has no completed height).
- Sources-shelf Flow entries are not added in P1 (the shelf creation path is
  separate from the ADD GENERATOR menu).

## 9. Validation

Performed: static source review of every edited call site (gather store round-trip,
dispatch bindings, publication keys, preview plumbing, creation defaults, inspector
wiring, authoring JSON keys). The editor diagnostics tooling has no UE include paths,
so it cannot compile this project.

Requires local Unreal Engine 5.8 (not run):

- UHT/C++ compilation (new shader parameter structs, editor creation enum, inspector
  rows).
- Shader compilation of `GeneratedFlowCS` / `FlowComposeCS` (new entry points in
  `MixtormatNoise.usf`).
- GPU runtime: Write Height OFF contributes zero height; Write Flow publishes a
  tileable canonical field; existing Noise families/masks unchanged; save/load,
  undo, clipboard, instances, groups.
- Acceptance tests from the P1 brief, sections "Height weight" through "Tile seams".