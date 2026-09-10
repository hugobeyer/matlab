# Mixtormat — Deferred Import, Aspect, and UX Implementation Plan

Status: **Proposed**

Requirements source: [`Mixtormat_Deferred_Import_FeatureMasks_and_UX_Tasks.md`](Mixtormat_Deferred_Import_FeatureMasks_and_UX_Tasks.md)

## 1. Scope

Finish the remaining mask-assignment UX, then add project-owned mask/material imports, rectangular surface support, and global canvas rotation.

This plan builds on the implemented foundation:

- Flat effect-mask ownership through `FMixtormatLayerChild::ScopeOwnerChildId`.
- Scoped masks owned by `Effect` children only.
- Existing generic mask inspector and compositor mask blending.
- Completed Worn Edges roughness routing.
- Existing `FMixtormatSurfaceImporter`, `UMixtormatSurface`, and `UMixtormatMask` assets.
- Existing material and mask gallery wheel zoom.
- Existing Pattern/Cluster Region ID textures consumed by downstream effects.

## 2. Guardrails

- Do not modify Pattern shaders, controls, modes, or output contracts.
- Do not let Pattern or discrete ID producer rows own scoped masks.
- Do not introduce recursive child arrays or arbitrary node graphs.
- Preserve serialized fields, enum values, legacy effect-mask behavior, and old recipes.
- Store user content under `/Game/Mixtormat`, never `/MaterialLab`.
- Never silently distort source aspect ratios.
- Keep BC, Normal, and RAMH dimensions identical per imported surface.
- Keep all gallery layout values in the live design-token system.
- Add no external dependency unless separately approved.

## 3. Phase R1 — Scoped-mask hardening

### Goal

Make ownership deterministic across loading, editing, duplication, and malformed data.

### Work

1. Add one shared ownership-normalization function.
2. Run it after recipe load/deserialization and structural child edits.
3. Enforce these invariants:
   - Owners exist in the same layer.
   - Owners are `Effect` children.
   - Scoped children are masks.
   - Scoped masks cannot own children.
   - Every owner block is contiguous and immediately follows its owner.
4. Clear invalid ownership to layer scope rather than dropping mask data.
5. Preserve local ownership when moving or duplicating a complete owner block.
6. Keep source-instance identity separate from local scope ownership.

### Acceptance

- Old recipes with invalid GUIDs still load without losing masks.
- Save/load preserves effect ownership and visible ordering.
- Duplicated owners receive remapped owner and mask IDs.
- Later siblings receive the original layer `CombinedMask`.

## 4. Phase R2 — ID-driven mask and Worn noise offsets

### Goal

Let masks phase-shift their texture per Pattern/Cluster region, and make Worn Edges phase-shift its full noise domain per Region ID.

This is read-only consumer behavior. Do not change Pattern or Cluster producer shaders or outputs.

### Data model

Append mask fields to `FMixtormatMaskLayer`:

- Stable Region ID source `ChildId`.
- ID offset amount, default `0`.
- ID offset seed, default `0`.

Append Worn Edges fields to `FMixtormatLayerEffect`:

- Optional explicit Region ID source `ChildId`.
- Noise offset amount, default `0`.

An invalid Worn source means Auto / nearest upstream, preserving current behavior.

### Mask inspector

1. Add `ID Offset Source` with None plus compatible upstream Pattern IDs and Cluster IDs rows.
2. Add `ID Offset Amount` and `ID Offset Seed`.
3. Store source identity by `ChildId`, never row index or label.
4. Exclude downstream producers that do not exist when the mask evaluates.
5. Show a missing-source warning without dropping or redirecting the mask.

### Mask evaluation

```text
Existing placed mask UV
→ add Hash2(RegionId, Seed) * Amount
→ wrap
→ sample texture
→ existing shaping and blend
```

The hashed offset must remain constant inside each region. Amount `0`, no source, or invalid IDs must be exact identity paths.

### Worn Edges evaluation

1. Resolve Auto to the current nearest upstream producer.
2. Resolve an explicit source from the producer texture map by stable child identity.
3. Hash Region ID plus the existing Worn seed into one two-dimensional offset.
4. Add that offset to the Macro, Cellular, Ridge, Micro, and Warp input domains.
5. Keep existing per-ID radius, slope, strength, and noise-family weight variation.
6. Do not offset Region IDs, Pattern `OutputEdge`, or edge localization.

### Acceptance

- A mask uses a different stable texture phase in each selected Pattern/Cluster region.
- Two pixels with the same Region ID receive the same offset.
- Different seeds produce deterministic alternative placement.
- Worn noise changes phase per ID rather than only changing family weights.
- All new default values reproduce current renders exactly.
- Pattern and Cluster outputs remain unchanged.

