# Shaders

## Directory organization

- Shader sources: `Shaders/Private/*.usf` (entry points) and `*.ush` (shared
  headers). Mapped to virtual path `/Plugin/Mixtormat` by
  `Source/MixtormatShaders/Private/MixtormatShadersModule.cpp`.
- C++ shader classes live beside their dispatch in
  `Source/MixtormatShaders/Private/` (`MixtormatGpu*Passes.cpp`) and
  `Private/Effects/`.
- `.usf` = a shader with an entry point (`MainCS`, `FieldCS`, …).
- `.ush` = shared helpers / capacity constants, included by other shaders.

## Class ↔ file mapping

`IMPLEMENT_GLOBAL_SHADER(FClass, "/Plugin/Mixtormat/Private/File.usf", "EntryCS", SF_Compute)`.

Find the mapping with `rg "IMPLEMENT_GLOBAL_SHADER" Source/MixtormatShaders`.
Convention: `FMixtormat<Feature><Stage>CS` → `Mixtormat<Feature>.usf` →
`<Stage>CS`. One `.usf` may host several entry points (e.g.
`MixtormatCraquelureGrow.usf` → `SeedCS`/`GrowCS`/`ResolveCS`).

## Shared helpers (`.ush`)

`MixtormatMaskOps.ush`, `MixtormatColorOps.ush`, `MixtormatHeightOps.ush`,
`MixtormatNoise.ush`, `MixtormatFlow.ush`, `MixtormatFieldSample.ush`,
`MixtormatUV.ush`, `MixtormatDriver.ush`, `MixtormatEikonal.ush`,
`MixtormatRegionId.ush`, `MixtormatCurvature.ush`, `MixtormatHeightNormal.ush`,
`MixtormatEdgeShading.ush`, `MixtormatMaskShaping.ush`, `MixtormatDebugColor.ush`,
`MixtormatGeneratorPlacement.ush`, `MixtormatGeneratorHeightModules.ush`,
`MixtormatGully.ush`, `MixtormatCellular.ush`, `MixtormatGeneratorWarp.ush`,
`MixtormatNoiseV2.ush` (Noise-only extensions included after the legacy Noise definitions).
Noise V2.1 also provides optional multi-octave layers for the historically single-frequency
families, continuous cell-distance modulation, and an independently authored high-frequency
jagged domain warp. Opt-in values default to zero so the previous shader outputs are unchanged;
see `AgentDocs/NOISE_V2.md` for exact contracts and performance risks.

Capacity headers (`MixtormatScalarRampCapacity.ush`,
`MixtormatColorRampCapacity.ush`) are included by **Runtime C++** too
(`MixtormatScalarRamp.h`) so CPU and GPU share one limit.

## Parameter tags

Shader uniforms that correspond to an authored parameter carry a tag adjacent to
the declaration:

```
// @param BreakupSeed
uint Seed;

// @param BreakupGapWidth
float GapWidth;

// @param BreakupFoldWidth divisor
float FoldWidth;

// @param BreakupDensity saturates
float Density;
```

`MixtormatShaderParamScanner` (`Editor/Private/UI/Parameters/`) reads these to
verify shader drift against `MixtormatParameterDefinition.h`. Tag suffixes:
`saturates`, `divisor`. Keep the tag adjacent to its uniform.

## Bindings / parameter structs

Each pass declares `BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )` with
`SHADER_PARAMETER`, `SHADER_PARAMETER_RDG_TEXTURE`, `SHADER_PARAMETER_RDG_TEXTURE_UAV`.
Render-data structs (`F*RenderData`) are filled by gather and read by the pass.

## RDG / pass conventions

- Passes are `Add*Passes(Ctx, LayerCtx, …)` in `MixtormatGpuCompositor` namespace.
- `FMixtormatComposeContext` / `FMixtormatLayerPassContext` are the shared
  contexts (`MixtormatGpuCompositorInternal.h`).
- Targets: BaseColor, Normal, RAM, Height, Debug, RegionIdPick (ping-ponged).
- `DECLARE_GPU_STAT_NAMED` per family for `stat gpu` / ProfileGPU.
- Distance solves cap the grid at `DistanceSolveMaxSize = 1024`.

## Resolution / format

- Resolution is the compositor's `FIntPoint` (preview vs bake differ).
- Height is `PF_R32_FLOAT`; BaseColor/Normal/RAM are `float4`.
- Substrate defaults in `MixtormatGpuComposePipeline.cpp` (`MixtormatSubstrate`).

## Moss canopy and material coverage

Stages 2/3 specialize by `MOSS_SDF_CUSHION` at compile time. Deposition does not
read SDF distance/inflation textures; SDF does not read deposition thickness.
`ClearUnusedGraphResources` removes the inactive mode's bindings before RDG pass
registration. A runtime mode branch retained both sets of reads, causing an
unproduced-resource ensure because only the selected solver runs. Both solvers
and their authored settings remain unchanged; this fix is source-reviewed only.

