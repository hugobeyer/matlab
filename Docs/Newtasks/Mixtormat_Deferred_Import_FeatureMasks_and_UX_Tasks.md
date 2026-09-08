# Mixtormat — Deferred Import, Aspect, Feature Masks, and Library UX Tasks

Status: **Deferred**
Next active feature: **Worn Edges**

This document collects the recent features that should be implemented after Worn Edges. They should not interrupt the current Pattern/Worn Edges work.

---

## 1. User Mask Import

### Goal

Allow users to import their own PNG masks into the Mixtormat mask library.

### Requirements

- Add an **Import Mask** action to the mask library.
- Support single-file and multi-file PNG import.
- User content must be stored in the project, not in the plugin content:
  - `/Game/Mixtormat/Masks`
  - `/Game/Mixtormat/Textures`
- Do not overwrite or mix user content with shipped `/MaterialLab/...` content.
- Configure imported masks as data:
  - sRGB off
  - mask-appropriate compression
- Maximum imported working dimension: **4096**.
- Supported normalized working sizes should include:
  - 512
  - 1024
  - 2048
  - 4096
- Default import working size: **2048**.
- Sources above the selected maximum should be resampled down.
- Preserve the source aspect ratio unless the user explicitly chooses otherwise.

### Rectangular masks

Rectangular masks are valid.

Examples:

- 2048×1024
- 1024×2048
- 4096×2048
- 2048×4096

Provide explicit placement/resampling choices when normalization is required:

- Preserve / Fit
- Fill / Crop
- Stretch

Never silently distort aspect ratio.

### Channel selection

RGBA masks should support:

- Luminance
- R
- G
- B
- A

---

## 2. Material Import Wizard

### Goal

Import arbitrary user PBR texture sets and normalize them into Mixtormat's canonical surface format:

- Base Color
- Normal
- RAMH

### Canonical rule

All maps belonging to one imported Mixtormat surface must end up with the **same exact width and height**.

They do not need to be square.

Valid examples:

- 2048×2048
- 2048×1024
- 1024×2048
- 4096×2048
- 2048×4096

Maximum dimension for now: **4096**.

### Wizard layout

The wizard should expose:

- Material name
- Source files
- Detected dimensions
- Detected aspect ratio
- Target maximum dimension
- Channel routing
- Normal convention
- Warnings
- Final output dimensions
- Preview / validation
- Import

### Supported source inputs

Allow both:

- files from disk
- existing Unreal `UTexture2D` assets

### Normal handling

Support:

- DirectX
- OpenGL
- Flip Green / Y

---

## 3. Generalized Channel Router / RAMH Packer

### Goal

Do not assume one source file equals one PBR map.

Users must be able to route arbitrary texture channels into Mixtormat outputs.

Example:

```text
Stone_BaseColor.png
    RGB -> Base Color
    A   -> AO

Stone_Normal.png
    RGB -> Normal

Stone_MRA.png
    R -> Metallic
    G -> Roughness
    B -> Height
```

Result:

```text
BC       = Stone_BaseColor.RGB
Normal   = Stone_Normal.RGB

RAMH.R   = Stone_MRA.G
RAMH.G   = Stone_BaseColor.A
RAMH.B   = Stone_MRA.R
RAMH.A   = Stone_MRA.B
```

### Source options per channel

Each routed scalar output should allow:

- Texture.R
- Texture.G
- Texture.B
- Texture.A
- Texture.Luminance
- Constant 0
- Constant 0.5
- Constant 1
- Custom constant

Useful lightweight operations:

- Invert
- Range / Levels

Do not turn this into a full image editor.

### Common presets

Presets should only pre-fill the routing table. The routing table remains authoritative.

Support common conventions:

- Separate Maps
- ORM
- ARM
- RMA
- MRA
- RAM
- RAMH
- BC Alpha = AO
- BC Alpha = Height
- Normal Alpha = Height

Also support glossiness by routing it to Roughness with `Invert`.

### Color-space correctness

Channel extraction must preserve the correct interpretation:

- Base Color RGB is color/sRGB.
- Roughness, AO, Metallic, Height are linear data.
- A data channel packed in Base Color alpha must still be treated as linear when written into RAMH.

---

## 4. Batch Folder Import

### Goal

Make folder import the fast primary workflow.

### Flow

```text
Pick Folder
    ↓
Scan recursively
    ↓
Group files into candidate materials
    ↓
Detect suffix / packing conventions
    ↓
Show review table
    ↓
Fix ambiguous entries if needed
    ↓
Import all selected
```

### Auto grouping

Support both:

#### Basename grouping

