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
- `W` toggles the overlay; `V` returns to the original material. W/E toggles work
  during mouse drags, including RMB. Bare WASD events are consumed by the viewport
  rather than passed to the inherited fly camera; the viewport client also restores
  its pre-tick view transform after
  inherited editor Tick, since Unreal independently polls held WASD keys there.
  Mixtormat's explicit orbit/pan/zoom/focus remain the only camera controls,
  while RMB mouse drag still rotates the studio light.
- W ownership is an authored `FMixtormatChildAddress` plus output name in
  `SMixtormat` (`MaskOverlaySource`, `MaskOverlayOutputName`, `MaskOverlayOutputKind`, enabled state),
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
  raw masks and Scalar01 remain unchanged. The viewport resolves a completed,
  matching diagnostic resource generation each tick, never a stored published-field pointer.
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

## Raw Flow integration — E (local main, 2026-10-12)

- **W remains scalar/mask/gate; E activates raw Flow.** Both use the existing
  `ResolveMaskOverlayTarget`, canonical ownership, group expansion, instance payload
  resolution and GUID-bound hierarchy indicators. There is only one active PostFX
  source. The same key on the same target toggles it off; W -> E and E -> W on the
  same owner switch the diagnostic type in place; either key on another eligible
  source transfers ownership. An ineligible selection never dismisses an active
  overlay -- the press is a no-op and says so in the status bar. With no active
  overlay, an ineligible selection does nothing.
  Selection and Inspector rebuilds do not redirect it. Selecting a Flow module
  or editing Write Flow no longer automatically activates the legacy unlit Flow
  replacement; E explicitly activates raw Flow PostFX. Existing V/Shift+V resets,
  channel cycling, explicit preview eyes and legacy debug blits are unchanged.
- E is handled only by the existing viewport client, bare, including while mouse
  buttons are held. W has the same mouse-independent activation. No workspace/global
  input processor is added; modified keys retain their existing routing. Diagnostic
  activation does not enable Unreal flight movement; orbit/pan/zoom remain unchanged.
- Selection uses typed `GetChildCapabilities` outputs, not texture channel count.
  Prefer a primary `Flow` output, then the first declared `Flow`, then `UVMap`.
  Retain its exact name and preview kind for all later resolves. Noise with Write
  Flow selects `GeneratedFlow`; other Noise selections select `FlowDirection`.
  Generic `Vector2` gradients are not silently treated as Flow. Native Flow uses
  the producer's existing demand/publication path; no new Flow generator exists.
- Supported producers are Noise (including Flow preset/Write Flow), native Flow
  outputs declared by Cracks, Rock Formation, Pebbles and Cliff Strata, traced
  Behavior outputs when actually published, Feather beneath Noise or Flow Field,
  and local typed Flow/UV Output References. Feather publishes its existing filtered
  result as typed `FlowDirection` under its own child identity, without changing
  the parent's publication. Noise Feather uses filtered GeneratedFlow when that
  contribution is published, otherwise filtered FlowDirection; Flow Field Feather
  uses its parent's filtered FlowDirection. These are stage-specific snapshots:
  editing Feather updates its active E diagnostic even after another row is selected.
  Height-only Feather beneath other behaviors is not a Flow source and is rejected
  by the resolver. No Flow is synthesized for it. Nested/group-authored placements
  and child instances use the same
  resolver. Sources shelf rows, unpublished operations and generic Vector2 outputs
  remain unsupported directly; no placeholder field is created for them.
- The compositor demands the exact field before prefix-cache restoration, resolves
  it from the final published registry, and exports **`Field.Texture`**, not its
  `FlowSmooth` companion or a colourized legacy preview. A lazy RGBA32F render
  target holds the raw GPU snapshot; the data-only `MainRawFlowCS` performs a Load
  and store with no saturate, normalize, frac, gamma conversion or visualization.
  This is a data export within the existing compose/PostFX path, not another
  capture or rendering pipeline. Flow's existing half-float values widen without
  additional quantization; UVMap's float32 RG is retained at float32 precision.
- `Flow` is a relative directional/transport vector payload; RG units/magnitude
  are those of the actual producer, not universally unit-length. `UVMap` is an
  absolute/transformed coordinate payload. No UV-minus-base conversion is done.
  Existing Flow B/A payloads pass through too; they are producer-specific and
  should not be assumed to be a universal validity mask. For two-channel UVMap
  textures only RG has a semantic contract.
