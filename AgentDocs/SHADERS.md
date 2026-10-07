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
`MixtormatGully.ush`, `MixtormatCellular.ush`, `MixtormatGeneratorWarp.ush`.

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

## Structural Warp / bundle alignment (steps 5–6)

- `FMixtormatGeneratorStructuralWarpCS` → `MixtormatGeneratorStructuralWarp.usf::MainCS`.
- Fresh per-target RG32F D / R32F B outputs compose `D_new = d + sample(D_old, psi)` and
  `B_new = sample(B_old, psi)`. State lives in `GeneratorStructuralDisplacements` and
  `GeneratorHeightPushFields`; this does not resample finished Strata outputs.
- Flow uses the stage-8 reference trace helper with amount once; scoped masks gate the
  resulting displacement after tracing. UVMap uses periodic lifted-coordinate sampling.
- Strata applies placement once at warped coordinates; gradients are `g*A*J + grad(B)`
  where applicable. B's destination gradient receives no second J. Active warp writes a
  direct RG32F negative-inside distance/validity boundary; inactive retains bundle stage 8.
- `RegisterNamedMask` producer descriptors select bundle stages 10 (ID-anchored attributes)
  and 11 (owner/phase-aware bed T), plus validity-aware distance remapping retaining
  `length(g_source)` in the metric ratio. All companions read old IDs/boundaries from one
  immutable snapshot. Apply owns Height/Coverage; `PebbleCoverage` aliases moved Coverage.
- `CrackDistance` remains a crack-cell attribute, distinct from the internal UV boundary.
  Other structural targets and Noise semantics remain gated for step 7.

Implementation and source review are not compile/runtime validation. Only the user's earlier
step-2 compile is confirmed; the gather missing-header fix is present, newest build unconfirmed.
Broken StructuralWarp tests were removed at user request; no agent build, shader compile,
runtime or test results are claimed. Raster derivatives/distances remain approximations;
legacy centre/orientation inverse handling and geological defects are not fixed here.

## Adding a shader parameter

Trace the full path: CPU declaration (the struct's owning runtime header, e.g.
`MixtormatLayerTypes.h` / `MixtormatGeneratorTypes.h` / `MixtormatMaskTypes.h`) →
gather (`Compositing/Mixtormat*Gather.cpp`) → dispatch/binding (`MixtormatGpu*Passes.cpp`)
→ defaults → inspector metadata → `.usf`/`.ush` (+ `// @param` tag).
