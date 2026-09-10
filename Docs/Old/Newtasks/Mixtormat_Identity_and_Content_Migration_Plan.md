# Mixtormat — Identity and Content Migration Plan

Status: **In progress — I2–I3 implemented; I4 migration tool added, execution pending**

Current progress:

- I1: PowerShell baseline captured 34 surfaces and 34 matching preview instances; live metadata verification remains.
- I2: editor-side plugin/package paths are centralized in `FMixtormatPaths`.
- I3: new document names, project save defaults, and live-theme storage use `Mixtormat`.
- Plugin mount and shader paths still use `MaterialLab` until the atomic I5 change.
- I4 has a guarded Unreal AssetTools command; dry-run, build, and execution remain.
- The I4 surface scan is dynamic so later imported surfaces are included.

## 1. Goal

Remove active `MaterialLab` / `MatLab` naming and make `Mixtormat` the single product identity across:

- Plugin descriptor and physical plugin directory.
- Plugin discovery.
- Mounted content paths.
- Shader virtual paths.
- Editor save/config paths.
- User-facing text.
- Active asset names and references.
- Import and registry services.

This is a staged migration, not a global text replacement.

## 2. Preservation boundary

### Preserve

Imported and shipped surface-library content is valuable and must survive intact:

- `UMixtormatSurface` assets.
- Base Color, Normal, RAM/RAMH textures.
- Surface metadata: display name, family, subtype, and finish.
- Preview material instances.
- Master material and preview meshes used by surfaces.
- Source files consumed by `FMixtormatSurfaceImporter`.

### Do not migrate

Previously composited `UMixtormatMaterial` documents do not need compatibility migration.

- New documents should save under `/Game/Mixtormat/Materials`.
- No old `/Game/MaterialLab/Materials` package redirects are required.
- Existing composed-material assets should not influence migration design.
- Do not delete old composed materials as part of this task without explicit approval.

## 3. Target identity

| Area | Current | Target |
|---|---|---|
| Plugin folder | `Plugins/MaterialLab` | `Plugins/Mixtormat` |
| Descriptor | `MaterialLab.uplugin` | `Mixtormat.uplugin` |
| Plugin lookup | `FindPlugin("MaterialLab")` | `FindPlugin("Mixtormat")` |
| Content mount | `/MaterialLab` | `/Mixtormat` |
| Shader root | `/Plugin/MaterialLab` | `/Plugin/Mixtormat` |
| Saved theme | `Saved/MaterialLab` | `Saved/Mixtormat` |
| New composed materials | `/Game/MaterialLab/Materials` | `/Game/Mixtormat/Materials` |
| C++ modules/classes | `Mixtormat*` | unchanged |
| Friendly name | `Mixtormat` | unchanged |

## 4. Naming policy

Use one rule for future work:

- Product and code identity: `Mixtormat`.
- Plugin-owned packages: `/Mixtormat/...`.
- Project-owned user content: `/Game/Mixtormat/...`.
- C++ types and modules: existing `Mixtormat` names.
- Shader files: existing `Mixtormat*.usf` / `.ush` names.
- Unreal assets: normal type prefixes, with no `MaterialLab`, `MatLab`, or `ML` brand prefix.

Suggested active asset names:

- `M_Mixtormat_Substrate`.
- `MI_Mixtormat_StudioFloor`.
- `SM_Mixtormat_Sphere`.
- `SM_Mixtormat_Plane`.
- `SM_Mixtormat_Cube`.

Surface names should describe the surface, not repeat the plugin brand. Prefer standard Unreal type prefixes over a new custom abbreviation.

## 5. Known active rename sites

The initial static inventory found active legacy identity in:

- `MaterialLab.uplugin` filename and plugin directory.
- `MixtormatShadersModule.cpp` plugin lookup and shader mapping.
- `MixtormatGpuCompositor.cpp` shader registrations.
- `MixtormatStyle.cpp` resource-root plugin lookup.
- `SMixtormatInternal.h` icon-root plugin lookup.
- `MixtormatRegistry.cpp` surface, mask, normal, and effect package roots.
- `MixtormatSurfaceImporter.cpp` plugin lookup, destinations, and master paths.
- `MixtormatBakeService.cpp` master material path.
- `SMixtormatPreviewViewport.cpp` material and mesh paths.
- `SMixtormat_Preview.cpp` lighting package root.
- `SMixtormat_Document.cpp` default save path and `Untitled MatLab Material` text.
- `MixtormatLiveTheme.cpp` saved theme folder and messages.
- Active documentation that describes old paths as current.

No active `FMaterialLab*`, `UMaterialLab*`, `SMaterialLab*`, or `EMaterialLab*` C++ symbols were found. C++ class renaming is not needed.

## 6. Phase I1 — Surface inventory and migration manifest

Status: **Filesystem inventory complete; live registry metadata pending**

Static manifest: [`Mixtormat_Surface_Migration_Manifest.md`](Mixtormat_Surface_Migration_Manifest.md)

### Goal

Record exactly what must survive before changing package identity.

### Work

1. Enumerate all current surface assets under `/MaterialLab/Surfaces`.
2. Record for each surface:
   - Object path.
   - Display name.
   - Family, subtype, and finish.
   - Base Color texture.
   - Normal texture.
   - RAM/RAMH texture.
   - Preview material.
3. Record the master material, preview meshes, lighting assets, masks, and normals.
4. Confirm source texture files used by `FMixtormatSurfaceImporter` still exist.
5. Record counts by asset type and family.
6. Create a user-controlled backup/checkpoint before any rename operation.

### Exit condition

The manifest is complete enough to compare old and new surface libraries one-to-one.

## 7. Phase I2 — Centralize identity paths while behavior is unchanged

Status: **Implemented for editor-side paths**

Implementation:

- `Source/MixtormatEditor/Private/Services/MixtormatPaths.h`
- `Source/MixtormatEditor/Private/Services/MixtormatPaths.cpp`

Shader module mapping remains intentionally deferred to atomic Phase I5.

### Goal

Remove scattered literals before changing their values.

### Work

1. Add a focused editor-side path service, for example `FMixtormatPaths`.
2. Centralize:
   - Plugin lookup name.
   - Plugin content mount.
   - Surface, texture, mask, normal, effect, lighting, material, and mesh roots.
   - Project-owned import/save roots.
   - Master material and preview asset paths.
   - Saved theme directory.
3. Resolve the mounted asset root from the plugin where the engine-version API supports it.
4. Keep current `MaterialLab` values during this phase.
5. Keep shader virtual-root handling separate because shader registration requires an atomic compile-time mapping change.
6. Replace active hard-coded path call sites with the service.

### Exit condition

Runtime behavior and asset paths are unchanged, but identity values have one source of truth.

## 8. Phase I3 — Low-risk visible naming

Status: **Implemented**

Notes:

- New recipes default to `/Game/Mixtormat/Materials`.
- New recipe names and default asset names use `Mixtormat`.
- Live themes now save to `Saved/Mixtormat/LiveTheme.json`.
- No legacy theme fallback or copy was added.

### Goal

Remove old branding that does not affect plugin package identity.

### Work

1. Change `Untitled MatLab Material` to `Untitled Mixtormat Material`.
2. Change the default save destination to `/Game/Mixtormat/Materials`.
3. Change `Saved/MaterialLab/LiveTheme.json` to `Saved/Mixtormat/LiveTheme.json`.
4. Update active error messages and tooltips.
5. If theme preservation is desired, perform a one-time copy only when the new file is absent.
6. Do not add a permanent dual-path fallback without approval.

### Exit condition

New user-owned files and visible UI text use `Mixtormat` only.

## 9. Phase I4 — Rename plugin assets inside Unreal

Status: **Tool implemented; not yet built or executed**

Implementation:

- `Source/MixtormatEditor/Private/Services/MixtormatAssetMigration.h`
- `Source/MixtormatEditor/Private/Services/MixtormatAssetMigration.cpp`
- Console command: `Mixtormat.MigrateAssets`
- Apply command: `Mixtormat.MigrateAssets Apply`
- No argument always performs a non-destructive dry run.
- The tool uses Unreal `AssetTools`; it never filesystem-renames `.uasset` files.
- Redirectors are retained until separately approved cleanup.

Planned conventions:

- Every discovered `UMixtormatSurface` named `ML_*` becomes `DA_*`.
- Existing `DA_*` surfaces are accepted and skipped.
- New or later-added surfaces are included automatically.
- Preview instances remain descriptive `MI_*`; they have no old brand prefix.
- `M_MaterialLab_*`, `MF_MaterialLab_*`, `MI_ML_*`, `T_ML_*`, and
  `SM_MaterialLab_*` support assets receive explicit Mixtormat names.
- `MLFX_Peeling_Standard_01` becomes `DA_Peeling_Standard_01`.
- The obsolete stain asset becomes `DA_Stain`; it is preserved, not deleted.

### Goal

Remove old names from valuable plugin assets before changing the plugin mount.

### Work

Use Unreal Content Browser or AssetTools operations, never filesystem renames for `.uasset` files.

1. Rename the master material to `M_Mixtormat_Substrate`.
2. Rename preview meshes and studio material to the target names.
3. Rename other active assets containing `MaterialLab`, `MatLab`, or obsolete `ML` branding.
4. Preserve descriptive imported surface names.
5. Update importer-generated naming rules so reimport does not recreate old names.
6. Run the command without arguments and review every planned source/target pair.
7. Run the command with `Apply` only when the dry run has no errors.
8. Recount surfaces and verify all renamed targets before any redirector cleanup.
9. Fix redirectors while the old `/MaterialLab` mount is still valid, after approval.
10. Resave surface assets and preview material instances so references use renamed dependencies.
11. Compare the surface inventory against Phase I1.

### Exit condition

All valuable surfaces still resolve every texture and preview dependency under the existing mount.

## 10. Phase I5 — Atomic plugin and shader identity rename

### Goal

Switch the plugin mount and shader namespace together.

This phase requires explicit approval because it includes a physical directory/descriptor rename and broad path changes.

### Atomic change set

1. Close Unreal Editor before filesystem-level plugin rename work.
2. Rename:
   - `Plugins/MaterialLab` → `Plugins/Mixtormat`.
   - `MaterialLab.uplugin` → `Mixtormat.uplugin`.
3. Change all plugin lookups to `FindPlugin("Mixtormat")`.
4. Change the centralized content mount to `/Mixtormat`.
5. Change shader mapping:
   - `/Plugin/MaterialLab` → `/Plugin/Mixtormat`.
6. Change every `IMPLEMENT_GLOBAL_SHADER` virtual path in the same edit.
7. Change registry and importer roots to `/Mixtormat/...`.
8. Change master, preview mesh, lighting, and material paths to `/Mixtormat/...`.
9. Add temporary Unreal package redirects from `/MaterialLab/...` to `/Mixtormat/...` for plugin-owned assets only.
10. Do not add redirects for discarded composed materials under `/Game/MaterialLab/Materials`.

### Exit condition

The plugin loads as `Mixtormat`, shaders resolve from the new virtual root, and plugin content mounts at `/Mixtormat`.

## 11. Phase I6 — Surface relink and resave

### Goal

Rewrite valuable surface references to the new package mount.

### Work

1. Open the editor with temporary package redirects active.
2. Load every `UMixtormatSurface` from the manifest.
3. Confirm all BC, Normal, RAM/RAMH, and preview material references resolve.
4. Resave surfaces and their preview material instances under the new mount.
5. Confirm `FMixtormatRegistry::GetSurfaces()` finds the full library under `/Mixtormat/Surfaces`.
6. Confirm masks, normals, effects, lighting, master material, and preview meshes are found.
7. Do not automatically reimport or regenerate a missing surface.
8. If relinking fails, stop and inspect before using the importer; surface regeneration requires explicit approval.