- Material bindings: `DA_DiagnosticFlow` remains the raw texture;
  **`DA_DiagnosticColor`** supplies code-rendered, mip-filtered linear premultiplied
  RGB and independent coverage A in **both modes 0 and 1**. Mode 0 encodes the
  normalized mask/scalar coverage with the existing linear mask ink; mode 1 encodes
  Flow/UV visualization. Color has its own completed-publication flag, independent
  of raw Flow availability. The current C++ PostFX implementation emits
  `DA_DiagnosticMode` **0 for W mask/scalar coverage and 1 for E Flow/UV**.
  Flow versus UVMap is selected by the typed completed target in the color pass,
  not a separate material mode. `DA_DiagnosticUvBuffer`, `DA_DiagnosticField` and
  `DA_DiagnosticOpacity` keep their names and mask behavior. During E,
  `DA_DiagnosticField.R` is field availability (1 for a successfully exported
  field, otherwise 0), not a Flow visualization or per-texel Flow validity.
- The raw target uses linear data and wrapping U/V addressing. Captured mesh UVs
  stay non-tiled. Global quarter-turn rotates texture lookup positions exactly
  like the material outputs but does **not** rotate, wrap or otherwise change RG
  component values. The separate color pass evaluates the original published field
  before any coordinate resampling can affect its winding. Its linear RGBA16F target
  uses 2×2 subpixel evaluation, integrated periodic checker/grid coverage and explicit
  color mip generation after each successful compute write. Only display color is
  mip-filtered; the raw field remains unchanged.
- The compositor retains the last completed diagnostic resource generation, with
  its textures pinned, separately from the pending request. It recognizes completion
  both at request entry and during viewport binding. Same-owner recomposition keeps
  the completed binding visible instead of disabling E on every parameter edit.
  Both W coverage and E Flow bind against matching owner/child/name/kind metadata;
  a transfer stays hidden until its matching request completes. Failed submissions
  invalidate retained diagnostics, and completed unavailable fields clear coverage
  or reject Flow binding rather than retaining an older successful result. Mask
  activation removes the Flow binding and sets mode zero. The UV capture/displaced
  proxy, canonical UI ownership and legacy previews remain unchanged.

### Artist-authored Material Editor setup

Modify your local `M_MixtormatDiagnosticPost` yourself; plugin code does not edit
or regenerate this material or any `.uasset`. When no artist-authored material is
found at the searched paths, the plugin builds a self-contained default post-process
material with the same parameter contract (filtered mask/scalar ink for mode 0,
filtered Flow/UV ink for mode 1), so W/E never depend on a `/Game/`
asset existing. Existing authored overrides must add the new color sample themselves.

1. Add **Texture Sample Parameter 2D**, parameter name **`DA_DiagnosticFlow`**.
   Use **Sampler Type: Linear Color** and **Sampler Source: Shared: Wrap**.
   Supply any suitable linear default texture in the material; the active E
   preview replaces it with the real GPU field, never generated placeholder Flow.
2. Connect the existing sampled **`DA_DiagnosticUvBuffer.RG`** directly to this
   sample's **UVs**. Keep the UV-buffer sample itself at screen/view UVs and its
   existing nearest/non-tiled sampling; do not add tiling to the capture buffer.
   Wrap applies to **field lookup addresses**, never to sampled Flow RG values.
3. Add **Texture Sample Parameter 2D `DA_DiagnosticColor`** for the code-rendered
   visualization in **both modes 0 and 1**. Use the existing Color sampler convention for runtime render targets,
   **Sampler Source: From texture asset** and automatic mip selection (no explicit mip 0).
   The bound target is forced-linear, trilinear and wrapping. Connect
   **`DA_DiagnosticUvBuffer.RG`** directly to its UVs, without `Frac`; derivatives of the
   continuous mesh UV select color mips. Replace the Custom-node checker/tint calculation
   with this sample. Flow is direction hue/strength value; UVMap is warped checker/grid
   plus determinant tint. Styling constants live in `MixtormatDiagnosticColor.ush`.
4. Add **Scalar Parameter `DA_DiagnosticMode`**, default **0**. Mode 0 is mask/scalar;
   mode 1 is Flow/UV. Both read `DA_DiagnosticColor`; only the extra availability
   gate differs. Existing raw mask/scalar graphs may still use `DA_DiagnosticField`.
   `DA_DiagnosticFlow` stays available for intentional raw-data visualization, without
   normalization, wrapping or saturation of its stored values.
