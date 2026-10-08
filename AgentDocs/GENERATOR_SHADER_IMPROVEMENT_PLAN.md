# Generator and Erosion Shader Improvement Plan

Status: **partially implemented; not a completion record**. Original date: 2026-10-07.
Source-status review: 2026-10-08. No tests, commands, builds, profiling, or visual comparisons run.

## 0. Current source status and cross-plan boundaries

The former blanket “not implemented” header conflicted with C1's own implemented status.
The following is based on targeted source reads, not runtime acceptance:

| Item | Current source evidence | Status |
|---|---|---|
| A1 Strata zero-work gates | `MixtormatStrataCarver.usf`: Bend, Lamination and Breakup blocks are conditional | Present in the redesigned shader; not evidence of old-output equivalence |
| A1 Erosion dependency gates | `MixtormatErosion.usf`: secondary/variation-dependent fields are gated | Present |
| A1 Cracks gates | `MixtormatCracks.usf`: push-band FBMs and Width/Region variation samples still execute without the proposed local guards | Incomplete |
| A1 Noise zero-amplitude octave exit | `MixtormatNoise.ush`: octave loops stop on invalid period, not zero amplitude | Not implemented as proposed |
| A2 uniform-scale distance acceleration | Pebbles and Rock shader paths still differentiate outlines with four extra samples | Not implemented as proposed |
| A3 Cliff sweep dispatch | Shader still uses 8×8 threads and serial row/column predicates; C++ dispatch has no dedicated 1D permutation | Not implemented as proposed |
| A3 Cliff endpoint | Stage 4 reads its last `OutSweep` row after a forward loop beginning at row 1; row 0 has no initial write in that stage. C++ binds a fresh SweepB texture without an explicit clear | Source-level initialization risk remains; do not describe it as fixed |
| A4 Rock facet prebuild | `RockFacetCut` still computes per-round plane work in the field path | Not implemented as proposed |
| A5 carve-depth state | `PreviousCarve`/`SeedCarve`, R32 carve ping-pong, retained centre and final height resolve exist; constant velocity textures are removed | Implemented, but numerical equivalence/performance is unconfirmed |
| B shared contracts | Typed Noise fields, completed bundle remap, structural-link validation and shared Noise mask production exist | Partial integration; not completion of every architectural proposal |
| C1 geological Strata | Ordered interfaces, hard shelves, soft recesses and joint slabs exist; inspector exposes their controls | Implemented as a replacement, not opt-in; faults/pinch-outs remain future |
| C2 weathering/water simulation | Existing Erosion remains directional carving; Gravity Flow is a generator deformation tool, not water/sediment transport | Proposed, not delivered by the Noise/Gravity work |
| C3 new generator algorithms | Proposed hierarchical fractures, packing, cliff faults and footprint filtering are not established by this review | Do not mark complete |

### Compatibility conflict to keep explicit

C1 says the Strata shader was replaced; §§2/5 originally require opt-in visual changes and
retaining old behavior. Those statements are not simultaneously satisfied by the current
replacement. `RampShape` is serialized/deprecated and no longer drives shader output; making
its old control visible would not restore the old algorithm. No legacy mode or migration was
added during this review. A future restoration/migration needs an explicit decision, not a
UI-only re-enable or a claim that the old appearance is preserved.

### Relationship UI and noise masks do not conflict with shader optimization

`code_docs/generator_relationship_ux_plan.md` remains an unimplemented authoring/display plan.
Its editor-only target-row projection must preserve the currently authored execution array and
runtime compatibility. That is not a ban on separately authorized shader improvements or
Noise mask routing; it prevents a UI refactor from silently changing execution semantics.

The implemented Noise mask source reuses raw field evaluation and converts only the consumer's
coverage view. It does not force raw Value/Gradient/Height into 0..1 and therefore preserves the
Track B field-contract separation. Its value-only shader permutation skips outputs only on the
inline-mask path; the generator still produces its existing fields. This is not C3 footprint
filtering and should not be reported as it.

All current generators now allow the four generator-owned flow tools, including Cliff Strata
and Noise. Their Height-only steering is a real missing-boundary-field constraint; Signed
Distance must remain discoverable but disabled with an explanation there. No arbitrary
per-generator menu whitelist should hide the tools again.

### UI availability rule for follow-up work

