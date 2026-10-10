# Mixtormat — Viewport & Preview System Audit

Repository: [hugobeyer/matlab](https://github.com/hugobeyer/matlab) · Branch: `main` · Engine: UE 5.8 · Static source review

## 1. Executive assessment

Technical feasibility

# 8.5/10

Launch value

# 9/10

Integration risk

# 6/10

Verdict: Implement the persistent channel HUD, combined lit overlays, and A/B wipe first. Defer the quad-grid and synchronized 2D/3D canvas.

The current implementation already provides much of the underlying diagnostic data. The main architectural change is separating what is being previewed from how it is presented.

### Verified repository findings

| Component                 | Observed state                                  | Assessment       |
| ------------------------- | ----------------------------------------------- | ---------------- |
| 3D channel solo           | Nine modes including Material, F0 and Fuzz      | Reusable         |
| Diagnostic outputs        | Masks, IDs, flow, UV grids, SDF and color       | Reusable         |
| Preview lighting          | Lit and unlit presentation states               | Needs adaptation |
| Region-ID hashing         | Centralized color helper                        | Preserve         |
| Channel UI                | `V` cycling, no persistent channel toolbar      | UX gap           |
| Overlay over lit material | No combined presentation path identified        | New feature      |
| A/B comparison            | No integrated wipe identified                   | New feature      |
| Quad-grid                 | No existing four-way diagnostic view identified | Defer            |
| 2D/3D cursor sync         | No corresponding mapping system identified      | Defer            |

Evidence: `SMixtormatPreviewViewport.cpp/.h`, `SMixtormat_Preview.cpp`, `MixtormatGpuCompositor.h`, `MixtormatGpuDebugPreviewPasses.cpp`, and `MixtormatDebugPreviewBlit.usf`, read from the connected GitHub repository.

### Architectural recommendation

The current system effectively treats preview modes as alternatives:

Composited material outputs

BaseColor · Normal · RAMH · Height · Debug

Lit material

3D shading

Unlit diagnostic

Replaces material

The recommended architecture allows these to coexist:

Lit 3D surface

Scene color + depth

Diagnostic field

Mask / ID / Flow / SDF

Preview presentation compositor

Overlay · Wipe · Heatmap · Solo

One final viewport image

Preserves lighting and displaced geometry

The important boundary: Mixtormat's existing RDG material compositor produces texture data. It does not own the editor viewport's final scene color. Therefore, adding scene-color blending exclusively inside `MixtormatGpuDebugPreviewPasses.cpp` would be the wrong abstraction.

## 2. Architectural and shader recommendations

### A. Separate diagnostic source, visualization, and presentation

Introduce three independent concepts:

| State         | Examples                          | Responsibility        |
| ------------- | --------------------------------- | --------------------- |
| Source        | Roughness, child mask, Region IDs | Which data to read    |
| Visualization | Grayscale, heatmap, ID palette    | How to color the data |
| Presentation  | Solo, Lit Overlay, Wipe           | How to display it     |

Keep the existing `EMixtormatDebugPreviewMode` and `EMixtormatChannelPreview` enums. Do not reorder serialized or potentially persisted values.

A unified editor-owned preview controller should translate these states into requests for the existing compositor and viewport.

This also fixes a current conflict: `ApplyChannelPreview()` takes precedence over an active child-output debug preview by replacing the mesh material. The intended result should instead be explicit presentation-state selection.

### B. RDG blending implementation

Use one global shader with runtime uniform parameters, not one shader permutation per color, opacity, or wipe position.

Conceptual USF kernel:

```
float4 Lit = LitInput.Load(int3(Pixel, 0));
float4 Diag = DiagnosticInput.Load(int3(Pixel, 0));

float Coverage = saturate(Diag.a);
float Alpha = saturate(Opacity) * Coverage;

float3 OverlayColor = lerp(
    Lit.rgb,
    Diag.rgb,
    Alpha
);

float Side = step(
    WipePosition,
    (Pixel.x + 0.5) / OutputSize.x
);

float3 Result = Mode == 1
    ? OverlayColor
    : lerp(Lit.rgb, Diag.rgb, Side);

Output[Pixel] = float4(Result, Lit.a);
```

This is a conceptual kernel rather than a drop-in shader: the actual pass must use appropriately resolved scene-color textures and their correct color spaces.

The implementation needs two rendering integrations:

- Texture-side: extend the existing debug-color conversion passes to produce the requested diagnostic visualization.
- Viewport-side: combine that visualization with the lit viewport's resolved scene color using an editor viewport postprocess integration, or a suitable material-based overlay.

For standard Lit mode, do not enqueue any new diagnostic visualization or blending passes.

### C. Zero-cost idle behavior

Target: zero additional diagnostic work in Lit mode, not literally zero GPU cost for the preview.

Use a CPU-side predicate before enqueueing passes:

```
const bool bNeedsDiagnostic =
    PreviewState.Presentation != EPreviewPresentation::Lit;

if (bNeedsDiagnostic)
{
    EnqueueRequiredDiagnosticPasses(GraphBuilder);
}
```

Avoid allocating optional heatmap, wipe, and quad-grid intermediate targets until required.

Also audit existing producers that populate the debug target inline. Skipping a final debug blit alone does not establish zero incremental cost if producers still perform debug writes.

The existing Region ID pick path is appropriately conditional; retain its on-demand behavior.

### D. Do not rebuild materials during UI interaction

The repository already creates transient channel-preview materials and updates `DA_ChannelPreviewMode` through a dynamic material instance.

Retain that pattern.

- Channel change: update an enum/scalar parameter.
- Overlay opacity: update a scalar.
- Wipe position: update a scalar.
- Overlay palette: update uniforms or shared palette data.
- Visualization type: select an already compiled shader path.

The target is no shader compilation on slider changes, not zero initial shader compilation. New global shaders and transient material graphs still require initial compilation.

### E. UE 5.8 scene-color integration

Recommended: Investigate an editor-scoped `FSceneViewExtensionBase` for the final compositing stage.

UE 5.8 exposes `ISceneViewExtension::SubscribeToPostProcessingPass` with an `InView` argument. The older overload without `InView` is deprecated.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://dev.epicgames.com\&sz=32)

Epic Developer Community

+1



Required safeguards:

- Activate only for Mixtormat's preview view.
- Never affect the main Unreal Editor level viewport.
- Use the current view rectangle, not the full backbuffer size.
- Respect output render-target ownership and RDG lifetimes.
- Apply visualization at a clearly defined pre- or post-tonemap stage.
- Restore the exact original rendering path when disabled.

Critical distinction: A scene-color overlay drawn without mesh coverage can tint the background, lights, and studio floor. A proper implementation needs mesh coverage, depth, or stencil information.

## 3. Slate UI and interaction

### A. Channel HUD ribbon

Reuse the existing preview overlay controls and theme tokens.

Do not build another standalone toolbar styling system.

VIEWPORT PREVIEW

LitBaseColorNormalRoughnessAOMetallicHeight

LIT MATERIAL

3D Shaded

Material rendering with studio lighting

Interactive UI concept only; not connected to the plugin.

The ribbon must expose existing F0/Specular and Fuzz modes through an overflow menu rather than removing them.

Shortcut conflicts found in current code:

| Key       | Existing behavior    | Recommendation |
| --------- | -------------------- | -------------- |
| `H`       | Toggle overlay UI    | Preserve       |
| `M`       | Cycle module preview | Preserve       |
| `V`       | Cycle channel        | Preserve       |
| `Shift+V` | Reset channel        | Preserve       |
| `U`       | Cycle module preview | Preserve       |
| `Z`       | Displacement toggle  | Preserve       |

Do not assign `M` to Metallic, `H` to Height, or `O` to both AO and overlay.

Use clickable channel buttons as the primary interface. Add a central, configurable shortcut map later.

### B. Wipe dragging

Implement a dedicated Slate interaction surface layered over the viewport, with hit-testing only around the wipe divider.

Interaction contract:

1. `OnMouseButtonDown`: capture mouse only when the left button hits the divider.
2. `OnMouseMove`: update normalized wipe position while captured.
3. `OnMouseButtonUp`: release capture and end the drag.
4. `OnMouseCaptureLost`: reset drag state.
5. Otherwise, return `FReply::Unhandled()` so camera navigation continues.

Use `MyGeometry.AbsoluteToLocal()` to convert pointer coordinates, accounting for viewport offsets and DPI scaling.

Dragging must not request another material composition. The wipe only changes presentation uniforms.

### C. HUD and GLOBAL inspector sharing

Create one canonical `FMixtormatPreviewPresentationState` owned by the Editor layer.

Both the on-viewport HUD and the GLOBAL inspector modify that state through common setters.

Keep the state separate from `FMixtormatDebugPreviewSettings`, which already describes compositor-side diagnostic selection.

This avoids duplicating state between the layer inspector, viewport widget, and GPU compositor.

## 4. Risks and failure modes

| Priority | Risk                                            | Mitigation                                          |
| -------- | ----------------------------------------------- | --------------------------------------------------- |
| Critical | Overlay tints studio background                 | Mesh coverage/depth gating                          |
| Critical | Overlay and material have mismatched UV mapping | Sample diagnostic with the same material UV mapping |
| High     | Post-tonemap gamma mismatch                     | Explicit linear/sRGB conversion contract            |
| High     | Channel and debug preview override one another  | Unified presentation state                          |
| High     | Wipe intercepts camera orbit                    | Divider-only hit-testing                            |
| High     | Diagnostic remains stale after child deletion   | Stable child IDs plus invalid-target fallback       |
| High     | Quad-grid triggers multiple Lumen renders       | Reuse one scene render                              |
| Medium   | Height/SDF values become visually misleading    | Preserve signed values and show range/units         |
| Medium   | ID palette becomes muddy over albedo            | Coverage-aware tint and optional outlines           |

### Substrate, Legacy PBR, and Nanite

The overlay should not depend on whether the underlying preview material uses Substrate or Legacy PBR.

A postprocess overlay operates on the resolved 3D view after material shading, allowing the existing shader model and lighting pipeline to remain intact.

Nanite displacement introduces a different constraint: UV correspondence and geometry coverage.

- The diagnostic must follow the displaced material surface.
- A screen-space UV reconstruction must use the actual mesh mapping, not screen coordinates.
- Different UV channels, tiling, rotations, and seam handling must remain consistent.
- Compare modes must distinguish texture differences from geometry differences.

The current transient channel-preview materials already attempt to retain height displacement. Preserve that behavior.

### Region ID colors

`MixtormatDebugColor.ush` already contains `MixtormatRegionDebugColor(uint Root)` and sRGB-to-linear conversion helpers.

Do not duplicate or replace the hash.

For overlay mode, use the same region color and blend it over the lit surface in the correct color space. Offer an outline-oriented ID visualization for situations where color tints obscure albedo detail.

## 5. Quad-grid and 2D/3D architecture

### Quad-grid: one lit scene plus three derived panels

Lit 3D

BaseColor

Normal

Height

Illustrative quad-grid concept; example textures are independent visual references, not matched material outputs.

Do not use four independent Lumen-enabled viewports for launch.

A combined blit of texture maps is efficient, but it will show flat 2D textures, not four camera-synchronized 3D surfaces.

To display the same geometry from the same camera with four different modes, the renderer must generate or retain the necessary surface-space data. A simple full-screen texture blit cannot reconstruct arbitrary displaced 3D geometry.

Recommended P1 approach: one lit 3D viewport and three 2D diagnostic panels.

### Dual 2D canvas / 3D hover synchronization

This requires more work than the proposed UI suggests.

The mapping from 2D UV to 3D surface is generally one-to-many because overlapping and repeated UV islands can represent multiple triangles.

Required systems include:

- Triangle or surface picking
- UV0 / selected UV-channel interpolation
- UV island overlap handling
- Mesh-to-texture resolution mapping
- Tile-repeat and wrapping behavior
- Cursor projection on displaced geometry

This should be a separate post-launch feature rather than a dependency of the viewport overlay.

## 6. Commercial assessment

Adobe Substance 3D Painter already supports channel solo views and combined 2D/3D layouts. Marmoset Toolbag supports multiple viewports and render-pass inspection. Consequently, those features improve parity rather than establishing competitive differentiation.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://experienceleague.adobe.com\&sz=32)

Adobe Substance 3D Painter

+3



Mixtormat's stronger opportunity is showing how a procedural mask changes the actual lit material without breaking the 3D look-development workflow.

| Feature                    | UX value    | Marketing value | Decision |
| -------------------------- | ----------- | --------------- | -------- |
| Lit diagnostic overlay     | Very high   | Very high       | P0       |
| Draggable A/B wipe         | High        | Very high       | P0       |
| Persistent channel ribbon  | Very high   | High            | P0       |
| SDF and flow visualization | High        | High            | P0/P1    |
| Quad-grid                  | Medium      | Medium          | P1       |
| 2D/3D cursor sync          | Medium–high | Medium          | Later P1 |

A 10-second promotional demonstration of worn-edge coverage evolving directly over a lit displaced rock or metal surface is likely to communicate Mixtormat's value more effectively than a quad-grid of texture maps.

Avoid marketing claims of zero GPU overhead, guaranteed Nanite/Lumen parity, or performance figures until validated on the target hardware.

## 7. Implementation roadmap

## P0 — Launch essential

Recommended scope

0 of 7 complete

Reset

Introduce unified editor preview presentation state

Preserve existing enums, compositor APIs and channel modes.

Add persistent channel HUD

Reuse Slate preview rail recipes and GLOBAL state.

Implement lit mask overlays

Use existing debug output with viewport-scoped presentation.

Add overlay opacity and coverage controls

No new composition during opacity changes.

Add draggable A/B wipe

Material versus diagnostic; lit comparison through controlled rendering.

Correct keyboard conflicts and stale-preview handling

Preserve current shortcuts and restore state safely.

Verify lighting, displacement and color-space parity

Static inspection cannot establish runtime behavior.

### P1 — Quality of life

- Additional diagnostic palettes and user-defined color ranges.
- Flow direction arrows and richer vector-field visualizations.
- Quad-grid using a single shaded viewport.
- Synchronized 2D tile canvas.
- UV-to-3D cursor mapping.
- Live GPU versus baked asset parity comparison, subject to consistent geometry, UVs, lighting and material configurations.

### Recommended file ownership

| File / subsystem                          | Planned responsibility                                         |
| ----------------------------------------- | -------------------------------------------------------------- |
| `SMixtormatPreviewViewport.h/.cpp`        | Presentation integration, viewport controls and material state |
| `SMixtormat_Preview.cpp`                  | Workspace preview state and selection                          |
| `SMixtormat_PreviewControls.cpp`          | Persistent HUD ribbon                                          |
| `MixtormatGpuCompositor.h`                | Extend diagnostic request only if necessary                    |
| `MixtormatGpuDebugPreviewPasses.cpp`      | Optional diagnostic conversion passes                          |
| `MixtormatDebugPreviewBlit.usf`           | Diagnostic coloring kernels                                    |
| New Editor preview presentation component | Lit compositing, wipe and overlay                              |
| `MixtormatDebugColor.ush`                 | Retain central debug palette/hash                              |

Source constraints: `AGENTS.md` prohibits running builds and tests without explicit consent. No files were changed, and no runtime, shader compilation, or GPU profiling validation was performed.

## Final recommendation

Implement one Viewport Presentation V2 rather than five independent preview systems.

The best architectural investment is a shared preview presentation state that can expose existing diagnostics as solo, lit overlay, or wipe views without disturbing composition, saved materials, or current preview controls.

That delivers the most visible improvement for launch with the smallest reasonable expansion of Mixtormat's rendering architecture.