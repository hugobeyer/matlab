# Unified PostFX diagnostics — implementation contract

Implementation target: `main`. Original design audit performed against `feature/behavior-system-v2`; current implementation must follow `main` source.

## Source audit

- `SMixtormatPreviewViewport.cpp` currently swaps the mesh material for `CreateDebugMaterial()` when `DebugSettings.Mode != None`, and `ApplyChannelPreview()` swaps to another unlit material. These are replacement previews, **not** overlays.
- `MixtormatGpuDebugPreviewPasses.cpp` reads existing GPU fields: scalar, mask, vector, signed distance, region IDs, colour. `MixtormatDebugPreviewBlit.usf` encodes them to RGB. This machinery should remain available as existing full-screen replacement diagnostics.
- `SMixtormat_Preview.cpp` already resolves child output semantics through `GetChildCapabilities()` and manages `ChildPreviewTarget`. Reuse it for automatic selection; don't create a second child-output registry.
- `FMixtormatGpuCompositor::BindOutputs` already binds composed surface textures and height into MIDs. Reuse retained published child GPU textures for data sources; do not trigger another gather/composite or a GPU readback.

## Mask-first PostFX implementation on main (2026-10-11)

The W experiment now uses a **viewport-only post-process blend** instead of
tinting Base Color or creating a replacement lit material.

- The GPU compositor retains the active source's **raw effective mask/gate coverage**
  and writes it to the existing debug target as float coverage, not green/cyan.
  Regular BaseColor/Normal/RAM/Height and baked outputs are unchanged.
- `FMixtormatViewportMaskPostFx` owns a second, capture-only static mesh that
  uses the same authored mesh UVs, transform, height texture, displacement
  scaling and enable flags as the main preview, via the existing
  `MixtormatPreview::AddHeightDisplacement` helper. It writes mesh UV into
  HDR R/G and a subject-validity bit in B.
- The capture component renders only the proxy into a view-sized HDR target.
  The UV target uses **nearest** filtering because its B channel is a binary
  mesh-validity signal; interpolating it with background black creates a false
  translucent fringe. The locally authored Post Process material must still
  gate its Lerp alpha with `DA_DiagnosticUvBuffer.B >= 0.5`:
  `saturate(Field.R) * (Validity.B >= 0.5) * Opacity`. The viewport client adds
  the material blendable to its own view (no scene-wide unbound volume). The
  material samples UVs to look up the selected mask field and alpha-blends
  its artist-authored color over `PostProcessInput0` after tonemapping, without
  changing the PBR master.
- `W` toggles the overlay; `V` returns to the original material. RMB+WASD
  events are consumed by the viewport rather than passed to the inherited fly
  camera; the viewport client also restores its pre-tick view transform after
  inherited editor Tick, since Unreal independently polls held WASD keys there.
  Mixtormat's explicit orbit/pan/zoom/focus remain the only camera controls,
  while RMB mouse drag still rotates the studio light.
- W ownership is an authored `FMixtormatChildAddress` plus output name in
  `SMixtormat` (`MaskOverlaySource`, `MaskOverlayOutputName`, enabled state),
  independent of Inspector selection and legacy `ChildPreviewTarget`.
  `ResolveMaskOverlayTarget` resolves that address on every submitted compose,
  including group-authored children mapped to an enabled member's effective GUID.
  Reordering and rebuilding rows do not replace ownership. W on the same address
  disables it; W on another eligible address transfers it. The existing diagnostic
  reset also clears the stored address; V/Shift+V and channel cycling are unchanged.
- Coverage supports Texture/Noise/Noise Gate masks, scoped gates, Generated,
  Craquelure, Color ID and Random ID masks, including nested/group/instance
  placements. Layers and fill layers expose aggregate coverage only when an
  enabled root coverage producer exists, not an invented diagnostic field.
- Other children use `GetChildCapabilities` to choose the first actual scalar
  output in capability order: Noise Value, Behavior Influence when published,
  and scalar feature outputs. The activated output name is retained; losing it
  clears W rather than silently switching to another field. Instances continue
  using the existing resolved placement payload. Missing/disabled owners, children,
  scope ancestors or instance sources are rejected; cycles are bounded.
