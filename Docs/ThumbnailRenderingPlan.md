# Mixtormat Thumbnail Rendering Plan

## Goal

Replace live Unreal asset thumbnails in Mixtormat galleries with stable, plugin-owned,
persistent `256×256` textures generated during import or reimport.

The gallery must never render thumbnail scenes, recreate thumbnails on selection, or depend on
`FAssetThumbnailPool` while Slate is painting.

## Approved thumbnail preset

### Surface thumbnails

- Output resolution: `256×256`.
- Viewport aspect ratio: `1:1`.
- Preview mesh: Mixtormat sphere.
- Floor: Mixtormat studio floor, visible.
- Environment background: hidden.
- Studio lighting: the plugin's startup default, currently **Neutral**.
- Preview quality: **Medium**.
- Screen percentage: `100%`.
- Camera FOV: `40°` horizontal.
- Camera framing: use the corrected horizontal and vertical fit calculation.
- Displacement: disabled, matching startup behavior.
- Bloom: disabled.
- Exposure: fixed using the shared preview profile.
- Anti-aliasing: completely disabled; do not use FXAA, TAA, or TSR.
- UI overlays: never constructed or rendered.

### Mask thumbnails

Masks should not use the lit sphere scene.

- Output resolution: `256×256`.
- Output aspect ratio: `1:1`.
- Presentation: flat grayscale mask preview.
- Lighting, floor, fog, and post-processing: not applicable.
- Anti-aliasing: disabled.
- Preserve the mask's visible grayscale values.
- Create a standard UI-safe texture rather than displaying a pooled asset thumbnail.

## Current implementation constraints

- Surface assets expose `PreviewMaterial`, but no dedicated thumbnail texture.
- Surface gallery entries currently pass `FAssetData` into `FAssetThumbnail`.
- Shipped masks are imported as raw `UTexture2D` assets under `/Mixtormat/Masks`.
- `UMixtormatMask` supports a `Thumbnail` property, but the shipped mask importer does not create
  `UMixtormatMask` data assets.
- The mask registry recursively treats textures under `/Mixtormat/Masks` as selectable masks.
- Generated mask thumbnails therefore must not be stored below `/Mixtormat/Masks`, or they will
  appear as duplicate mask entries.
- The existing preview scene configuration is private to `SMixtormatPreviewViewport.cpp` and must
  be shared before an offscreen renderer can use the exact same setup.

## Target content layout

```text
/Mixtormat/Thumbnails/Surfaces/<Family>/<SurfaceName>_Thumbnail
/Mixtormat/Thumbnails/Masks/<MaskName>_Thumbnail
```

Add path helpers to `FMixtormatPaths`:

- `ThumbnailsRoot()`
- `SurfaceThumbnailsRoot()`
- `SurfaceThumbnailFamilyRoot(Family)`
- `MaskThumbnailsRoot()`

Thumbnail folders must remain outside the surface and mask registry roots.

## Architecture

### 1. Shared preview-scene settings

Extract the scene settings currently owned by `SMixtormatPreviewViewport.cpp` into a small editor
module component, for example:

```text
Private/Preview/MixtormatPreviewSceneSettings.h
Private/Preview/MixtormatPreviewSceneSettings.cpp
```

It should configure:

- Mixtormat sphere and fallback sphere paths.
- Studio floor material.
- Preview mesh floor clearance.
- Neutral studio light values.
- Fog configuration.
- Fixed exposure and tone mapping.
- Bloom disabled.
- Medium-quality show flags.
- Camera defaults and aspect-aware focus calculation.

Both the interactive viewport and thumbnail renderer must call this shared code. Do not duplicate
lighting or camera literals in the renderer.

### 2. Dedicated thumbnail renderer

Add an editor-only service, for example:

```text
Private/Services/MixtormatThumbnailRenderer.h
Private/Services/MixtormatThumbnailRenderer.cpp
```

Responsibilities:

- Own a reusable offscreen preview scene.
- Own one sphere mesh component.
- Create a square `256×256` render target or scene viewport.
- Apply one surface preview material at a time.
- Reset camera, lighting, fog, and show flags before every render.
- Disable all anti-aliasing show flags explicitly.
- Render without constructing Mixtormat overlay widgets.
- Read back BGRA pixels only after rendering is complete.
- Create or update the persistent thumbnail `UTexture2D`.
- Save the texture through the existing guarded plugin-asset save path.
- Return a structured success or error result to the importer.

