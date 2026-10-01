# Step 8: Rock Formation upgrade (port the Copernicus proto)

Paste `00-Shared-Rules.md` above this. This **replaces the Rock part of Step 07**. Step 07 keeps only the shared edge-toolkit `.ush` and the Cracks side; Edge Push is dropped everywhere.
- Run it after Step 2 if Generator layers exist. Otherwise port into the current generator child.
- The math is the same either way.

## The spec is a working prototype
Hugo tuned it in Houdini Copernicus:

- **Target:** `AgentDocs/Prototypes/RockFormation_EdgeToolkit.opencl.txt`
- **Today's plugin:** `AgentDocs/Prototypes/RockFormation_Current.opencl.txt`. It is a 1:1 port of `Shaders/Private/MixtormatRockFormation.usf`.
- **Plugin code:**
  - `Shaders/Private/MixtormatRockFormation.usf`
  - `AddRockFormationPasses`, `ResolveRockLayout` and `FMixtormatRockFormationCS` in `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
  - `FMixtormatRockFormation` in `MixtormatMaterial.h`
  - the gather (`MixtormatGpuCompositor.cpp`)
  - `BuildRockFormationControls` in `SMixtormat_Inspector.cpp`

Diff the two proto files first; everything marked `NEW` is in scope. Port the math **exactly**: same hashes, salts, constants and ratios, translated from OpenCL to HLSL.

## The plugin's structure stays
- **Stage 0 (Build):** one thread per cell. Writes each leaf's data into structured buffers.
- **Stage 1 (Field):** per pixel. Reads the leaves of the nearby cells, and is node-cached.
- **Stage 2 (Combine):** writes the layer height.

The proto rebuilds chunks per pixel only because an OpenCL COP has no buffers. **Do not copy that.** Everything per-chunk goes into Stage 0 and the leaf buffers:
- `FRockLeaf` gains:
  - `ck` and `pk` (the cell and piece hashes)
  - `rv`
  - `JagOffset` and `JagSize`
  - `SeamBits`
  - the facet-plane data, or the inputs to draw it in Stage 1
- Edge tags need a per-edge `uint` buffer beside `Edges`.
- Stage 1 evaluates the leaves as the proto's `rk_leaf_height` / `rk_leaf_sd*` / `rk_facet_cut` do.

## Principles Hugo set (keep them everywhere)
- **Normalised parameters, no degrees.** Angles and amounts are 0..1 or -1..1; the full-scale values are named constants.
- **Relative, not absolute.** Sizes are relative to the cell or to each chunk, never to the tile or to pixels. The exception is a one-pixel additive floor, so things don't alias away.
- **Ratios, not controls.** Secondary behaviour follows the main controls through named constants instead of getting its own sliders. Examples: seams vs outline, piece vs rock variation.
- **Pieces follow their rock.** Split pieces take the rock's (cell's) random draws, plus a small share of their own (`RK_PIECE_LEAN`, `RK_PIECE_FACET`).
- **No clamps.** Use guards only for undefined math. Use eased curves (e.g. `x / (1 + x^4)^0.25`) where geometry must stay valid.
- **Remove whatever a better control replaces.**

## Changes, in order (each is visible in the proto)
1. **Remove:**
   - Warp and WarpScale
   - Slope
   - ChamferBias
   - the preset columns `dip`, `slope_rand` and `bottom`
   - Fix all preset indices.
2. **Normalised ranges:**
   - Spin ±1 = ±180°; SpinRandom 0..1 = 0..180°.
   - Tilt ±1 = ±45°; TiltDirection 0..1 = a full turn; TiltRandom 0..1 = 0..45°.
   - StretchAngle 0..1 = 0..180°.
   - Stretch ±1 = ×2 along / across (`exp2(-stretch)`).
   - Style 0..1 = preset table positions 1..2.5 (`RK_STYLE_FROM`/`TO`).
   - Gap 0..1, where 1 = twice the natural gap, plus a one-pixel floor when above 0.
3. **Tilt is the only lean:**
   - shared lean;
   - plus the cell's random lean;
   - plus `RK_PIECE_LEAN` × the piece's own.
4. **Spin fits, it does not clip.** Scale the turned chunk uniformly to fit its leaf, keeping its edge tags.
5. **Search reach follows the row height**, in both Build (Voronoi neighbours) and Field (chunk reach): `build_reach_*` and `field_reach_*`. The plugin's fixed ±2 caused hard seams at row borders when rows > cells (boulder style showed it most).
6. **Chamfer:**
   - Chamfer is 0..1, a share of each edge's room (edge to centre × 0.75).
   - Seams take `RK_SEAM_CHAMFER` (0.6) of it.
   - Chamfer Jag varies the bevel width along each rim, with its own key, and seams take ×1.5 of it.
7. **Edge Jag:**
   - zigzag in 3 octaves;
   - relative to each chunk (`RK_JAG_REF_RADIUS` 0.7);
   - a per-chunk offset along the edge;
   - seams get ×0.5 strength at ×2 bend frequency;
   - the outline SDF uses the half-plane form when jag is on;
   - the chunk radius grows by the jag.
8. **Rim Chips:** angular (an asymmetric triangle, with a flat floor on half of them), with the chamfer widened by the bite.
9. **Facet Chips:**
   - Up to `FacetIterations` rounds of 3, 5, 7… planes.
   - `FacetFalloff` scales each round; values above 1 grow it.
   - Bites eased so a plane never passes the centre; the facet jag is eased the same way.
   - Each break line carries jag at the round's size.
   - `FacetRandom` varies, per rock, the round count, the depth and the per-round falloff. Pieces deviate by `RK_PIECE_FACET`.
   - `FacetAlign` ±1 swings the planes toward the low side (+) or the high side (−) of each chunk's lean.
10. **Height mode:**
   - Raw.
   - Analytic 0..1 (the default), using the proto's `height_lo`/`height_hi`.
   - Measured, using the existing `FMixtormatFieldRangeCS` pass to normalise the field. The plugin can do this; the proto can't.

## Defaults (Hugo's tuned values; use these as the UPROPERTY defaults)
| Property | Value | | Property | Value |
|---|---|---|---|---|
| Style | 0.05 | | Edge Jag | 0.2 |
| Cells | 4 | | Jag Scale | 4 |
| Rows | 12 | | Jag Detail | 0.2 |
| Seed | 1 | | Chamfer Jag | 1 |
| Fracture | 1 | | Rim Chips | 0.1 |
| Chamfer | 0.1 | | Rim Chip Size | 0.075 |
| Fracture Height Bias | 0 | | Facet Chips | 0.5 |
| Gap | 1 | | Facet Iterations | 3 |
| Chamfer Random | 1 | | Facet Falloff | 4 |
| Spin / Spin Random | 0 / 0 | | Facet Random | 1 |
| Tilt Angle | 0.1 | | Facet Align | 0.75 |
| Tilt Direction | 0.75 | | Height Mode | Analytic |
| Tilt Random | 0.2 | | Height Scale | 1 |
| Size Random | 0 | | | |
| Stretch / Stretch Angle | 0.5 / 0.5 | | | |
| Stretch Random | 0 | | | |
| Height Clusters / Skew | 0.5 / 0.5 | | | |

## C++ and UI
- `FMixtormatRockFormation`:
  - Add, rename and remove properties to match. Unshipped: no redirects.
  - Use UPROPERTY UIMin/UIMax for the normalised ranges.
  - Height Mode is an enum.
- Update the gather, render data, pass parameters, `ResolveRockLayout` (only if it reads changed preset columns), and the node cache key (every field-affecting property).
- **Inspector groups:**
  - **SHAPE:** Style, Cells, Rows, Seed, Fracture, Fracture Height Bias, Size Random, Stretch ×3, Height Clusters, Skew
  - **TILT:** Angle, Direction, Random, Spin, Spin Random
  - **EDGES:** Gap, Chamfer, Chamfer Random, Chamfer Jag, Edge Jag, Jag Scale, Jag Detail, Rim Chips, Rim Chip Size
  - **FACETS:** Chips, Iterations, Falloff, Random, Align
  - **HEIGHT:** Mode, Scale
- Pair rows with `MixtormatRow::MakePair` the way the neighbouring panels do.
- Outputs and capabilities are unchanged (height, top/chamfer/wall masks and ramps, edge distance, IDs).
- `Docs/index.html`: rewrite the Rock Formation card.

## Static checks
- dxc for `MixtormatRockFormation.usf`, every `ROCK_STAGE` (0, 1, 2), at HV 2018 and HV 2021.
- Compare the shader globals against `SHADER_PARAMETER`, and the buffer struct layout between C++ and HLSL (`FRockLeaf` size and stride).
- Grep for zero hits of `WarpAmount`, `WarpScale`, `SlopeAmount`, `ChamferBias` and `bottom`.

## Checklist for Hugo
- With the defaults above, the plugin matches the Copernicus proto at the same seed.
- Rows 12 / Cells 4 shows no seams at row borders, in every style.
- Spin no longer carves corners.
- Facet Falloff 4 has no collapse at cell edges.
- Height Mode Analytic stays 0..1-ish while you tweak; Measured fills 0..1 exactly.
- Editing only Height Scale stays fast (the node cache).
