# Mixtormat Source and Shader Audit

## Scope

This audit reviewed only the plugin's `Source` and `Shaders` implementation areas.

- No assets, binaries, prototypes, resources, or intermediate output were audited.
- The plugin manifest was read only to establish module loading and packaging context.
- This is a static audit; no build, cook, packaging, or runtime test was executed.

## Executive Summary

Mixtormat has a clear three-module separation:

1. `MixtormatRuntime` owns serializable asset schemas and parameter/reference logic.
2. `MixtormatEditor` owns the Slate workspace, library import, migration, previews, and baking.
3. `MixtormatShaders` owns the render-thread GPU compositor and compute shaders.

Mixtormat is an editor authoring and baking tool. It mixes source surfaces in the editor, then
bakes Base Color, Normal, RAM, and Height outputs for use by ordinary runtime materials. The
`MixtormatRuntime` module appropriately provides runtime-safe asset schemas and shared logic; its
presence does not claim or require live in-game compositing.

The plugin includes a shipped library of 35 materials, as reported by the plugin author. This audit
did not inspect `Content`, so the count and asset quality were not independently verified.

The main FAB readiness consideration is distribution behavior. The editor importer can create and
save assets inside the installed plugin's `/Mixtormat` content mount. The shipped 35-material
library should make this unnecessary for first use. Customer-created recipes and baked outputs
should still default to a project-owned destination, rather than modifying an installed plugin.

## Architecture

### `MixtormatRuntime`

**Responsibility:** persistent data model and reference/parameter behavior.

Key primary data assets:

- `UMixtormatMaterial`: layer-stack recipe, layer children, bindings, references, and bake metadata.
- `UMixtormatSurface`: material-surface library entry with Base Color, Normal, and RAM/RAMH maps.
- `UMixtormatMask`: mask-library entry with source texture, thumbnail, tags, and default shaping.
- `UMixtormatEffect`: effect-library entry with type and defaults.

Strengths:

- Uses `UPrimaryDataAsset` and explicit `FPrimaryAssetId` values for all core library assets.
- Surface identity fields are `AssetRegistrySearchable`, supporting editor library search/filtering.
- `FMixtormatParameterBinding` provides stable GUIDs, direct references, linked edits, and instance
  resolution without mutating the serialized source data during evaluation.
- Existing assets are protected by deliberate compatibility behavior in `PostLoad` and deprecated
  serialized properties.

Boundary observation:

- `MixtormatRuntimeModule.cpp` has empty startup and shutdown functions.
- Runtime currently provides asset types and shared data logic, not the GPU composition pipeline.

### `MixtormatEditor`

**Responsibility:** authoring UI, library management, preview, baking, and migration.

Main areas:

- `Widgets/`: the primary Mixtormat Slate workspace, inspector, layers, library, preview, and theme.
- `UI/`: reusable Slate atoms, controls, menus, layer rows, and drag/drop behavior.
- `Services/`: library registry, asset-path policy, source import, preview material setup, and migration.
- `Compositing/`: bake service that writes PBR textures and a material instance.
- `Tests/`: compositor, layer-preview, and normal-height automation coverage.

Strengths:

- The workspace is registered as a Nomad tab and correctly unregistered on module shutdown.
- The bake service validates output naming/path inputs and detects existing output assets.
- The registry retrieves library entries through the Asset Registry rather than hard-coded asset lists.
- The importer configures texture sRGB, compression, alpha retention, LOD group, mip settings, and
  streaming behavior by map role.
- Asset migration is exposed as a dry-run-by-default console command; applying changes requires the
  explicit `Apply` argument.

### `MixtormatShaders`

**Responsibility:** GPU composition and shader registration.

Main components:

- `FMixtormatGpuCompositor`: render-target allocation, dispatch orchestration, output publication,
  debug outputs, and a render-thread cache for expensive generated networks.
- `FMixtormatNormalHeightGenerator`: GPU normal-to-height processing using compute passes.
- `MixtormatShadersModule`: maps `/Plugin/Mixtormat` to the plugin `Shaders` directory during
  `PostConfigInit`.

Strengths:

- Compute shader dispatch is isolated from editor widgets.
- The compositor protects game-thread and render-thread paths with thread checks.
- It double-buffers output targets and caches craquelure networks, avoiding expensive rebuilds during
  interactive tuning.
- The shader source mapping is installed early enough for global shader compilation.

## Shader Inventory

The compositor registers compute shader implementations for the main rendering graph, including:

- Surface compositing, texture masks, generated masks, and colour IDs.
- Craquelure generation, growth, distance transform, and relief.
- Erosion, grading, carved shading, mask blur, and min/max reduction.
- Chipping, edge wear, procedural peel fields, peeling, and stains.
- Cluster IDs, random IDs, pattern IDs, ramp IDs, relief, and edge shading.
- Normal-to-height decoding, FFT passes, Poisson solve, and height extraction.

Shared `.ush` files centralize UV transforms, mask operations, colour operations, region IDs,
curvature, driver behavior, cellular noise, gully behavior, and debug colour output.

### Shader maintenance risk

The region palette capacity is intentionally mirrored in three places:

- `MixtormatComposite.usf`
- `FMixtormatCompositeCS` in C++
- `FMixtormatHsvIdFilter`

The code comments acknowledge this duplication. It is a maintenance risk: changing one copy can
produce an incorrect clamp, data mismatch, or shader/C++ contract failure. Document the single
canonical value and add an automated assertion or shared generated define when practical.

### Likely unwired shader work

`MixtormatFlow.usf` and `MixtormatFlow.ush` were not found in shader registration or as consumers
from the reviewed source. They appear to be unused implementation work rather than an active
feature. Confirm with a full compile/reference search before deleting them.

This is not a recommendation to remove the files immediately. It is a targeted cleanup candidate.

## Legacy and Compatibility Findings

### Intentional serialized compatibility

The following are intentionally retained so older recipe assets load without losing data:

- `EMixtormatHeightSource::Automatic`, shown as `Automatic (Legacy)`.
- Legacy height blend controls in the editor inspector.
- Deprecated Stain defaults in `UMixtormatEffect`.
- Deprecated Stain properties in `FMixtormatLayer`.
- Serialized legacy height-reference behavior covered by compositor tests.

These should remain until a documented asset migration and a compatibility policy exist. Do not
remove or reorder serialized enums/properties casually.

### Stain behavior is confusing or incomplete

The implementation comments state that Stain:

- Is classified as a filter effect.
- Resolves a layer mask.
- Writes no effect data target.
- "Shades nothing."

The older Stain colour and roughness properties are therefore deprecated because nothing reads them.

This is a product/documentation issue, not merely unused code:

- If Stain is intentionally a mask-only transport solve, rename and document it accordingly.
- If users expect a visible stain surface effect, it is incomplete and should not be marketed as a
  finished visual effect until it affects output channels.
- Hide it from beginner-facing workflows until its purpose is explicit.

### Likely obsolete effect-texture import path

`MixtormatSurfaceImporter` still recognizes effect texture suffixes:

- `_PDM`
- `_MSK`
- `_SDF`
- `_BN`
- `_H`

However, `UMixtormatEffect` states that peeling is generated procedurally and effect assets no
longer ship texture sets. This makes the effect-texture import branch a likely gather-era legacy
path.

Recommended resolution:

1. Decide whether authored effect texture packs remain supported.
2. If supported, document their file naming, output behavior, and compatibility status.
3. If unsupported, deprecate the importer path with a migration note before removing it.

## FAB Store Readiness

### Must resolve before submission

#### 1. Do not require writing into the installed plugin

The importer builds library assets under the plugin's `/Mixtormat` content root and explicitly
refuses to save outside the plugin:

> `Refused to save ... outside the Mixtormat plugin`

This is acceptable for internal library-generation tooling, but it is risky as a customer workflow.
A FAB-installed engine plugin may be read-only, overwritten by updates, or unsuitable for storing
user work.

Required product behavior:

- Ship the required library assets already imported in plugin `Content`.
- Treat shipped plugin content as immutable.
- Default bakes and user-created recipes to a project-owned path such as `/Game/Mixtormat/...`.
- If reimport is supported, offer an explicit project-library destination.
- Never make "import into plugin content" the only path to first successful use.

#### 2. Describe the baked runtime workflow accurately

The manifest declares:

- `MixtormatRuntime` as a Runtime module.
- `MixtormatShaders` as an Editor-only module.
- `MixtormatEditor` as an Editor-only module.

This matches the intended workflow: composition runs in the editor, while the resulting baked PBR
textures and material instance are used at runtime. The Editor-only compositor is therefore not a
functional gap for the baked-material product.

Before FAB listing:

- State that Mixtormat is an editor authoring and baking workflow.
- State that baked outputs are suitable for normal project/runtime material use.
- Do not imply live in-game procedural mixing unless that becomes a separate supported feature.
- Ensure listing screenshots and feature claims match the shipped baking workflow.