5. Keep **`DA_DiagnosticUvBuffer.B >= 0.5`** and **`DA_DiagnosticOpacity`**.
   Color is **premultiplied**; use the same source-over blend in both modes:
   `Availability = (Mode == 1) ? DA_DiagnosticField.R : 1`;
   `Gate = Opacity * MeshValidity * Availability`;
   `Result = PostProcessInput0 * (1 - Gate * Color.a) + Gate * Color.rgb`.
   Mode 0 already carries mask coverage in `Color.a`: **do not multiply by Field.R again**.
   Do not use `lerp(Scene, Color.rgb, Gate * Color.a)`: that multiplies coverage twice.
   Keep the material after tonemapping; color filtering is independent of TAA/TSR.
   Capture silhouettes and temporal projection alignment remain separate limitations.

No builds, tests, shader compilation or Unreal launches were run. Static source
review only; sampler/material behavior and runtime rebinding need Unreal review.

## Remaining work

- Confirm exact matching between capture pixels and the preview viewport's
  actual projection under non-default aspect ratios and temporal upscaling.
- Sources shelf diagnostic rows remain unsupported directly. Generic Vector2,
  colour, ID and SDF data are not exposed by E. Published Flow and absolute UVMap
  data retain their raw binding and now also supply code-rendered filtered color. Signed scalar fields
  use midpoint-normalized coverage, not a two-colour signed-height display.
- Flow/UV color is code-owned; authored PostFX overrides must connect
  `DA_DiagnosticColor` to use it. No arrows, animation or new inspector controls are
  added. Height/ID overlays are not added here. Active-diagnostic GPU cost is unprofiled.
- Existing replacement previews, Inspector eyes and channel shortcuts remain
  supported and are not migrated or removed by W/E integration.

## One semantic shader

| Semantic | Visualization |
|---|---|
| Flow direction | Raw RG preserved; code-rendered direction hue/strength value in filtered diagnostic color |
| UVMap | Raw RG preserved; lifted warped checker/grid with determinant tint in filtered diagnostic color |
| IDs | Deterministic hash of integer ID to subdued categorical RGB; optional exact-ID neighbour boundary |
| Mask | Single-colour translucent coverage, proportional to mask value |
| Height | Warm positive, cool negative, zero transparent; preserve signed input |
| Behavior | Dispatch using published output semantics: vector -> Flow, scalar influence -> Mask/Height as declared |

Single shader dispatch mode, shared opacity `[0,1]`; compositing should be `lerp(SceneColor, InkColor, Opacity * Coverage)`. Build no separate per-type overlay pipelines. Fully off means no UV pass, no additional scene pass, and no compositor requests.

## UX contract

The contextual viewport hint strip (`SMixtormat_PreviewHints.cpp`) must match implemented shortcuts. Eligible selection shows `W · Show diagnostic`; the active owner shows `W · Hide diagnostic`; another eligible source shows `W · Preview selected source`. Existing V reset help remains visible while W is active even if the selected row is ineligible. Preserve all existing V/Shift+V, channel-preview cycling, Inspector eyes and replacement rendering paths; W/E share persistent ownership and row presentation. Eligible Flow selections also show `E · Show raw Flow`, `E · Hide diagnostic` or `E · Preview selected Flow`. While an overlay is active, W/E remain advertised even with an ineligible selection and say `Diagnostic active` for that key. Keep `Esc`, plane flip and missing-material hints in their existing contexts.

Future overlay controls may add shared opacity and exact-ID boundaries. They must not redefine or migrate existing shortcuts or Inspector diagnostics as part of W persistence. No authored material or local `.uasset` is changed by this implementation.

## Integration points

1. `Source/MixtormatEditor/Private/Preview/MixtormatViewportMaskPostFx.{h,cpp}`: private capture/proxy/after-tonemapping material owner.
2. `Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.{h,cpp}`: viewport lifetime, active state and camera/mesh synchronization.
3. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp`: semantic selection / GUID routing.
4. `Source/MixtormatShaders/Private/MixtormatGpuDebugPreviewPasses.cpp`: existing GPU selected-field export, shared diagnostic data target.
5. `Shaders/Private/MixtormatDebugPreviewBlit.usf`: raw mask field output, no colorization.
6. `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`: stable snapshot and final export.

No builds or tests; user owns Unreal validation.
