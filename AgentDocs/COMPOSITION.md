# Composition

The GPU compositor turns an authored `UMixtormatMaterial` into BaseColor /
Normal / RAM / Height render targets. Gather (game thread) → RDG passes
(render thread).

Warp filtering is automatic, with no Slope Feather / Warp AA artist controls.
Own-height Warp gradients use a wrapped 3×3 Sobel kernel; Deform retains central
differences. Post-generation Warp filters signed height using its UV Jacobian's
source-pixel footprint, blending extra filtering only above a one-texel footprint.
Identity-sized footprints keep ordinary bilinear sampling. IDs, coverage and SDF
fields retain their existing transport; filtering does not clamp displacement or
prevent genuine UV folds. Inherited/traced Flow is not implicitly blurred.

Flow diagnostics use `MixtormatDiagnosticOutputs::AccumulatedFlow`, a preview-only
registry name, not a copyable authored source. The ordered generator chain retains
immutable RDG Flow/validity references after each scoped producer or Behavior;
Sculpt/Deform retain the Flow they consume, Warp retains its transported result,
and direct Flow Feather retains its filtered revision. A demanded Feather under a
scoped Noise producer computes an isolated composition snapshot at that filter's
position without altering the real running sum. Height-less root Noise exposes its
completed generated/directional result, including its Flow filters. Demand includes
the owning generator so native Flow is evaluated even for a selected consumer.
These are generator-scope results, not a new cross-layer global Flow channel.
Pre-generation Warp publishes its composed coordinate map. Existing diagnostic
render targets are reused; no persistent per-module preview targets are allocated.

## Feather Blur

Feather's Blur mode uses separable horizontal/vertical Gaussian passes for both
signed Height and Flow. Width retains the former kernel's sigma of Width / 2;
unit-texel taps cover three sigma rather than spreading a fixed 5×5 kernel apart.
Sampling wraps at tile edges. Float32 intermediates carry weighted sums and valid
weight, with normalization after the vertical pass. Flow excludes invalid samples
without overweighting sparsely valid rows, keeps the original validity and Z/W
channels, and applies Strength/drivers/scoped masks only to the final result.
Existing Eikonal paths and inspector controls are unchanged.

## Fill Pass Through

A Fill layer optionally sets `bFillPassThrough` (off by default). This is a
layer mode, not another layer type or a preset that destructively zeroes
Channel Influence. Authoring values remain intact when toggling modes.

Pass Through uses the prior ping-pong composite (BC / N / RAM incl. F0 /
Height) as the Fill's private, untiled source via `AddLayerInputPass`, ahead
of region producers, masks, and effects. The layer input pass copies previous
texels into independent output-space textures to avoid aliasing the substrate
when scoped effects transform the input. Composite explicitly bypasses
Fill-value overrides, authored HSV / roughness shaping and direct height
replacement while retaining local masks, Grade, effect normal/AO/height, and
post-composite Layer Blur. It preserves existing occupancy instead of treating
the inherited surface as a new covering surface. Existing Fill mode, shader
parameters, and saved authored values are unaffected when the mode is off.

The Fill Properties inspector shows a Fill / Pass Through segmented control;
Fill channels and their Channel Influence / Composition controls hide in Pass
Through. The Layers row displays the short `PASS` badge. Other layer kinds
cannot enable this behavior; `GatherLayerRenderData` guards it by Fill type.

Surface IDs feature source chips (Primary / Secondary) use full-width rows so
long options such as Normal Flatness remain readable at the authored Inspector
width; producer math and feature enumeration are unchanged.

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

Scoped Behavior validation follows generator ownership, not the enclosing layer
kind. Layer types are `Material` and `Fill`; generator modules live inside `FMixtormatLayer::Children`
on either layer type (detected via `FMixtormatLayer::HasGeneratorModules()`). A Behavior requires a valid owning generator.
Layer-owned Height/Flow/UV input references and published Noise Value mask sources
likewise validate their child producers independently of the enclosing layer kind.
Whole-layer generator output previews also accept Fill and Material layers with
generator modules. Input typing, enabled-state, scope and ordering checks remain.

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

Peeling is authored procedurally. Effect assets no longer carry peel shape defaults
or source-texture identity; layer/group asset additions use parameter authoring
defaults. The obsolete shipped-effect texture importer has been removed.

## Masks

- `MixtormatMask.h`, `MixtormatMaskShaping.h`, `MixtormatBlur.h`,
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

### Owner-typed Blur — Mask integration checkpoint

- Canonical `FMixtormatBlur` replaces the mask-only payload. Enabled, Radius X/Y,
  Amount and future root-Layer channel flags live in `MixtormatBlur.h`.
- Gather retains each direct Mask-owned Blur independently, in authored order;
  radii no longer sum or clamp to a shared tap budget. Parameter contracts sanitize
  non-finite values, nonnegative radii and Amount in [0,1].
- Mask placement/shaping completes first; Curvature filters then complete in order,
  each followed by its own nested Blur. Direct Mask Blurs post-filter the completed
  coverage before merge. Sibling Blur/Curvature ordering does not select an operation rail.
- Enabled zero-weight Curvature is identity but retains its nested Blur. Disabling
  Curvature suppresses its subtree. Mask and post-Curvature Blur use the shared
  typed Gaussian; the superseded Mask Blur shader/class are removed.
- Blur supports scoped copy/paste and instances under Mask or Curvature; root paste
  remains unavailable until Layer scheduling lands. Radius links work in layer/group
  creation; instance resolution and reflected prefix hashing include the new payload.
- Layer/Generator/Flow/effect-owned Blur scheduling and publication/source connections
  remain pending. Existing LayerBlur Effect and Flow Feather are not migrated here.
- Source review only for this checkpoint; the user confirmed the preceding shared
  Gaussian/internal-smoothing checkpoint compiled. No new compile/runtime results claimed.

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

Generator module node caches stay
settings-only and are safe: they hold input-independent fields, while input
combination is never node-cached.

## Published fields / preview / debug

- `EMixtormatDebugPreviewMode`, `EMixtormatPreviewOutputKind`,
  `FMixtormatChildPreviewTarget` — `MixtormatGpuCompositor.h`.
- `GetChildCapabilities` (`Editor/Private/Widgets/MixtormatChildCapabilities.cpp`)
  is the single source of what a child publishes (previewable / copyable).
- Debug blit: `MixtormatGpuDebugPreviewPasses.cpp` + `MixtormatDebugPreviewBlit.usf`.