Both cushion modes retain their native body solver. `MixtormatMoss.usf` Stage 13
resolves a supported top envelope after signed-height normalization/HeightScale,
using the composed substrate (including enclosing upstream height). Opposing-bank
samples span at most CushionSize in destination UV; sampled growth coverage and
scope gates limit support. Only already-covered, positive-relief pixels are lifted.
The lift scales linearly with positive HeightScale, including above one; negative
relief and diagnostic previews retain their prior path. Amount and the center's scope gate apply once,
after this resolve. The later leaf/appearance work below adds reflected Moss fields.

This closes supported narrow height-field depressions, not mesh holes or true
volumes. It does not extend growth coverage into bare pixels, create undersides,
or guarantee bridging across gaps wider than the support span. The raw field cache
excludes the canopy resolve. Pass Through's inherited height enters growth analysis
and canopy support once, not as both the layer input and an additional lower composite.
Field and simulation cache revisions were advanced for this substrate correction.
Stage 7 now evaluates provisional growth/creep at each edge endpoint and exchanges
bounded slump thickness symmetrically. Growth, creep, and halo decay remain; only
the slump exchange is conservative before floating-point storage. This increases
simulation sampling work and adds one full-resolution canopy pass; GPU cost and
appearance have not been measured.

Moss passes GrowthCoverage as `FSurfacingFields::MaterialCoverage`. SurfacingApply
multiplies color/RAM/optics weight by this footprint in addition to Amount and the
scoped gate. Bare substrate retains its incoming material values. Height and leaf
detail normals already contain coverage and are not multiplied by it again. Skin
leaves MaterialCoverage null and retains its existing behavior. Published Moss
Color/Roughness remain raw fields, with GrowthCoverage published separately.
Compilation and render verification remain user-run.

### Procedural colonies and shared leaf envelope

Stage 1 now establishes stable parent colonies with connected child lobes on neutral
flat substrates. Cavity/shelter bias suitability but no longer hard-gate procedural
seeding. The optional seed mask adds inoculation; scoped masks still gate composition.
Stage 4's local drain/neighbor-height signs were corrected. Both cushion solvers remain.

`MixtormatMossLeaves.ush` is shared by Stages 3/12. It evaluates cushion rosettes,
swept tufts, creeping alternate leaves or feather pairs in a bounded 3x3 neighborhood.
Each of four footprint samples chooses its upper leaf before averaging. Attached leaf
profiles taper into their support; cup/keel/bend exist only in height. Independent
lamina striations remain in DetailNormal, with NormalStrength and scope/Amount each
applied once. Zero NormalStrength leaves the incoming normal input untouched.

New reflected controls: GrowthForm, LeafScale, Hydration, SurfaceWetness, WetDarkening,
ColonyVariation, LeafVariation, TipColor and Damage. Hydration changes leaf posture;
surface water changes effective roughness and optional darkening; damaged shoots
collapse and brown independently. Palette changes reuse cached growth state, although
the final leaf/material evaluation still reruns. Field/simulation/prefix salts advanced.
Ten built-in looks preserve the original five names and add Rock Cushion, Forest Pillow,
Swept Tuft, Creeping Carpet and Feather Weft. They are artistic, not reference-fitted.

Limits: positive attached height envelopes, not cards, buried-fragment collision,
overhangs or independent leaf transmission. Final height normals still differentiate
the single envelope; owner-aware discontinuity filtering and metric calibration are
not implemented. Flow varies locally across a tuft; output-resolution behavior and
GPU cost need user testing. Optional height normalization and both cushion modes remain.
Moss currently preserves incoming metallic/optics; this is not a complete vegetation
optical model. Existing shared Skin/Moss metadata-name ambiguity remains; Rust uses a
separate typed parameter owner rather than inheriting that lookup order.

## Rust analytic ferrous corrosion

`MixtormatGpuRustPasses.cpp` / `MixtormatRust.usf` produce signed height, visible oxide,
pigment, RAM and optics from an immutable substrate-height snapshot. Periodic parent
clusters, intermediate plates and fine aggregates share seeded morphology. Smooth,
Granular and Layered alter retained deposit structure, not the definition of metal loss.
Coverage reveals fixed-depth profiles without whole-image normalization. Flaking removes
retained deposit and visible oxide without healing recession. Retained thickness is
bounded by Buildup before HeightScale; all lengths remain explicitly relative.

Four footprint samples accumulate signed relief independently of oxide coverage;
material color/roughness alone are unpremultiplied. HeightScale is a pure multiplier.
Shared apply owns Amount/scoped masks once. Rust's Write Material opts into RAM.B=0,
F0=.04 and zero fuzz/SSS weights under visible oxide; uncovered incoming channels remain.
Skin/Moss do not opt into that metallic write. No master asset, renderer or project
setting was changed. Final composed height owns normals/AO; Rust emits no duplicate
normal or baked occlusion in albedo. Existing layer opacity/channel influence still apply.

