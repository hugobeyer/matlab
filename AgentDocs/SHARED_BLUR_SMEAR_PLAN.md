# Shared Blur / Directional Blur / Smear — implementation handoff

Status: plan only. No implementation in this task. Read AGENTS.md first.

## Goal and boundaries

Extend the existing Blur child and shared filtering infrastructure; do not create
separate owner-specific smear implementations. Preserve Gaussian behavior by default.
“Global” means reusable across supported owners, not an automatic whole-stack pass.
Initial coverage: Layer output, Mask, Curvature, Generator, Behavior, and Color Ramp.
Do not advertise support for every module: categorical IDs, other effects and other
owners need explicit output contracts before admission. Whole-stack use remains a
Pass Through Fill over the layers being filtered.

## Verified current implementation

- `Source/MixtormatRuntime/Public/MixtormatBlur.h`: canonical payload, Radius,
  X/Y Scale, Amount, layer channel weights, identity checks and UI range constants.
- `Source/MixtormatRuntime/Private/MixtormatChildScope.cpp`:
  `CanOwnScopedBlurs` accepts Mask, Curvature, Generator and Behavior, not Color Ramp.
- `Source/MixtormatShaders/Private/MixtormatGpuGaussianBlur.h/.cpp`:
  shared scalar/vector/encoded-normal/valid-Flow filtering. Gaussian resolves
  Amount and masking after both axes; ValidFlow requires separate validity.
- `Source/MixtormatShaders/Private/MixtormatGpuBlurPasses.cpp`:
  root Blur filters isolated layer channels, before the layer merge.
- `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp`:
  Mask and Curvature filters use the shared Gaussian path.
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`:
  the inspected generic Generator/Behavior Blur branch filters Module.Height.
  Flow Feather is a separate existing path; ownership permission does NOT prove
  generic Blur already filters all generator outputs.
- Color ramps capture inputs top-to-bottom, then composite bottom-to-top.
  Their current shader maps and blends in one dispatch.
- Parts of COMPOSITION.md/UI.md describe older integration checkpoints. Trust
  current call sites; update only directly affected documentation.

## Required contract

1. Gaussian is the default and retains its existing math, radii and scheduling.
2. Directional Blur averages samples on both sides along a direction/Flow path.
3. Smear uses one-sided trailing samples. Positive Length trails opposite the
   travel direction; negative Length reverses the trail. It is a spatial filter,
   not a temporal paint simulation or repeated feedback operation.
4. Amount remains 0..1: mix original and completed filtered output once.
   Layer channel weights multiply Amount; scoped filter masks gate this mix.
5. Add Direction Source = Angle / Flow, Angle, Flow Source, Flow Influence,
   and Length. Keep Gaussian Radius/X/Y Scale intact. Put defaults/ranges in the
   canonical payload/range definitions, not duplicated inspector literals.
6. Flow Influence is 0..1 steering weight, separate from Amount/channel weights.
   Use normalized Flow for direction; magnitude must not silently multiply Length.
   Blend the angle direction with Flow direction; normalize nonzero blends.
   A zero/cancelled vector gives no displacement. Directional Length is a
   nonnegative extent; Smear may use signed Length. UI must explain their units.
7. Reuse existing typed Flow references and validation. No invented global Flow
   field, implicit nearest-source selection, or automatic substitute source.
   Invalid required Flow leaves the effect unchanged and exposes unavailable
   guidance in the existing UI. Angle mode needs no Flow reference.
8. Capture guidance and masks at the owner's valid evaluation point. Never read
   future producers or the output being written. Existing Flow Feather may smooth
   guidance upstream; do not add a duplicate smoothing system.
9. Sampling wraps at tile boundaries and converts texel distances correctly on
   non-square textures. Preserve signed scalar values. Renormalize normals.
   Flow filtering preserves validity; do not smear categorical region IDs.
10. Keep root Layer Blur before the final layer merge. Layer masks, opacity and
    layer blend modes must not be applied again inside the filter.

## Implementation sequence and files

### 1. Extend the shared model and eligibility

- `Source/MixtormatRuntime/Public/MixtormatBlur.h`: add mode, directional settings,
  typed guidance settings, defaults/ranges and mode-aware identity checks.
- `Source/MixtormatRuntime/Private/MixtormatChildScope.cpp`: admit Color Ramp.
- Preserve existing scopes, instance behavior, copy/paste and authored identities.
- Trace generator/Behavior output eligibility before exposing Flow as a filter
  target. Guidance Flow is not the same as filtering the owner's Flow output.
  Reuse the existing owner/output selector if available; do not overload layer
  channel weights to select a generator field. Missing target support is an
  explicit integration task, not a reason to silently filter Height instead.

### 2. Gather and resolve once

- `Source/MixtormatShaders/Private/Compositing/MixtormatGatherCommon.h`:
  extend `SanitizeBlur` for mode, finite values and new ranges.
- `Source/MixtormatShaders/Private/Compositing/MixtormatMaskGather.cpp`:
  propagate settings for root, Mask and Curvature-owned filters.
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`,
  `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`, and
  `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`:
  gather Generator/Behavior/Color Ramp filters and resolved Flow source addresses.
