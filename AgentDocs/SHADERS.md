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

## Skin staged tissue, folds and material response

`MixtormatGpuSurfacingPasses.cpp::AddSkinSurfacePasses` dispatches:

1. `AnalysisCS`: completed upstream substrate curvature, full output resolution.
2. `TissueCS` (`SKIN_STAGE=0`), full output resolution: elliptical fat support,
   layered five-step RK2 drift, periodic offsets and independent positive thickness.
3. `SeedCS` (`SKIN_STAGE=1`): generated-tissue Hessian plus upstream curvature;
   connected marble/cellular broad and crossing folds, signed valleys/shoulders,
   compression, crown stretching and destination-space normal directors.
4. `MixtormatSkinSolve.usf`: restrict full-resolution rest/directors to each mechanics
   level; initialize/prolongate the correction; spend **0–512 total relaxation iterations**
   from coarse to fine, each reading an immutable previous R32F correction. No procedural
   noise is reevaluated inside iterations. Zero iterations returns zero correction.
5. `MicroCS` (`SKIN_STAGE=3`): output-resolution medium/fine wrinkles, warped elliptical
   pores and grain, cached once in RGBA32F. Medium/fine detail is strained, not simulated.
6. `ResolveCS` (`SKIN_STAGE=4`): fine-residual-only directional filtering and the final
   signed Skin height (HeightScale once), written before material evaluation.
7. `FieldCS` (`SKIN_STAGE=2`): seven material/diagnostic outputs, reading resolved relief.
   Cavity AO uses eight wrapped horizon rays with five logarithmic distance samples each,
   local tangent-plane correction and the shared height-to-normal metric. Curvature remains
   a roughness/fuzz cue, not AO. Skin visibility multiplies incoming AO through the existing
   single masked weight; neutral Skin AO cannot erase upstream crevice occlusion.

Tissue and fold seeds use full output resolution, independently of `SolverResolution`.
Only mechanics corrections use bounded grids (default maximum dimension 512, authored
maximum 2048); aspect ratio is retained and smaller outputs are not upscaled. The coarsest
level targets four samples per fold, bounded to 32–128 texels on the longest axis, then
doubles toward the final grid. Half the remaining iteration budget is spent on each coarse
level; the finest consumes the remainder. Default total budget is 128, hard maximum 512.
This improves low-frequency progress over one fine-grid explicit solve; it is not a
convergence guarantee. Correction prolongation is bilinear; final correction reconstruction
is cubic B-spline. Full-resolution analytic folds/pores are never replaced by the grid.
Microdetail uses full output resolution and independent `DetailTiling`.

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
biharmonic stencils use each axis' UV spacing; singleton axes are excluded. A conservative
Hessian row-sum bound controls the explicit relaxation step (safety 0.8). `Bending` and
`Support` are normalized artistic controls, not measured biological material constants.

Lengths and residual heights are normalized by the authored placed fold rate, bounded
by grid resolution. Directors use the placement/PreUV Jacobian; mechanics still operates
in destination UV, not the preview mesh's physical metric. Highly distorted UVs therefore
do not constitute physically metric-correct skin mechanics. State lifetime is per compose,
not persistent real-time dynamics. RDG owns transient reuse; every iteration has distinct
logical output state and no read/write alias.

### Shader controls and relief resolve

- `WarpScale` is a float, default **1**, hard range **0.125–8**. It multiplies the
  integer-rounded drift/phase-noise lattice frequencies, independently of `MarbleDrift`;
  higher values make smaller-scale wandering without changing fold or tissue cell counts.
- `MarbleDrift` budgets advection in noise-cell spacings: reference travel `0.34 * amount`,
  divided across five RK2 steps and normalized geometric octave weights. `WarpLayers`
  adds structure, not another full travel budget. Direct fold-phase noise uses bounded
  `0.34 * amount / (1 + amount)` wandering instead of repeating unbounded advection.
- `PoreSize` is a float, default **1**, hard range **0.25–3**. It multiplies ellipse
  radii, independently of `PoreCells` density. Existing variation supplies ellipticity;
  no extra stretch control is introduced. The warped pixel covariance filters both
  ellipse axes with determinant-based area compensation and size-aware periodic support.
- `PoreDepth` remains nonnegative with **no hard maximum**; its ergonomic slider is **0–4**.
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
  height unit. Resolve applies the inverse (**128**) exactly once with `HeightScale`;
  this is a unit conversion, not arbitrary gain or 0–1 normalization. AO/geometry convert
  resolved canonical height back to UV. Cached micro curvature and tissue Hessians already
  use UV relief; the material `Fat` ratio stays `TissueState.r / SkinTissueRelief`.

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
albedo. Set both `PigmentVariation` and `VascularAmount` to zero for uniform midpoint albedo.
AO reads the exact resolved Skin relief, including filtered microdetail. It is a local
height-field visibility approximation, not ray tracing against the preview mesh or substrate.
The separate curvature-based material cavity/filtering remains approximate.
RAM.a remains F0; baked RAMH.a remains height. Fuzz/SSS share the existing Substrate master.
GBuffer/platform support can still limit simultaneous fuzz and per-pixel diffusion SSS;
no renderer settings are changed by this implementation.

Runtime owns five built-in looks: Elephant, Rough, Smooth, Pored and Human. The existing
module Presets menu applies them through shared undo/instance synchronization while
preserving Amount, write flags, enable state, destination bindings and scoped masks.
Strict JSON preset loading still requires the complete current reflected schema.

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