Creation, inspector, ramps, instances, bindings, presets and ordered nested scopes use
the existing Surfacing paths. Rust has its own parameter owner (appended value 33),
so ambiguous Skin defaults cannot override its ranges/defaults. Published outputs are
completed-scope Height delta, completed Color and Amount/gate-weighted OxideCoverage.
Seven presets: Atmospheric Granular, Thin Discoloration, Localized Attack, Compact Crust,
Granular Buildup, Layered Accumulation and After Shedding. The final pair shares its
attack settings and differs only in shedding. Preset apply retains destination gates.

Explicit assumption: exposed ferrous substrate supplied by the user. No generated
steel, inferred alloy/protection, coating recovery, runoff transport, hidden layers,
physical calibration or true overlapping plate geometry. Adaptive Substrate is unchanged.
New reflected Rust payload and Moss properties require complete current strict JSON;
no migration or reconstructed parameter-authoring JSON was added. Source-reviewed only;
these changes have not been compiled, profiled or render-verified by the agent.

## Skin staged tissue, folds and material response

`MixtormatGpuSurfacingPasses.cpp::AddSkinSurfacePasses` dispatches:

1. `AnalysisCS`: completed upstream substrate curvature, full output resolution.
   Pass Through height already contains the composite below and is not added twice.
   Optional Auto Remap runs on the lower composite before the first height-writing
   Surfacing layer, never on the resulting Skin/Moss relief (see `COMPOSITION.md`).
2. `TissueCS` (`SKIN_STAGE=0`), full output resolution: elliptical fat support,
   layered five-step RK2 drift, periodic offsets and independent positive thickness.
3. `SeedCS` (`SKIN_STAGE=1`): generated-tissue Hessian plus upstream curvature;
   connected marble/cellular broad and crossing folds, signed valleys/shoulders,
   compression, crown stretching and destination-space normal directors.
4. `MixtormatSkinSolve.usf`: restrict full-resolution rest/directors to each mechanics
   level; initialize/prolongate the correction; spend **0–512 total relaxation iterations**
   from coarse to fine, each reading an immutable previous R32F correction. No procedural
   noise is reevaluated inside iterations. Zero iterations returns zero correction.
5. `MicroCS` (`SKIN_STAGE=3`): output-resolution medium/fine cellular crease networks,
   warped elliptical pores and grain, cached once in RGBA32F. Medium/fine detail is
   procedurally strain-directed, not simulated.
6. `ResolveCS` (`SKIN_STAGE=4`): fine-residual-only directional filtering and the final
   signed Skin height (HeightScale once), written before material evaluation.
7. `FieldCS` (`SKIN_STAGE=2`): seven material/diagnostic outputs, reading resolved relief.
   Cavity AO uses eight wrapped horizon rays with five logarithmic distance samples each,
   local tangent-plane correction and the shared height-to-normal metric. Curvature remains
   a roughness/fuzz cue, not AO. Skin visibility multiplies incoming AO through the existing
   single masked weight; neutral Skin AO cannot erase upstream crevice occlusion.
   Skin has no separate detail-normal texture, write flag or strength: wrinkles and pores
   contribute through Height and the shared height-to-normal path. Moss retains its separate
   leaf-detail normals. Retired Skin normal fields are no longer part of reflected preset
   payloads; strict JSON presets containing those fields must be updated before import.
   Source-reviewed only; not compiled or render-validated.

Binding policy: Skin authoring ranges are wide and finite-only; hard clamps remain
only where GPU-dangerous (`SolverIterations` dispatch budget, `SolverResolution`
allocation, `WarpLayers` shader loop, true divisors floored in contracts, pore
support and solver rigidity/foundation safety clamps in-shader). Resolve applies a
shared soft-capped micro budget (fine + pores) over the tissue/fold base. Tissue fat
is multi-scale (half-frequency strokes carry the silhouette). Cell-local ellipses follow
`Direction`/`Elongation` (displayed as Stretch). Gravity no longer rotates/narrows them or
adds a one-sided sample-difference height boost. Instead, `GravityBulge` transports each
positive fat lobe downhill inside its pinned footprint: the upper body thins and the
lower body bulges. In gravity-aligned unit-ellipse coordinates the mapping is
`y_dst = y_rest + sag * (1 - x*x - y_rest*y_rest)`, inverted analytically; dividing the
lobe height by its Jacobian preserves that lobe's integral before filtering and the
existing spatial tissue modulation. `sag < 0.45` keeps the mapping single-valued; the
footprint and periodic 3x3 neighbour search remain unchanged. AA accounts for the
maximum compression. Stretch narrows ellipse support independently of gravity rather
than scaling absolute UVs, so the periodic lattice stays intact. Upstream substrate curvature modulates tissue
(`CurvatureStrength` × `TissueCoupling`). Solver strain includes
dynamic feedback from the current solved slope. The general-purpose defaults use
`TissueCells` 6, `DetailCells` 96, `PoreCells` 128 and `GravityBulge` 0.18. Broad
relief is restrained (`Fullness` .25, `FoldDepth` .24, Stretch 1.8) without reducing
the independent Height Scale (1). Detail remains footprint-filtered at output resolution.