Supported authored operations stay accessible. Context-inapplicable or unsupported operations
must identify the missing contract/prerequisite; prefer disabled-with-reason over disappearance.
Preview-only outputs are not evidence of reusable publication. Retired no-reader parameters
need documented retirement, not fake enabled controls. Do not reduce output demand or remove
an authoring feature to make a shader optimization appear faster.

The older measurement/validation proposals below are not authorization to run tests, commands,
builds, captures, or diagnostics; current repository/user no-testing instructions take priority.

## 1. Scope and evidence

This plan records the generator performance review and proposed visual improvements,
with particular attention to replacing the repetitive ramp look of Strata Carver
and separating directional carving from weathering and water erosion.

Evidence is static shader inspection, not GPU profiling or rendered comparisons.
The relative priorities below are hypotheses; no timing or speedup is claimed.

Reviewed relevant sections of:
- `Shaders/Private/MixtormatStrataCarver.usf`
- `Shaders/Private/MixtormatCracks.usf`
- `Shaders/Private/MixtormatRockFormation.usf`
- `Shaders/Private/MixtormatPebbles.usf`
- `Shaders/Private/MixtormatCliffStrata.usf`
- `Shaders/Private/MixtormatNoise.usf` and `MixtormatNoise.ush`
- `Shaders/Private/MixtormatGeneratorPlacement.ush`
- `Shaders/Private/MixtormatErosion.usf`

Architecture guidance comes from `ARCHITECTURE.md`, `GENERATORS.md`, `SHADERS.md`,
and `CONVENTIONS.md`. C++, bindings, dispatch sizes, actual caching, composite,
generator flow, and height-module implementations were not audited. Line references
below identify the reviewed revision and may move.

## 2. Non-negotiable separation

| Track | Intent | Compatibility requirement |
|---|---|---|
| A: performance | Remove work without changing authored results | Preserve existing outputs and contracts |
| B: uniformity | Clarify ownership, units, and shared operations | Do not silently change seeds, math, or APIs |
| C: visual features | Improve structure and art direction | Explicit opt-in; preserve existing assets |

Do not change defaults, clamp authored ranges, lower resolution, reduce iterations,
or drop output fields as a performance shortcut. Do not replace the existing Erosion
or Strata behavior without approval. New modes/features require separate approval;
this document does not authorize an architectural rewrite or migration.

## 3. Track A: performance implementation candidates

### A1. Skip disabled procedural work first

Small shader-local changes; retain all output writes and exact zero semantics.

| Shader / location | Current work | Proposed change |
|---|---|---|
| Cracks, 406–408 | Two three-octave soft-cell FBMs even outside the push band | Evaluate only when `Band != 0` |
| Cracks, 428–434 | Width and region noise evaluated with zero variation | Guard each noise by its own nonzero coefficient |
| Cracks, `CrackSoftFbm` / `CrackJagShift` | Three octaves even when later amplitudes are zero | Stop only at exactly zero amplitude |
| StrataCarver, 148–150 | Two bend noise samples even at zero Bend | Guard bend field evaluation |
| StrataCarver, 189–190 | Chip noise even at zero Breakup | Guard chip evaluation |
| StrataCarver, 214–221 | Lamina construction even at zero Lamination | Guard lamina-only calculations |
| Noise.ush, FBM/Ridged/Billow | Remaining octaves run at zero amplitude | Break at exactly zero amplitude |
| Erosion, 264–284 | Secondary/variation fields evaluated when disabled | Gate by actual dependency, as detailed below |

Erosion dependency gates:
- `CellDepth` is needed by either secondary seeds or chipped-depth variation.
- AngularUV and its warp are needed whenever `CellDepth` or secondary noise is used.
- FlowNoise is needed only for secondary seeds.
- BroadNoise is needed only for nonzero Variation.
- Preserve horizon exposure and every published ridge/height output.

Prefer uniform branches for uniform controls. The Cracks band branch is spatially
varying: measure divergence against the work it saves. Check compiled results before
assuming the compiler was executing all apparent source work. Exact-zero guards are
not permission to discard small nonzero contributions or change NaN handling blindly.

### A2. Placement-related repeated distance evaluation

Evidence:
- Pebbles, 232–245: scaling adds four `PebbleStone` evaluations to the central one.
- RockFormation, 1108–1119: scaling adds four outline evaluations.
- Both weaken/disable candidate culling for generator-layer placement.

