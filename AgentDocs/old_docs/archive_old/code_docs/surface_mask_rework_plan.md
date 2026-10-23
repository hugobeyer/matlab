# Surface Mask rework — plan

Status: plan only, no code yet. Legacy materials are **not** preserved (decided 2026-09-28): the
Generated Mask is rewritten in place, fields are removed or renamed freely, and old recipes that
used it may load with defaults.

## 1. Audit — what is wrong today

Generated Mask = `FMixtormatGeneratedMask` (`Source/MixtormatRuntime/Public/MixtormatMaterial.h`),
shader `Shaders/Private/MixtormatGeneratedMask.usf`, pass `AddGeneratedMaskPass`
(`Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`), panel `BuildGeneratedMaskControls`
(`Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`, ~30 rows).

| Problem | Detail |
| --- | --- |
| Five signals in one node | Curvature, slope direction, AO, height, ridge — each with its own weight, mixed by a Normalize toggle. The layer's mask stack already mixes masks with blend modes; this is a second, worse mixer inside one node. |
| ~23 parameters | Weights (5) + per-signal params (8) + Normalize + Broadness/Smoothing + Bias + Warp (3) + Shaping (4) + Blend/Weight. |
| Double shaping | `Bias` (Bias01) then `Balance` (power) do nearly the same job, back to back. |
| Warp is not a signal | Flow warp (Amount/Source/Radius) lives inside the node. Its height gradient **clamps** at the tile edge instead of wrapping — seam bug. |
| Height is unusable | `saturate(Height + HeightBias)` on raw height: mostly flat 0 or 1 unless height happens to sit in 0..1. No range. |
| Overlap | Curvature signal duplicates much of the existing Curvature filter child. |
| Modifiers are texture-mask only | Blur and Curvature children (`EMixtormatLayerChildType::Blur/Curvature`) only work under a Texture Mask — they are folded into `FMaskRenderData` and applied in `AddMaskFilterPasses`. Generated, Craquelure, Color ID and Random ID masks cannot be blurred or filtered. |
| Shaping duplicated per producer | `FMixtormatMaskShaping` (Balance/Contrast/Offset/Invert) is embedded in 5 structs and applied inside each producer's kernel. |

## 2. Goals

- One mask node = one signal, 1–4 parameters visible.
- Modifiers (levels, warp, blur, curvature keep) are child nodes, stackable, usable under **any** mask producer.
- Masks combine through the stack's blend modes only.
- Every parameter stays drivable (child nodes are driver destinations; that is why modifiers are nodes, not fields).

## 3. Target design

### 3.1 Surface Mask (replaces Generated Mask)

Keeps `EMixtormatLayerChildType::Generated` and owner `EMixtormatParameterOwnerType::Generated`
(display names become "Surface Mask"); `FMixtormatGeneratedMask` is rewritten.

Common: `bEnabled`, `Source`, `BlendMode`, `Weight`.

| Source | Parameters | Signal |
| --- | --- | --- |
| Curvature | Cavity↔Convex (0..1), Radius (texels), Strength | curvature from the normal, as today's curvature term |
| Slope | Angle (deg), Spread | faces pointing toward a direction (dust, drips, lit side) |
| Occlusion | Strength | 1 − AO from the surface below |
| Height | Low, High | height remapped from [Low, High] — a real range, not a bias |
| Ridge | Strength | erosion crest/drainage output from below |

Removed from the node: per-signal weights, Normalize, Bias, Warp, embedded Shaping.
Signal output is the raw signal; the kernel blends into the chain only when the node has no modifiers
(fast path, same cost as today).

### 3.2 Mask modifiers (children under any mask producer)

Evaluated in authored order under their owner. Existing Blur and Curvature join this list.

| Modifier | Parameters | Notes |
| --- | --- | --- |
| **Levels** (new) | Contrast, Balance, Offset, Invert | the shared shaping, as a node; stack two for S-curves |
| **Warp** (new) | Amount, Source (normal ↔ height), Radius | flow warp of the mask, tile-wrapped (fixes the seam) |
| Blur (existing) | Radius X/Y | moves onto the generic pipeline |
| Curvature (existing) | Source, Mode, Kernel, Scale, Range, Invert, Weight | "keep where curvature in range"; moves onto the generic pipeline |

New child types `Levels`, `Warp` and owner types `Levels`, `Warp` are **appended** to their enums.

### 3.3 Compositor: produce → modify → composite

