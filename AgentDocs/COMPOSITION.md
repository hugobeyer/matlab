# Composition

The GPU compositor turns an authored `UMixtormatMaterial` into BaseColor /
Normal / RAM / Height render targets. Gather (game thread) → RDG passes
(render thread).

## Entry points

| Thing | File |
|---|---|
| `FMixtormatGpuCompositor` (public API) | `Shaders/Public/MixtormatGpuCompositor.h` |
| `RequestComposeInternal` (gather dispatch) | `Shaders/Private/MixtormatGpuCompositor.cpp` |
| `EnqueueCompose` (RDG graph) | `Shaders/Private/MixtormatGpuComposePipeline.cpp` |
| Shared private surface, render data, caches | `Shaders/Private/MixtormatGpuCompositorInternal.h` |

`RequestCompose` takes layers (+ optional groups). Groups are flattened by
`MixtormatLayerGroups::BuildEffectiveLayers` before anything else, so every pass
sees a flat array of the same length and order.

## Gather

`Shaders/Private/Compositing/`:

| File | Gathers |
|---|---|
| `MixtormatLayerGather.*` | layer source, placement key, fields |
| `MixtormatMaskGather.*` | mask children |
| `MixtormatIdGather.*` | ID producers |
| `MixtormatGeneratorGather.*` | generator modules |
| `MixtormatEffectGather.*` | effects |
| `MixtormatComposeHash.*` | content/field hashing |
| `MixtormatGatherCommon.h` | `FLayerRenderData` and shared helpers |

## Passes

`Shaders/Private/MixtormatGpu*Passes.cpp`:

| File | Category |
|---|---|
| `MixtormatGpuMaskPasses.cpp` | masks |
| `MixtormatGpuPatternPasses.cpp` | region/ID producers (`AddRegionProducerPasses`) |
| `MixtormatGpuSurfaceIdPasses.cpp`, `MixtormatGpuUvIdPasses.cpp`, `MixtormatGpuReliefIdPasses.cpp` | ID variants |
| `MixtormatGpuGeneratorPasses.cpp` | generators |
| `MixtormatGpuEffectPasses.cpp` | effect dispatch |
| `MixtormatGpuSimulationPasses.cpp` | iterative solves (peeling, stain) |
| `MixtormatGpuRunoffPasses.cpp` | runoff |
| `MixtormatGpuNoisePasses.cpp` | noise field producer |
| `MixtormatGpuMaskShaping.cpp` | shared mask shaping |
| `MixtormatGpuDebugPreviewPasses.cpp` | debug/preview blit |

Effect bodies: `Shaders/Private/Effects/Mixtormat{Breakup,Craquelure,Erosion,
FlowWarp,Grade,LayerBlur,WornEdges}Passes.cpp`, `MixtormatEffectCommon.cpp`.

## Masks

- `MixtormatMask.h`, `MixtormatMaskShaping.h`, `MixtormatMaskBlur.h`,
  `MixtormatMaskCurvature.h` (Runtime).
- Shared shaping block `FMixtormatMaskShaping` is embedded by every mask
  producer; shader maths is one `MixtormatShapeMask` in `MixtormatMaskOps.ush`.
- Scoped masks: `MixtormatChildScope::CanOwnScopedMasks` (Runtime) gates who may
  own a scoped Mask child; gather and editor share it.

## IDs

- `EMixtormatPublishedFieldKind` (`MixtormatOutputReference.h`) — append-only.
- Producers publish named outputs; `MixtormatOutputReferences::CanonicalFieldOutputName`
  maps a kind to its canonical name.

## References / instances

- `Runtime/Public/MixtormatParameterBinding.h` — `ResolveChildInstances`,
  `ApplyDirectReferences`, `ResolveLinkTarget`, `TryWrite*/TryResolve*`.
- `Runtime/Public/MixtormatOutputReference.h` — `ValidateDependency`,
  `ResolveSource`, `ResolveEarlierSource`.
- Referenced compositions get their own `FMixtormatGpuCompositor` per source
  asset, recomposited only on content-hash change.

## Caching

`MixtormatGpuCompositorInternal.h`: `FMixtormatNetworkCache` (grown networks),
`FMixtormatPrefixCache` (layer prefixes), `FMixtormatNodeCache`. Keyed on the
parameters that shape them. `ResetCaches()` on resolution change or when final
normal settings change. `LastPrefixHashes` finds the lowest changed layer.

## Published fields / preview / debug

- `EMixtormatDebugPreviewMode`, `EMixtormatPreviewOutputKind`,
  `FMixtormatChildPreviewTarget` — `MixtormatGpuCompositor.h`.
- `GetChildCapabilities` (`Editor/Private/Widgets/MixtormatChildCapabilities.cpp`)
  is the single source of what a child publishes (previewable / copyable).
- Debug blit: `MixtormatGpuDebugPreviewPasses.cpp` + `MixtormatDebugPreviewBlit.usf`.
