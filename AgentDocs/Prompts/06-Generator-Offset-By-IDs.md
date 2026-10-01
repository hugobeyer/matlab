# Step 6: offset Strata and Cracks per region from IDs below

Paste `00-Shared-Rules.md` above this. Prerequisites: Steps 1–3 (Generator layers, and the cross-layer ID source picker from 3b) are merged.

## Goal
A Strata or Cracks generator can read a Region ID map from below (for example a Rock or Lattice layer's IDs, chosen with the 3b **ID Source** picker). Each region then gets its own random offset of the pattern. The result: every stone or tile has its own strata or crack layout, and the pattern breaks at region borders instead of running straight across them.

## Behaviour
- New generator properties, on both Strata and Cracks:
  - `IdSource`: the same picker type as the other ID consumers. "None" by default.
  - `IdOffset`: 0..1. How far each region shifts the pattern, as a share of the pattern's own period (a bed for Strata, a cell for Cracks).
  - `IdRotation`: 0..1, the share of a full random rotation per region. Optional. For Strata, rotation must snap to the tileable lattice directions; for Cracks, any angle is fine inside a region. If this complicates tiling, skip it and say why.
  - `IdSeed`: int, reshuffles the draws.
- Per pixel: read the region ID and, if it is valid, hash it (with `MixtormatRegionSeed` / `MixtormatRegionRandomSalted` in `MixtormatRegionId.ush`) into offset draws.
  - **Strata:** add the draw to the bedding coordinate `S`, as `IdOffset * draw * <beds>`. Beds stay continuous inside a region and jump at its border.
  - **Cracks:** offset the cell lattice lookup position (the domain the Voronoi/crack field is evaluated in) by the draw, in cell units.
- An invalid ID (`MIXTORMAT_INVALID_REGION`) means no offset.
- At `IdOffset` 0, or with no source, the shader must be **bit-identical** to having no ID input. Branch on it; don't multiply by zero (Strata's existing `IDInfluence` does the same).
- **Tiling:** IDs are already tileable, and a constant offset inside a region keeps it tileable.
- **Region borders:** the jump at a border is wanted (it reads as separate stones). Optionally add a small crack or groove at the border later; out of scope here.

## Clean-up
- Strata's existing `IDInfluence` (a per-region depth scale, nearest-above IDs) moves onto the same `IdSource` picker.
- Keep it as "ID Depth Variation" if still useful. Otherwise delete it. Report the decision.

## Touch points
- Generator structs in `MixtormatMaterial.h`, the render data, the gather and the pass parameters (`MixtormatGpuGeneratorPasses.cpp`).
- `MixtormatStrataCarver.usf` and `MixtormatCracks.usf`.
- The inspector: an "IDs" caption group in each generator panel.
- `Docs/index.html`.
- **Node cache:** an ID-driven generator depends on the layer below, so the Cracks cache key must include the ID source texture's identity, or the cache must be skipped when an ID source is set. Report which.

## Static checks
- dxc for both shaders at both HV versions.
- Compare each shader's globals against its `SHADER_PARAMETER` list.

## Checklist for Hugo
- A Rock or Lattice layer, with a Strata layer above it picking the Rock IDs:
  - Each stone gets its own bed offset.
  - The beds jump at stone borders.
  - Offset at 0 is identical to having no IDs.
- Cracks over Lattice IDs: each tile has its own crack layout.
- Everything stays seamless.