```text
Rock01_BaseColor
Rock01_Normal
Rock01_ORM
```

becomes one material.

#### Folder-per-material grouping

```text
/Brick/
    BaseColor.png
    Normal.png
    ORM.png
```

Folder name may become the material name.

### Common filename detection

Support common suffix families such as:

```text
Base Color:
_BC
_BaseColor
_Base_Color
_Albedo
_Diffuse

Normal:
_N
_Normal
_NormalDX
_NormalGL

Roughness:
_R
_Rough
_Roughness

AO:
_AO
_AmbientOcclusion

Metallic:
_M
_Metal
_Metallic

Height:
_H
_Height
_Disp
_Displacement

Packed:
_ORM
_ARM
_RMA
_MRA
_RAM
_RAMH
```

### Review table

Each candidate should show status such as:

- Auto detected
- Missing optional map
- Ambiguous packed map
- Source resolutions differ
- Aspect mismatch
- Source exceeds 4096

Auto-detection suggests routing; it must not silently commit ambiguous interpretations.

---

## 5. Resolution and Aspect Normalization

### Material rule

All canonical outputs for one surface must have identical dimensions:

```text
BC
Normal
RAMH
```

Example:

```text
Source:
BC    8192×4096
N     4096×2048
ORM   2048×1024

Target max dimension:
2048

Output:
BC    2048×1024
N     2048×1024
RAMH  2048×1024
```

Smaller maps may be resampled upward to the common working size. This does not create detail, but it keeps all channels spatially aligned.

### Aspect mismatch

Different source resolutions are acceptable when their aspect ratio matches.

Different aspect ratios should produce an explicit warning.

Do not silently stretch PBR maps.

Offer:

- Cancel / Fix Source
- Fit
- Fill / Crop
- Stretch

Default for material-map aspect mismatch should be **Cancel / Fix Source**.

---

## 6. Intrinsic Surface Aspect

### Goal

Rectangular materials should not be stretched into a square UV domain.

Each imported surface should retain its intrinsic width/height aspect.

Example:

```text
Surface = 2:1
```

Its sampling should compensate for that aspect automatically.

Conceptually:

```text
Canvas UV
    ↓
Source Aspect Compensation
    ↓
Layer Tiling
    ↓
Layer Rotation / Flip / Offset
    ↓
Sample BC / N / RAMH
```

A 2:1 source should remain visually 2:1 when tiled.

Do not bake the aspect correction destructively into the texture.

### Local 90° rotation

When a rectangular surface is rotated 90°:

```text
2:1 -> 1:2
```

The intrinsic aspect compensation must swap automatically.

The user should not need to manually repair X/Y scale after a quarter turn.

---

## 7. Global Canvas Rotation

### Goal

Provide a root-level global quarter-turn rotation for the entire Mixtormat composition.

Values:

- 0°
- 90°
- 180°
- 270°

This should be a **material/document/canvas** property, not technically a substrate property, even if the UI places it near substrate controls.

### Evaluation order

```text
Pixel UV
    ↓
Global Canvas Rotation
    ↓
Layers
Masks
Patterns
IDs
Effects
```

It must rotate the whole authored material consistently.

### Rectangular canvas behavior

For a rectangular composition:

```text
4096×2048 at 0°/180°
2048×4096 at 90°/270°
```

Do not rotate 90° and then squash back into the original dimensions.

### Normals

Tangent-space normal XY must rotate with the canvas transform.

---

## 8. Feature-Local Masks

### Current problem

`MASK BLENDING` is currently the inspector for an actual sibling `Mask` child.

It is not a mask belonging to the selected feature/effect.

When Pattern IDs or an effect is selected, the panel can appear even though no feature-local mask exists. Clicking or dragging a mask currently adds/selects a layer-level mask instead.

### Goal

Allow effects/features to own their own local mask chain.

Example:

```text
Material Layer
├─ Pattern IDs
│   └─ Mask · Grunge 03
├─ Worn Edges
│   ├─ Mask · Edge Noise
│   └─ Mask · Painted Areas
├─ HSV From IDs
└─ Layer Mask
```

### Scope

Implement **one level of nesting only**.

Do not introduce arbitrary recursive node trees.

A child feature may own local mask entries, but masks do not themselves own children.

### Reuse existing mask controls

Feature-local masks should reuse:

- Blend Mode
- Weight
- Balance
- Contrast
- Offset
- Invert
- Tiling X/Y
- UV Offset X/Y
- Flip U/V
- Quarter-turn Rotation

### Evaluation

Conceptually:

```text
FeatureInput
    ↓
Feature Solve
    ↓
Feature Local Mask
    ↓
Blend FeatureInput ↔ FeatureOutput
```