- Named fields resolve from the current published typed scalar/mask registry after
  composition, with prefix-cache demand included. Raw mask snapshots are copied
  before later layers reuse ping-pong targets. Missing GPU fields clear the debug
  target instead of retaining stale pixels. Typed signed scalars use the existing
  scalar-preview convention `saturate(0.5 * Value + 0.5)` in the W export only;
  raw masks and Scalar01 remain unchanged. The viewport continues obtaining the
  compositor's latest published debug target each tick, never a stored field pointer.
- The hierarchy uses dynamic GUID-bound right-edge markers from UI STYLE > Layers
  > Diagnostic. Exact owners and their ancestors use separate opacity tokens;
  selection's top hairline and instance-source left marker/glow remain independent.
- While W owns the field, selection-only bypass/solo/composition-before overrides
  are suspended so they cannot omit or alter its source. Their state is retained.
  Explicit Inspector preview-eye actions keep their existing behavior and rendering
  paths, including dismissing W. Ordinary selection and parameter edits do not.
- The capture, proxy and post process are created lazily the first time W
  is activated; when W is off the capture and post-process pass are disabled.
  Original old green inspector-eye preview paths remain available and unchanged.

The overlay has a temporary opacity of 0.55. One additional mesh capture per
frame is required while active to preserve displaced-surface UVs. This is
a source implementation only: no Unreal build, shader compile or runtime
inspection has been performed.

## Remaining work

- Confirm exact matching between capture pixels and the preview viewport's
  actual projection under non-default aspect ratios and temporal upscaling.
- Sources shelf W routing remains unsupported; no field is invented for sources
  without a scalar publication. Vector, colour, ID, UV and SDF visualizations remain
  outside this scalar-coverage overlay. Signed scalar fields use midpoint-normalized
  coverage, not a two-colour signed-height display.
- Add Flow / Height / IDs / UV visualizations using the same post pipeline;
  permit Flow and UV lines over a mutually exclusive scalar/ID base.
- Remove retired replacement preview paths only after their full functionality
  has migrated; the current W path does not modify them.

## One semantic shader

| Semantic | Visualization |
|---|---|
| Flow/UV direction | Sparse UV-space arrows or short trails; density anchored to material UV, direction from vector field |
| IDs | Deterministic hash of integer ID to subdued categorical RGB; optional exact-ID neighbour boundary |
| Mask | Single-colour translucent coverage, proportional to mask value |
| Height | Warm positive, cool negative, zero transparent; preserve signed input |
| Behavior | Dispatch using published output semantics: vector -> Flow, scalar influence -> Mask/Height as declared |

Single shader dispatch mode, shared opacity `[0,1]`; compositing should be `lerp(SceneColor, InkColor, Opacity * Coverage)`. Build no separate per-type overlay pipelines. Fully off means no UV pass, no additional scene pass, and no compositor requests.

## UX contract

The contextual viewport hint strip (`SMixtormat_PreviewHints.cpp`) must match implemented shortcuts. Eligible selection shows `W · Show diagnostic`; the active owner shows `W · Hide diagnostic`; another eligible source shows `W · Preview selected source`. Existing V reset help remains visible while W is active even if the selected row is ineligible. Preserve all existing V/Shift+V, channel-preview cycling, Inspector eyes and replacement rendering paths; this work changes only W ownership and row presentation. Keep `Esc`, plane flip and missing-material hints in their existing contexts.

Future overlay controls may add shared opacity and exact-ID boundaries. They must not redefine or migrate existing shortcuts or Inspector diagnostics as part of W persistence. No authored material or local `.uasset` is changed by this implementation.

## Integration points

1. `Source/MixtormatEditor/Private/Preview/MixtormatViewportMaskPostFx.{h,cpp}`: private capture/proxy/after-tonemapping material owner.
2. `Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.{h,cpp}`: viewport lifetime, active state and camera/mesh synchronization.
3. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp`: semantic selection / GUID routing.
4. `Source/MixtormatShaders/Private/MixtormatGpuDebugPreviewPasses.cpp`: existing GPU selected-field export, shared diagnostic data target.
5. `Shaders/Private/MixtormatDebugPreviewBlit.usf`: raw mask field output, no colorization.
6. `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`: stable snapshot and final export.

No builds or tests; user owns Unreal validation.
