# Rock Formation — shape controls rework (plan)

Status: plan only. Legacy look is not preserved. Shader: `Shaders/Private/MixtormatRockFormation.usf`;
passes: `AddRockFormationPasses` in `MixtormatGpuGeneratorPasses.cpp`; struct `FMixtormatRockFormation`.

## 1. What is wrong today

| Control | What it really does | Why it looks wrong |
| --- | --- | --- |
| **Tilt / Tilt Random** | Rigid in-plane spin of each finished chunk about its own centroid (`RockBlockTransform` → `CosA/SinA`, applied in the field stage). | Chunks are packed first, then spun independently: neighbours overlap in places, holes open in others. And it is a spin around the vertical axis, not a tilt. |
| **Warp** | Rigid sideways drift of each finished chunk by low-frequency noise (`Mx/My`). | Same: overlaps and holes. |
| **Slope** | Leans the chunk top plane (`P.dip` fixed downward + random directions). Walls follow because they are derived from the top. | Works, but its direction is fixed (downward) and it is the only real tilt. |

Rule for the fix: **nothing moves a chunk after packing.** Every control either reshapes the packing
itself, reshapes a chunk *inside its own footprint* (the way gap and chamfer do), or changes height.

## 2. New controls

| Control | Params | How | Keeps packing? |
| --- | --- | --- | --- |
| **Size Random** | amount | Per-cell weight → power (Laguerre) diagram: each bisector shifts by the weight difference. Big and small cells, still a partition, still tileable (weights hashed on wrapped cell ids). | Yes — it *is* the packing. |
| **Stretch** | amount, angle | Global anisotropic metric along an authored direction (generalizes today's strata squash `a2`, which is vertical only). | Yes. |
| **Stretch Random** | amount | Per chunk: scale the leaf polygon about its centroid along a random axis, then clip it back to its own leaf. Shrink-only in effect: it narrows, never grows into a neighbour. | Yes. |
| **Spin** (was Tilt) | angle, random | Per chunk: rotate the leaf polygon about its centroid, then clip it back to its own leaf — corners trim like a chamfer. Gaps and chamfers are computed after, on the spun shape. | Yes. |
| **Tilt** (new, real) | angle (lean), direction, random | Height: adds a lean plane to the chunk top (`Sx, Sy`); walls and chamfers lean with it because they derive from the top. Replaces Slope's fixed downward dip with an authored direction. | Yes (height only). |
| **Warp** | amount, scale | Domain warp of the pixel coordinate before the cell lookup (tileable noise, integer period). Bends the whole pattern consistently; edges curve, nothing overlaps. Replaces rigid `Mx/My`. | Yes. |

Slope stays as the per-chunk *random* lean (roughness of the top), Tilt is the authored, directional lean.

## 3. Shader changes

- Build stage (`ROCK_STAGE 0`):
  - `RockSite` gains a per-cell weight; `RockBisector` takes both weights (power-diagram offset).
  - Metric becomes `Stretch` along an angle (rotate `d` into the stretch frame, then the existing
    `x² + a²·y²`).
  - After the BSP leaf is known: **Stretch Random** and **Spin** transform the leaf polygon about its
    centroid, then `RockClip` it against the original leaf's edges (keeps crack/cell tags on the
    clipped edges so gap/seam logic still applies).
  - Tilt adds to `s` (top slope) with an authored direction; Slope keeps its random part.
  - `CosA/SinA`, `Mx/My` are removed from `FRockLeaf` (and from the C++ stride mirror `FRockLeafStride`).
- Field stage (`ROCK_STAGE 1`): warp `p` (tileable) before `ci/cj` and before the per-leaf test; no per-leaf
  rigid transform.
- Search reach: today widened by `1.2·|warp|`; becomes the domain-warp displacement bound.

## 4. C++ / UI

- `FMixtormatRockFormation`: remove `RockTilt`, `RockTiltRandom`, `RockWarp` semantics; add
  `RockSizeRandom`, `RockStretch`, `RockStretchAngle`, `RockStretchRandom`, `RockSpin`, `RockSpinRandom`,
  `RockTilt` (lean deg), `RockTiltDirection`, `RockTiltRandom`, `RockWarp`, `RockWarpScale`.
- `FRockFormationRenderData`, gather, `FillParameters`, shader params; all shape params stay in the field
  hash (they change the cached field).
- Inspector sections: **Pattern** (Cells, Rows, Skew, Size Random, Stretch, Stretch Angle, Warp, Warp Scale),
  **Chunks** (Fracture, Spin, Spin Random, Stretch Random, Gap, Chamfer…), **Height** (Tilt, Tilt Direction,
  Tilt Random, Slope, Clusters, Normalize, Remap, Scale, Blend).
- Authoring defaults JSON for the new fields.

## 5. Phases

1. Remove rigid Warp/Tilt; add Spin (clip-to-leaf) and domain Warp. Biggest visual fix.
2. Tilt (directional lean) + Stretch / Stretch Angle.
3. Size Random (power diagram) + Stretch Random.
4. UI regroup, defaults, doc update.

## 6. Open questions

1. Spin/Stretch Random trim corners (shape stays inside its leaf). Acceptable, or should chunks be
   allowed to grow and push neighbours (would need a relaxation pass — much more expensive)?
2. Tilt direction: one global direction + random spread (planned), or per-row?
3. Keep Slope, or fold it into Tilt Random?

## 7. Verification (Hugo)

- Shader + C++ build. Rock field caches and re-evaluates only when a shape param changes.
- Spin / Warp: no overlaps, no holes, gaps stay even; tiles at 1K and 4K.
- Size Random: mixed sizes, still seamless.
- Stretch + angle: elongated chunks along the angle; Stretch Random narrows individual chunks.
- Tilt: whole chunks lean in the chosen direction; walls/chamfers follow; Normalize keeps 0..1.
- Boundary field (`OutRockBoundaryField`) and flow tools still line up with the new outlines.