### Exit condition

Every surface from the original manifest exists under `/Mixtormat`, with equivalent metadata and resolved dependencies.

## 12. Phase I7 — Importer normalization

### Goal

Ensure future surface imports create only Mixtormat-named content.

### Work

1. Update `FMixtormatSurfaceImporter::GetPluginTexturesRoot()` to find `Mixtormat`.
2. Update generated destinations:
   - `/Mixtormat/Textures/...`.
   - `/Mixtormat/Surfaces/...`.
   - `/Mixtormat/Materials/Instances/...`.
   - `/Mixtormat/Masks`.
   - `/Mixtormat/Normals/...`.
3. Update master material lookup.
4. Replace old `ML_` stripping/generation assumptions with the agreed neutral asset naming rule.
5. Preserve family grouping and all existing surface metadata.
6. Ensure reimport updates an existing Mixtormat surface rather than creating a duplicate.

### Exit condition

A controlled surface import/reimport creates no active `MaterialLab`, `MatLab`, or obsolete brand-prefixed package names.

## 13. Phase I8 — Compatibility cleanup

### Goal

Remove migration-only support after all valuable plugin content is canonical.

### Work

1. Search active source/config for remaining old names.
2. Allow only intentional temporary package redirects during the migration window.
3. Resave all migrated plugin assets.
4. Confirm no surface references resolve through a redirect.
5. Remove package redirects only after a separate verified release/checkpoint.
6. Leave archived historical documents unchanged or label them historical.
7. Update active plans and release documentation to use current paths.
8. Review old composed materials separately before any deletion.

### Exit condition

`Mixtormat` is the only active identity and valuable surfaces no longer depend on redirects.

## 14. Validation matrix

Run build/editor validation only with explicit authorization.

### Static checks

- No active `FindPlugin("MaterialLab")` calls.
- No active `/Plugin/MaterialLab` shader paths.
- No active `/MaterialLab/...` paths outside temporary redirects.
- No new `/Game/MaterialLab/...` defaults.
- No active `MaterialLab` / `MatLab` user-facing strings.
- Module and class names remain `Mixtormat*`.

### Plugin checks

- Unreal discovers `Mixtormat.uplugin` once.
- All three existing modules load.
- Resources and SVG icons resolve.
- Every global shader compiles from `/Plugin/Mixtormat`.

### Surface checks

- Surface count matches the Phase I1 manifest.
- Family/subtype/finish metadata matches.
- Every BC, Normal, RAM/RAMH, and preview material reference resolves.
- Surface thumbnails and previews render.
- Surface add/replace actions work.
- Controlled reimport updates the expected surface.

### Editor checks

- Preview sphere, plane, and cube load.
- Studio floor and HDRI library load.
- Bake service finds the renamed master material.
- New composed documents default to `/Game/Mixtormat/Materials`.
- Live theme saves under `Saved/Mixtormat`.

Composed-material compatibility is explicitly excluded.

## 15. Rollback rules

- Stop immediately if surface counts or references diverge.
- Restore the user-controlled checkpoint rather than creating dual plugin identities.
- Do not keep both `/MaterialLab` and `/Mixtormat` as permanent content roots.
- Do not add silent asset lookup fallbacks.
- Do not regenerate missing surfaces until their original metadata and sources are confirmed.

## 16. Recommended delivery units

1. I1–I2: inventory and path centralization.
2. I3: visible naming and new project-owned defaults.
3. I4: Unreal asset renames and surface verification.
4. I5: atomic plugin/mount/shader rename.
5. I6–I7: surface relink, resave, and importer normalization.
6. I8: redirect retirement and documentation cleanup.

Do not combine this migration with Pattern, compositor behavior, or unrelated UI work.