Tissue and fold seeds use full output resolution, independently of `SolverResolution`.
Only mechanics corrections use bounded grids (default maximum dimension 512, authored
maximum 2048); aspect ratio is retained and smaller outputs are not upscaled. The coarsest
level targets eight samples per fold, bounded to 64–256 texels on the longest axis, then
doubles toward the final grid. One third of the remaining iteration budget is spent on each
coarse level; the finest consumes the remainder. Default total budget is 128, hard maximum 512.
Stage 0 restricts full-resolution seed and tissue fields with a 4-tap subtexel box filter.
This improves low-frequency progress over one fine-grid explicit solve; it is not a
convergence guarantee. Correction prolongation is bilinear; final correction reconstruction
is cubic B-spline. Full-resolution analytic folds/pores are never replaced by the grid.
Microdetail uses full output resolution and independent `DetailTiling`.
Medium/fine creases use periodic jittered nearest-cell gaps, not perpendicular wave families.
Both bands use the seed's double-angle-interpolated normal; its destination-space tangent
is pushed through the advected-domain Jacobian. A bounded, area-preserving distance metric
biases the networks along that tangent under compression, relaxing toward isotropy under
stretch. Cell centres remain fixed and periodic; no locally rotated absolute-UV lattice is
introduced. Crease filtering uses the local metric and projected edge gradient; rapid
spatial changes of the metric are not integrated analytically.
Pore axes retain cell randomness at neutral Stretch; increasing Stretch aligns them with
the shared direction field and elongates the ellipses. Tensile strain adds further
area-preserving stretch; the existing bounded neighbour search includes enlarged support
up to its GPU safety cap. The additional pore warp also transforms the tangent.
Pores are not forced onto crease junctions. Roughness compensation remains unchanged.

`DirectionVariation` drives a periodic two-band scalar phase potential and its analytic
gradient, at a broad scale derived from `TissueCells`. Tissue, pores and pigment use its
local tangent; fold phases add the same potential without spatially varying/quantizing
the integer wavevector. This is phase-gradient directionality, not UV advection.
It works with `MarbleDrift = 0`; the existing optional drift and pore distortion remain
separate. Direction, Stretch and Direction Variation appear together in SURFACE.
Pigment combines broad tissue-scale blotches with three independent fine bands, filtered
along the local direction. `PigmentVariation` controls ramp-coordinate contrast and
thickness-linked chromophore absorption; zero retains the ramp midpoint and disables
pigment absorption without disabling vascular absorption.

`GravityHold` directly resists fat transport through `1 / (1 + Hold*Hold)`, including
when Solver Iterations is zero. `GravityBulge = 0` leaves the fat lobes undeformed;
higher Hold reduces their downhill redistribution without cancelling Fullness or
changing Stretch. This is an art-directed anchored height-field deformation, not a
volumetric gravity simulation, contact solve, or overhang model.

Hold also retains its existing positive foundation-spring contribution in thick tissue
and seeded depressions during film relaxation. That spring targets the seeded rest
shape, not zero height, and its stiffness remains in the relaxation step bound.
Source-reviewed only; the new fat transport is not compiled or render-validated.

### Relaxation model and bounds

The solver is an art-directed compressed-film **height-field approximation**, not XPBD,
not a full Föppl–von Kármán solve, and not volumetric tissue or self-contact. In-plane
strain is prescribed from the seed's compression/stretch/director fields; it is not solved.
It minimizes a discrete energy consisting of a spring to the seeded rest shape, variable
thickness-cubed bending `B*(Laplacian h)^2/2`, and symmetric directional edge energies:

`E(s,e) = s²/2 - (1 + 2e)*log(1 + s²)/2`

Here `s` is edge slope and `e` projected compressive strain minus tensile strain. Its
small-slope expansion has compressive negative stiffness and stabilizing quartic strain;
large slopes have a bounded Hessian. The force uses its actual derivative, not repeated
noise or a blur posing as simulation. Wrapped 8-neighbor strain and variable-rigidity
biharmonic stencils use each axis' UV spacing (with the biharmonic inverse-spacing squared
capped at 256 in fold-wavelength units so fine grids do not freeze explicit steps); singleton
axes are excluded. A local per-texel/per-edge Hessian row-sum bound controls the explicit
relaxation step (safety 0.8). `Bending` and `Support` are unclamped artistic controls
(rescaled foundation 0.5–32 and rigidity 0.00002–0.0025 over 0–1), not measured biological
material constants.

Lengths and residual heights are normalized by the authored placed fold rate, bounded
by grid resolution. Directors use the placement/PreUV Jacobian; mechanics still operates
in destination UV, not the preview mesh's physical metric. Highly distorted UVs therefore
do not constitute physically metric-correct skin mechanics. State lifetime is per compose,
not persistent real-time dynamics. RDG owns transient reuse; every iteration has distinct
logical output state and no read/write alias.

### Shader controls and relief resolve

