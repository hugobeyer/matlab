# Material Lab — Peeling Effect

Last updated: 2026-09-02

This document is the crash-safe implementation handoff for the first Material Lab effect.
It describes the authored-texture path, which is the only one implemented.

A second, procedural source for the same peeling shader is designed in
`ProceduralPeelingPlan.md`: generated noise plus surface signals (convexity, cavity, AO, height,
ordered child masks) replace the imported PDM/MSK/H/SDF set. That plan generates the maps and
leaves the peeling math below unchanged, so nothing in this document is superseded by it.

## Product rule

Peeling is an effect owned by a Material or Fill layer. It is not a standalone recipe layer.

```text
Accumulated lower material
        ↓
Upper Material or Fill layer
        ├─ Masks
        └─ Ordered Effects
             └─ Peeling
        ↓
Peeling removes parts of the upper layer and reveals the accumulated material below.
```

The upper layer still owns Base Color, Roughness, Metallic, IOR/F0, Normal, and AO. The
peeling effect only controls its coverage, edge profile, height transition, and normal detail.

Do not remove the legacy `EMaterialLabLayerType::Effect` value yet. Existing recipes may use
it. New peeling UI must add Peeling under `FMaterialLabLayer::Effects`, not create an Effect
Layer.

## Source maps

Live files currently exist at:

```text
Plugins/MaterialLab/Content/Textures/Effects/
├─ TX_Effect_Peeling_Standard_01_PDM.png
├─ TX_Effect_Peeling_Standard_01_MSK.png
├─ TX_Effect_Peeling_Standard_01_H.png
├─ TX_Effect_Peeling_Standard_01_SDF.png
└─ TX_Effect_Peeling_Standard_01_BN.png
```

All files are Raw/Linear and must never use sRGB.

```text
_PDM  RGB = normalized arrival distance, macro field, micro field
_MSK  RGB = peel coverage, edge/crest, secondary relief detail
_H    R   = normalized peel height
_SDF  R   = signed peel field encoded with the active boundary at 0.5
_BN   RGB = legacy compatibility data; not used by the runtime Peeling compositor
```

Recommended Unreal compression:

```text
PDM / MSK / H / SDF  TC_Masks, sRGB false, no generated mips
BN                    TC_Normalmap, sRGB false, WorldNormalMap LOD group, no generated mips
All effect textures    Never Stream
```

Every map in one effect set must have matching dimensions.

## Houdini source

Generator:

```text
Handoff/Houdini/OpenCl/peel.cl
```

The OpenCL generator now preserves the original `arrival`, `height`, and `mask` outputs and
adds:

```text
height01
sdf
pdm_r / pdm_g / pdm_b
mask_r / mask_g / mask_b
```

New controls:

```text
micro_warp
micro_morph
distance_range
sdf_range
height_range
```

`micro_morph = 0` gives macro-only peeling. `micro_morph = 1` adds the full micro field.

## Runtime schema — implemented and built

New files:

```text
Source/MaterialLabRuntime/Public/MaterialLabEffect.h
Source/MaterialLabRuntime/Private/MaterialLabEffect.cpp
```

`UMaterialLabEffect` currently stores:

```text
Identity
- DisplayName
- Category
- EffectType
- SourceTextureBaseName

Textures
- PeelData
- Mask
- Height
- SDF
- BentNormal

Decode metadata
- DistanceRange
- SDFRange
- HeightRange

Peeling defaults
- Front
- Width
- MacroWarp
- MicroWarp
- MicroMorph
- Thickness
- Lift
- DetailStrength
```

`FMaterialLabLayerEffect` is implemented in `MaterialLabMaterial.h` with an effect reference,
enabled state, strength, and local peeling controls.

Every `FMaterialLabLayer` now owns:

```text
TArray<FMaterialLabLayerEffect> Effects
```

This ordered array is the required layer sub-hierarchy.

## Importer — implemented and built

Changed files:

```text
Source/MaterialLabEditor/Private/Services/MaterialLabSurfaceImporter.h
Source/MaterialLabEditor/Private/Services/MaterialLabSurfaceImporter.cpp
Source/MaterialLabEditor/Private/Widgets/SMaterialLab.cpp
```

Implemented behavior:

- Recursively scans `Content/Textures/Effects` for PNG files.
- Groups matching `_PDM`, `_MSK`, `_H`, `_SDF`, and optional `_BN` files.
- Excludes `Effects` from normal BC/N/RAM surface discovery.
- Imports PDM/MSK/H/SDF as linear, full-resolution mask textures.
- Imports BN as a linear, full-resolution normal texture.
- Disables streaming and generated mips for all Peeling maps.
- Validates matching dimensions.
- Creates or updates a `UMaterialLabEffect` asset.
- Saves imported textures and effect assets automatically.
- Includes effect counts in the Reimport result and status message.

Generated destinations:

```text
/MaterialLab/Textures/Effects/Peeling/Raw
/MaterialLab/Effects/Peeling/MLFX_Peeling_Standard_01
```

Use **Reimport Shipped Library** after the source compiles.

## Effect registry — implemented and built

Changed files:

```text
Source/MaterialLabEditor/Private/Services/MaterialLabRegistry.h
Source/MaterialLabEditor/Private/Services/MaterialLabRegistry.cpp
```

Implemented:

- `FMaterialLabEffectEntry`.
- `FMaterialLabRegistry::GetEffects()` scanning `/MaterialLab/Effects` recursively.
- Effect thumbnails prefer the effect mask, then PDM.

Still required:

- Complete UI integration that consumes `GetEffects()`.
- Build and fix any compiler/UHT errors from Steps 1–3.

## Required layer UI

### Layer RMB context menu

Right-clicking a non-base layer must open a normal context menu, not the mask list.

Initial actions:

```text
Add Effect
└─ Peeling

Duplicate Layer
Delete Layer
```

Adding Peeling appends one `FMaterialLabLayerEffect` to that layer's `Effects` array and copies
the effect asset defaults into the local controls.

### Expanded layer hierarchy

```text
Layer
├─ Effect: Peeling
│  ├─ Enabled
│  ├─ Strength
│  ├─ Front
│  ├─ Width
│  ├─ Macro Warp
│  ├─ Micro Warp
│  ├─ Micro Morph
│  ├─ Thickness
│  ├─ Lift
│  └─ Detail Strength
└─ Masks
   ├─ Mask 01
   ├─ Mask 02
   └─ Add Mask
```

Effects and masks are children of the owning layer. They are not peer recipe layers.

### Mask thumbnail popover

- The mask thumbnail on the layer row opens the mask picker.
- The picker is a square thumbnail grid.
- Names belong in tooltips.
- Do not show masks as a vertical text list.
- Layer RMB must not open the mask picker.
- Dropping or selecting a mask appends it to the mask stack.

## GPU compositor — procedural SDF normal/AO update requires rebuild and verification

Changed files:

```text
Source/MaterialLabShaders/Private/MaterialLabGpuCompositor.cpp
Shaders/Private/MaterialLabComposite.usf
Shaders/Private/MaterialLabPeeling.usf
Source/MaterialLabEditor/Private/Tests/MaterialLabLayerPreviewTests.cpp
```

Implemented behavior:

- Evaluates enabled effects in their owning layer's stored order.
- Uses two reusable effect ping-pong targets, independent of effect count.
- Binds PDM, MSK, H, and SDF maps; BN remains serialized only for compatibility.
- Multiplies the owning layer alpha by the accumulated peeling keep amount.
- Shapes the edge with travel distance, SDF, mask channels, thickness, and lift.
- Derives tangent normals from the runtime SDF/height gradient on the GPU.
- Derives edge AO from SDF proximity and lift/thickness on the GPU.
- Applies procedural edge normals after the owning layer normal so Fill layers retain them.
- Adds a GPU test proving full peeling reveals the accumulated lower layer.

Peeling runs while preparing its owning upper layer's transition alpha. It is not an
independent BC/N/RAM layer pass.

Recommended decode:

```text
T = PDM.r × DistanceRange
M = PDM.g × 2 - 1
U = PDM.b × 2 - 1

D = T
  + M × MacroWarp
  + U × MicroWarp × MicroMorph
  - Front

X = D / max(Width, epsilon)
Coverage = 1 - Smooth01(X × 0.5 + 0.5)
Edge = exp(-2 × X²)
```

Layer behavior:

```text
PeelAmount = saturate(Coverage × EffectStrength)
UpperLayerAlpha *= 1 - PeelAmount
```