Implementation sequence:
1. Keep identity placement unchanged.
2. Prototype equal-magnitude uniform-scale distance conversion using `abs(scale)`.
3. Preserve zero-gradient and singular-scale behavior explicitly.
4. Compare against the current finite-difference implementation before promotion.
5. Consider analytic gradients for anisotropic scaling only as a separate change.
6. Restore culling only with a proven conservative bound in destination units.

Uniform-scale division is mathematically appropriate for a valid distance normal,
but current finite differences have zero-gradient and floating-point edge cases.
Do not claim bitwise equivalence in advance. Analytic gradients can alter ties,
corners, and jag discontinuities; they are not automatically behavior-preserving.
Unused height/facet outputs in Pebbles' derivative calls may already be eliminated
by the compiler; five calls does not necessarily mean five full roof calculations.

### A3. Cliff Strata: serial sweeps and candidate explosion

Evidence:
- MainCS uses 8x8 threads; stages 3/6 accept only `D.x == 0`, stages 4/7 only `D.y == 0`.
- At most eight lanes in each useful group perform a whole serial row/column sweep.
- Stage 0 quarter copies use `16 * QuarterYCount` phases, each visiting nine tile offsets.
- Each candidate then runs polygon intersection work.

Proposals, in order:
1. Inspect actual CPU dispatch before changing thread dimensions.
2. Give sweep stages dedicated 1D indexing and matching dispatch dimensions.
3. Preserve sweep order and recurrence in the first implementation.
4. Benchmark exact parallel min-plus scans for distance stages 6/7 separately.
5. Precompute invariant per-block geometry for stage 0 if repeated arithmetic dominates.
6. Tighten ray candidate bounds without excluding valid leaning/jittered blocks.

Do not apply a generic scan replacement to nonlinear carving stages 3/4. Even the
simple distance scans may change floating-point accumulation order. Account for
extra passes, scratch memory, and build cost before accepting a scan or prebuild.

Correctness prerequisite: stage 4 initializes `val` from `SweepIn[x,0]` but starts
forward writes at y=1. The backward pass reads `OutSweep[x,0]`; for height 1, the
endpoint read is also unwritten by this stage. Verify output initialization/aliasing
in dispatch before calling this a bug. If uninitialized, fix separately from perf.

### A4. Rock Formation: precompute invariant facet data

`RockFacetCut`, around 699–732, evaluates `r² + 2r` planes for r rounds per accepted
candidate/pixel, including hashes, directions, and nonlinear shaping.

- Reuse the existing Build stage for per-leaf plane coefficients where worthwhile.
- Keep position-dependent jag evaluation in Field.
- Calculate memory/stride growth and buffer traffic before adding storage.
- Preserve random draws, plane order, signed chips, IDs, and fracture behavior.
- Compare total cold-build + field cost, not just the faster field dispatch.

Pebbles could similarly prebuild stone/facet data, but it currently has no analogous
Build stage in the reviewed shader. That is a larger, separately approved proposal.

### A5. Erosion: reduce iteration bandwidth only after measurement

Each relaxation pixel samples eight neighbors from both SourceHeight and
PreviousHeight. Velocity is the same authored direction at every pixel (line 296).

- Candidate: propagate a scalar carve-depth field rather than two height fields.
- Preserve sampling semantics: interpolating stored carve depth can differ from
  subtracting two interpolated heights and clamping afterward.
- Candidate: avoid repeated invariant velocity writes if all consumers permit it.
- Neither change is shader-local: inspect dispatch, resources, and consumers first.
- Preserve masks, ridge output, resampling normals, deposit semantics, and old assets.

### A6. Defer micro-optimizations

Avoid blanket unrolling, half precision, texture-format changes, or hash replacement.
Profile register pressure and generated instructions first. Caching nine soft-cell
candidates in Cracks may save repeated hashes but increase registers/spills.
Existing analytic Noise gradients are preferable to extra finite-difference samples.

## 4. Track B: uniformity and architecture

### Preserve documented ownership

`Runtime ← Shaders ← Editor` remains the dependency direction.