The renderer should be reused for a batch instead of constructing a scene per material.

### 3. Persistent texture creation

Surface and mask thumbnails should use normal persistent `UTexture2D` assets.

Recommended texture settings:

- Size: `256×256`.
- Source format: BGRA8.
- Surface thumbnail `SRGB`: enabled.
- Mask thumbnail `SRGB`: enabled for predictable Slate display after grayscale conversion.
- Compression: UI/editor-appropriate color compression.
- Streaming: disabled because thumbnails are small and used interactively.
- Addressing: clamp.
- Mips: optional; retain if gallery downsampling benefits from them.

Creating a thumbnail must update an existing texture in place when possible. Replacing the UObject
on every reimport would reintroduce resource-lifetime churn.

## Surface import flow

Extend `FMixtormatSurfaceImporter::ImportDirectory` after the preview material has been updated:

1. Import or reuse BC, Normal, and RAM/RAMH textures.
2. Create or update the surface preview material.
3. Compute the thumbnail cache key.
4. Reuse the existing thumbnail when the key matches.
5. Otherwise render the preview material with the approved preset.
6. Create or update the persistent thumbnail texture.
7. Save the preview material, thumbnail texture, and surface asset.
8. Report thumbnail failures without discarding an otherwise valid surface import.

A failed thumbnail render should leave the previous valid thumbnail in place. It should not replace
it with an empty texture.

## Surface cache invalidation

Add a deterministic thumbnail version or hash. It must change when any visual input changes:

- BC source hash.
- Normal source hash.
- RAM/RAMH source hash.
- Preview master material version or renderer version.
- Default IOR.
- Sphere mesh version.
- Floor material version.
- Camera preset version.
- Lighting preset version.
- Thumbnail resolution.

A practical cache string can combine source file hashes with a manually incremented renderer preset
version. Store the resulting value on the surface asset or as searchable thumbnail metadata.

Do not rerender every surface on every editor startup.

## Mask import flow

The shipped mask importer currently creates raw textures rather than mask data assets. Keep that
behavior for compatibility.

For every successfully imported or reused mask texture:

1. Resolve the deterministic thumbnail path under `/Mixtormat/Thumbnails/Masks`.
2. Read the source grayscale pixels from import source data or the imported texture source.
3. Convert them to a `256×256` BGRA grayscale image.
4. Use high-quality downsampling for larger masks.
5. Create or update the persistent thumbnail texture.
6. Save it separately from the selectable mask texture.
7. Preserve the previous thumbnail if conversion fails.

The mask cache key should include:

- Mask source file hash.
- Conversion version.
- Thumbnail resolution.

No 3D renderer is required for masks.

## Registry changes

Update registry entries so galleries receive the generated thumbnail texture directly.

### Surfaces

`FMixtormatRegistry::GetSurfaces()` should resolve the deterministic surface thumbnail path. If the
thumbnail is missing, it may return no thumbnail and let the tile display its static placeholder.
It must not fall back to a live `FAssetThumbnail` render.

### Masks

`FMixtormatRegistry::GetMasks()` should resolve the deterministic mask thumbnail path for raw
texture masks. For `UMixtormatMask` assets, prefer the explicit `Thumbnail` property when valid,
then the deterministic cached thumbnail.

Do not include `/Mixtormat/Thumbnails` in selectable surface or mask filters.

## Slate gallery changes

Replace `FAssetThumbnail` usage in gallery tiles with a plugin-owned texture image widget.

Requirements:

- The widget must hold a strong reference to its `UTexture2D` for its complete Slate lifetime.
- The `FSlateBrush` must live as long as the `SImage` using it.
- Never use a temporary brush returned from a local function.
- Never release or replace a texture resource during a Slate paint pass.
- Missing thumbnails use `Mixtormat.ThumbnailBackground`.
- Selection should update through bound state only.
- Clicking a surface must not rebuild the surface or mask gallery.
- Gallery rebuilds should occur only after import/reimport, filtering, or explicit tile-size changes.

Remove gallery dependencies on:

- `FAssetThumbnail`
- `FAssetThumbnailPool`
- `FAssetThumbnail::MakeThumbnailWidget`

The pool may remain temporarily for unrelated editor tools, but galleries must not use it.

