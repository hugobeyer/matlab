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

## Color Ramp Base Color

Generators evaluate top-to-bottom in child order. Each Color Ramp captures its
resolved Height / Flow / UV / IDs and independent scoped mask at its row, before
later producers change those fields. Color compositing then runs bottom-to-top,
so visually higher ramps blend over lower ramps without reversing dependencies.

Scoped Blur under a Color Ramp pre-filters the ramp's raw evaluated RGBA (`bMapOnly = true`
in `AddGeneratorHeightColorRampPass`) using `EField::ColorAlpha` (straight RGBA in/out,
premultiplied math). This filters the evaluated colors before gate masking and before
blending over previous ramp layers or Base Color, eliminating transparent RGB halos and
preventing lower-ramp smearing. The filtered color is then composited bottom-to-top
in `AddGeneratorColorCompositePass`.

The bottom ramp starts from the layer's authored Base Color override when enabled,
otherwise its resolved input color. Referenced Color and Fill Pass Through keep
priority over that override. Higher ramps blend against the accumulated color,
using their own blend mode and Weight × stop alpha × ramp-scoped mask. Each ramp
publishes and previews its accumulated result over the ramps below it. Color
references read earlier layers, after those layers complete their ramp stacks.

The override is applied only when seeding the ramp chain; layer grading, layer
masks, opacity and Base Color blend with the composite below remain in the layer
compositor.

Color Ramp's Signed Flow toggle defaults off, preserving one-sided magnitude plus
directional bias. When enabled, Flow modulation uses the signed projection of
Flow onto Flow Angle: aligned Flow shifts positively, opposite Flow negatively,
and perpendicular Flow contributes zero. Flow Shift scales either mode (including
negative strength); ramp Weight, source selection and upstream Flow are unchanged.

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

### Owner-typed Blur integration

- Canonical `FMixtormatBlur` (`MixtormatBlur.h`) provides the shared parameter contract:
  - Modes: `Gaussian`, `Directional`, `Smear`.
  - Targets (`EMixtormatBlurTarget`): `Height`, `Flow`, `HeightAndFlow` (Both).
  - Direction sources: `Angle` (degrees), `Flow` (guided by an explicit published flow reference).
  - Radii / Length, Amount, per-channel weights (BaseColor, Normal, Roughness, AO, Metallic, F0, Height).
- Scheduling and execution:
  - **Root Blurs:** Gathered as `SurfaceBlurs` on the layer. Dispatched in `AddLayerSurfaceBlurPasses`
    over isolated layer buffers (`OutputBC`, `OutputN`, `OutputRAM`, `OutputHeight`) before composite merge.
  - **Masks and Curvature:** Evaluated in `MixtormatGpuMaskPasses.cpp` (`ApplyBlurs`). Mask shaping
    runs first; Curvature filters run in order with nested blurs; direct mask blurs post-filter coverage before merge.
  - **Generators and Behaviors:**
    - Scoped under non-Noise generators: `ApplyGeneratorPostBehaviors` runs direct blurs on native height
      (if `Target != Flow`) and native Flow (if `Target != Height`). Downstream behaviors (e.g. Sculpt, Warp)
      receive the filtered `WorkingFlow`.
    - Scoped under top-level Noise: Height is blurred in `ApplyGeneratorPostBehaviors`. Flow filtering
      runs after `AddNativeFlowPass` (lines 4833–4865), filtering `GeneratedFlow` and `FlowDirection` without
      duplicate filtering passes.
    - Scoped under a PostGeneration Behavior: Filters that behavior's published `FlowDirection`.
  - **Color Ramps:** Scoped blur pre-filters raw evaluated RGBA in premultiplied space (`ColorAlpha`)
    before gate masking and bottom-to-top blending, preventing transparent RGB halos and lower-ramp smearing.
- Guidance and Cache demand:
  - Flow-guided blurs register demand for `DirectionSourceKey` in `PublishedFieldDemand` ahead of prefix cache lookup.
  - If a referenced flow field is missing or invalid, the filter defaults safely to identity without dispatching or bridging.
  - Demanded fields survive cache restore via `FieldDemandCovered` and `RegisterExternalTexture`.
- Single-point application: Filter masks, Amount, channel weights, and coverage clamp each apply exactly once at the resolve stage.
- Field preservation: Normals are renormalized in resolve; Flow fields preserve separate validity and untouched Z/W components.

## Structural height

Ramp / Relief From IDs, Pattern relief, Craquelure relief and Worn Edges operate
on the accumulated surface after the layer merge, including when Breakup or root
Blur requires isolated incoming-material preparation. Erosion / Breakup retain
that preparation path; root Blur filters it before the merge. Structural relief
then runs in order: Ramp / Pattern → Craquelure → Worn Edges.

Height remains signed displacement: these stages do not clamp it to coverage
range or consume per-region 0..1 headroom. Ramp blend modes use the shared mask
operation on a neutral local 0.5 signal and add the resulting delta to the
substrate, preserving its detail. Pattern face, bevel, gap and feather remain
independent controls. Final Auto Remap and shared mask shaping are unchanged.
Worn Edges also wears equal-height ID boundaries; its published coverage follows
the feathered carve, and normals reconcile from the actual written height.

## IDs

HSV, Exact Color ID, Random From IDs, Ramp / Relief, implicit Boundary and Worn
Edges share `FindRegionIdsAboveWithIndex`: select the nearest authored earlier
producer in the consumer's scope (or its owning ID producer), then read that
producer's map. Missing/deferred publication never falls back to older IDs.
Color Range retains its color-source path. Explicit Boundary references retain
typed, completed-publication and same-layer earlier-row checks. UV From IDs
runs before source sampling; deferred IDs stay unavailable rather than creating
a source-sampling cycle. Private ID Group maps remain private.

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

## Bake Pipeline & Custom Material Masters

- `Editor/Private/Compositing/MixtormatBakeService.*` — orchestrates GPU composition, asset creation, and material instance wiring.
- Generates 7 coordinated output assets:
  - Textures: Base Color (`T_*_BC`), Normal (`T_*_N`), packed RAM (`T_*_RAM`), Height (`T_*_H`), RAMH (`T_*_RAMH`).
  - Instances & Surfaces: Material Instance (`MI_*`), Surface (`SF_*`).
- Material Parent modes (`EMixtormatBakeMaterialParentMode`):
  - **Mixtormat Default**: Targets `/Mixtormat/Materials/Masters/M_Master_Mixtormat.M_Master_Mixtormat`. Binds `Result.RAM` to `DA_RAMH`, while `Result.RAMH` is generated for the `UMixtormatSurface` definition.
  - **Custom Material**: Accepts any user-specified `UMaterialInterface`. Discovers texture parameters across material and function expressions. Mappings support Base Color, Normal, Packed RAM, and Height, with explicit Unassigned options.
- Identity-aware override tracking: `UMixtormatMaterial::LastAppliedBakeOverrides` tracks the target Material Instance identity and parameter keys. Re-baking safely resets only previously applied Mixtormat overrides without modifying user-authored parameters on existing instances.
- Validation preflight executes in `SMixtormat::ExecuteBake` before GPU composition and asset generation, checking master validity, parameter name collisions, and G16 sampler compatibility.
