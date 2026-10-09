# Generator Parameter Necessity Audit

Audit of every generator parameter's shader behavior, to decide which controls
must stay visible, which are conditional (only matter when another control is
active), which are live but unexposed, and which are vestigial.

- **Repo:** `C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat`
- **Status:** audit only — no code changed.
- **Method:** read of the five generator shaders + `MixtormatGeneratorGather.cpp`.
  No build run.

---

## 1. Summary

| Generator | Shown params | Conditional | Hidden-live | Vestigial |
|---|---|---|---|---|
| Strata Carver | 16 | 2 | 0 | 0 |
| Cracks | 23 | 4 | 0 | 0 |
| Rock Formation | 35 | 9 | 0 | 1 |
| Pebbles | 19 | 0 | 0 | 0 |
| Cliff Strata | 48 | 12 | 4 | 0 |

No parameter is behaviorally duplicate. Nothing is safe to cut on behavior
grounds; the actionable items are conditional gating, the four hidden-live
Cliff toggles, and one deprecated field.

---

## 2. Strata Carver — `MixtormatStrataCarver.usf`

Core (always meaningful): Seed, Size (StrataFrequency), Depth, Direction,
Thickness Var, Height Var, Verticality, Ramp Shape, Bend, Breakup,
Height Follow, Lamination.

Conditional:
- **Bend Scale** — only when Bend > 0 (L145–149: period of the bend noise).
- **Cross Bedding** — only when Lamination > 0 (L213–223: shifts lamina
  phase; grooves are scaled by Lamination, so it is invisible at 0).

Notes: Depth (L245–249) is the relief scale; StrataFrequency is beds per tile.
Distinct.

---

## 3. Cracks — `MixtormatCracks.usf`

Core: Cells, Seed, Jitter, Width, Depth, Rough, Scale, Feather, Per Crack,
Regional, Along, Chips, Floors, Slip, Tilt, Chamfer Amount.

Conditional:
- **Chip Size** — only when Chips > 0 (L449–455).
- **Floor Width** — only when Flat Floors > 0 (`rand < Gap` gate, L452).
- **Along Scale** — only when Along > 0 (L427–432: period of the wobble).
- **Chamfer Edge** — only when Chamfer Amount ≠ 0 (L511–535).

Notes: Detail is a refinement of Rough (adds octaves), not conditional.

---

## 4. Rock Formation — `MixtormatRockFormation.usf`

Core: Style, Cells, Rows, Seed, Fracture, Height Bias, Gap, Size Random,
Stretch, Stretch Random, Height Clusters, Skew, Spin, Spin Random, Tilt Angle,
Tilt Random, Edge Jag, Jag Scale, Rim Chips, Facet Chips, Depth Min/Max,
Scale, Normalize.

Conditional:
- **Chamfer Random, Chamfer Jag** — only when Chamfer ≠ 0 (RockEdgeLine
  L489–511: no bevel, no width to vary).
- **Jag Detail** — only when Edge Jag > 0 (`jag_amp == 0` short-circuits,
  L255–258).
- **Rim Chip Size** — only when Rim Chips > 0 (L273–303).
- **Facet Iterations, Falloff, Random, Align** — only when Facet Chips ≠ 0
  (`ChipOn = chip > 0`, L606; cut skipped at L755–759).
- **Facet Align** — additionally needs a lean (`leanLength > 1e-6` guard,
  L709).
- **Stretch Angle** — only when Stretch ≠ 0 (metric is isotropic at 0,
  L893–905).
- **Tilt Direction** — only when Tilt Angle ≠ 0 (`s = dir*tan(angle) + …`,
  L581–584).

Notes: Fracture Height Bias applies even with 0 splits (L569), so it is not
conditional. Spin Random adds to Spin; independent.

Vestigial:
- **RockHeightMode** — deprecated, never read, not shown; kept only so old
  assets load (header comment). Remove only with an asset-migration pass.

---

## 5. Pebbles — `MixtormatPebbles.usf`

Core: Cells, Seed, Density, Jitter, Size, Size Var, Rotation, Cut Direction,
Cuts, Chamfer, Steepness, Steep Var, Facet Offset, Height, Height Var,
Facet IDs, Scale, Normalize.

No conditional parameters — every control feeds the stone shape directly
(L132–165 shape/height, L183–232 placement, L254–258 per-stone drop).

Notes: **Irregularity** is a convenience master (jitters radius, chamfer, cut
angle and cut count, L132–153 + `CutJitter = 6*Irregularity`, L187). It
overlaps in effect with Cuts/Chamfer/Steep Var but is not a duplicate of any
single control. Keep, but it is the one candidate for "advanced" grouping.

---

## 6. Cliff Strata — `MixtormatCliffStrata.usf`

Core: Count X/Y, Density, Size Min/Max, Aspect, Jitter, Flow Var,
Height Min/Max, Rotation, Lean X/Y, Formation Cells, Amount, Sides, Seed,
Yaw, Pitch, View Scale, Depth Min/Max, Unit Distance, ID Variation,
Carve Depth, Y Bias, Voronoi Cells, Chamfer Amount, Cavity Amount.

Conditional:
- **Steps** — only when Rotation ≠ 0 or Jitter > 0 (quantizes the per-cell
  angle, L99–107).
- **Quarter Y Count, Fill, Size, Height, Jitter X/Y** — only when
  `bQuarterCopies` (hidden, default on; `phases = QuarterCopies ? … : 1`).
- **Chamfer Width, Chamfer Voronoi** — only when Chamfer Amount ≠ 0
  (L139–146: `res = plain + (beveled-plain)*ChamferIntensity`).
- **Cavity Block, Row, Threshold, Gain** — only when Cavity Amount ≠ 0
  (L139–146: whole cavity block is gated).
- **Carve Voronoi** — only when Carve Depth ≠ 0 (L110–116: `carve_scale`
  multiplies CarveDepth).
- **Y Bias Voronoi, Negative Y Taper** — only when Y Bias ≠ 0 (both scale the
  vertical unit distance, L117–123).
- **Flow Voronoi** — only when Flow Var ≠ 0 (warp is driven by flow, L101).

Hidden-live (read by the shader, no UI anywhere):
- **bQuarterCopies** (default true) — gates the whole QUARTERS card.
- **bShapeRandom** (default false) — 4/6/8-sided stones.
- **bYBiasVoronoiInvert** (default false) — flips the Y pattern.
- **bReverse** (default false) — flips the sweep direction.

These are reachable behavior with no control. Decide per toggle: expose or
accept as fixed.

---

## 7. Cross-generator

- **Normalize + Scale** — necessary on all five; the shared signed pass is
  the only output convention.
- **Depth Min/Max** — exists on Rock and Cliff only. Strata, Cracks and
  Pebbles have no signed-depth remap. Consistency question, not necessity.
- **Conditional controls** — the Noise panel already disables rather than
  hides; the same treatment fits every conditional listed above.

---

## 8. Recommendations

1. Keep all Core controls visible.
2. Gate conditional controls (disable when their driver is off) instead of
   hiding them.
3. Decide the four hidden-live Cliff toggles: expose or fix.
4. Leave RockHeightMode until an asset-migration pass; it is inert.
5. No behavior-level cuts are justified by the shaders.
