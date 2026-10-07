# File Map

Task → likely files, and symbol → owning file. Search the symbol first; open
the range, not the file.

## Task → files

| Task | Start here |
|---|---|
| Add/change a layer or child field | `Runtime/Public/MixtormatMaterial.h` |
| Add a child type | `EMixtormatLayerChildType` → `Widgets/MixtormatChildCapabilities.cpp` → `Widgets/Layers/MixtormatLayerChildren.cpp` → inspector → gather |
| Add an effect | `Runtime/Public/MixtormatEffect.h` → `Effects/Mixtormat*Passes.cpp` → `Shaders/Private/Mixtormat*.usf` → `Widgets/Inspector/MixtormatInspectorEffects.cpp` |
| Add a generator | `GENERATORS.md` |
| Add a mask producer | `MixtormatGpuMaskPasses.cpp` + `Compositing/MixtormatMaskGather.cpp` + `Shaders/Private/MixtormatMask*.usf` |
| Add an ID producer | `MixtormatGpuPatternPasses.cpp` + `Compositing/MixtormatIdGather.cpp` + `Shaders/Private/Mixtormat*Ids.usf` |
| Change a parameter default/range | `UI/Parameters/MixtormatParameterUiMeta.*`, `MixtormatParameterAuthoring.*`, `Config/MixtormatParameterAuthoring.json` |
| Change a hard bound / sanitize | `Runtime/Public/MixtormatParameterDefinition.h` |
| Change preview behaviour | `Widgets/SMixtormatPreviewViewport.*`, `Widgets/SMixtormat_Preview.cpp`, `MixtormatGpuDebugPreviewPasses.cpp` |
| Change bake | `Compositing/MixtormatBakeService.*`, `Widgets/Dialogs/SMixtormatBake*Dialog.*` |
| Change theme/style | `Style/MixtormatDesignTokens.h`, `Style/MixtormatThemeStore.*`, `Config/UIStyleTheme.json` |
| Change layer hierarchy UI | `UI/Layers/*`, `Widgets/Layers/*` |
| Change clipboard / copy output | `Widgets/Layers/MixtormatLayerClipboard.cpp`, `Widgets/MixtormatChildCapabilities.*` |
| Change references / instances | `Runtime/Public/MixtormatParameterBinding.h`, `MixtormatOutputReference.h` |
| Change group behaviour | `Runtime/Public/MixtormatLayerGroups.h` |
| Change scoped masks | `Runtime/Public/MixtormatChildScope.h` |

## Symbol → owner

| Symbol | File |
|---|---|
| `FMixtormatLayer`, `FMixtormatLayerChild`, `UMixtormatMaterial` | `Runtime/Public/MixtormatMaterial.h` |
| `EMixtormatLayerChildType`, `EMixtormatGeneratorType`, `EMixtormatParameterOwnerType` | `Runtime/Public/MixtormatMaterial.h` |
| `EMixtormatEffectType`, `MixtormatEffectClassOf` | `Runtime/Public/MixtormatEffect.h` |
| `MixtormatParameterContracts::*` | `Runtime/Public/MixtormatParameterDefinition.h` |
| `MixtormatParameterBinding::*` | `Runtime/Public/MixtormatParameterBinding.h` |
| `MixtormatLayerGroups::*` | `Runtime/Public/MixtormatLayerGroups.h` |
| `MixtormatChildScope::*` | `Runtime/Public/MixtormatChildScope.h` |
| `MixtormatOutputReferences::*` | `Runtime/Public/MixtormatOutputReference.h` |
| `FMixtormatMaskShaping` | `Runtime/Public/MixtormatMaskShaping.h` |
| `FMixtormatScalarRamp`, `FMixtormatColorRamp` | `Runtime/Public/MixtormatScalarRamp.h`, `MixtormatColorRamp.h` |
| `FMixtormatGpuCompositor` | `Shaders/Public/MixtormatGpuCompositor.h` |
| `EnqueueCompose` | `Shaders/Private/MixtormatGpuComposePipeline.cpp` |
| `AddGeneratorLayerPasses` | `Shaders/Private/MixtormatGpuGeneratorPasses.cpp` |
| `GatherGeneratorChild` | `Shaders/Private/Compositing/MixtormatGeneratorGather.cpp` |
| `GetChildCapabilities` | `Editor/Private/Widgets/MixtormatChildCapabilities.cpp` |
| `SMixtormat` | `Editor/Private/Widgets/SMixtormat.h` |
| `MixtormatParameterUi` | `Editor/Private/UI/Parameters/MixtormatParameterUiMeta.h` |
| `MixtormatParameterAuthoring` | `Editor/Private/UI/Parameters/MixtormatParameterAuthoring.h` |
| `MixtormatTokens` | `Editor/Private/Style/MixtormatDesignTokens.h` |

## Large files — read by section

| File | ~Lines | Sections / useful symbols |
|---|---|---|
| `Runtime/Public/MixtormatMaterial.h` | 4,400 | enums L24–3706; structs L92–3871; `FMixtormatLayer` L3933; `UMixtormatMaterial` L4311 |
| `Shaders/Private/MixtormatGpuGeneratorPasses.cpp` | 2,400 | `AddGeneratorLayerPasses` L2312; module combine L780 |
| `Shaders/Private/MixtormatGpuCompositor.cpp` | 1,700 | `RequestComposeInternal` L1685; gather dispatch |
| `Shaders/Private/MixtormatGpuComposePipeline.cpp` | 1,000 | `EnqueueCompose` L834; final AO/normal L29–73 |
| `Editor/Private/Widgets/SMixtormat.cpp` | 1,600+ | main widget; split across `SMixtormat_*.cpp` |
| `Editor/Private/Style/MixtormatDesignTokens.h` | 830 | `namespace MixtormatTokens` |
| `Config/UIStyleTheme.json` | 570 | `global` / `controls` / `layers` / `typography` sections |

`SMixtormat` is decomposed into `SMixtormat_Document.cpp`, `_Inspector.cpp`,
`_Layers.cpp`, `_Library.cpp`, `_Parameters.cpp`, `_Preview.cpp`, `_Shell.cpp`,
`_Theme.cpp` — search the member function, not the header.
