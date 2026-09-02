# Material Lab audit report

## Executive verdict

- **Core technology:** promising and differentiated.
- **Static implementation:** major compositor paths exist and have automation coverage.
- **Paid V1:** not ready until editor, bake, packaging, and release validation pass.
- **Biggest risk:** implemented code remains unproven in representative UE workflows.
- **Best commercial angle:** fast, non-destructive material authoring for environment artists.

This report separates facts verified by static code inspection from work that requires
running Unreal Editor, automation tests, cooks, packaged projects, or visual review.
No tests were run as part of this audit.

## Runtime verification still required

### P0 — release blockers

1. **Verify the complete compositor in UE**
   - Compile and run every global shader on the supported UE, RHI, and platform matrix.
   - Explicitly verify `PCD3D_SM6`; current predicates accept SM5-or-higher but do not
     enforce or prove a specific shader platform.
   - Run existing compositor automation tests.
   - Test save/reopen ordering for mixed masks and effects.
   - Verify Fill picker accept/cancel behavior.
   - Visually verify Peeling, RAMH, Contact AO, Border Lift, HSV, and F0 outputs.
   - Decide whether Bent Normal is intentionally stored-only or should affect composition.

2. **Test bake correctness**
   - Confirm BC, Normal, RAM, and Height outputs visually.
   - Confirm baked textures retain intended compression, color, mip, and streaming settings.
   - Confirm overwrite behavior preserves asset references.
   - Verify clear behavior after partial creation, parameter, or save failures.
   - Test cooking and packaged-project use, not only editor preview.

3. **Verify undo and dirty-state safety**
   - Test real slider grouping and cancellation behavior.
   - Confirm selection remains valid after layer and child undo/redo.
   - Confirm New, Open, Close, and asset replacement never lose changes.
   - Verify dirty-state text and prompts after save failures and cancelled edits.

4. **Finish clean-install and distribution validation**
   - Validate Fab package structure, plugin description, and screenshots.
   - Test clean installation, cooking, and packaged projects.
   - Confirm third-party asset licensing.
   - Test migration from every recipe schema that has shipped externally.

## Verified static findings

### Confirmed — high priority

- Bake progress begins at **Readback**.
  - `EMaterialLabBakeStage` contains `Readback`, `CreateTextures`,
    `CreateMaterial`, and `Save`.
  - `FMaterialLabBakeService::Bake` receives completed render targets, so
    composition currently happens before the bake service owns the operation.
  - Files: `MaterialLabBakeService.h`, `MaterialLabBakeService.cpp`
  - Document this ownership or add a higher-level Compose stage around both operations.

- **Resolved:** Bake now reports failure when assigning `ML_Height`.
  - BC, Normal, RAM, and Height all use the checked texture-parameter helper.
  - Missing or renamed master-material parameters are added to bake errors.

- Bake is not atomic and has no rollback.
  - Existing preflight validates settings, target dimensions, saved recipe state,
    the master material, editor asset subsystem, and incompatible output asset types.
  - Later texture creation, material parameter, or save failures can still leave
    created, modified, dirty, or partially saved assets.
  - **Improved:** bake results now list created, updated, saved, and failed paths,
    and the failure dialog displays this recovery summary.
  - Runtime failure-path testing is still required.

- All baked output textures set `NeverStream = true`.
  - This includes BC, Normal, RAM, and Height.
  - File: `MaterialLabBakeService.cpp:119-126,168-175`
  - Profile runtime memory before deciding whether this is an intentional constraint.

### Confirmed — performance-sensitive

- Preview composition synchronously calls `FlushRenderingCommands()`.
  - File: `SMaterialLabPreviewViewport.cpp:291-295`
  - Every preview update waits for queued rendering work before binding outputs.
  - Profile interaction latency before redesigning this path.

- Normal-derived height performs many full-resolution FFT passes and blocks.
  - It uses full-resolution float spectrum ping-pong textures, forward and inverse
    transforms, Poisson solving, extraction, `FlushRenderingCommands()`, and readback.
  - File: `MaterialLabNormalHeightGenerator.cpp:327-465`
  - Keep it editor-only and add progress or asynchronous ownership where safe.

- Curvature performs four neighboring normal loads when evaluated.
  - Evaluation is conditional on `FeatureInfluence > 0` or the relevant debug mode.
  - `FeatureInfluence` defaults to `0`, so curvature is effectively disabled by default.
  - Files: `MaterialLabCurvature.ush`, `MaterialLabComposite.usf`,
    `MaterialLabMaterial.h`

- Peeling evaluates its effect once at the center and four additional times.
  - The neighboring evaluations produce the height gradient used for dynamic normals.
  - File: `MaterialLabPeeling.usf`
  - Optimize only if profiling shows effect-heavy stacks are too slow.

- Stain height warp performs eight height samples when enabled.
  - The warp loop runs two iterations with four neighboring height samples each.
  - It executes only when `HeightWarp > 1.0e-4`; the default warp value is zero.
  - File: `MaterialLabStain.usf`

### Nuanced or corrected findings

- Shader compilation is **not restricted to SM5**.
  - Shader classes use `IsFeatureLevelSupported(Parameters.Platform, SM5)`.
  - This permits SM5-or-higher feature levels; it does not explicitly require
    `PCD3D_SM6` or prove support for a particular RHI/platform combination.
  - Files: `MaterialLabGpuCompositor.cpp`,
    `MaterialLabNormalHeightGenerator.cpp`
  - Explicit SM6 runtime compilation and execution validation is still required.