## 5. Phase R3 — Persistent mask selection and RMB placement

Status: **Implemented**

### Goal

Keep the bottom mask gallery stable and make placement an explicit layer-stack action.

### Implemented behavior

1. A bottom-gallery click stores the selected mask path and display name.
2. The selected tile uses the same persistent selection border as material tiles.
3. Clicking a mask does not add, replace, or reorder any layer child.
4. Layer RMB adds the selected mask at layer scope.
5. Effect RMB adds the selected mask at effect scope.
6. Mask-row RMB replaces with the selected mask.
7. All direct actions are disabled until a mask is selected.
8. Layer/effect add and mask replacement no longer open gallery popovers.
9. Dragging remains a separate explicit gesture.

### Acceptance

- Repeated mask selection does not rebuild or mutate the layer stack.
- RMB labels identify the selected mask before the action runs.
- Layer, effect, and replacement actions use the path captured when the menu opens.
- Existing drag-to-layer behavior remains available.

## 6. Phase R4 — Effect-row mask drag/drop

### Goal

Accept mask assets directly on supported effect rows.

### Work

1. Extend `SMixtormatChildDropTarget` to recognize `FMixtormatMaskDragDropOp`.
2. Add a mask-drop delegate carrying layer index, child index, and mask path.
3. Accept mask drops only when the target child is an `Effect`.
4. Keep current child reorder/move behavior unchanged.
5. Highlight only the actual target row during drag hover.
6. Use destination-specific tooltip text such as `Mask Worn Edges`.
7. Reset the drag tooltip on leave and drop.
8. Prevent rejected child drops from bubbling into the enclosing layer target.

### Acceptance

- Dropping on an effect creates a scoped mask.
- Dropping on a layer creates a layer-scoped mask.
- Dropping on Pattern IDs or another unsupported child changes nothing.
- Existing child reorder and cross-layer move behavior remains intact.

## 7. Phase R5 — Shared gallery spacing token

### Goal

Use one live spacing value for material and mask galleries.

### Work

1. Replace `MaterialGalleryTileGap` and `MaskGalleryTileGap` with:

```cpp
inline float GalleryTileGap = 1.0f;
```

2. Register `GalleryTileGap` under `Galleries` in the developer style panel.
3. Migrate:
   - Material library.
   - Mask library.
   - Add/replace mask pickers.
   - Peeling seed picker where it uses a gallery grid.
4. Use the same token for future import-wizard thumbnail grids.
5. Keep zoom ranges separate where their content needs different sizes.

### Acceptance

- One developer-panel value updates all gallery gaps live.
- Default spacing remains `1px`.
- No removed material replacement popover is reintroduced.

## 8. Phase R6 — User mask import

### Goal

Create project-owned mask textures and `UMixtormatMask` assets from PNG files.

### Architecture

Add a focused editor import service beside `FMixtormatSurfaceImporter`; do not mix user imports into the shipped plugin bootstrap path.

### Work

1. Add `Import Mask` to the persistent mask library.
2. Open a single/multi-file PNG picker.
3. Preflight every source before creating assets:
   - Dimensions.
   - Aspect ratio.
   - Channel availability.
   - Selected maximum dimension.
   - Name collisions.
4. Expose channel selection: Luminance, R, G, B, or A.
5. Expose normalization: Preserve/Fit, Fill/Crop, or Stretch.
6. Default to Preserve/Fit and a 2048 maximum dimension.
7. Clamp either axis to 4096 while preserving aspect unless explicitly overridden.
8. Create textures under `/Game/Mixtormat/Textures`.
9. Configure texture data settings: sRGB off and mask-appropriate compression.
10. Create `UMixtormatMask` assets under `/Game/Mixtormat/Masks`.
11. Refresh the registry and select the imported mask without rebuilding unrelated galleries.
12. Report per-file failures without discarding successful imports.

### Acceptance

- Square and rectangular PNG masks import without silent distortion.
- Multi-file import produces one mask asset per valid source.
- Imported textures are linear data and no axis exceeds 4096.
- Shipped `/MaterialLab` content is never modified.

## 9. Phase R7 — Material import model and channel router

### Goal

Represent arbitrary input channels before building the wizard UI.

### Data model

Each canonical output channel stores:

- Source texture or constant.
- Source channel: R, G, B, A, Luminance, or RGB where valid.
- Invert.
- Input/output range.
- Custom constant where selected.

The routing table is authoritative. Presets only populate it.

### Work

