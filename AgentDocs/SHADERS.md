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