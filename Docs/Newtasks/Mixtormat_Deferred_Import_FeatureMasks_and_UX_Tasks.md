# Mixtormat — Deferred Import, Aspect, and Remaining Library UX Tasks

Status: **Deferred backlog — partially implemented**

Implementation plan: [`Mixtormat_Deferred_Import_Aspect_and_UX_Implementation_Plan.md`](Mixtormat_Deferred_Import_Aspect_and_UX_Implementation_Plan.md)

This document is the requirements backlog for work that follows the current scoped-effect-mask and Worn Edges foundation. It is not an implementation handoff for work already completed.

## Current implementation state

| Area | State |
|---|---|
| Worn Edges W7 roughness and generated wear coverage | Implemented; tests added but not run |
| One-level scoped masks owned by Effect children | Implemented |
| Shared scoped-mask inspector and RMB Add Mask | Implemented |
| ID-driven mask UV offsets from Pattern/Cluster IDs | Remaining |
| Worn Edges per-ID noise-coordinate offset | Remaining |
| Persistent mask selection and direct RMB placement | Implemented |
| Dragging masks directly onto Effect rows | Remaining |
| Mask gallery wheel zoom | Implemented |
| One shared material/mask gallery gap token | Remaining |
| User mask/material import and channel routing | Deferred |
| Rectangular aspect and global canvas rotation | Deferred |

Pattern producers, controls, shaders, and output contracts remain out of scope.

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

## 8. ID-Driven Mask and Worn Noise Offsets

Status: **Required; not implemented**

This is consumer behavior. It does not make Pattern IDs or Cluster IDs mask owners and must not modify their producers.

### Mask ID offset

Every layer-scoped or effect-scoped texture mask should optionally consume an upstream Region ID source.

Inspector controls:

- `ID Offset Source`: None, or a compatible upstream Pattern IDs / Cluster IDs row.
- `ID Offset Amount`: strength of the deterministic two-dimensional UV phase shift.
- `ID Offset Seed`: changes the deterministic offset without changing Region IDs.

Store the selected source by stable `ChildId`, not row index or display name.

Evaluation:

```text
Canvas UV
→ existing mask tiling / flip / quarter-turn / UV offset
→ deterministic float2 offset from selected Region ID + seed
→ wrap
→ sample mask texture
→ existing shaping and mask blending
```

The offset must be constant inside one region and change between IDs. Amount `0` or no valid source must preserve current mask sampling exactly. Only already-evaluated upstream ID producers may appear in the dropdown.

### Worn Edges noise offset

Current Worn Edges behavior already consumes the nearest upstream Region IDs and varies radius, slope, strength, and relative noise-family weights per ID. It does **not** currently offset the noise coordinates per ID.

Add:

- `ID Source`: Auto / nearest upstream, or an explicit upstream Pattern IDs / Cluster IDs row.
- `Noise Offset`: deterministic per-ID phase-offset amount.
- Reuse the existing Worn Edges seed when hashing the two-dimensional offset.

Apply the offset to the complete Macro / Cellular / Ridge / Micro / Warp noise domain. Do not offset or alter Pattern `OutputEdge`, Region IDs, or edge localization.

Compatibility:

- Default `ID Source` keeps the current nearest-upstream behavior.
- Default `Noise Offset = 0` preserves current Worn Edges output.
- Missing explicit sources fall back safely to Auto during normalization.

## 9. Scoped Effect Masks

Status: **Implemented foundation; hardening remains**

The implemented model is intentionally narrower than the original feature-local proposal:

- Only `Effect` children can own scoped masks.
- Pattern IDs, Cluster IDs, and other discrete-data producers cannot own them.
- Ownership uses flat `FMixtormatLayer::Children` storage plus `ScopeOwnerChildId`.
- Only one ownership level is supported.
- Scoped masks remain complete `FMixtormatLayerChild` entries.
- Existing mask controls, drivers, references, copies, and instances are reused.
- Scoped masks affect only their owning effect.
- Later siblings retain the unchanged layer-level `CombinedMask`.
- Missing or unsupported owners fail safely to layer scope.

Example:

```text
Material Layer
├─ Worn Edges
│  ├─ Mask · Edge Noise
│  └─ Mask · Painted Areas
├─ HSV From IDs
└─ Layer Mask
```

`MASK BLENDING` now appears only when a real mask child is selected, whether that mask is layer-scoped or effect-scoped.

Remaining hardening:

- Add one deterministic ownership normalizer shared by load and edit paths.
- Add save/load ownership tests.
- Add optional owner disclosure/collapse UX.
- Add explicit same-layer mask reassignment between supported effect owners.

The detailed implemented architecture remains documented in [`Mixtormat_Scoped_Feature_Masks_Plan.md`](Mixtormat_Scoped_Feature_Masks_Plan.md).