- `WarpScale` is a float, default **1**, divisor-floored (`> 0`) with UI/JSON owning upper
  clamps. It multiplies the integer-rounded drift/phase-noise lattice frequencies,
  independently of `MarbleDrift`; higher values make smaller-scale wandering without changing
  fold or tissue cell counts.
- `MarbleDrift` budgets advection in noise-cell spacings: reference travel `0.34 * amount`,
  divided across five RK2 steps and normalized geometric octave weights. `WarpLayers` (0–8)
  adds structure, not another full travel budget. Direct fold-phase noise uses bounded
  `0.34 * amount / (1 + 0.25 * amount)` wandering instead of repeating unbounded advection.
- `PoreSize` is a float, default **0.85**, divisor-floored (`> 0`) with loop reach capped at 6
  for GPU safety. It multiplies ellipse radii, independently of `PoreCells` density. Existing
  variation supplies ellipticity; no extra stretch control is introduced. The warped pixel
  covariance filters both ellipse axes with determinant-based area compensation and
  size-aware periodic support.
- `PoreDepth` has no hard maximum; its ergonomic slider is **0–4**.
  Depth does not change pore size/density, and the pore diagnostic remains available.
- GPU uniforms are exactly `float WarpScale` and `float PoreSize`. `MicroState` and
  `OutMicro` use `float4` / `PF_A32B32G32R32F`: **R** = medium/fine wrinkle + grain UV
  residual, **G** = pore mask, **B** = pore UV relief, **A** = zero. Only R receives the
  directional wrinkle filter; B is added afterward, once. Field cavity uses R/B relief,
  not G as painted occlusion; capillary generation and pore publication are preserved.
- Full-resolution `TissueState.r` and `SeedState.r` retain analytic UV relief. Final
  `FoldState` is **solved fold height minus the same sampled seed rest R**, in UV units,
  on the final solve grid; zero iterations publishes zero correction. Resolve sums tissue,
  full-resolution seed, periodic cubic B-spline correction, filtered R and unblurred B.
  Four bilinear taps implement the positive-weight bicubic correction filter; analytic
  seed detail is never upsampled from the solver grid or smoothed by this filter.

- The prototype's amplitudes divided by its **3.4-unit** patch width are UV relief,
  while canonical height uses `8 / MIXTORMAT_HEIGHT_NORMAL_REFERENCE_RES = 1/128` UV per
  height unit. Resolve applies the inverse (**128**) exactly once with
  `HeightScale / SurfaceTiling`: module repeats shrink relief proportionally (2× repeats
  gives a ½ relief multiplier), while Height Scale remains a pure multiplier. The repeat
  count uses the existing CPU whole-repeat rounding/minimum-one contract. This is not
  0–1 normalization or physical centimetre calibration. AO/geometry read the corrected
  resolved height; cached micro cavity cues receive the same inverse-repeat factor. Layer UV scaling, Detail Tiling and physical MFP are unchanged. Cached tissue
  Hessians remain in UV relief; `Fat` stays `TissueState.r / SkinTissueRelief`.
  Not compiled or render-validated.

### Fields, periodicity and optical relationships

Relief retains the prototype's amplitudes divided by its 3.4-unit patch width; no automatic
0–1 normalization. `HeightScale` applies once at final field output. Shared composition
still owns Amount, masks, ancestry, write flags and publications; no second renderer exists.
Integer wavevectors and periodic drift offsets preserve the underlying tile. Direction is
quantized at low fold counts. Seamless placed results require integer UV winding and a
compatible PreUV map; arbitrary noninteger scale cannot be promised seamless.

Pigment and vascular fields coordinate albedo with RGB MFP (centimeters); thickness is a
separate tissue field, not displacement used as optical depth. Absorption coefficients are
art-directed rather than measured spectral skin data. Vasculature uses sparse jittered trees
with bent trunks, tapered forks and attached side twigs, not zero-contours of lattice noise.
Branch footprints and thickness-dependent subdermal blur preserve line area; a separate soft
perfusion field supplies background blood concentration. These are procedural capillary
clusters, not a solved anatomical vascular network. Neither cavity nor AO is painted into
albedo. For uniform midpoint albedo, set `PigmentVariation`, `VascularAmount`,
`RednessAmount`, `VeinAmount` and `Flaking` to zero.
AO reads the exact resolved Skin relief, including filtered microdetail. It is a local
height-field visibility approximation, not ray tracing against the preview mesh or substrate.
The separate curvature-based material cavity/filtering remains approximate.
RAM.a remains F0; baked RAMH.a remains height. Fuzz/SSS share the existing Substrate master.
GBuffer/platform support can still limit simultaneous fuzz and per-pixel diffusion SSS;
no renderer settings are changed by this implementation.

### Independent skin appearance controls

The existing pigment and vascular response is retained. Nine new reflected parameters
use the shared Surfacing contracts, finite-value gather, GPU bindings and member-slider
inspector. Their names do not collide with Moss parameters. Reflected metadata supplies
defaults/ranges; the parameter-authoring JSON is not rewritten.

