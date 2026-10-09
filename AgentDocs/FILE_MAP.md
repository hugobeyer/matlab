# File Map

Task → likely files, and symbol → owning file. Search the symbol first; open
the range, not the file.

## Task → files

| Task | Start here |
|---|---|
| Add/change a layer or child field | `Runtime/Public/MixtormatLayerTypes.h` |
| Add/change a mask payload | `Runtime/Public/MixtormatMaskTypes.h` |
| Add/change an ID payload | `Runtime/Public/MixtormatIdTypes.h` |
| Add/change a generator payload | `Runtime/Public/MixtormatGeneratorTypes.h` |
| Add/change a parameter/reference/driver type | `Runtime/Public/MixtormatParameterTypes.h` |
| Add a child type | `EMixtormatLayerChildType` (`MixtormatLayerTypes.h`) → `Widgets/MixtormatChildCapabilities.cpp` → `Widgets/Layers/MixtormatLayerChildren.cpp` → inspector → gather |
| Add an effect | `Runtime/Public/MixtormatEffect.h` → `Effects/Mixtormat*Passes.cpp` → `Shaders/Private/Mixtormat*.usf` → `Widgets/Inspector/MixtormatInspectorEffects.cpp` |
| Add a generator | `GENERATORS.md` |
| Add a mask producer | `MixtormatGpuMaskPasses.cpp` + `Compositing/MixtormatMaskGather.cpp` + `Shaders/Private/MixtormatMask*.usf` |
| Add an ID producer | `MixtormatGpuPatternPasses.cpp` + `Compositing/MixtormatIdGather.cpp` + `Shaders/Private/Mixtormat*Ids.usf` |
| Change a parameter default/range | `UI/Parameters/MixtormatParameterUiMeta.*`, `MixtormatParameterAuthoring.*`, `Config/MixtormatParameterAuthoring.json` |
| Change a hard bound / sanitize | `Runtime/Public/MixtormatParameterDefinition.h` |
| Change preview behaviour | `Widgets/SMixtormatPreviewViewport.*`, `Widgets/SMixtormat_Preview.cpp`, `MixtormatGpuDebugPreviewPasses.cpp` |
| Change bake | `Compositing/MixtormatBakeService.*`, `Widgets/Dialogs/SMixtormatBake*Dialog.*` |
| Change theme/style | `Style/MixtormatTheme.h`, `MixtormatThemeSchema.cpp`, `MixtormatResolvedStyle.*`, `MixtormatThemeStore.*`, `MixtormatDesignTokens.h`, `Config/UIStyleTheme.json` |
| Change UI STYLE locator | `Style/MixtormatStyleLocator.*`, `MixtormatLocatorOutline.*`, `MixtormatThemeSchema.cpp` |
| Add/change an icon | `AgentDocs/ICONS.md` → `UI/Atoms/MixtormatIcons.*` → `Style/MixtormatStyle.cpp` (`SetSvgIcon` / `SetPngIcon`) → `Resources/Icons/` |
| Add/change a tooltip, hint or hotkey text | `AgentDocs/HELPERS.md` → the widget's `.ToolTipText`, menu `.Shortcut`, or the key handler (`SMixtormat.cpp::OnKeyDown`, viewport `InputKey`) |
| Change layer hierarchy UI | `UI/Layers/*`, `Widgets/Layers/*` |
| Change clipboard / copy output | `Widgets/Layers/MixtormatLayerClipboard.cpp`, `Widgets/MixtormatChildCapabilities.*` |
| Change references / instances | `Runtime/Public/MixtormatParameterBinding.h`, `MixtormatOutputReference.h` |
| Change group behaviour | `Runtime/Public/MixtormatLayerGroups.h` |
| Change scoped masks | `Runtime/Public/MixtormatChildScope.h` |

## Symbol → owner

