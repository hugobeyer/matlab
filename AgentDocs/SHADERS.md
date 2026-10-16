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

## Shared Gaussian Blur — consolidation in progress

- `MixtormatGpuGaussianBlur.h/.cpp` owns private typed dispatch;
  `MixtormatGaussianBlur.usf` owns horizontal, vertical and resolve stages.
- Radius X/Y are independent output-texel radii: sigma = radius / 2,
  integer texel taps through ceil(3 * sigma), periodic wrapping, no support cap.
- Full-precision numerator and mass survive both axes. Non-finite samples are
  excluded before weighting; no per-row normalization biases the 2D result.
- Flow uses the compositor's separate validity texture. Only valid samples
  contribute; invalid destination vectors and the validity texture are preserved.
  Flow magnitude is not normalized; packed Z/W are not treated as validity.
- Amount/scoped mask, coverage clamp and encoded-normal renormalization run
  only in the final resolve. Signed Height stays unclamped. Output retains the
  source format; scalar sources may be R16F or R32F.
- Border Height smoothing, Height-mask smoothing and layer-input Height smoothing
  use this service. Border smoothing no longer clamps signed Height.
- Authored Mask and post-Curvature Blur now use this service, with separate
  ordered `FMixtormatBlur` payloads and per-module Amount. The old Mask Blur
  shader and shader class are removed. Mask-owned Blur post-filters completed
  coverage before merge; nested Curvature Blur never pre-filters its input.
- Layer Blur and Height Feather have not yet migrated. Flow Feather remains
  intentionally unchanged. Broader owner scheduling/publication is still pending.
- User confirmed the preceding shared-Gaussian/internal-smoothing checkpoint
  compiled. This Mask integration is source-reviewed only; no new build, shader
  compilation, runtime checks or tests run.

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

## Adding a shader parameter

Trace the full path: CPU declaration (the struct's owning runtime header, e.g.
`MixtormatLayerTypes.h` / `MixtormatGeneratorTypes.h` / `MixtormatMaskTypes.h`) →
gather (`Compositing/Mixtormat*Gather.cpp`) → dispatch/binding (`MixtormatGpu*Passes.cpp`)
→ defaults → inspector metadata → `.usf`/`.ush` (+ `// @param` tag).



Read REVIEWS.md, there are shader necessities there maybe.