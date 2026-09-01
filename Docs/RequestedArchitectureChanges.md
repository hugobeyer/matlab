# Material Lab — Requested Architecture and UI Changes

Status: Unified mixed children and Fill picker are implemented and built; editor verification is pending. Normal-detail layers remain unimplemented.

This document records the requested next changes discussed during Material Lab development.
It is a planning source of truth and must remain consistent with the protected-master and Fab rules.

## Non-negotiable constraints

- Keep all shipped assets, source PNGs, shaders, and generated content inside `Plugins/MaterialLab`.
- Never generate, clear, patch, rewire, or save the protected master graph from C++.
- Protected master: `/MaterialLab/Materials/M_MaterialLab_Substrate`.
- Continue using the GPU compositor for ordered editor-time composition.
- Keep saved `UMaterialLabMaterial` recipes as the nondestructive source of truth.
- Do not impose a shader-slot layer limit.

## Base/root layer mutability

- A new recipe may start with a required base/root layer to guarantee a valid composition.
- The root layer is not an immutable or protected material assignment.
- Users must be able to replace or swap its assigned surface like any other material layer.
- Its standard layer controls and inspector editing must remain available.
- Do not lock source replacement merely because the layer occupies index `0`.
- Structural rules for an empty stack are separate from editing the root layer's content.

## 1. Normal-only detail compositing

### Goal

Support ordered normal-only layers for building detailed surfaces without replacing Base Color or RAM.

Example wood stack:

```text
Base wood material
└─ Broad wood undulation normal
   └─ Fiber-direction normal
      └─ Fine pore normal
```

Each normal-detail layer changes only the accumulated normal output:

```text
BC   Preserve accumulated Base Color
N    Combine incoming normal with accumulated normal
RAM  Preserve accumulated Roughness, AO, Metallic, and F0
```

### Normal sources

A normal-detail layer must accept either source type:

1. Existing `UMaterialLabSurface`
   - Reuse only the surface `_N` texture.
   - Do not duplicate the texture.
   - Ignore the surface BC and RAM for this layer.

2. Standalone normal texture
   - Import and register a normal map without requiring BC or RAM.
   - Intended for reusable fibers, pores, waves, hammering, brushing, and similar details.

### Standalone normal source layout

```text
Plugins/MaterialLab/Content/Textures/Normals/Source/<Category>/*.png
```

Examples:

```text
Plugins/MaterialLab/Content/Textures/Normals/Source/Wood/TX_Normal_Wood_Fibers_01.png
Plugins/MaterialLab/Content/Textures/Normals/Source/Wood/TX_Normal_Wood_Pores_01.png
Plugins/MaterialLab/Content/Textures/Normals/Source/Metal/TX_Normal_Metal_Brushing_01.png
```

Importer requirements:

- Exclude `Textures/Normals` from BC/N/RAM surface discovery.
- Import standalone normals through a dedicated normal-library path.
- Import as linear normal textures using normal-map compression.
- Save generated normal assets automatically under the plugin.
- Reimport changed plugin-owned normal PNGs through Unreal's supported reimport API.

Suggested generated path:

```text
/MaterialLab/Normals/<Category>
```

### Normal composition behavior

- Use reoriented normal mapping or another correct tangent-space normal-combination method.
- Each layer can choose RNM Combine or masked Normal Override.
- Both modes use the layer's final opacity, mask stack, generated features, and effects.
- Never combine tangent normals with a plain RGB lerp.
- Normalize every composed result.
- Keep the existing neutral-normal fallback.
- Preserve optional normal Y flipping.
- Support unlimited ordered normal-detail layers through the existing sequential compositor model.

Normal-detail controls:

```text
Enabled
Source surface or standalone normal
Opacity / blend weight
Integer tiling
Normal intensity
Optional mask stack
```

Recommended implementation model:

- Add a Normal Detail effect subtype or explicit normal-only channel mode.
- Keep the base recipe layer as a complete material.
- Allow upper material/effect layers to use a surface as a normal-only source.

## 1A. RAMH height-aware layer blending

