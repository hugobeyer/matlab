# Unified PostFX diagnostics — implementation contract

Implementation target: `main`. Original design audit performed against `feature/behavior-system-v2`; current implementation must follow `main` source.

## Source audit

- `SMixtormatPreviewViewport.cpp` currently swaps the mesh material for `CreateDebugMaterial()` when `DebugSettings.Mode != None`, and `ApplyChannelPreview()` swaps to another unlit material. These are replacement previews, **not** overlays.
- `MixtormatGpuDebugPreviewPasses.cpp` reads existing GPU fields: scalar, mask, vector, signed distance, region IDs, colour. `MixtormatDebugPreviewBlit.usf` encodes them to RGB. This machinery should remain available as existing full-screen replacement diagnostics.
- `SMixtormat_Preview.cpp` already resolves child output semantics through `GetChildCapabilities()` and manages `ChildPreviewTarget`. Reuse it for automatic selection; don't create a second child-output registry.
- `FMixtormatGpuCompositor::BindOutputs` already binds composed surface textures and height into MIDs. Reuse retained published child GPU textures for data sources; do not trigger another gather/composite or a GPU readback.

## Mask-first implementation on main (2026-10-11)

The initial W-key experiment is **material-integrated**, not a scene-space PostFX pass.
It uses the original lit/Substrate preview master and its existing material UVs,
normals, roughness/metallic, AO and displacement. The compositor holds the selected
mask snapshot until the end of the layer loop, then writes a tinted **display-only**
albedo to the existing Debug render target. The master binds that debug target to
`DA_BaseColor` only while W is active. Original composited BaseColor and bake
targets remain untouched; no extra persistent target, GPU readback or generator pass
is created. Overlay off runs no additional tint dispatch.

This first pass tints material albedo before PBR shading; it does not independently
blend over the *finished* lit scene. Lighting and specular response may therefore
differ slightly inside tinted areas, especially for metallic materials. A true
post-shading overlay still requires authored UV/depth capture as described below.

Mask-first selection: enabled Mask, Generated, Craquelure, Color ID or Random ID
children of a concrete selected layer. No group-child selection or generic
published scalar Behavior coverage yet. One fixed initial opacity (0.55), no UI.
V now resets to the original PBR material; V and M/U cycling hotkeys are retired. The old replacement-preview implementation remains internally for inspector preview controls until their visualizations migrate.

Changed integration points: `SMixtormatPreviewViewport.*`, `SMixtormat_Preview.cpp`,
`SMixtormat.cpp`, `SMixtormat.h`, `MixtormatGpuCompositor.h`,
`MixtormatGpuCompositorInternal.h`, `MixtormatGpuCompositor.cpp`,
`MixtormatGpuComposePipeline.cpp`, `MixtormatGpuDebugPreviewPasses.cpp`,
`MixtormatDebugPreviewBlit.usf`, and `MixtormatDiagnosticOverlay.ush`.

## Future true PostFX rendering decision

Use **one viewport-scoped, surface-aware overlay pass** after opaque material shading and before temporal upscaling, invoked only while a diagnostic is active. Input: scene colour/depth plus the selected published field and a per-pixel mapping back to the preview mesh's **true authored material UV**. Output: scene colour blended with diagnostic ink. Opaque surface, normals, lighting, Substrate inputs and displacement are untouched.

A generic Unreal post-process material has screen position, depth and GBuffer attributes, **not the preview mesh's interpolated material UV**. Consequently sampling the field at screen UV or reconstructing planar UV from world position is incorrect on sphere/cylinder/cube, across seams, or with nonuniform UV transforms. This mapping is a prerequisite: render the preview object's UV to an auxiliary GPU scene target at the **same displaced geometry positions**, depth-tested against the preview surface, or add a viewport-only mesh UV buffer via a suitable mesh pass. Do not fake this with screen UV. Material-derived UV data must use the same UV transform and displacement as the existing preview mesh; avoid a parallel approximation.

Mask the overlay to the preview subject (depth/object identity). Keep the auxiliary UV pass dormant and unallocated when the overlay is off. The UV pass is view-dependent, not a generator evaluation; published fields remain cached. Use integer/point sampling for IDs and calculate boundaries from neighbouring *integer* IDs only; softness is opacity at the final scene blend, never a blur of ID data.

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

One compact `Overlay` toggle, shared `Opacity` slider, optional `Boundaries` for IDs. Selection follows the selected child and its primary previewable published output, with manually selected secondary output preserved until selection changes. Child capabilities remain the source of semantic truth. Preserve Shift+V channel previews and existing diagnostic preview paths without changing their shortcuts or semantics.

## Integration points

1. `Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.{h,cpp}`: viewport-scoped scene extension/UV attachment lifecycle; disabled means no render work.
2. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp`: automatic capability-driven selection.
3. `Source/MixtormatShaders/Private/MixtormatGpuDebugPreviewPasses.cpp` and `Shaders/Private/MixtormatDebugPreviewBlit.usf`: reuse existing diagnostic colour math where suitable; do not replace these full-preview paths.
4. `Source/MixtormatEditor/Private/Widgets/SMixtormat_PreviewControls.cpp`: the shared toggle and opacity control, using theme tokens.

## Explicit blocker

There is no verified existing displaced-surface UV scene buffer in the inspected preview integration. A conventional post-process material alone **cannot** meet the per-surface UV accuracy requirement. Do not wire in a screen-UV blend as a provisional implementation: it would display incorrect data and silently violate the stated contract.

No builds or tests; user owns Unreal validation.