- Runtime owns serialized settings, defaults, sanitization, and parameter identity.
- Shaders owns gather, GPU resources, procedural evaluation, and output publication.
- Editor owns controls and presentation; it must not become a second math/default owner.
- Generators rewrite input height; Erosion remains an effect over composited height.
- Shared height-derived normal passes remain normal owners.

Before a binding/API change, request permission to inspect the relevant C++ files.
Trace declaration → gather → dispatch/binding → shader → defaults/UI metadata.
The current shader-only review cannot establish resource lifetime or cache validity.

### Standardize contracts, not appearances

Document each published field's units, sign, range, coordinate space, neutral value,
and invalid-ID convention. Distinguish raw height, signed height delta, normalized
preview gates, source-space distances, destination-space distances, and gradients.
Do not force them all into 0..1 or normalize raw height per image.

Reuse existing placement and distance helpers where contracts genuinely match.
Child-generator identity placement must retain its current evaluation frame.
Keep generator-specific hashes and seed sequences: replacing them with one shared
hash would change every existing procedural result.

Keep generator scale, noise frequency, solver propagation distance, and erosion
transport distance distinct. Document current units before introducing conversions.
Do not silently reinterpret existing pixel-based distances as world or UV distances.

### Small focused boundaries

Prefer the following logical stages where the algorithm benefits:

`settings/placement → invariant build → field evaluation → derived outputs → combine`

This is not a requirement to split every shader into five passes. Noise and Strata
may remain single-pass. Share only proven common math, using existing `.ush` owners;
do not introduce a universal generator framework or duplicate compatibility layers.

CliffStrata's compressed functions/stages should first be reformatted with descriptive
locals and stage comments, preserving expressions and evaluation order. Keep that
review separate from algorithm changes. Extract helpers only when responsibility is
clear or reuse is real, not simply to create more files.

### Caching and optional outputs: proposals, not verified defects

If later CPU inspection permits:
- Distinguish invariant geometry keys from field/resolution/placement keys.
- Include every parameter that influences a cached result.
- Preserve authored child ordering and reference invalidation.
- Generate optional fields only when all preview/reference/combine consumers allow it.
- Never drop IDs, masks, or debug outputs based only on shader-local observations.

Prefer a few measured stage/family permutations over a combinatorial feature matrix.
Do not assume the uniform NoiseType branch causes per-pixel divergence.

## 5. Track C: visual redesigns (opt-in)

### C1. Strata: geological interfaces rather than decorated ramps

Current shader builds a bedding coordinate, per-bed ramp/face, crest chips, and laminae.
That structure can produce a repeated sawtooth impression; this is a design inference,
not a rendered comparison.

Proposed new geological model:
1. Construct ordered layer interfaces with correlated thickness variation.
2. Fold interfaces coherently instead of independently warping each bed.
3. Assign bed hardness to derive shelf width and softer recessed layers.
4. Break shelves along coherent joints into slabs, not just fine noise bites.
5. Add explicit faults for offsets; keep continuity everywhere else.
6. Add controlled pinch-outs later, with explicit handling of vanishing layers.

Initial controls: bed spacing, thickness variation, fold strength/scale, hardness
contrast, joint spacing, and fault offset. Separate structural controls from detail.
A height field can suggest recesses but cannot represent true overhangs or undercuts.

Status: shader redesign implemented as a replacement, not an opt-in mode. `MixtormatStrataCarver.usf` builds ordered interfaces, hard shelves, soft recesses and joint-cut slabs; `RampShape` is deprecated (serialized only), and `LedgeWidth`, `HardnessContrast`, `SoftRecession`, `JointScale` and `JointWidth` are the new controls. Steps 5–7 later added ordered Strata Structural Warp and typed completed-bundle warp for the other five generators; see `GENERATORS.md` and the two warp design docs. These are implementation/source-review claims, not validation: current build, shader compilation, runtime and visual behavior remain unconfirmed. Faults and pinch-outs remain future work.

### C2. Erosion: separate three different processes

The current implementation is directional material carving:
- Eight horizon samples seed exposure from immutable source height (234–288).
- Min-plus relaxation spreads carve depth with directional cost (300–325).
- Its velocity is global `DownhillDir`, not local downhill flow (296).
- Deposit is a six-tap directional refill, capped by source/current height (205–220).
- There is no water state, sediment transport, or sediment conservation here.