Status: Implemented and included in a successful UE 5.8 build; GPU visual verification pending.

### Source texture contract

Keep `_RAM` fully supported and add `_RAMH` as an explicit opt-in format:

```text
_RAM     R Roughness · G AO · B Metallic
_RAMH    R Roughness · G AO · B Metallic · A authored blend height
```

Rules:

- Never reinterpret legacy `_RAM.A` as height.
- The importer accepts either `_RAM` or `_RAMH` for a complete BC/N/packed set.
- If both packed maps exist for one set, `_RAMH` wins and import reports the ambiguity.
- `UMaterialLabSurface` records whether its packed texture contains authored height.
- Preview instances continue using scalar `ML_DielectricF0` with `ML_UsePackedF0 = 0`.
- Composited and baked `_RAM.A` remains spatial dielectric F0.
- Composed height remains separate from RAM.A and bakes to `T_<Name>_H`.
- The protected master may expose `ML_Height`, `ML_UseHeight`, and `ML_HeightAmount`.
- `ML_UseHeight = 0` disables displacement; `1` enables the authored displacement path.
- `ML_HeightAmount` defaults to `1` and scales height around the neutral `0.5` center.
- Plugin code binds these parameters but never modifies the protected master graph.
- Planned `_RAM` normal-derived height reconstruction is specified in `NormalDerivedHeightPlan.md`.
- Derived height is importer-owned, provenance-marked, and always superseded by authored `_RAMH`.

### Recipe data

Keep the existing height-source enum serialized for compatibility, but hide it from the
current inspector. New layers use `Layer Height`: RAMH alpha when authored, otherwise scalar
Layer Height.

Expose these per-Material/Fill controls:

```text
Height Blend Enabled       default false
Mask Strength              0–4; default 1
Threshold                  0–1
Softness                   positive transition width
Base Height Bias           signed
Blend Height Bias          signed
Layer Height               default 0.5; shown only when RAMH is unavailable
Contact AO Amount/Width
Border Lift/Width/Intensity
```

The existing serialized fields back these labels without duplicating recipe data.

Compatibility rules:

- Existing recipes keep height blending disabled and render unchanged.
- Legacy height-source enum values and reference fields remain serialized.
- New `Layer Height` uses `_RAMH.A` when authored, otherwise scalar Layer Height.
- The ordered combined child mask is the kernel's placement-mask input.
- A root/initial layer establishes accumulated height without competing with an empty stack.
- Normal-detail layers preserve accumulated height.
- Effect and Mask entries remain ordered children of Material or Fill layers.
- Peeling never becomes a standalone recipe layer.
- Preserve the legacy Effect layer enum/value.

### Height evaluation

Use the layer's existing integer tiling for packed RAMH height and evaluate:

```text
A = AccumulatedBaseHeight + BaseHeightBias
B = ActiveRAMHOrScalarHeight + BlendHeightBias
M = saturate(CombinedMask * MaskStrength)
S = max(Softness, 1e-6)
BlendMask = smoothstep(Threshold - S, Threshold + S, M + B - A)
CompositedHeight = lerp(A, B, BlendMask)
```

Opacity and existing generated-feature/effect coverage multiply `BlendMask` consistently.
The resulting weight drives Base Color, Normal, Roughness, AO, Metallic, F0, and Height.
Contact AO and Border Normal sample this same transition field with independent widths.
Final output height is normalized; composited and baked `RAM.A` remains dielectric F0.

### GPU implementation plan

1. Extend `UMaterialLabSurface` with packed-height metadata.
2. Extend `FMaterialLabLayer` with backward-compatible height settings.
3. Parse `_RAMH` before `_RAM` in `MaterialLabSurfaceImporter.cpp`.
4. Keep the existing packed texture pointer; do not duplicate RAM/RAMH storage.
5. Add two transient `PF_R16F` height targets to each RDG composition graph.
6. Pass previous height, output height, source mode, and controls to the composite shader.
7. Sample `_RAMH.A` only when surface metadata confirms authored height.
8. Reuse the already-composited ordered layer mask for mask fallback and influence.
9. Publish Height beside BC/N/RAM and bake it to dedicated `T_<Name>_H`.
10. Bind optional `ML_Height`, preview-only `ML_UseHeight`, and `ML_HeightAmount` without modifying the protected master graph.

