# Architecture Plan: Unified Layer & Module Composition Contract (V2)

> **Goal:** Eliminate the legacy GeneratorLayer distinction, establish explicit per-channel validity/ownership across all layers, and provide typed scalar source resolution for Color Ramps without visual regressions or data loss.

---

## 1. Core Principles & Architectural Contract

* **Layer = Unified Composition Container:**
  * Every layer has the exact same capabilities regardless of how it was created.
  * **Material vs. Fill:** Creation presets only, describing the initial default source (surface textures vs. uniform color/RAM), not execution capabilities.
  * Owns UV transformation (tiling, rotation, offset), placement masking, opacity, and stack blending.

* **Module = Procedural Producer or Modifier:**
  * Runs inside **any** layer's module stack (HasGeneratorModules() is already universal).
  * Modules produce or modify typed fields (Height, Color, Normal, Flow, RegionIds).

* **Explicit Channel Ownership & Validity (The 4-Channel Rule):**
  * Channels (BaseColor, Normal, RAM, Height, Occupancy) are written based on **explicit module production and authoring toggles**, never inferred from layer enum type.
  * Replaces legacy IsGenerator suppression with discrete per-channel contribution flags.

* **Zero Visual Regressions:**
  * Existing assets must look bit-identical after loading.
  * No silent overrides; no blind FillPassThrough flips.

---

## 2. The 5-Phase Implementation Roadmap

`mermaid
flowchart TD
    P1["Phase 1: Define Per-Channel Ownership & Validity Contracts"] --> P2["Phase 2: Compatibility Migration & Asset Safety"]
    P2 --> P3["Phase 3: Compositor & USF Shader Cleanup (Eliminate IsGenerator)"]
    P3 --> P4["Phase 4: UI, Menus & Capability Clean-Up"]
    P4 --> P5["Phase 5: Typed Scalar Resolution for Color Ramp"]
`

---

### Phase 1: Define Per-Channel Ownership & Validity Contracts
Establish precise rules for when each channel updates OutputBC, OutputN, OutputRAM, OutputHeight, and OutputOccupancy:

1. **Base Color:**
   * Contributes if: Surface has valid BC texture, or layer has explicit Fill enabled, or a module produced GeneratedColor with GeneratorAlbedo = true.
   * **Fix Fill Stomp:** OverrideBaseColor on Fill layers must not clobber GeneratedColor unless the user explicitly disabled module albedo.
2. **Normal:**
   * Contributes if: Surface has normal texture, or layer has height-derived normal enabled, or a module authors relief/normals.
   * Does **not** dump a flat (0,0,1) normal over the lower stack when a generator only authors height or color.
3. **RAM (Roughness, AO, Metallic):**
   * Contributes only if surface RAM is bound, explicit Fill overrides are active, or a module writes RAM.
4. **Height & Occupancy:**
   * **Fix Ghost Height:** AddGeneratorLayerPasses must track whether any module actually authored non-zero height (ActualHeightAuthored). Heightless modules (e.g. pure Flow) must **never** set GeneratedHeight = true.
   * Occupancy updates only when actual height/silhouette geometry is produced.

---

### Phase 2: Compatibility Migration & Asset Safety
Protect all existing projects before touching shader evaluation:

* **Preserve Serialized Enum:**
  * Layer types are `Material` and `Fill`; generator modules live inside `FMixtormatLayer::Children` (detected via `FMixtormatLayer::HasGeneratorModules()`).
  * Hide it from creation menus (UMETA(Hidden)).
* **Automated Load-Time Migration (MixtormatMaterial.cpp / PostLoad):**
  * Convert legacy generator layers to Fill or Material base containers with neutral defaults.
  * Explicitly copy existing flags: preserve GeneratorAlbedo, HeightBlend.Op, softness, and child hierarchies identically.
  * Do **not** automatically enable FillPassThrough (which alters how layers sample the substrate below).

---

### Phase 3: Compositor & USF Shader Cleanup (Eliminate IsGenerator)
Remove all type-dependent branches in C++ and shaders:

* **USF Shader Cleanup (MixtormatComposite.usf):**
  * Remove uint IsGenerator; parameter.
  * Replace line 1370:
    `hlsl
    // Legacy:
    OutputBC[Pixel] = IsGenerator != 0 && (GeneratorAlbedo == 0 || HasGeneratorColor == 0)
        ? PreviousBaseColor : float4(ResultBaseColor, 1.0f);
    `
    With discrete contribution flags:
    `hlsl
    OutputBC[Pixel] = ContributeBaseColor != 0 ? float4(ResultBaseColor, 1.0f) : PreviousBaseColor;
    OutputN[Pixel]  = ContributeNormal != 0    ? float4(EncodeNormal(ResultNormal), 1.0f) : PreviousNormalEncoded;
    OutputRAM[Pixel]= ContributeRAM != 0       ? float4(ResultRoughness, ResultAO, ResultMetallic, ResultF0) : PreviousPacked;
    `
* **Compositor Parameter Binding (MixtormatGpuCompositor.cpp):**
  * Bind ContributeBaseColor, ContributeNormal, ContributeRAM, and ContributeOccupancy using the Phase 1 validity rules.
  * Remove `Parameters->IsGenerator` and legacy generator layer type flags.

---

### Phase 4: UI, Menus & Capability Clean-Up
Make authoring intuitive and consistent across all layer rows:

* **Creation Menus (MixtormatLayerMenus.cpp):**
  * Remove "Add Generator Layer".
  * Ensure + Child -> Generator is available on all layers.
* **Inspector Harmonization (SMixtormat_Inspector.cpp):**
  * Expose generator and behavior controls consistently on any layer hosting modules.
  * Retain the explicit "Albedo" / "Height" channel contribution toggles.

---

### Phase 5: Typed Scalar Resolution for Color Ramp
Replace implicit/fragile scope guesses with an explicit, robust architecture:

* **Expand EMixtormatColorRampSource:**
  * Add typed choices: RunningHeight, ModuleHeight, FlowMagnitude (Speed), FlowAngle, NoiseValue, LayerInputHeight, CompositeBelow.
* **Scope Suggests Defaults, Never Overrides:**
  * When scoped under Flow, default the initial picker to FlowMagnitude.
  * **Never** forcibly lock Source to ModuleRef in gather code.
  * Never hide the Source dropdown in MixtormatInspectorGenerators.cpp.
* **Shader Evaluation (MixtormatGeneratorHeightColorRamp.usf):**
  * When FlowMagnitude or FlowAngle is selected, compute the scalar from Texture2D<float4> SourceFlow and feed the ramp transfer function.

---

## 3. Execution Verification Checklist

- [ ] Existing projects load with identical visual output (golden image comparisons).
- [ ] Fill layer with Generator + Color Ramp displays the ramp's albedo properly.
- [ ] Adding Flow does not generate false "phantom" height.
- [ ] Color Ramp allows picking between Height, Flow Speed/Angle, or Noise Value explicitly.
- [ ] Unreal Engine 5.8 Live Coding clean compilation.
