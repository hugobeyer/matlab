# Mixtormat — Surface Migration Manifest

Status: **I1 blocked — plugin `Content` directory is empty in this workspace**

Created for Phase I1 of [`Mixtormat_Identity_and_Content_Migration_Plan.md`](Mixtormat_Identity_and_Content_Migration_Plan.md).

## Workspace observation

- `MaterialLab/Content` currently contains no indexed files or folders.
- No `Content/Surfaces/**/*.uasset` files are available to inspect statically.
- No surface asset count or dependency list can be recorded from this checkout.
- Source code still expects imported assets under `/MaterialLab/...`.

Do not begin Phase I4 or the plugin mount rename until the live Unreal project surface registry is inventoried.

## Required live inventory

For every `UMixtormatSurface` under `/MaterialLab/Surfaces`, record:

| Old object path | Display name | Family | Subtype | Finish | Base Color | Normal | RAM/RAMH | Preview material | Status |
|---|---|---|---|---|---|---|---|---|---|
| _Pending live Unreal inventory_ | | | | | | | | | Pending |

## Shared dependencies

| Type | Expected current object | Resolved | Notes |
|---|---|---|---|
| Master material | `/MaterialLab/Materials/M_MaterialLab_Substrate` | Pending | Source reference only |
| Studio floor | `/MaterialLab/Materials/MI_ML_Studio_Floor` | Pending | Source reference only |
| Sphere mesh | `/MaterialLab/Meshes/SM_MaterialLab_Sphere` | Pending | Source reference only |
| Plane mesh | `/MaterialLab/Meshes/SM_MaterialLab_Plane` | Pending | Source reference only |
| Cube mesh | `/MaterialLab/Meshes/SM_MaterialLab_Cube` | Pending | Source reference only |
| Lighting root | `/MaterialLab/Lighting` | Pending | Source reference only |
| Source textures | `Plugins/MaterialLab/Content/Textures` | Missing here | Import source root |

## Exit requirement

I1 is complete only when the live surface count, metadata, texture references, and preview references are recorded and can be compared after migration.