Today each producer blends into the previous mask inside its own kernel. New generic path, per mask
producer that has at least one enabled modifier:

1. **Produce:** the producer writes its raw signal to a scratch texture (blend Replace, weight 1).
2. **Modify:** each modifier runs in order, ping-ponging scratch textures. Blur keeps its two 1D passes.
3. **Composite:** one pass blends the result into the chain with the producer's BlendMode/Weight — the
   existing `MixtormatMask.usf` `UsePreShaped` path already does exactly this.

No modifiers → today's single in-kernel blend (no extra cost). Modifier gather lives in one helper
shared by all producers instead of the texture-mask-only loop in `MixtormatGpuCompositor.cpp`.

Producers covered: Texture Mask, Surface Mask, Craquelure, Color ID, Random ID.

### 3.4 Embedded shaping

Remove `FMixtormatMaskShaping` from the five producer structs; shaping is only ever a Levels child.
New nodes created from the add menu that used to need shaping get a Levels child by preset.

### 3.5 UI

- **Add menu:** Surface Mask ▸ *Edges, Cavities, Dust (up-facing), Peaks, Blank* — presets set Source +
  params and, where useful, add a Levels child.
- **Row context menu (any mask):** Add Modifier ▸ Levels / Warp / Blur / Curvature.
- **Inspector:** Surface Mask shows Blend chip, Source chip, then only that source's rows (≤3) + Weight.
  Levels: one 2×2 slider block. Warp: 3 rows.
- **Badges:** Surface Mask badge shows its source (`CURV`, `SLOPE`, `AO`, `HGT`, `RIDGE`); modifiers
  `LVL`, `WARP`. Badge click = blend menu (already wired for Generated).
- Tree connectors already show modifier nesting.

### 3.6 Clamping

Authored parameters pass through unclamped (project rule). The mask chain's own `saturate` on the
composited 0..1 coverage stays: that is the mask domain, not a parameter clamp. Levels' Offset can
push past it; that is the intended way to force full black/white.

## 4. Phases

| # | Work | Main files |
| --- | --- | --- |
| 1 | Surface Mask rewrite: struct, gather, shader (single source), inspector, badges, presets. Warp removed from the node. | `MixtormatMaterial.h`, `MixtormatGpuCompositor.cpp`, `MixtormatGpuMaskPasses.cpp`, `MixtormatGeneratedMask.usf`, `SMixtormat_Inspector.cpp`, `SMixtormat_Layers.cpp`, `MixtormatLayerBadges.cpp` |
| 2 | Generic modifier pipeline + **Levels** node. Surface Mask and Texture Mask first. | `MixtormatGpuMaskPasses.cpp`, `MixtormatGpuCompositorInternal.h`, new `MixtormatMaskModifiers.usf`, child/owner enums, binding owner mapping (`SMixtormat_Parameters.cpp`, `MixtormatParameterBinding.cpp`) |
| 3 | **Warp** modifier (tile-wrapped). Blur and Curvature moved onto the generic pipeline. | same + `MixtormatMaskBlur.usf`, `MixtormatMaskCurvature.usf` |
| 4 | Extend modifiers to Craquelure, Color ID, Random ID. Remove embedded shaping from all producers. | producer passes, structs, inspector panels |
| 5 | Cleanup: authoring defaults JSON, child capabilities/previews (per-modifier preview), docs, remove dead code. | `MixtormatParameterAuthoring.json`, `MixtormatChildCapabilities.cpp` |

Each phase leaves a working build; phase 1 alone already delivers the slimmer node.

## 5. Open decisions

1. One Surface Mask node with a Source switch (planned) vs. five node types.
2. Keep the existing Curvature **filter** alongside Surface Mask's Curvature **source**, or fold the
   filter into Levels-style "keep range" later.
3. Phase 4: remove embedded shaping everywhere (planned) or keep it on Texture Mask for quick edits.
4. Height source range: absolute height values, or normalized to the layer's height range.

## 6. Verification (Hugo)

- Build; `MixtormatGeneratedMask.usf` and the new modifier shader compile.
- Each Surface Mask source produces the expected mask; Blend/Weight combine with the stack.
- Presets add the right node (+ Levels).
- Levels/Warp/Blur/Curvature under Texture and Surface masks, stacked in order; disabling a modifier bypasses it.
- Warp tiles cleanly across seams.
- Drivers/links on Surface Mask, Levels and Warp parameters work.
- No-modifier masks cost the same as before (one pass).