Keep this implementation as the existing behavior. Any UI relabeling or new mode
must be deliberate and must not silently reinterpret serialized parameters.

**First choice: Rock Weathering**
- Analyze exposure and convexity at a meaningful structural scale.
- Use hardness to resist wear, with optional bed/region-based hardness input.
- Separate edge rounding from fracture-guided slab chipping.
- Preserve broad faces; concentrate changes near vulnerable edges and joints.
- Publish wear depth/coverage for material response if approved.
- Expose wear scale, amount, hardness contrast, rounding, and chipping controls.

**Later: Water Erosion**
- Derive local downhill transport from height, with explicit boundary conditions.
- Accumulate runoff to form connected channels that merge downstream.
- Track water and sediment separately from the height field.
- Limit removal by available material and hardness.
- Deposit carried sediment when transport capacity decreases.
- Handle flats/pits explicitly; do not hide a global-direction fallback.
- Define periodic boundaries, timestep stability, and mass accounting before coding.

True water erosion needs extra state, iterations, and bandwidth. Start with rock
weathering for material surfaces; do not present a cheap directional blur as hydraulics.

### C3. Other generators

| Generator | Visual feature | Implementation direction |
|---|---|---|
| Cracks | Primary fractures with smaller branches/dead ends | Separate structural crack hierarchy from local jag; preserve junction IDs and tiling |
| Rock | Hardness-linked weathering | Correlate bevel width and chip response per chunk; reuse existing stable identity |
| Pebbles | Size-aware packing with small gap fillers | Build placement before pixel evaluation; retain deterministic ordering and periodic neighbors |
| Cliff | Shared bedding and fault offsets | Correlate strata across blocks, reserving discontinuities for faults |
| Noise | Footprint-aware octave filtering | Derive footprint from resolution and placement; filter value and gradient consistently |

Noise filtering intentionally changes high-frequency results, so it belongs here,
not in behavior-preserving optimizations. Hierarchical cracks and size-aware packing
also change topology and cost; make them separate features, not extra arbitrary noise.

## 6. Validation and acceptance

No builds, commands, or GPU captures were run for this review. Obtain consent before
running those workflows or inspecting implementation files outside the agreed scope.

### Baseline matrix

- Small, representative preview, and high-resolution bake sizes; include non-square outputs.
- Fixed seeds and settings recorded with every comparison.
- Cold generation and warm/cache-reuse measurements separately.
- Identity, translated, rotated, flipped, uniform and anisotropic placement.
- Singular placement where currently supported; do not invent new accepted ranges.
- Zero/nonzero feature controls, one-cell cases, high detail, and tile boundaries.
- Erosion mask edges, Deposit 0, SecondaryAmount 0, Variation 0, and normal resampling.
- Cliff sweep endpoints, including one-pixel dimensions if supported.

### Track A acceptance

- Compare every published field, not only final shaded appearance.
- IDs and coverage decisions must remain exact.
- Aim for bitwise equality for local guards/refactors on the same backend.
- Report floating-point differences explicitly; obtain approval for non-exact changes.
- Check tile seams, finite outputs, source constraints, and deterministic repeats.
- Record per-stage GPU time, total compose time, resource bytes, and compiler registers/spills.
- Reject optimizations that merely move cost to build, memory, or downstream passes.

### Track C acceptance

- Compare against existing behavior without overwriting it.
- Review broad shapes first, then medium structure, then fine detail.
- Verify parameter predictability, tiling, scale consistency, and stable IDs.
- For water erosion, test mass accounting under documented sources/sinks and boundaries.

## 7. Recommended delivery order

1. Establish image/field baselines and inspect dispatch only with permission.
2. Add exact-zero work guards; validate each family independently.
3. Resolve Cliff endpoint initialization if dispatch confirms a defect.
4. Measure placement distance work and prototype uniform-scale acceleration.
5. Optimize Cliff sweep dispatch and Rock invariant facet work in separate changes.
6. Document shared field contracts; make small behavior-preserving readability changes.
7. Prototype opt-in Rock Weathering as a separate visual feature; geological Strata is done
   (replacement, see C1).
8. Add Noise filtering and hierarchical cracks after visual review.
9. Consider water transport and size-aware packing only with an agreed cost budget.

This file is a proposal and review record, not the canonical owner of current defaults,
parameter contracts, architecture, or verified performance results.
