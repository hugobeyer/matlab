# Shared rules (paste at the top of every step prompt)

Repo: `C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat`.
- Unreal 5.8 editor plugin "Mixtormat", a layered procedural material compositor.
- RDG compute shaders live in `Shaders/Private/*.usf` / `*.ush`.
- Owner: Hugo.
- Before starting, read the memory index: `C:\Users\hugob\.claude\projects\C--Tools-MaterialLab-MatLab-Plugins-Mixtormat\memory\MEMORY.md`, especially `generator-layer-roadmap.md`.

## Hard rules
- **Do NOT build, launch Unreal, or run tests.** Hugo builds and tests. Validate statically only:
  - Read diffs and grep call sites.
  - Syntax-check HLSL with dxc at `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe`:
    1. Copy `Shaders/Private/*.usf` and `*.ush` to a scratch dir.
    2. Stub `Engine/Public/Platform.ush` as an empty file.
    3. Strip the `/Plugin/Mixtormat/Private/` include prefix.
    4. Rewrite `"/Engine/Public/Platform.ush"` to `"Engine/Public/Platform.ush"`.
    5. Compile every touched entry point and permutation with `-T cs_6_0 -I .` at both `-HV 2018` and `-HV 2021`.
  - For every touched shader, diff the `.usf` globals against the C++ `SHADER_PARAMETER` list, both directions. dxc does not catch a mismatch.
- **No legacy, no back-compat.** The plugin is unshipped.
  - Delete unused code outright: no hidden UPROPERTYs, no deprecated shims, no CoreRedirects, no migration, no "old materials load as defaults" notes.
  - Enums can be reordered or renamed freely.
- **Never add value clamps** in gather, contracts or shaders. Only guard undefined math (division by zero, `sqrt` or `log` of negative numbers, `pow(0, <=0)`).
- **Match the surrounding style:** tabs, and comments that explain *why*, not what.
- **No git commits** unless Hugo asks. Never `git add`; Hugo stages himself.
- **Report** in terse bullets (Hugo has ADHD). End with a checklist for Hugo to test in the editor, with no essay.
- **Python edits on Windows:**
  - Read and write with `newline=""` so line endings survive.
  - Don't put multi-line Python with quotes in a bash heredoc; write the `.py` to a scratch dir and run it.
  - Assert that every replaced snippet matches exactly once.

## Codebase map (verify; line numbers drift)
- Data model: `Source/MixtormatRuntime/Public/MixtormatMaterial.h`.
  - Layer struct `FMixtormatLayer`.
  - Children `FMixtormatLayerChild` and `EMixtormatLayerChildType`.
  - Generators `FMixtormatGenerator` (Strata, Cracks, Rock, Pebbles).
- Gather (UObject data to render data): `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`, the per-layer gather around the generator switch and the `Data.bSmoothHeightMerge` area.
- Render data structs: `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`.
  - `FLayerRenderData`, `FChildRenderData`, `F*RenderData`.
  - `FMixtormatLayerPassContext`, which holds `RegionIdMaps`, `GeneratorFields`, `LayerInputHeight` and more.
  - `PublishRegionIds` and `FindRegionIdsAbove`.
- Layer loop: `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`, in this order:
  1. ID phase: `AddRegionProducerPasses`, `AddGeneratorFieldPasses`, `AddUvIdPasses`.
  2. `AddLayerHeightSmoothPasses`.
  3. `AddGeneratorPasses`.
  4. The child loop.
  5. `CollectPendingRampTilts`.
  6. `AddLayerCompositePass`.
  7. Deferred effects.
  8. Layer blur.
  9. Height snapshots.
- Composite: `AddLayerCompositePass` in `MixtormatGpuCompositor.cpp`, plus `Shaders/Private/MixtormatComposite.usf`. The height contest is `EvaluateHeightBlend`; the final height is written near the end of `MainCS`.
- Substrate (the ground under layer 0): `MixtormatSubstrate` in `MixtormatGpuComposePipeline.cpp`. Its height is 0.5.
- Generators: `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`, which has `AddGeneratorPasses`, `AddGeneratorFieldPasses` and `Add<Rock|Pebbles|Cracks|StrataCarver>Passes`.
- Editor:
  - `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`: panels, `BuildHeightBlendControls`, generator panels.
  - `SMixtormat_Layers.cpp`: add menu, child creation.
  - `MixtormatChildCapabilities.cpp`: what each child publishes, for preview and Copy Output.
  - `UI/Layers/MixtormatLayerBadges.cpp`.
- Parameters and bindings: `Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp`, `MixtormatParameterBinding.cpp`, `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp`, `Config/MixtormatParameterAuthoring.json`.
- Docs: `Docs/index.html`.
- Tests (keep them compiling; update them to the new model): `Source/MixtormatEditor/Private/Tests/*.cpp`.
