# Boolean Ops — candidate modules and feasibility

Status: **ideation only.** Nothing here is implemented except where a row says otherwise.
This list previously existed only in conversation; it is written down here so it stops
being lost. No module below is committed, scheduled, or promised.

Read `AgentDocs/BOOLEAN_STACK.md` first for the accepted Boolean architecture, and
`AgentDocs/GENERATORS.md` > *Boolean Ops — Scatter Carve and field combining* for what
the retired four-mode generator did.

---

## 1. What exists today

| Thing | Where | State |
|---|---|---|
| Boolean **group container** (`EMixtormatLayerChildType::Boolean`) | `MixtormatBooleanStack.usf`, `AddBooleanStackPasses` | Implemented. Owns up to two scoped operands; `Self` when one. Hard max/min union+intersect, one-sided relief subtract, `Round`/`Bevel` joins. |
| **Scatter Carve** finite cutters | `MixtormatBooleanCarve.usf`, `AddBooleanCarvePasses` | Implemented. Ellipsoid / polygon-prism cutters, subtract or union, root-refined chamfer. Now the only card set in its inspector. |
| Chamfer prototype chain | `AgentDocs/Prototypes/ScatterCarve/chamfer_*.cl` | Prototype only. Two branches: older **Eikonal** (`chamfer_eikonal_step` + `chamfer_resolve`) and newer **Envelope** (`chamfer_envelope_init` / `_step` / `_resolve`). |
| Heightfield `Round` / `Bevel` joins | `MixtormatBooleanStack.usf` (`SmoothMax`, `BevelMax`, `CutDepth`) | Implemented, but these are **signed-height joins**, not metric fillets. |

**Known gaps in Scatter Carve:** no combined-surface SDF, no IDs, no native Flow.
`geometry_mask` is only the cutter footprint, not a coverage field for the combined surface.

---

## 2. Hard limits of the heightfield model

These bound every row in §3. They are not implementation shortfalls; they are the model.

- **No true 3D undercuts or overhangs.** A heightfield is single-valued. Subtraction is
  one-sided relief removal, never a solid difference.
- **Signed height is not a metric SDF.** `max`/`min` on signed height is a *surface contest*,
  not a Euclidean boolean. Do not feed ordinary signed height to a metric SDF visualizer.
- **No constant-radius fillet in world units** without a real distance field.
- **No mesh input, no arbitrary mesh projection.**
- **No combined-surface SDF** from the carve path, so downstream distance-based tools cannot
  consume a carved result as a solid.

Anything that genuinely needs a metric solid must be a **separate field kind and pipeline**,
not a new mode on the existing compositor.

---

## 3. Candidate modules

"Belongs as" is the honest classification: several of these are shaping or propagation tools
that were filed under Boolean because they were discussed alongside it.

| # | Module | What it would do | Verdict | Belongs as | Precedent already in repo |
|---|---|---|---|---|---|
| 1 | General SDF Union / Subtract / Intersection | Metric booleans on a real distance field | **Already covered** for heightfields by the Boolean group. A *metric* version is a separate project. | Boolean (heightfield) / new pipeline (metric) | `MixtormatBooleanStack.usf` |
| 2 | Rounded / Chamfered SDF Booleans | Constant-radius fillet at the join | **Partially exists.** Heightfield `Round`/`Bevel` + Scatter Carve chamfer are done; a metric fillet needs #1's metric path. | Boolean | `SmoothMax`/`BevelMax`; `chamfer_*.cl` |
| 3 | Morphological Dilation / Erosion | Grow / shrink coverage by a radius | **Feasible, cheap.** Separable min/max, or distance-transform based. | Mask / field shaping | `EdgeClose` (dilate then erode) in `FMixtormatClusterFilter`; JFA set in `MixtormatGpuPatternPasses.cpp` |
| 4 | Opening / Closing | Erode→dilate / dilate→erode | **Feasible, cheap.** Composition of #3; already proven once. | Mask / field shaping | `EdgeClose` in Surface IDs |
| 5 | Directional Morphology | Min/max along a line, not isotropic | **Feasible.** Needs a direction source; Flow is the natural one. | Mask / field shaping | Erosion's directional transport; `MixtormatFlowTrace.usf` |
| 6 | Distance-Transform Beveling | Build a bevel band from a distance transform | **Feasible.** Essentially what the Scatter Carve envelope chain already does in prototype form. | Boolean / shaping | `MixtormatEikonal.ush`; `CraquelureDistance.usf`; `chamfer_envelope_*.cl` |
| 7 | Curvature-Driven Smoothing | Smooth where curvature is high (or low), leave flats alone | **Feasible.** A curvature-gated blur is a small pass. | Mask / height shaping | `MixtormatCurvature.ush`; wrinkling prototypes' curvature-gated diffusion |
| 8 | Voronoi / Power-Diagram Fracture | Cell lattice with per-cell weights, as a standalone producer | **Largely exists** inside generators; exposing it standalone is the work. | Generator / ID producer | Rock Formation power-diagram BSP; Cliff Strata 3D Voronoi; Craquelure cell walls |
| 9 | Domain-Warped Lamination | Layered slabs in a warped coordinate space | **Largely exists** as Strata / Cliff Strata internals. | Generator | Strata Carver bedding + folds; Cliff Strata block/row lamination; `MixtormatGeneratorWarp.ush` |
| 10 | Anisotropic Eikonal Propagation | Eikonal with a direction-dependent speed | **Feasible.** Extend the existing eikonal with a directional speed field. | Propagation | `MixtormatEikonal.ush`; `feather_eikonal_directed.cl` prototype |

---

## 4. Suggested phasing

Ordered by cost, not by ambition. Each phase is independently useful.

**Phase A — mask/field modifiers (cheap, reuses existing scoped-child system)**
- #3 Morphological Dilation / Erosion
- #4 Opening / Closing
- #7 Curvature-Driven Smoothing

These fit the existing scoped-child pattern that `Blur` and `Curvature` already occupy under a
mask owner, so they need a child type, a gather case, one compute pass and an inspector panel —
not a new subsystem.

**Phase B — direction- and distance-driven shaping**
- #5 Directional Morphology (needs a direction source; Flow already exists)
- #6 Distance-Transform Beveling (reuses the eikonal / JFA machinery)

**Phase C — propagation**
- #10 Anisotropic Eikonal Propagation (extend `MixtormatEikonal.ush` with a directional speed)

**Phase D — separate project, not a mode**
- #1 metric SDF booleans and #2 metric fillets. New field kind, new pipeline, new previews,
  new publication contract. Do not attempt this by adding a mode to the heightfield compositor.

**No new work needed**
- #8 and #9 are already implemented inside generators. The only work is *exposing* them if a
  standalone node is genuinely wanted.

---

## 5. Explicit non-goals

- No metric SDF mode on the existing Boolean group.
- No mesh input or mesh projection in the carve path.
- No combined-surface SDF, IDs or Flow from Scatter Carve without a separate publication design.
- No new boolean *modes* on the retired `BooleanOps` generator; its field-combine members are
  already unreachable from authoring and are queued for deletion (see `UNIFORMITY_ROADMAP.md`,
  Chunk 1A).
- Do not reclassify #3–#7 as booleans merely because they were discussed here. They are shaping
  and propagation tools.