This reveals the accumulated material below without modifying either source material.

Height behavior:

- Decode `_H` with `HeightRange`.
- Use the peel field and height to shape the transition, not only a binary mask.
- Apply lift and thickness around the edge band.
- Keep the transition coherent across BC, Normal, Roughness, AO, Metallic, and F0.

Normal behavior:

- Derive normals at runtime from the SDF-shaped height gradient on the GPU.
- Thickness, Lift, and Detail Strength directly change that procedural gradient.
- Apply the procedural lip normal after the owning surface normal, including Fill layers.
- Derive AO from SDF edge proximity and the lift/thickness scale.
- Keep `_BN` serialized/importable for compatibility, but do not composite it.
- Combine tangent normals with RNM/reoriented normal mapping.
- Never blend encoded normal RGB directly.

Ordered effects:

- Evaluate effects in the order stored under the layer.
- First implementation only needs Peeling.
- Keep the data model ordered so later effects can compose without schema migration.

## Inspector — implemented in source, not built yet

Selecting a Peeling child now shows one compact Peeling panel:

```text
Enabled
Intensity / Strength
Bias / Front
Transition Width
Macro Warp
Micro Warp
Micro Morph
Thickness
Lift
Detail Strength
```

Use compact scrub controls with exact numeric entry. Preview invalidations remain coalesced to
one compositor request per Slate tick.

## Viewport UI corrections still required

- Remove the separate `LIVE PREVIEW` header bar.
- Put a compact mesh icon group inside the viewport at the top-right.
- Put regular lighting presets and HDRI choices inside the viewport at the bottom-right.
- Build both groups as overlays so they consume no viewport layout height.
- Use `16×16` icons with roughly `24×24` button hit areas.
- Keep the 3D viewport as the dominant visual area.
- Preserve RMB orbit, LMB lighting rotation, and wheel zoom.
- Keep the studio ground fade/fog behavior.

## Icon rules

- Use canonical Lucide SVG files downloaded from the official Lucide repository.
- Keep the canonical `24×24` SVG viewBox.
- Render compact row/action icons at `16×16`.
- Render major toolbar icons at `20×20`.
- Render compact viewport-overlay icons at `16×16`.
- Do not use letters such as `N`, `S`, `D`, and `R` as lighting icons.
- Do not use text glyphs such as `×`, `+`, or `≡` when an icon exists.
- Keep icons theme-aware through `FSlateVectorImageBrush` tinting.
- Ship the Lucide license with the SVG resources.
- Keep material and mask gallery tiles square.

## Remaining implementation sequence

```text
Step 3 — implemented and built
- Added the layer RMB context menu.
- Added Peeling to the owning layer's Effects array.
- Show Peeling as an expanded child row.
- Moved mask selection to the mask-thumbnail grid popover.

Step 4 — implemented and built; Unreal shader verification remains
- Added ordered peeling render data and reusable effect ping-pong targets.
- Bound PDM/MSK/H/SDF/BN textures.
- Evaluate peeling before the owning layer's composition alpha.
- Added dynamic height-derived normal handling.

Step 5 — implemented and built
- Added selected-effect inspector controls.
- Added selectable mask children with a separate per-mask inspector below Layer Settings.
- Local effect settings persist through recipe save/load and layer duplication.
- Added effect enable/remove/reorder behavior.
- Added per-layer RNM Combine and masked Normal Override modes.

Step 6 — canonical Lucide replacement built; viewport overlay pending
- Remove the LIVE PREVIEW bar.
- Move mesh controls to a top-right viewport overlay group.
- Move regular lighting and HDRI controls to a bottom-right viewport overlay group.
- Use the downloaded canonical Lucide icons at `16×16`.

Step 7
- Build `MatLabEditor Win64 Development`.
- Click Reimport Shipped Library.
- Verify generated textures and MLFX asset.
- Add Peeling under a Fill or Material layer.
- Verify macro/micro morph, height edge, reveal behavior, normals, save/load, and bake.
```

## Validation status

`MatLabEditor Win64 Development` builds successfully with the procedural SDF normal/AO
update. Restart Unreal so `FMaterialLabPeelingCS` recompiles, then verify the generated
normal and AO outputs visually.

The Unreal build does not validate `peel.cl`; validate the Houdini OpenCL node separately.