| Symbol | File |
|---|---|
| `FMixtormatLayer`, `FMixtormatLayerChild`, `FMixtormatLayerGroup`, `EMixtormatLayerChildType`, layer enums | `Runtime/Public/MixtormatLayerTypes.h` |
| `UMixtormatMaterial`, `FMixtormatFinalSettings`, `MixtormatCompositionReferences` | `Runtime/Public/MixtormatMaterial.h` |
| `EMixtormatGeneratorType`, `FMixtormatGenerator`, generator payloads, `MixtormatCanOwnGeneratorFlow`, `MixtormatGeneratorHasFlowBoundary` | `Runtime/Public/MixtormatGeneratorTypes.h` |
| `EMixtormatParameterOwnerType`, `FMixtormatParameterAddress`, `FMixtormatParameterBinding`, driver/reference types | `Runtime/Public/MixtormatParameterTypes.h` |
| `EMixtormatHeightOp`, `FMixtormatHeightBlend` | `Runtime/Public/MixtormatHeightTypes.h` |
| `FMixtormatMaskLayer`, `FMixtormatGeneratedMask`, `FMixtormatColorIdMask`, `FMixtormatRandomIdMask`, `FMixtormatCraquelure`, `EMixtormatMaskSource` (incl. inline `Noise`) | `Runtime/Public/MixtormatMaskTypes.h` |
| `FMixtormatClusterFilter`, `FMixtormatHsvIdFilter`, `FMixtormatPatternFilter`, `FMixtormatIdGroup`, `FMixtormatRampIdFilter`, `FMixtormatUvIdFilter`, `FMixtormatReliefIdFilter`, `FMixtormatBoundaryIdFilter`, `FMixtormatCombineIdFilter` | `Runtime/Public/MixtormatIdTypes.h` |
| `FMixtormatLayerEffect`, `EMixtormatGradeTonemap`, `EMixtormatStainMode`, `EMixtormatPeelType` | `Runtime/Public/MixtormatEffect.h` |
| `EMixtormatEffectType`, `MixtormatEffectClassOf` | `Runtime/Public/MixtormatEffect.h` |
| `MixtormatParameterContracts::*` | `Runtime/Public/MixtormatParameterDefinition.h` |
| `MixtormatParameterBinding::*` | `Runtime/Public/MixtormatParameterBinding.h` |
| `MixtormatLayerGroups::*` | `Runtime/Public/MixtormatLayerGroups.h` |
| `MixtormatChildScope::*` | `Runtime/Public/MixtormatChildScope.h` |
| `MixtormatOutputReferences::*` (incl. `ResolvePublishedMaskSource`) | `Runtime/Public/MixtormatOutputReference.h` |
| `FMixtormatMaskShaping` | `Runtime/Public/MixtormatMaskShaping.h` |
| `FMixtormatScalarRamp`, `FMixtormatColorRamp` | `Runtime/Public/MixtormatScalarRamp.h`, `MixtormatColorRamp.h` |
| `FMixtormatGpuCompositor` | `Shaders/Public/MixtormatGpuCompositor.h` |
| `EnqueueCompose` | `Shaders/Private/MixtormatGpuComposePipeline.cpp` |
| `AddGeneratorLayerPasses` | `Shaders/Private/MixtormatGpuGeneratorPasses.cpp` |
| `GatherGeneratorChild` | `Shaders/Private/Compositing/MixtormatGeneratorGather.cpp` |
| `GetChildCapabilities` | `Editor/Private/Widgets/MixtormatChildCapabilities.cpp` |
| Mask sources / Noise gate (`CreateNoiseGate`, `SelectMaskNoiseValue`, `BuildMaskNoiseValueMenu`) | `Editor/Private/Widgets/Layers/MixtormatMaskSources.cpp` |
| `AddNoiseMaskPass`, `AddNoiseCoveragePass` | `Shaders/Private/MixtormatGpuNoisePasses.cpp` |
| `SMixtormat` | `Editor/Private/Widgets/SMixtormat.h` |
| `MixtormatParameterUi` | `Editor/Private/UI/Parameters/MixtormatParameterUiMeta.h` |
| `MixtormatParameterAuthoring` | `Editor/Private/UI/Parameters/MixtormatParameterAuthoring.h` |
| `MixtormatTokens` | `Editor/Private/Style/MixtormatDesignTokens.h` |
| `FMixtormatTheme`, `EMixtormatIconRole` | `Editor/Private/Style/MixtormatTheme.h` |
| `FMixtormatThemeSchema` | `Editor/Private/Style/MixtormatThemeSchema.*` |
| `FMixtormatStyleLocator`, `SMixtormatLocatorOutline` | `Editor/Private/Style/MixtormatStyleLocator.*`, `MixtormatLocatorOutline.*` |

## Large files — read by section

| File | ~Lines | Sections / useful symbols |
|---|---|---|
| `Runtime/Public/MixtormatEffect.h` | 1,180 | effect enums + `UMixtormatEffect`; `FMixtormatLayerEffect` L268 |
| `Runtime/Public/MixtormatGeneratorTypes.h` | 890 | generator enum + payloads; `FMixtormatGenerator` L853 |
| `Runtime/Public/MixtormatIdTypes.h` | 845 | ID filters; `FMixtormatPatternFilter` L329 |
| `Runtime/Public/MixtormatLayerTypes.h` | 680 | layer enums; `FMixtormatLayerChild` L164; `FMixtormatLayer` L328 |
| `Runtime/Public/MixtormatMaskTypes.h` | 600 | mask enums + payloads; `FMixtormatMaskLayer` L90 |
| `Runtime/Public/MixtormatParameterTypes.h` | 240 | parameter address/reference/driver/binding |
| `Runtime/Public/MixtormatMaterial.h` | 134 | `FMixtormatFinalSettings` L16; `UMixtormatMaterial` L76 |
| `Shaders/Private/MixtormatGpuGeneratorPasses.cpp` | 2,400 | `AddGeneratorLayerPasses` L2312; module combine L780 |
| `Shaders/Private/MixtormatGpuCompositor.cpp` | 1,700 | `RequestComposeInternal` L1685; gather dispatch |
| `Shaders/Private/MixtormatGpuComposePipeline.cpp` | 1,000 | `EnqueueCompose` L834; final AO/normal L29–73 |
| `Editor/Private/Widgets/SMixtormat.cpp` | 1,600+ | main widget; split across `SMixtormat_*.cpp` |
| `Editor/Private/Style/MixtormatDesignTokens.h` | 830 | `namespace MixtormatTokens` |
| `Config/UIStyleTheme.json` | 570 | `global` / `controls` / `layers` / `typography` sections |

`SMixtormat` is decomposed into `SMixtormat_Document.cpp`, `_Inspector.cpp`,
`_Layers.cpp`, `_Library.cpp`, `_Parameters.cpp`, `_Preview.cpp`, `_Shell.cpp`,
`_Theme.cpp` — search the member function, not the header.
