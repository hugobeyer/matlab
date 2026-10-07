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
`MixtormatGully.ush`, `MixtormatCellular.ush`.

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

## Adding a shader parameter

Trace the full path: CPU declaration (`MixtormatMaterial.h`) → gather
(`Compositing/Mixtormat*Gather.cpp`) → dispatch/binding (`MixtormatGpu*Passes.cpp`)
→ defaults → inspector metadata → `.usf`/`.ush` (+ `// @param` tag).