---

## 10. Persistent Mask Selection and RMB Placement

Status: **Implemented**

Current behavior:

- Clicking the persistent bottom mask gallery selects and highlights a mask only.
- Gallery clicks never mutate the layer stack.
- Layer RMB shows `Add Mask · <selected mask>` and creates a layer-scoped mask.
- Effect RMB shows the same selected mask and creates a mask scoped to that effect.
- Mask-row RMB offers `Replace with <selected mask>` directly.
- These actions are disabled until a mask is selected.
- Add and replacement actions no longer open secondary mask gallery popovers.

Mask dragging remains available as a separate explicit gesture. Pattern IDs and other discrete-data producers remain unsupported as scoped-mask owners.

---

## 11. Drag-and-Drop Masks onto Effects

Status: **Deferred**

Current behavior:

- Layer-row drops append layer-scoped masks.
- Child-row drop targets accept child reorder/move operations only.
- Effect rows do not yet accept `FMixtormatMaskDragDropOp`.

Required behavior:

```text
Drop mask on layer row
→ append layer-scoped mask

Drop mask on supported Effect row
→ append mask scoped to that effect
```

During hover:

- Highlight the actual destination row.
- Show a destination-specific tooltip such as `Mask Worn Edges`.
- Keep the generic layer tooltip only for a layer destination.
- Reject unsupported child destinations without falling through to the layer.

Pattern and ID producer rows remain unsupported. Drag and RMB placement must produce the same ownership semantics.

---

## 12. Mask / Texture Gallery Zoom

Status: **Implemented**

Current behavior:

- Mouse-wheel zoom uses the material gallery interaction model.
- Mask range is `52–124px` with a `12px` step.
- The persistent mask strip, add/replace mask popovers, and Peeling seed picker share it.
- Dynamic tiles render at sufficient thumbnail resolution for the maximum zoom.

Retain this behavior; do not introduce another zoom system.

---

## 13. Shared Gallery Gap Token

Status: **Partially implemented**

Current state:

- Material and mask galleries both default to a `1px` gap.
- They still use separate `MaterialGalleryTileGap` and `MaskGalleryTileGap` tokens.
- `MaskGalleryTileGap` is exposed live in the developer style panel.
- The secondary material replacement gallery no longer exists.

Required cleanup:

- Replace both tokens with one live `GalleryTileGap` source of truth.
- Keep it in the developer style panel under `Galleries`.
- Use it for the material library, mask library, mask add/replace pickers, and future import grids.
- Preserve the current `1px` default and live refresh behavior.

---

## Implementation Order and Status

### R1 — Harden scoped-mask ownership

- Shared load/edit normalization.
- Save/load ownership tests.
- Preserve compatibility behavior.

### R2 — Add ID-driven offsets

- Masks select an upstream Pattern IDs or Cluster IDs source.
- Deterministic per-ID UV offset with amount and seed.
- Worn Edges offsets its complete noise domain per ID.
- Consume published ID maps without modifying producers.

### R3 — Persistent selection and RMB placement — complete

- Bottom-gallery clicks select only.
- Layer/effect RMB actions use the selected mask.
- Mask replacement uses the selected mask without a popover.

Mask drops onto supported Effect rows remain in the next phase.

### R4 — Unify gallery spacing

- One live `GalleryTileGap` token.
- Migrate material and mask galleries/pickers.

### R5 — User Mask Import

- Project-owned `UMixtormatMask` and texture assets.
- Max dimension 4096.
- Aspect-aware normalization and channel selection.

### R6 — Material Import Wizard + Channel Router

- Extend or refactor `FMixtormatSurfaceImporter` for project-owned imports.
- BC / Normal / arbitrary packed inputs.
- RAMH routing and same-dimension canonical outputs.

### R7 — Batch Folder Import

- Recursive scan, grouping, convention detection, and review table.

### R8 — Rectangular surface/aspect support

- Intrinsic surface aspect and quarter-turn-safe sampling.

### R9 — Global Canvas Rotation

- Root 0/90/180/270 rotation.
- Swap logical output dimensions for 90/270.
- Rotate tangent-space normal XY correctly.

---

## Constraints

- Do not store user imports inside shipped plugin content.
- Do not allow imported working textures above 4096 on either axis for now.
- Do not silently distort material-map aspect ratios.
- Do not introduce arbitrary recursive child graphs.
- Do not regress existing layer masks, Pattern IDs, Region IDs, Drivers, Ramp/HSV/Random From IDs, or existing effect semantics.
- Do not add scoped-mask ownership to Pattern or discrete ID producer rows.
- Do not modify Pattern shaders, controls, modes, or output contracts as part of this plan.
- Keep UI styling token-driven.
- Preserve current serialized enum values; append rather than reorder where serialization depends on numeric values.
