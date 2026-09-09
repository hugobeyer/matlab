# Mixtormat — Surface Migration Manifest

Status: **Filesystem baseline captured; live metadata verification pending**

Created for Phase I1 of [`Mixtormat_Identity_and_Content_Migration_Plan.md`](Mixtormat_Identity_and_Content_Migration_Plan.md).

## Filesystem baseline

PowerShell inspection on the real plugin directory found:

| Asset/source type | Count |
|---|---:|
| All `.uasset` files | 244 |
| Surface assets | 34 |
| Surface preview instances | 34 |
| Texture `.uasset` files | 120 |
| Source PNG files | 153 |
| Mask assets | 35 |
| Effect assets | 2 |
| Lighting assets | 6 |
| Preview meshes | 3 |
| Source FBX files | 3 |
| Source HDR files | 6 |

Current surface families:

| Family | Surfaces |
|---|---:|
| Bricks | 2 |
| Concrete | 4 |
| Marble | 3 |
| Metal | 11 |
| Mold | 1 |
| Moss | 1 |
| Plaster | 3 |
| Rust | 3 |
| Stone | 4 |
| Wood | 2 |
| **Total** | **34** |

This is a migration baseline, not an allow-list. Surfaces added later must be discovered from the registry root and imported directly into the canonical Mixtormat paths.

## Required live inventory

For every `UMixtormatSurface` under `/MaterialLab/Surfaces`, record:

| Old object path | Display name | Family | Subtype | Finish | Base Color | Normal | RAM/RAMH | Preview material | Status |
|---|---|---|---|---|---|---|---|---|---|
| 34 filesystem assets found; rows pending live registry export | | | | | | | | | Pending |

## Shared dependencies

| Type | Expected current object | Resolved | Notes |
|---|---|---|---|
| Master material | `/MaterialLab/Materials/M_MaterialLab_Substrate` | File present | Internal refs pending |
| Studio floor | `/MaterialLab/Materials/MI_ML_Studio_Floor` | File present | Internal refs pending |
| Sphere mesh | `/MaterialLab/Meshes/SM_MaterialLab_Sphere` | File present | |
| Plane mesh | `/MaterialLab/Meshes/SM_MaterialLab_Plane` | File present | |
| Cube mesh | `/MaterialLab/Meshes/SM_MaterialLab_Cube` | File present | |
| Lighting root | `/MaterialLab/Lighting` | 6 files present | |
| Source textures | `Plugins/MaterialLab/Content/Textures` | 153 PNGs present | Import source root |

## Exit requirement

The filesystem half of I1 is complete. Before Phase I4, export or inspect the live Unreal registry metadata and confirm all 34 current surfaces resolve their textures and preview instances. Repeat the baseline immediately before migration so later-added surfaces are included.
