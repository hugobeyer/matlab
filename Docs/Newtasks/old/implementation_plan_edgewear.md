# Implementation Plan: Edge Wear Filter (Step 1 — Slope Blur with Noise Types & Mask Blending)

Add a tile-safe **Edge Wear** post-composite filter based on Substance Designer-style **slope blur MIN height erosion** constrained to Region ID boundaries, featuring **selectable noise types** and an **optional mask blending mix**.

## User Review Required

> [!IMPORTANT]
> - `EMixtormatEffectType::EdgeWear` will be appended as value `5` to preserve binary asset serialization.
> - An enum `EMixtormatEdgeWearNoiseType` (`Perlin`, `Cellular`, `Curl`) is introduced to provide organic vs. faceted chip profiles.
> - An optional mask input (`UMixtormatMask` or `UTexture2D`) with `MaskStrength`, `bInvertMask`, and blend control is integrated so wear can be localized.
> - Normal regeneration reuses the existing Sobel pass so lighting reacts immediately to the carved silhouette.

---

## Proposed Changes

### MixtormatRuntime

#### [MODIFY] [MixtormatEffect.h](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatRuntime/Public/MixtormatEffect.h)
- Append `EdgeWear = 5` to `EMixtormatEffectType`.
- Map `EMixtormatEffectType::EdgeWear` to `EMixtormatEffectClass::Filter` in `MixtormatEffectClassOf()`.

#### [MODIFY] [MixtormatMaterial.h](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatRuntime/Public/MixtormatMaterial.h)
- Declare `EMixtormatEdgeWearNoiseType` enum:
  - `Perlin` (Smooth rock/stone erosion)
  - `Cellular` (Faceted, angular brick/tile chipping)
  - `Curl` (Directional / flow-distorted wear)
- Add Edge Wear properties to [`FMixtormatLayerEffect`](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatRuntime/Public/MixtormatMaterial.h#L299):
  - **Core**:
    - `EdgeWearAmount` (float, default 0.25f, range 0..1)
    - `EdgeWearWidth` (float, default 8.0f px, range 0.5..64)
    - `EdgeWearSamples` (int32, default 6, clamp 4..12)
    - `EdgeWearIdVariation` (float, default 0.5f, range 0..1)
    - `EdgeWearSeed` (int32, default 1)
  - **Noise**:
    - `EdgeWearNoiseType` (`EMixtormatEdgeWearNoiseType`, default `Perlin`)
    - `EdgeWearNoiseScale` (float, default 6.0f, range 1..64)
    - `EdgeWearNoiseStrength` (float, default 0.5f, range 0..2)
  - **Mask Blending**:
    - `EdgeWearMask` (`TSoftObjectPtr<UMixtormatMask>`)
    - `EdgeWearMaskTexture` (`TSoftObjectPtr<UTexture2D>`)
    - `EdgeWearMaskTiling` (int32, default 1)
    - `EdgeWearMaskStrength` (float, default 1.0f, range 0..1)
    - `bEdgeWearInvertMask` (bool, default false)
    - `EdgeWearMaskBlendMode` (`EMixtormatMaskBlendMode`, default `Multiply`)

---

### Shaders

#### [NEW] [MixtormatEdgeWear.usf](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Shaders/Private/MixtormatEdgeWear.usf)
- Input: Accumulated Height, Normal, RegionIds, Optional Mask Texture, LinearWrapSampler.
- Edge detection from Pattern edge data (or Cluster ID neighbor cross-check).
- Noise generation branching on `EdgeWearNoiseType` (Perlin gradient noise, 2D Voronoi cellular distance/hash, or 2D curl).
- Directional offset ray: perturbed per Region ID via `hash2(RegionId, Seed)` and scaled by noise strength.
- Ray marching: `Samples` taps along the noisy offset ray wrapping UV coordinates with `frac()` for seamless tiling.
- Evaluates `min(CurrentHeight, DisplacedHeight)` along the taps.
- Optional mask sampling and blend application (`Multiply`, `Min`, etc.).
- Blends between original height and eroded height based on edge band, mask, and `EdgeWearAmount`.
- Output: Modified Height buffer and optional Debug mask.

---

### MixtormatShaders

#### [MODIFY] [MixtormatGpuCompositor.cpp](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp)
- Declare `FMixtormatEdgeWearCS` global shader binding `/Plugin/MaterialLab/Private/MixtormatEdgeWear.usf`.
- In the post-layer filter loop, dispatch `FMixtormatEdgeWearCS` when an `EdgeWear` effect is active.
- Handle mask texture binding (fallback to 1x1 white dummy texture if unset).
- Run the Sobel pass over the eroded height to update the normal buffer.

---

### MixtormatEditor

#### [MODIFY] [SMixtormat_Inspector.cpp](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp)
- Add `BuildEdgeWearControls()`:
  - **Slope Blur**: Amount, Width, Samples, ID Variation, Seed.
  - **Noise**: Noise Type (Segmented control or dropdown: Perlin / Cellular / Curl), Scale, Strength.
  - **Mask**: Mask picker (Asset/Texture), Tiling, Strength slider, Invert toggle, Blend Mode dropdown.

#### [MODIFY] [SMixtormat_Layers.cpp](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp) & [MixtormatLayerBadges.cpp](file:///c:/Tools/MaterialLab/MatLab/Plugins/MaterialLab/Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp)
- Add "Edge Wear" option to the Add Effect dropdown menu and badge display.

---

## Verification Plan

### Automated / Build Tests
- Compile `MatLabEditor Win64 Development` via UnrealBuildTool.
- Verify SM6 shader compilation for `MixtormatEdgeWear.usf`.

### Manual Verification
- In the Editor UI, add an **Edge Wear** effect above a Pattern ID layer (e.g. bricks or tiles).
- Switch Noise Type between **Perlin** (soft rock wear) and **Cellular** (angular chipped corners) to confirm visual distinction.
- Assign an external grunge mask to `EdgeWearMask` and verify wear is localized only to the unmasked regions.
- Scrub `Amount`, `Width`, and `Samples` to verify real-time 60fps performance and seamless tiling across UV borders.