Primary files:

```text
Source/MaterialLabRuntime/Public/MaterialLabSurface.h
Source/MaterialLabRuntime/Public/MaterialLabMaterial.h
Source/MaterialLabEditor/Private/Services/MaterialLabSurfaceImporter.cpp
Source/MaterialLabShaders/Public/MaterialLabGpuCompositor.h
Source/MaterialLabShaders/Private/MaterialLabGpuCompositor.cpp
Shaders/Private/MaterialLabComposite.usf
Source/MaterialLabEditor/Private/Widgets/SMaterialLab.h
Source/MaterialLabEditor/Private/Widgets/SMaterialLab.cpp
Source/MaterialLabEditor/Private/Tests/MaterialLabLayerPreviewTests.cpp
Docs/TextureSourceLayout.md
```

### UI plan

- Add a compact `HEIGHT BLENDING` inspector section for Material and Fill layers.
- Hide source-shaping controls while height blending is disabled.
- Show whether the selected surface supplies `RAMH` height.
- Keep mask-stack editing separate; expose one layer-level Mask Height Influence.
- Height settings are recipe data, unlike preview-only lighting choices.

### Verification plan

- Legacy `_RAM` import and old recipes remain pixel-compatible.
- `_RAMH` import marks height availability and preserves linear mask compression.
- Automatic source selection follows RAMH → combined mask → constant.
- Height bias, range, threshold, contrast, offset, inversion, and amount affect ordering.
- Ordered multi-mask output drives fallback height and Mask Height Influence.
- Normal-detail and disabled layers preserve accumulated height.
- Final baked `RAM.A` still contains F0 and no height data.
- Empty recipes retain the current neutral BC/N/RAM outputs.
- No code modifies or generates the protected master graph.

## 1B. Height-threshold contact AO and border lift

Status: Implemented and built successfully; Unreal shader/visual verification pending.

Material and Fill layers now expose opt-in height-detail controls:

```text
Contact AO
AO Width
Border Lift
Border Width
Border Normal
```

Behavior:

- Existing raw AO remains the authored input and is composed normally.
- Generated contact AO multiplies the composed AO into `RAM.G`; it is never added.
- Contact AO is derived from the incoming/accumulated height threshold and local gradient.
- Border Lift is signed: positive values raise a lip; negative values create an inset groove.
- It generates a tangent-space border normal from neighboring threshold samples.
- Border normals are combined with the composed normal using RNM.
- RAMH, combined masks, and constant height remain valid sources.
- A constant unmasked field has no spatial border normal; masks or varying height provide edges.
- Defaults are zero for AO amount and lift, preserving old recipes.
- No protected-master parameters or graph changes are required.

## 1C. Per-layer HSV color adjustment

Status: Implemented and built successfully; Unreal shader/visual verification pending.

Material and Fill layers expose:

```text
Hue Shift   -180° to 180°
Saturation  0 to 2; neutral 1
Value       0 to 2; neutral 1
```

The GPU compositor converts each layer's linear Base Color to HSV, applies the local
adjustment, converts back to linear RGB, and then performs normal layer blending. Defaults
are neutral, Normal Detail layers preserve Base Color, and the protected master is unchanged.

## 2. Multi-mask stacks

Status: Implemented through ordered `FMaterialLabLayerChild` Mask variants; editor verification pending.

### Goal

Replace the current single-mask-per-layer model with an ordered mask stack.

Implement this now rather than after more recipes ship, because changing from one mask field to an array later would require broader asset migration.

Implemented recipe data:

```text
FMaterialLabLayer
└─ TArray<FMaterialLabLayerChild> Children
   ├─ Type: Mask   + FMaterialLabMaskLayer
   └─ Type: Effect + FMaterialLabLayerEffect
```