- `RednessAmount` (0..1, default .12): additional broad perfusion color, independent of
  pigment and vein visibility. Existing `VascularAmount` still controls its original
  capillary/background appearance and existing MFP coupling.
- `VeinAmount` (0..1, default 0), `VeinDensity` (0..1, default .5), `VeinWidth`
  (.25..4, default 1) and `VeinDepth` (0..1, default .5): a separate, coarser seeded
  branching network. The existing branch evaluator is shared with explicit width and
  diffusion inputs. Greater depth appearance broadens and reduces contrast without
  moving branch centers; width and density remain independent. Relative RGB attenuation
  and background-tone visibility are artistic heuristics, not spectral blood transport,
  measured melanin or millimeter anatomy. Calibrate visually with Unreal SSS enabled.
- `OilAmount` (0..1, default .12): one effective roughness adjustment, never a tissue-color,
  height, F0 or MFP multiplier. Does not represent sweat or measured sebum mass.
- `Dryness` (0..1, default .08): at most 1.25x the fine-wrinkle/grain amplitude, plus a
  restrained roughness response. Medium wrinkles, broad folds, fat and pore amplitudes
  are not directly multiplied. The shared micro-height budget still applies.
- `Flaking` (0..1, default 0): sparse attached, beveled scale patches. Height and coverage
  are evaluated together with pixel-footprint filtering. MicroState now stores
  `(fine relief, pore mask, pore+flake relief, flake coverage)`; material color/roughness
  read the cached coverage. Normals derive from the resolved height only. No detached
  flakes, overhangs, independent flake scattering or physiological coverage claim.
- `FinishVariation` (0..1, default .55): bounded seeded oil/dryness patchiness. Zero makes
  those fields uniform; oil and dryness are separate and can coexist. Flakes retain
  independent seeded placement. All fields follow module placement/tiling.

The new effects do not modify MFP or SSS amount. They respect the existing channel
write gates and shared masked Amount. The general default has restrained redness,
oil and dryness; vein visibility and flaking remain off. Zero remains the neutral value
for each effect. No required external masks, material-contract extension or new renderer.
One roughness cannot represent independently adjustable narrow/broad specular lobes.
Research guidance: `auditdocs/skindocs/auditforskin.md`; cited measurements were not
independently revalidated. All numeric appearance mappings above are artistic.

### Default and built-in Skin looks

`FMixtormatSkin` owns the canonical General settings, including scalar reset defaults
and color/roughness ramps. The General preset uses its default-constructed settings
rather than duplicating them. Existing authored modules are not automatically reset;
apply General from the module Presets menu to use the new starting point.

Runtime provides fifteen looks in that existing menu:

| Look | Main distinction |
|---|---|
| General | Shallow tissue, fine crossing wrinkles, 128-cell pores, restrained warm pigment and sheen |
| Human | Warmer/redder, more defined folds, faint soft veins |
| Smooth | Low tissue/fold relief, small shallow pores, supported tissue |
| Pored | Larger irregular pores with a moderate oily finish, not deeper broad folds |
| Rough | Coarse crossed creases, granular matte finish, restrained flaking |
| Elephant | Heavy broad folds, grey-brown pigment, dry surface and low SSS amount |
| Oily | Strong patchy sheen, dense pores, shallow folds; not sweat |
| Dry Flaky | Coarser attached scales, broken dry microcreases, shallower pores and stronger matte finish |
| Mature | Loose tissue/sag, stronger creases and uneven pigment; not a biological age model |
| Veined | Light base tone, visible broad branches, subdued diffuse redness |
| Deep Tone | Deep warm-brown palette with General relief; no automatic age/dryness/MFP change |
| Marbled Folds | Long drifting relief folds, raised shoulders and sparse crossing creases; muted creature palette |
| Palm Creases | Broad crossing flexion-like creases over fine dry lines with minimal follicular pores |
| Pebbled Hide | Compact cellular creases, rounded tissue islands, dry grain and reduced SSS amount |
| Wet Amphibian | Soft small tissue lobes, subdued folds, stronger mottling and a patchy low-roughness finish |

All prior preset names remain. The four new studies reuse current Skin fields; they
add no schema, texture outputs or renderer settings. Palm Creases is not anatomical
palm-line placement, fingerprints or a handprint. Pebbled Hide is not overlapping
reptile scales; Wet Amphibian uses effective roughness, not a separate water film.
Dry Flaky lowers PoreCells to coarsen the existing coupled flake lattice, with smaller,
shallower pores. Flaking remains sparse pale attached scales, not scabs or wounds;
a distinct crust response would require separate feature work.