## Layer rows

Layer rows should use static Mixtormat type icons rather than gallery thumbnails.

- Material layer: material icon.
- Fill layer: fill icon.
- Normal-detail layer: normal icon.
- Other layer types: their existing type icon.

This avoids consuming gallery thumbnail resources and keeps layer rows visually stable during stack
rebuilds.

## Reimport behavior

After a successful import/reimport batch:

1. Finish all texture and thumbnail asset updates.
2. Save updated assets.
3. Refresh the registry once.
4. Rebuild each visible gallery once.
5. Preserve the selected surface and mask paths.
6. Preserve search, category, zoom, and scroll state.

Do not rebuild galleries once per imported asset.

## Progress and cancellation

Thumbnail generation can make reimport noticeably slower.

- Add thumbnail stages to the existing import result/progress reporting.
- Reuse one preview scene for the full batch.
- Permit cancellation between assets, never during texture save.
- Report rendered, reused, and failed thumbnail counts separately.
- Continue importing other assets when one thumbnail fails.

## Migration

Existing installations have surfaces and masks without plugin thumbnails.

- Missing thumbnails are generated on the next explicit shipped-library reimport.
- The gallery displays placeholders until generation completes.
- Do not silently rerender the full library every time the plugin opens.
- Existing surface, mask, preview material, and recipe paths remain unchanged.
- No existing source texture should be renamed or moved.

## Suggested implementation order

1. Extract shared preview scene and camera configuration.
2. Add thumbnail path helpers.
3. Implement persistent texture creation/update utilities.
4. Implement flat mask thumbnail generation.
5. Implement the square offscreen surface renderer.
6. Add surface and mask cache keys.
7. Integrate generation into import and reimport.
8. Return thumbnail textures from the registry.
9. Replace gallery `FAssetThumbnail` widgets.
10. Replace layer-row thumbnails with static icons.
11. Remove unnecessary gallery rebuilds on selection.
12. Add focused automation tests where rendering-independent logic permits.

## Validation checklist

### Surface output

- Output is exactly `256×256`.
- Sphere is centered and fully framed on both axes.
- The image contains the Mixtormat floor and Neutral startup lighting.
- Medium-quality shadows, AO, and reflections match the interactive viewport.
- No overlay controls appear.
- No FXAA, TAA, or TSR is active.
- Reimporting an unchanged surface reuses its thumbnail.
- Changing any source map regenerates the thumbnail.

### Mask output

- Output is exactly `256×256`.
- Black, white, and intermediate grayscale values remain distinguishable.
- The mask is neither gamma-shifted nor color-tinted.
- Generated thumbnail textures do not appear as selectable masks.
- Reimporting an unchanged mask reuses its thumbnail.

### UI stability

- Clicking materials does not reorder or blink either gallery.
- Selecting layers does not rebuild gallery thumbnail widgets.
- Collapsing and expanding the bottom library does not recreate textures.
- Scrolling galleries does not regenerate thumbnails.
- Repeated filtering and selection do not trigger destroyed Slate resource assertions.
- Closing and reopening the plugin releases widgets without a renderer assertion.

## Files expected to change

```text
Source/MixtormatRuntime/Public/MixtormatSurface.h             (only if storing an explicit thumbnail reference)
Source/MixtormatEditor/MixtormatEditor.Build.cs               (only if the chosen offscreen API needs another module)
Source/MixtormatEditor/Private/Preview/*                       (shared preview scene settings)
Source/MixtormatEditor/Private/Services/MixtormatPaths.*
Source/MixtormatEditor/Private/Services/MixtormatRegistry.*
Source/MixtormatEditor/Private/Services/MixtormatSurfaceImporter.*
Source/MixtormatEditor/Private/Services/MixtormatThumbnailRenderer.*
Source/MixtormatEditor/Private/UI/Controls/SMixtormatTile.*
Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
```

## Decisions still required before implementation

1. Confirm **Neutral**, not Dramatic, because Neutral is the current startup default.
2. Confirm whether generated thumbnail textures should include mipmaps.
3. Confirm whether explicit surface thumbnail references should be stored on `UMixtormatSurface`,
   or resolved only through deterministic asset paths.

Recommended defaults:

- Neutral lighting.
- Generate mipmaps.
- Store an explicit thumbnail reference on `UMixtormatSurface`.
