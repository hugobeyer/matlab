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

## Sources shelf producers

`RequestCompose` also takes the document's `Sources`. Only **demanded** sources
evaluate: `CollectDemandedShelfSources` (`Compositing/MixtormatSourceGather.*`)
seeds from generator `HeightSource`/`WarpSource` inputs and enabled masks bound to shelf Noise `Value`, then closes the set over
producer-to-producer references and returns it in **dependency order**
(a producer always follows the sources it reads). Shelf array order is
organisational only and never affects scheduling or results.

`GatherSourceProducers` gathers each demanded entry as an explicitly source-owned
producer (`FLayerRenderData::bIsShelfSource` / `SourceShelfId`, registry address
`SourceId` tagged `EMixtormatOutputReferenceOwnerKind::Shelf` via `PublishedKey`) --
never a synthetic material layer. Producers are gathered uncached, with
Follow/Link values resolved from real SourceId/ChildId parameter addresses in a
transient producer copy; references to external material-stack layers remain
ineligible for shelf producers.

On the render thread the producer section runs ahead of the layer loop and only
publishes: `AddRegionProducerPasses` + `AddGeneratorLayerPasses` fill
`Ctx.PublishedFieldOutputs` under `{SourceId, 0, OutputName}` (owner Shelf).
Producers never composite, never touch the output targets, and always run ahead of
the stack. Consumer input keys resolve through `ClassifyShelfSourceReference`,
which rejects a switched-off reference (`DisabledReference`), any kind outside the
shelf consumer contract -- ScalarSigned / Flow / UVMap (`UnsupportedOutputKind`)
-- and any output the named producer does not actually publish
(`UnpublishedOutput`, via `GeneratorPublishesField`), alongside the existing
malformed/disabled/missing endpoint checks; those stay unavailable and never fall
back to a layer.

The graph is a DAG, not a position rule: self references contribute no edge, and a
producer that can reach itself is excluded (all cycle members dropped from the
order), so its consumer's input resolves to nothing (a missing field is treated as
unavailable) and no partial producer is ever emitted. A producer's layer-kind
inputs resolve against an empty effective-layer array, so a shelf source can never
read the material stack. Structural modules and OutputReference children remain
layer-only until their own shelf paths land.

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
- Mask sources: Texture, Layer Values, appended inline `Noise` (`FMixtormatMaskLayer::Noise`,
  `UsesNoise()`), or a live published output. `MixtormatMaskGather` stores
  `FMaskRenderData.bNoise/Noise/SourceChildIndex` for the inline path and resolves published
  layer-owned sources through `MixtormatOutputReferences::ResolvePublishedMaskSource`, the
  canonical same-layer completed-earlier-scope check for Noise `Value`. A published
  shelf Noise `Value` instead uses the appended Source owner discriminator and
  SourceShelfId (no material LayerId), with its producer's root at child index 0.
- Noise masks reuse the producer: `AddNoiseMaskPass` (identity placement, no Height/Gradient/ID
  allocation) writes R32 coverage; `AddNoiseCoveragePass` maps typed `ScalarSigned`
  (`saturate(0.5*Value+0.5)`) / `Scalar01` to coverage. Converted coverage is cached per source
  in `FMixtormatComposeContext::NoiseMaskSources`; raw typed Value publication is untouched.
  The typed conversion takes precedence over legacy scalar aliases in `PublishedMaskOutputs`,
  which must not bypass signed-to-coverage conversion. Mask placement/shaping/blur run
  afterwards in the ordinary mask shader.

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

Sources shelf content is folded into the layer-prefix seed
(`RequestComposeInternal`), because producers run uncached and are not layers, so
they never enter the prefix chain themselves. Whenever at least one shelf producer is demanded, all shelf entries contribute
to that seed: CPU Follow/Link parameter dependencies can reference another entry
without needing its GPU output, and changing it must still invalidate cached
composites. Shelf entry order stays irrelevant. Generator module node caches stay
settings-only and are safe: they hold input-independent fields, while input
combination is never node-cached.

## Published fields / preview / debug

- `EMixtormatDebugPreviewMode`, `EMixtormatPreviewOutputKind`,
  `FMixtormatChildPreviewTarget` — `MixtormatGpuCompositor.h`.
- `GetChildCapabilities` (`Editor/Private/Widgets/MixtormatChildCapabilities.cpp`)
  is the single source of what a child publishes (previewable / copyable).
- Debug blit: `MixtormatGpuDebugPreviewPasses.cpp` + `MixtormatDebugPreviewBlit.usf`.