1. Define import-candidate and channel-route editor structs.
2. Represent BC RGB, Normal RGB, Roughness, AO, Metallic, and Height explicitly.
3. Add presets for Separate, ORM, ARM, RMA, MRA, RAM, RAMH, and alpha conventions.
4. Track source color interpretation per route.
5. Treat packed data channels as linear even when sourced from BC alpha.
6. Support DirectX/OpenGL normal convention and explicit green-channel flip.
7. Determine one canonical output width/height before processing pixels.
8. Warn on aspect mismatch; default to Cancel/Fix Source.
9. Keep operations limited to invert and range/levels.

### Acceptance

- Arbitrary packed layouts can produce canonical BC, Normal, and RAMH.
- Preset changes remain visible and editable in the routing table.
- Scalar data never receives accidental sRGB interpretation.

## 10. Phase R8 — Material import wizard

### Goal

Expose the routing model as a reviewable, non-destructive workflow.

### Work

1. Add pages or sections for:
   - Name and sources.
   - Detected dimensions/aspects.
   - Channel routing and presets.
   - Normal convention.
   - Target dimensions and mismatch handling.
   - Preview, warnings, and final import.
2. Accept disk files and existing `UTexture2D` assets.
3. Reuse proven import, asset creation, and save operations from `FMixtormatSurfaceImporter`.
4. Separate shipped plugin import behavior from project-owned user imports.
5. Write BC, Normal, and RAMH with identical dimensions.
6. Create `UMixtormatSurface` assets in a project-owned destination.
7. Refresh only affected registry/gallery state.

### Acceptance

- The wizard never imports while required routes are ambiguous.
- Final dimensions are visible before import.
- All three canonical outputs align exactly.
- Cancel leaves no partial assets.

## 11. Phase R9 — Batch folder import

### Goal

Build the fast workflow on the same candidate and routing model.

### Work

1. Scan recursively without creating assets.
2. Group by basename and folder-per-material conventions.
3. Detect suffix and packing presets as suggestions only.
4. Show a review table with warnings and selected state.
5. Open ambiguous candidates in the same routing editor.
6. Import selected valid candidates through the single-material pipeline.
7. Keep per-material results so one failure does not hide the others.

### Acceptance

- Folder scan is side-effect free until confirmation.
- Ambiguous packed maps are never silently interpreted.
- Batch and single import produce the same canonical assets.

## 12. Phase R10 — Intrinsic rectangular surface aspect

### Goal

Preserve a surface's authored aspect through layer transforms.

### Work

1. Store intrinsic width, height, or normalized aspect on `UMixtormatSurface`.
2. Populate it during import and derive it for compatible existing surfaces.
3. Apply aspect compensation before layer tiling, rotation, flips, and offsets.
4. Swap aspect compensation for 90°/270° local rotations.
5. Apply identical coordinates to BC, Normal, and RAMH sampling.
6. Define migration defaults that preserve existing square-surface behavior.

### Acceptance

- A 2:1 surface remains 2:1 when tiled.
- Local 90° rotation displays it as 1:2 without manual X/Y repair.
- Existing square surfaces render identically.

## 13. Phase R11 — Global canvas rotation

### Goal

Rotate the complete authored composition at the document root.

### Work

1. Add an append-only serialized quarter-turn canvas property.
2. Apply it before all layers, masks, IDs, and effects.
3. Swap logical output dimensions at 90°/270°.
4. Rotate tangent-space normal XY consistently with the UV transform.
5. Apply the same transform to preview, bake, and exported outputs.
6. Keep the UI root-level even if placed near substrate controls.

### Acceptance

- Every authored component rotates together.
- A rectangular canvas swaps width/height at quarter turns.
- Normal lighting orientation remains correct.
- 0° preserves existing documents exactly.

## 14. Validation order

Run only when build/test execution is explicitly authorized.

1. Static checks after each phase.
2. Focused automation for ownership, routing, and dimension math.
3. Save/load tests for all new serialized fields.
4. UI interaction checks for click, RMB, drag/drop, zoom, and live gap refresh.
5. Golden-image comparisons for aspect, rotation, normals, and packed channels.
6. Full editor build and relevant automation suite before release.

## 15. Delivery boundaries

Recommended review units:

1. R1 ownership hardening.
2. R2 ID-driven mask and Worn noise offsets.
3. R3–R5 mask UX and gallery cleanup.
4. R6 user mask import.
5. R7–R8 material router and wizard.
6. R9 batch import.
7. R10 intrinsic aspect.
8. R11 global canvas rotation.

Do not combine Pattern work with any of these changes.