- Mask-owned filters currently carry FMixtormatBlur arrays. Retain enough owner
  identity for per-filter reference validation and scoped mask evaluation; do not
  lose it when converting authored children into render data.
- Reuse canonical reference validation/selection. Do not relax the existing
  cross-layer OutputReference rules globally to support filter-local guidance.
  Check existing same-layer Flow connection facilities first; if none fit,
  implement explicit completed-earlier-producer validation for the filter socket.
- Verify reflected hashing, referenced-producer demand, cache invalidation,
  instancing and source-shelf contexts include the new settings/references.

### 3. Shared GPU filter

- Keep `MixtormatGpuGaussianBlur.h/.cpp` and `Shaders/Private/MixtormatGaussianBlur.usf`
  as the existing Gaussian implementation. Do not reroute unrelated internal
  Gaussian smoothing through authored directional modes.
- Add one focused shared dispatcher/directional implementation (proposed files:
  `MixtormatGpuDirectionalBlur.h/.cpp`, `MixtormatDirectionalBlur.usf`, under the
  existing shader source directories). Reuse existing field semantics and resolve
  logic rather than creating separate blur kernels per owner.
- Angle guidance uses straight paths. Flow guidance should sample the field along
  the path, reusing the established flow-tracing math where applicable. Inspect
  `Shaders/Private/MixtormatFlowTrace.usf` before implementing trajectory math.
- Use normalized kernel weights, bounded sample counts and source-resolution-aware
  steps. Do not introduce unbounded loops, per-frame feedback, or in-place writes.
- Separate gathering/sampling from final Amount/mask/channel-weight application.
- Add CPU parameters, shader declarations, binding and defaults together.

### 4. Wire every supported owner, not just Layer

- `MixtormatGpuBlurPasses.cpp`: layer channels, preserving isolated-surface merge.
- `MixtormatGpuMaskPasses.cpp`: completed Mask and post-Curvature coverage.
- `MixtormatGpuGeneratorPasses.cpp`: existing generic Blur branches, including
  nested Noise paths; preserve generator/Behavior evaluation and publication order.
- Respect existing Height/Flow target semantics. Reuse ValidFlow handling where
  Flow is an explicitly supported target. Do not replace or remove Flow Feather.
- Keep each owner's current authored filter order; only ramp COLOR compositing
  runs in reverse visual order.

### 5. Color Ramp integration — important

- `MixtormatGpuGeneratorPasses.cpp` and
  `Shaders/Private/MixtormatGeneratorHeightColorRamp.usf`:
  split raw ramp mapping from color blending when an owned filter is present.
- Capture/map the ramp's own source at its dependency position, filter the ramp's
  own raw color, then blend that result over lower ramps in the reverse color pass.
  Do NOT blur the accumulated layer color or previously composited lower ramps.
- Handle transparent ramp stops with alpha-aware/premultiplied filtering to avoid
  RGB halos; return the representation expected by the color blend operation.
- Ramp Weight × filtered stop alpha × ramp-scoped mask applies only during ramp
  composition. Filter Amount and its own scoped mask apply only during filtering.
- Publish/preview the ramp's composed result over lower ramps, as established.
- Unfiltered ramps may retain the current single-dispatch path; this is not a
  second compatibility system. Do not change the existing Signed Flow modulation.

### 6. Shared inspector and creation paths

- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`:
  extend `BuildBlurControls`; reuse existing controls, help and parameter metadata.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp` and
  `MixtormatLayerChildren.cpp`: expose scoped Blur on Color Ramp and preserve
  placement validation, copying/instances and nested masks. Trace existing callers
  of `CanOwnScopedBlurs` rather than adding parallel owner lists.
- Disable inapplicable controls instead of hiding them. Preserve Gaussian settings
  when switching modes. Explain Flow Influence versus Amount versus channel weights.
- Inspect parameter metadata/authoring registration and update it only where the
  actual existing control architecture requires explicit entries.
- Update COMPOSITION.md and the directly affected UI documentation.

## Acceptance checks (static first; execution requires user consent)

- Existing Gaussian scenes remain unchanged with default mode.
- Zero Amount, zero Length or disabled filter is identity; no invalid Flow reads.
- Directional sampling is centered; Smear reverses with negative Length.
- Flow Influence 0 follows Angle; 1 follows valid Flow. Cancellation is stable.
- Masks/Curvature remain coverage; height stays signed; normals stay normalized.
- Validity survives Flow filtering; shared guidance is never mutated by a consumer.
- Two ramps retain top-over-bottom compositing; a ramp's filter cannot smear the
  lower ramp. Transparent stops do not bleed unrelated RGB.
- Root layer masks, opacity, channel influence and blend mode apply exactly once.
- Reordering, copy/instance, cache invalidation, preview and bake use the same result.
- No tests, builds, diagnostics or Unreal launches without explicit user consent.