Example for Worn Edges:

```text
WornHeight = Solver(InputHeight)
FinalHeight = lerp(InputHeight, WornHeight, FeatureMask)
```

For Pattern IDs, initially mask the pattern's **contribution** rather than destroying/stomping the stable Region ID producer outside the mask. Preserve downstream ID behavior unless deliberately redesigned.

### Current misleading inspector

Until feature-local masks are implemented:

- `MASK BLENDING` should only be visible when a real `FMixtormatMaskLayer` child is selected.
- It should not appear as a fake section above Pattern IDs/effects.

---

## 9. Context-Sensitive Mask Assignment

### Goal

Clicking a mask in the mask library should use the current selection context.

Expected behavior:

```text
Layer selected
→ add mask to layer

Pattern IDs selected
→ add local mask to Pattern IDs

Worn Edges selected
→ add local mask to Worn Edges

Other supported effect/feature selected
→ add local mask to selected feature
```

If a selected feature cannot accept local masks, fall back only if the UX explicitly communicates that behavior. Do not silently attach to the layer.

---

## 10. Drag-and-Drop Masks onto Features

### Current behavior

Mask dragging currently targets the layer row and calls the layer-level mask assignment path.

### Required behavior

```text
Drop mask on layer row
→ append layer mask

Drop mask on supported feature/effect row
→ append local mask to that feature
```

Feature rows must accept mask drag operations.

During hover:

- highlight the actual destination feature
- show a destination-specific tooltip, e.g.:
  - `Mask Pattern IDs`
  - `Mask Worn Edges`

Do not show the generic layer-level `Release to append this mask` tooltip when the target is a feature.

Drag and click assignment must produce the same ownership semantics.

---

## 11. Mask / Texture Gallery Zoom

### Goal

The mask/texture gallery should have the same zoom interaction as the material gallery.

Requirements:

- same interaction model
- same min/max tile-size behavior where appropriate
- same zoom step token unless a concrete reason exists to separate it
- replacement-mask gallery and main mask library should remain visually consistent

Do not invent a second unrelated zoom system.

---

## 12. Shared Gallery Gap Tokens

### Goal

Material and mask/texture galleries should use the same style token for tile spacing.

The current visual gap should not be hard-coded independently in multiple galleries.

Use the existing material-gallery gap token as the shared source of truth, or rename/generalize it if needed:

```text
GalleryTileGap
```

Then use it consistently for:

- material library
- mask library
- mask replacement picker
- material replacement picker
- future import/wizard thumbnail grids where appropriate

Keep the token in the Mixtormat design/style token system.

---

## Recommended Implementation Order After Worn Edges

### D1 — Inspector cleanup

- Hide misleading `MASK BLENDING` unless an actual mask child is selected.

### D2 — Feature-local mask data model

- One-level local mask ownership.
- Serialization.
- No compositor behavior changes beyond pass-through yet.

### D3 — Feature mask compositor support

- Evaluate local mask chains.
- Apply to feature output.
- Preserve Region ID contracts.

### D4 — Context-sensitive click + drag/drop

- Library click targets selected feature or layer.
- Feature rows accept mask drag.
- Destination-specific hover feedback.

### D5 — Gallery consistency

- Mask/texture gallery zoom.
- Shared tile-gap token.
- Replace pickers use the same spacing behavior.

### D6 — User Mask Import

- Project-owned imported masks.
- Max dimension 4096.
- Aspect-aware normalization.

### D7 — Material Import Wizard + Channel Router

- BC / N / arbitrary packed inputs.
- RAMH routing and packing.
- Same-dimension canonical outputs.

### D8 — Batch Folder Import

- Recursive scan.
- Grouping.
- Convention detection.
- Review table.

### D9 — Rectangular surface/aspect support

- Intrinsic surface aspect.
- Aspect-compensated sampling.
- Quarter-turn-safe rectangular behavior.

### D10 — Global Canvas Rotation

- Root 0/90/180/270 rotation.
- Swap logical output dimensions for 90/270.
- Correct tangent-space normal rotation.

---

## Constraints

- Do not store user imports inside shipped plugin content.
- Do not allow imported working textures above 4096 on either axis for now.
- Do not silently distort material-map aspect ratios.
- Do not introduce arbitrary recursive child graphs.
- Do not regress existing layer masks, Pattern IDs, Region IDs, Drivers, Ramp/HSV/Random From IDs, or existing effect semantics.
- Keep UI styling token-driven.
- Preserve current serialized enum values; append rather than reorder where serialization depends on numeric values.