The old `Masks` array is migration-only and is not a live source of truth.

Each `FMaterialLabMaskLayer` should contain:

```text
Enabled
Mask asset or direct texture
Blend mode
Weight
Invert
Integer tiling
Balance
Contrast
```

### Mask blend modes

Required modes:

```text
Replace
Add
Subtract
Multiply
Min
Max
```

Suggested operation contract:

```text
OperationResult = Operation(AccumulatedMask, ShapedIncomingMask)
AccumulatedMask = Lerp(AccumulatedMask, OperationResult, Weight)
AccumulatedMask = Saturate(AccumulatedMask)
```

Specific operations:

```text
Replace   Incoming
Add       Accumulated + Incoming
Subtract  Accumulated - Incoming
Multiply  Accumulated * Incoming
Min       Min(Accumulated, Incoming)
Max       Max(Accumulated, Incoming)
```

Rules:

- Clamp the mask result to `0–1` after every operation.
- Mask Balance uses `0–1`, remains neutral at `0.5`, and reaches near-solid endpoints.
- Balance must be able to drive even hard black/white masks toward the opposite extreme.
- The first mask should default to `Replace`.
- Preserve mask ordering in saved recipes.
- Masks remain reusable immutable library inputs.
- Editing mask-stack controls must not modify source mask assets.

### GPU implementation direction

Do not bind a fixed number of mask textures to one material-composition pass.

Preferred approach:

1. Evaluate each layer's ordered mask stack through GPU ping-pong mask targets.
2. Produce one combined scalar mask for that material or normal-detail layer.
3. Feed that combined mask into the existing BC/N/RAM composition pass.
4. Reuse only two temporary mask targets regardless of mask count.
5. Recompute only when recipe or mask parameters change.

This keeps mask count independent of shader texture slots and follows the existing compositor architecture.

## 3. Unified layer-child hierarchy and row cleanup

Status: Implemented and built successfully; Unreal UI/GPU verification pending.

The current layer row duplicates mask-add actions, uses arrows for Effect ordering, exposes a
large parent mask button, and gives Effect and Mask children unrelated layouts. Replace this
with one compact hierarchy.

Target layout:

```text
Fill Layer 2                                      [chevron]
├─ Effect · Peeling                              […]
├─ Mask · Concrete Cracks       Multiply · 0.75 […]
├─ Mask · Grunge                 Add · 0.40      […]
└─ [+ Add Child]
```

Parent-row requirements:

- Keep one drag grip, enabled state, material thumbnail, layer name, summary, and disclosure chevron.
- Use canonical chevron-right/chevron-down icons only for foldout state.
- Remove the large parent-row mask thumbnail/add button.
- When collapsed, show a subtle child summary such as `1 effect · 2 masks` or tiny read-only thumbnails.
- Do not expose Add Mask in the parent row, expanded footer, inspector, and context menu simultaneously.

Child-row requirements:

- Effect and Mask children use the same row height, padding, selection treatment, and action placement.
- Draw a subtle theme-aware hierarchy rail and `L` branch connector at the left of every child.
- Row structure: drag grip · enabled · thumbnail/type icon · name · compact summary · overflow `…`.
- Remove Effect up/down arrows; drag is the only visible reordering interaction.
- Move remove, replace, and uncommon actions into the overflow menu.
- Keep mask mode/weight as a compact read-only summary; edit full values in the inspector.
- Peeling uses a canonical effect icon; masks use their square asset thumbnails.
- Keep delete actions available but avoid permanent trash buttons on every child row.

Add-child requirements:

- Provide exactly one `Add Child` action at the end of the expanded hierarchy.
- Its menu contains `Mask` and `Effect → Peeling`.
- Library mask drag/drop onto a layer still appends a Mask child.
- Layer RMB may retain advanced layer actions, but must not duplicate the primary Add Child workflow.

### Mixed Effect/Mask ordering

Dragging an Effect between Masks must represent real recipe order, not cosmetic order.
The current `Masks` and `Effects` arrays and GPU passes are separate, so cross-type ordering
cannot be added only in Slate.