- No request-generation ID exists, but out-of-order preview overwrite is not proven.
  - UE render commands are enqueued in order.
  - The current preview path immediately flushes after each compose request.
  - Add stale-result rejection if preview becomes asynchronous or overlapping callers
    are introduced; it is not currently a demonstrated release bug.

- Full-resolution intermediates are shared rather than allocated per layer.
  - Mask, peeling-effect, and stain work use shared ping-pong textures per composition.
  - Compositor outputs also use ping-pong targets.
  - Additional full-resolution textures are allocated selectively for referenced
    height snapshots and debug mask snapshots.
  - Memory still scales strongly with resolution and enabled stack features, but the
    previous claim that every layer allocates every intermediate was inaccurate.

- Bent Normal is imported and stored but is not consumed by the compositor.
  - `UMaterialLabEffect` exposes `BentNormal` and the importer populates it.
  - Compositor render data and peeling shaders do not reference it.
  - Product intent must determine whether this is deliberate or incomplete.

## Existing static safety and test coverage

- Compositor automation tests cover representative paths including:
  - multiple ordered layers;
  - mixed mask/effect ordering;
  - HSV adjustment;
  - Peeling;
  - RAMH-style height-mask blending;
  - F0 output;
  - disabled layers, channel influence, height comparison, and curvature input.
- These tests must still be run and do not replace visual UE verification.

- Undo/redo uses custom edit history rather than Unreal transactions.
  - Interactive edits coalesce within a `0.3` second window when structure matches.
  - Applying history clamps layer selection and resets effect selection.
  - New and Open call an unsaved-change confirmation path.
  - Close behavior, child selection, and real slider interaction still require testing.

- Legacy recipe migration exists for:
  - the original single-mask representation;
  - separate legacy mask/effect arrays into ordered children.
- Migration behavior still needs fixture-based testing for every schema actually shipped.

## Productivity improvements

### Resolved productivity gap

- A dedicated preview reset action now restores camera, FOV, and lighting.
  - Camera defaults: distance `225`, yaw `195`, pitch `-8`, FOV `50`.
  - Lighting returns to Neutral, HDRI selection clears, and HDRI rotation resets.

### Best-case workflow

1. Open Material Lab.
2. Select a surface from the library.
3. Drag it into the layer stack.
4. Add masks/effects from searchable categorized tiles.
5. Adjust with verified grouped undo.
6. Compare Before/After.
7. Bake with remembered settings.
8. Reveal or apply the result immediately.

### Highest-value additions

- Document the shortcut set visibly.
- Add concise tooltips for Coat, RAMH, height bias, Contact AO, and Border Lift.
- Add searchable layer/effect presets before batch baking.
- Add “duplicate variant” before a larger prop workflow.
- Separate preview and bake resolution only if profiling proves useful.
- Add compact bake history with destination, resolution, outputs, and failures.

## Marketing position

### Strongest positioning

> **Build layered, height-aware materials directly in Unreal, then bake reusable outputs.**

Avoid calling outputs “production-ready” until bake, cook, and packaged-project
validation is complete.

### Implemented differentiators

- Native Unreal Editor workflow.
- Non-destructive editable recipes.
- GPU compositor with ordered masks and effects.
- Height-aware layering and contact-detail controls.
- RAM/RAMH material data handling.
- Bake paths for BC, Normal, RAM, Height, and Material Instance assets.
- Preview debug views and displacement controls.

Implementation does not by itself prove visual correctness, performance, or supported
platform compatibility.

### Claims to avoid until validated

- “Production-ready.”
- “Physically accurate peeling.”
- “Works across all Unreal platforms.”
- “Real-time at 4K.”
- “Lossless material reconstruction.”
- “Fully compatible with every existing asset.”

### Fab and sales proof still needed

- 30–60 second workflow video.
- Before/After material comparisons.
- 2K versus 4K bake comparison.
- Mask/effect ordering demonstration.
- Height-aware blend demonstration.
- Packaged-project proof.
- Performance numbers by resolution, layer count, and enabled effects.
- Clear UE version, platform, RHI, and feature-level support table.

## Sales strategy

### Best initial customer

- Environment artists.
- Small Unreal teams.
- Technical artists without custom material tooling.
- Indie developers needing Unreal-native layered material authoring.

### Best-case pricing approach

- Launch with a focused paid V1 rather than a broad suite.
- Sell time saved and Unreal-native workflow, not shader complexity.
- Offer a documented trial/demo project if platform policy permits.
- Use a low-friction introductory price, then adjust after reviews and beta proof.

### Current conversion blockers

- No demonstrated packaged-project reliability.
- Unverified editor and bake behavior.
- Unclear support and compatibility policy.
- No external artist testimonials.
- No measured performance guidance.
- Runtime capability must not be inferred solely from implemented code paths.

## Recommended execution order

1. Run compositor automation and explicit SM6/RHI verification.
2. Test partial-output reporting and decide whether rollback is required.
3. Visually validate compositor and bake outputs in Unreal Editor.
4. Profile preview, normal-height generation, memory, and 2K/4K GPU time.
5. Verify undo, reset, close, replacement, and dirty-state interactions.
6. Run clean-install, cook, Fab, migration, and packaged-project tests.
7. Decide Bent Normal product intent.
8. Conduct an external artist beta.
9. Publish benchmarks, compatibility guidance, and workflow video.
10. Add presets, variants, bake history, and batch workflows.

**Bottom line:** Material Lab has a credible Unreal-native material-authoring core.
The fastest route to a stronger paid product is verified reliability, clear failure
recovery, responsive preview behavior, compatibility proof, and compelling evidence
that baked results work outside the editor.