Presets vary form, detail scale, finish and
color rather than only changing seeds. Solver budgets remain at or below the General
128-iteration/512-resolution settings; Surface Tiling defaults to two, while Detail
Tiling and Height Scale remain one. Built-in looks inherit that Surface Tiling default;
existing authored modules keep their saved value. No per-preset MFP fit or renderer
change is claimed.
The existing module Presets menu applies them through shared undo/instance synchronization
while preserving Amount, write flags, enable state, destination bindings and scoped masks.
These are source-reviewed starting looks, not render-validated production references.
Strict JSON preset loading still requires the complete current reflected schema.
Saved JSON containing a Skin payload must include the nine new fields before import
(including enclosing Surfacing/layer documents). Built-in looks construct current defaults;
no pre-release compatibility or migration path was added.

Research references: the supplied `Prototypes/skin.glsl` for tissue/drift/fold structure;
[Föppl–von Kármán equations](https://en.wikipedia.org/wiki/F%C3%B6ppl%E2%80%93von_K%C3%A1rm%C3%A1n_equations)
for bending/geometric-strain context (the regularized solver above is not that full system);
[GPU Gems 3, Chapter 14](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-14-advanced-techniques-realistic-real-time-skin)
for separate surface/subsurface response; and
[Epic Substrate overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine)
for the shared shading contract. No unread PDF or exact-paper implementation is claimed.

Only manual source/math review was performed. No build, shader compile, GPU timing,
render, automated validation or visual-quality confirmation has been run.

## Structural Warp / bundle alignment (steps 5–7)

- `FMixtormatGeneratorStructuralWarpCS` → `MixtormatGeneratorStructuralWarp.usf::MainCS`.
- `MixtormatGeneratorStructuralWarp.usf::CoordinateCS` turns final RG32F displacement into a
  lifted coordinate map for step-7 completed-bundle targets.
- Fresh per-target RG32F D / R32F B outputs compose `D_new = d + sample(D_old, psi)` and
  `B_new = sample(B_old, psi)`. State lives in `GeneratorStructuralDisplacements` and
  `GeneratorHeightPushFields`; Strata regenerates, while the other generators use one typed
  completed-bundle pullback.
- Flow references use `MixtormatFlowTrace.usf`: magnitude-preserving RK2 produces lifted
  coordinates without SDF collision solving. Scoped masks gate displacement after tracing.
  UVMap uses periodic lifted-coordinate sampling.
- Strata applies placement once at warped coordinates; gradients are `g*A*J + grad(B)`
  where applicable. B's destination gradient receives no second J. Active warp writes a
  direct RG32F negative-inside distance/validity boundary; inactive retains bundle stage 8.
- `RegisterNamedMask` producer descriptors select bundle stages 10 (ID-anchored attributes)
  and 11 (owner/phase-aware bed T), plus validity-aware distance remapping retaining
  `length(g_source)` in the metric ratio. All companions read old IDs/boundaries from one
  immutable snapshot. Apply owns Height/Coverage; `PebbleCoverage` aliases moved Coverage.
- `CrackDistance` remains a crack-cell attribute, distinct from the internal UV boundary.
  Rock, Pebbles, Cracks, Cliff and Noise use the completed typed bundle path; Noise Gradient
  retains heterogeneous source-frame meaning and is transport-sampled without an implicit vector transform.

Implementation and source review are not compile/runtime validation. Only the user's earlier
step-2 compile is confirmed; the gather missing-header fix is present, newest build unconfirmed.
Broken StructuralWarp tests were removed at user request; no agent build, shader compile,
runtime or test results are claimed. Raster derivatives/distances remain approximations;
legacy centre/orientation inverse handling and geological defects are not fixed here.

Noise preview/copy integration: typed scalar and Vector2 debug blits read completed
published fields. Signed values are display-remapped only. Noise Value and Gradient support
full-precision storage as well as the default half-precision contracts.
`MixtormatNativeFlow.usf` owns intrinsic downhill Flow derivation, with zero validity at
flat or non-finite slopes. Raw/smoothed publications share the resolved texture unless
explicitly filtered; validity is separate. Noise Gradient is not implicitly canonical Flow.
`MixtormatNoise.usf::GeneratedFlowCS` combines weighted Height/Slope/Curl/Force bases;
`FlowComposeCS` applies masked Add/Mix and `FlowTransportCS` samples accumulated Flow through
a coordinate remap without normalization. Scoped height masks do not gate raw Value/Gradient.
These contracts are source-reviewed only; no build/shader/runtime checks run.

## Shared Blur pipeline (Gaussian, Directional, Smear)

- Dispatch entries:
  - `MixtormatGaussianBlur::AddPasses` (`MixtormatGpuGaussianBlur.h/.cpp`) → `MixtormatGaussianBlur.usf`
    (separable X, Y, and resolve stages; `FMixtormatGaussianBlurCS`).
  - `MixtormatDirectionalBlur::AddPasses` (`MixtormatGpuDirectionalBlur.h/.cpp`) → `MixtormatDirectionalBlur.usf`
    (single-pass compute; `FMixtormatDirectionalBlurCS`).
  - Unified dispatcher: `AddOwnedBlurPass` and `AddOwnedFlowBlurPass` in `MixtormatGpuBlurPasses.cpp`.
- Mode behavior:
  - `Gaussian`: Separable horizontal and vertical passes, sigma = radius / 2, integer taps through ceil(3 * sigma).
  - `Directional`: Centered bidirectional filter along authored angle or Flow guidance, length measured as half-extent per side.
  - `Smear`: Unidirectional trail along velocity/direction vector (trail sign from length sign).
  - Tracing: Midpoint RK2 in output-texel units with periodic boundary wrapping and trapezoidal box kernel weighting.
- Five typed fields (`FIELD_TYPE` 0..4):
  - `0 (Scalar)`: R16F or R32F, optional coverage clamp at resolve (`ClampCoverage`).
  - `1 (Vector4)`: float4 fields (e.g. RAM, BaseColor without alpha weighting), per-channel weight masking (`ChannelWeights`).
  - `2 (EncodedNormal)`: [0, 1] encoded normals; decoded, renormalized with `rsqrt(dot(N, N))`, and re-encoded in resolve.
  - `3 (ValidFlow)`: 2D flow vectors. Preserves caller's validity texture; invalid destinations are identity. Flow magnitude is not normalized; packed Z/W are preserved intact.
  - `4 (ColorAlpha)`: Accepts and returns straight RGBA. Multiplies to premultiplied space (`rgb * a`) before filtering, mixes in premultiplied space, and unpremultiplies only where alpha > 0. Eliminates transparent RGB halos and color fringing on Color Ramps.
- Flow guidance (`FLOW_GUIDED`):
  - Direction source `EMixtormatBlurDirectionSource::Flow` reads `DirectionFlow` + `DirectionValidity`.
  - Bilinear periodic sampling excludes invalid corners before weighting.
  - Missing/unavailable flow falls back to identity without dispatch or bridging artifacts.
- Filter masks, Amount, ChannelWeights, and coverage clamp each apply exactly once at the resolve stage.


## Erosion carve-depth solve

- `MixtormatErosion.usf` seeds and relaxes nonnegative R32F carve depth, not absolute height.
  Active pixels retain the previous centre carve across the changing jump schedule.
- `Effects/MixtormatErosionPasses.cpp` binds `SeedCarve`, `PreviousCarve`, and `OutputCarve`;
  a final `ResolvePass` reconstructs absolute height and optionally fuses source-capped deposit.
  Height-derived normals and carve shading still consume absolute source/resolved heights.
- Deposit derives the constant authored direction directly, preserving the legacy gravity bias;
  no per-iteration velocity textures are needed. Disabled seed noise is skipped and the
  zero-weight final smear tap is omitted without changing the remaining tap positions.
- `MixtormatFlowWarp.usf::DownhillSlope` normalizes derivatives by each UV sample spacing,
  changing directions for unequal kernel radii or non-square outputs.
- These changes are source-reviewed only; shader compilation, runtime and GPU timing are unverified.

## Root Network stem shape & UV publication (RootNet4)

- Growth samples one spatially stable curl field rather than reseeding it each iteration.
- Apical persistence enables tip nodes to coast through attractor gaps, improving network longevity.
- Tortuous growth: RootKink injects biological circumnutation (alternating zigzag wandering)
  and stochastic obstacle deflection (sharp angular kinks).
- Profile Pinch & Broaden (RootProfileBias): super-ellipse cross-section exponent ($p = 2^{1 + 	ext{Bias}}$).
  Negative bias pinches into a crested / keeled gothic spine; positive bias broadens into steep-sided shoulders.
- Physical Soil Embedment (RootBurial): sinks cylinder center into the ground plane; thin roots submerge completely,
  while thick trunks break through with solid entry flanks and clean zeroed coverage masks underground.
- Steering & Tropism:
  - RootAzimuthAngle & RootAzimuthStrength: directional compass tropism (gravitropism / rheotropism).
  - RootSwirlStrength: toroidal vortex swirl steering curling branches around network center.
  - RootAxisRatio: anisotropic attractor distance metric favoring horizontal ($> 1$) or vertical ($< 1$) spread.
- Orthogonal root coordinate frame:
  - RootUV: continuous 2D UVMap ($U$ = normalized branch arc length, $V$ = normalized across-profile $[0, 1]$),
    blended to raw canvas coordinates outside roots for seamless downstream generator warping (e.g. Noise grain).
  - RootAcross: scalar mask of normalized distance from spine to flank ($0$ on axis, $1$ on edge).
  - RootArcLength: scalar mask of normalized cumulative longitudinal coordinate.
- The RootNet4 field-cache key separates this revision from previous cached fields.

## Adding a shader parameter

Trace the full path: CPU declaration (the struct's owning runtime header, e.g.
`MixtormatLayerTypes.h` / `MixtormatGeneratorTypes.h` / `MixtormatMaskTypes.h`) →
gather (`Compositing/Mixtormat*Gather.cpp`) → dispatch/binding (`MixtormatGpu*Passes.cpp`)
→ defaults → inspector metadata → `.usf`/`.ush` (+ `// @param` tag).



Read REVIEWS.md, there are shader necessities there maybe.