#### 3. Document system requirements and constraints

The normal-to-height generator only supports square textures at:

- 1024 × 1024
- 2048 × 2048
- 4096 × 4096

This is a significant authoring constraint and must be visible before purchase or before the user
imports a library. Also document GPU/compute requirements and expected editor-version support.

### Strongly recommended FAB package content

Provide these as concise, customer-facing documents:

- **Quick Start:** enable plugin, open the Mixtormat tab, create/load a recipe, compose, and bake.
- **Library Guide:** expected surface map names (`_BC`, `_N`, `_RAM`, `_RAMH`) and map meanings.
- **Bake Guide:** output textures, naming, destination path, overwrite behavior, and material instance.
- **Effects Guide:** active effects, current Stain behavior, and any legacy effect-data support.
- **Runtime Use:** explain that authored results are baked for normal runtime material use.
- **Requirements:** UE versions, supported desktop platforms, GPU/compute needs, and size limits.
- **Troubleshooting:** missing master material, unavailable source library, unsupported resolution,
  existing bake assets, and read-only plugin installations.
- **Changelog / Compatibility:** asset migration policy and meaning of `Legacy` UI entries.

### Store presentation considerations

The codebase supports a visually rich editor tool, so the listing should demonstrate outcomes rather
than implementation details:

- Show the layer stack, inspector, viewport preview, generated masks, and baked PBR result.
- Include before/after examples for compositing, craquelure, chipping, and edge wear.
- State the PBR output set clearly: Base Color, Normal, RAM, and Height.
- State whether the included material instance/master material is suitable for game runtime use.
- Explain that runtime uses baked outputs, not the editor's live compute compositor.

## Documentation Gap

The reviewed source is heavily commented for developers, especially around GPU contracts,
serialization stability, and effect behavior. That is useful internal documentation, but it does
not replace customer documentation.

Missing user-facing documentation should explain:

- The difference between a Surface, Mask, Effect, Layer, and Recipe.
- The layer evaluation order and why order affects IDs, masks, and post-composite filters.
- Why UV transforms use integer tiling and quarter-turn rotations.
- Which settings are legacy compatibility settings and should normally be avoided.
- How references, links, drivers, and child instances behave.
- Which values are non-destructive recipe settings versus baked output.
- Where user assets should be stored for a safe project workflow.

## Validation Status

No build or automated test run was performed for this audit.

Editor diagnostics currently report missing Unreal include paths and Unreal reflection macros. The
reported errors start with missing engine headers such as `CoreMinimal.h`; this indicates the editor
analysis environment is not configured with Unreal Engine compilation context. It does **not** prove
that the plugin fails to compile.

Before release, validate in the supported Unreal Engine versions:

1. Clean editor compile.
2. Shader compile from a fresh derived-data cache.
3. Plugin enable/disable and editor restart.
4. First-run workflow using only shipped content.
5. Baking to a project-owned content path.
6. Packaged-project use of baked textures and the generated material instance.
7. Plugin installation from a non-writable location.

## Prioritized Actions

### P0 — before FAB submission

1. Include the reported 35-material library as ready-to-use plugin content.
2. Do not depend on first-run writes into installed plugin content.
3. Document the editor-authoring and baked-runtime workflow.
4. Write Quick Start, requirements, library naming, bake, and limitations documentation.
5. Decide and document whether Stain is mask-only or a visible output effect.
6. Test an installation where plugin files are not writable.

### P1 — release hardening

1. Decide the fate of authored effect-texture importing and document/migrate it.
2. Document legacy height and Stain fields as compatibility-only behavior.
3. Establish one source of truth for shared C++/USF capacity constants.
4. Confirm whether the Flow shader files are intentionally reserved or unused.
5. Add customer-visible errors for unsupported normal-to-height resolutions.

### P2 — maintainability

1. Add a developer architecture diagram for Runtime → Editor → Shaders boundaries.
2. Add a shader dispatch inventory and parameter-contract maintenance guide.
3. Add release validation notes for shader compilation and package testing.
4. Maintain a compatibility table for serialized enum/property changes.

## Conclusion

The plugin has a solid editor-authoring architecture and a substantial compute-shader feature set.
Its intended path is clear: author and composite in the editor, bake the PBR outputs, and use those
outputs in normal runtime materials. The main release work is distribution safety and customer-facing
documentation, especially around the shipped 35-material library, import behavior, baking, and
project-owned output locations.