Implemented migration:

```text
FMaterialLabLayer
└─ TArray<FMaterialLabLayerChild> Children
   ├─ Type: Mask   + FMaterialLabMaskLayer
   └─ Type: Effect + FMaterialLabLayerEffect
```

- Keep legacy `Masks` and `Effects` serialized only for backward loading.
- When `Children` is empty, migrate existing Masks first and Effects second to preserve current GPU behavior.
- After migration, `Children` is the single source of truth; do not maintain duplicate live arrays.
- Preserve the legacy standalone Effect layer enum/value for old recipes.
- Peeling remains a child of Material or Fill and never becomes a standalone new recipe layer.
- Define and test mixed evaluation before enabling cross-type drag.

Recommended mixed evaluation contract:

1. A Mask child updates the current scalar mask accumulator.
2. An Effect child consumes the current mask state and modifies owning-layer coverage/normals.
3. Later Mask children affect later Effects and final layer coverage, not already-evaluated Effects.
4. Existing recipes migrate to Masks-before-Effects and remain visually compatible.
5. Use reusable mask/effect ping-pong targets; do not impose a child-count shader-slot limit.

## 4. Bottom library layout

Replace the single mixed bottom area with two clear columns:

```text
┌──────────────────────────────┬──────────────────────┐
│ Materials                    │ Masks                │
│ Surface card grid            │ Square mask grid     │
└──────────────────────────────┴──────────────────────┘
```

### Materials column

- Display library surfaces.
- Drag a material card onto the layer stack to add a material/effect layer.
- Keep material thumbnails and search/category filtering.

### Masks column

- Display square thumbnail-only mask icons.
- Keep names in tooltips rather than under every icon.
- Drag a mask onto a specific layer row to add it to that layer's mask stack.
- Clicking a mask may assign it to the selected layer.
- Include a compact clear/remove action where appropriate.

### Drag-and-drop behavior

- Create dedicated drag operations for material cards and mask cards.
- Show small translucent drag thumbnails.
- Highlight only valid drop targets.
- A mask drop target must identify the destination layer clearly.
- Dropping a mask adds a mask sublayer instead of silently replacing an existing mask.
- Layer reordering remains separate from material/mask assignment.

## 5. Inspector layout

The previous Properties, Parameters, and Textures tabs have been consolidated.

Target inspector structure:

```text
LAYER SETTINGS
Selected layer identity and map status
Material / effect / normal-detail controls
Composition and opacity controls
Generated normal-feature controls

MASK
Mask assignment
Ordered mask-stack controls
Selected mask parameters
```

Rules:

- Show one inspector for the selected layer.
- Show material properties and parameters first.
- Show mask controls below material controls.
- Show detailed mask tuning only when a mask child is selected.
- Mask child rows are selectable in the left layer hierarchy.
- Keep Layer Settings and the selected Mask Inspector as separate right-column sections.
- Standalone texture availability remains read-only information, not a separate editing tab.

### Fill-layer color editing

Status: Implemented and built successfully; interactive editor verification pending.

- Replace the non-interactive Base Color swatch/RGB dragger workflow with Unreal's native color picker.
- Clicking the Fill Base Color swatch opens `OpenColorPicker` / `SColorPicker`.
- The swatch shows the current `FLinearColor` and remains compact in the inspector row.
- Update the Fill layer and live preview interactively while dragging in the picker.
- Cancel restores the color present when the picker opened.
- Hide alpha unless Fill opacity is intentionally routed through the color value; layer Opacity remains separate.
- Keep optional numeric RGB/HSV/hex entry inside the picker rather than permanent inspector draggers.
- Store recipe color as linear `FLinearColor`; picker presentation may use sRGB display conversion.
- This changes recipe UI/data values only and must not edit the protected master graph.

## 6. Slider and interaction design

Replace thick or awkward numeric-only interactions with compact Unreal-style scrub controls.

Desired control row:

```text
Label | Scrubbable slider area | Exact numeric value
```

Requirements:

- Drag horizontally for fast adjustment.
- Allow precise typed values.
- Preserve existing min/max ranges.
- Coalesce preview invalidations to one compositor request per Slate tick.
- Integer tiling controls must remain integer-stepped.
- Avoid unusually tall controls or large empty hit regions.

## 7. Tab and visual styling

The current button-like tabs should be replaced with proper tab visuals.

Tab requirements:

- Clear selected state.
- Subtle active underline or active background.
- Distinct hover state.
- Compact height consistent with Unreal editor panels.
- Do not make tabs look like unrelated action buttons.

### Color palette

Follow Unreal's editor theme instead of using a strongly custom blue interface.

Use:

- `FAppStyle` brushes and colors where possible.
- Neutral charcoal and gray panel backgrounds.
- Unreal-style borders and separators.
- Subtle blue accent only for selected, focused, active, or valid-drop states.
- Existing foreground and subdued-foreground colors for text.
- Theme-aware styling rather than hardcoded bright colors.

## Viewport overlay and HDRI lighting

- Remove the external `LIVE PREVIEW` header row.
- Overlay a compact mesh icon group at the viewport's top-right.
- Overlay a compact lighting icon group at the viewport's bottom-right.
- Keep Sphere, Plane, and Cube in the top group.
- Keep Neutral, Soft, Dramatic, Rim, and HDRI choices in the bottom group.
- Discover `UTextureCube` HDRIs recursively under `/MaterialLab/Lighting`.
- Use Unreal-generated square asset thumbnails for HDRI buttons.
- Use canonical Lucide `16×16` icons and roughly `24×24` hit areas.
- Keep both group backgrounds subtle, translucent, and theme-aware.
- Keep HDRI backgrounds hidden by default; use them for lighting and reflections.
- Orbit the camera with LMB drag.
- Rotate the selected HDRI or regular preset key light with RMB drag.
- Preserve wheel zoom, floor, fog, and compositor behavior.
- Provide preview-only Low, Medium, and High quality controls at the viewport top-left.
- Default Medium keeps stable direct shadows, AO, and SSR while disabling Lumen.
- High enables Lumen; hardware ray tracing remains project-controlled.
- Keep all lighting choices preview-only; never alter recipes or baked outputs.
- Use fixed lookdev exposure so material brightness does not adapt between meshes or presets.
- Disable preview bloom and keep tone mapping enabled for stable highlight rolloff.
- Calibrate key, sky, and HDRI intensity to avoid clipping light materials.

## 8. Current completion state

Implemented in source:

- Normal-detail recipe data, standalone normal importer, and normal-only GPU composition.
- Ordered multi-mask migration and reusable GPU mask composition passes.
- Mask drag/drop and the two-column Materials/Masks library.
- Unified Effect/Mask child hierarchy, foldouts, connectors, overflow, and mixed ordering.
- Native Fill color picker with interactive preview and cancel restoration.
- Integer-stepped and spin-scrubbable numeric controls.
- Theme-aware tabs, hierarchy visuals, viewport overlays, and preview quality controls.
- Runtime SDF-generated Peeling normals and AO; BN remains compatibility-only.
- Per-layer threshold Contact AO and Border Lift normal generation.
- Per-layer GPU HSV adjustment for Material and Fill Base Color.

The hierarchy/color-picker request has no remaining implementation step before the new
height-detail work. The broader request still requires:

- Unreal global-shader compilation and visual verification.
- Save/reopen migration, reimport, preview, and bake-output validation.
- Final visual review of compact scrub interactions and theme styling.
- Shipping-quality existing-texture reimport/save error handling.

## 9. Current validation order

```text
1. Build MatLabEditor Win64 Development using Handoff/Docs/MaterialLab/Build.md.
2. Restart Unreal and verify PCD3D_SM6 global shaders.
3. Verify child ordering, migration, Fill picker, Peeling SDF normal/AO, and height details.
4. Verify recipe save/reopen, reimport, preview, and bake outputs.
5. Run Material Lab automation tests after explicit permission.
6. Complete packaging/Fab validation after editor behavior is approved.
```
