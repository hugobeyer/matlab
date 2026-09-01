# Material Lab — Product and Market Audit

Date: 2026-08-30

Scope: the compositor, editor tool, bake workflow, usability, compatibility, and commercial fit.
Starter-library breadth is not treated as the product's main value.

## Executive verdict

Material Lab has a commercially useful core: a nondestructive, ordered material compositor that
turns layered recipes into a conventional three-texture material. That is easier to ship and cheaper
to render than keeping a large live layer graph in every final material.

The strongest differentiators are:

- Complete-surface BC/N/RAM/F0 composition instead of color-only blending.
- Mixed ordered Mask and Effect children with real evaluation-order meaning.
- Height-aware blending, contact AO, border normals, and Peeling edge treatment.
- Editable recipes plus ordinary baked textures and a material instance.
- No generated or destructively modified user material graphs.
- Unlimited recipe layer count at the data-model level.

The main commercial risk is not missing compositor capability. It is release confidence and workflow
friction. Verified undo, bake options, progress feedback, packaging, and clear onboarding matter more
to buyers now than another advanced blend feature.

**Recommendation:** position it as an Unreal-native material look-development and baking workspace.
Do not market runtime controls yet.

## Readiness scorecard

| Area | Score | Assessment |
|---|---:|---|
| Compositor architecture | 8/10 | Strong, differentiated design |
| Material feature depth | 8/10 | Competitive for a focused V1 |
| Recipe workflow | 7/10 | Save/open and ordered editing exist |
| Usability | 6/10 | Good structure; missing safety and guidance |
| Baking | 6/10 | Correct core outputs; limited production controls |
| Compatibility | 4/10 | UE 5.8 Win64 build only; runtime/Fab unproven |
| Verification | 4/10 | Important GPU, migration, and visual checks pending |
| Marketing readiness | 5/10 | Strong story, but claims and media are not prepared |
| Sale readiness | 4/10 | Promising beta, not a low-support-risk release |

Scores are product judgments, not automated test results.

## User needs and product fit

### Environment and level artists

They need:

- Fast material variants without editing a large shader graph.
- Reusable masks and consistent controls across surfaces.
- Edge, cavity, height, dirt, wear, and coating workflows.
- Predictable performance in the final scene.
- A result they can assign like a normal Unreal material.

Material Lab fit:

- Strong for reusable complete-surface layering and baked performance.
- Strong for ordered masks, Peeling, and height-border treatment.
- Weak until applying/finding baked outputs is demonstrated clearly.
- Local undo history builds successfully; editor verification, reset actions, and examples remain.

### Material and look-development artists

They need:

- A responsive preview under stable lighting.
- Physically coherent channel blending.
- Exact numeric entry and controllable normals.
- Editable source recipes and repeatable rebakes.
- 1K/2K/4K output choices and trustworthy color handling.

Material Lab fit:

- Strong channel contract, F0 handling, RNM normals, and fixed exposure.
- Strong nondestructive recipe model.
- Preview and bake now share one selectable 1K/2K/4K resolution with 2K default; synchronous readback remains limiting.
- The shared-resolution workflow builds successfully and awaits editor verification.

### Technical artists and small teams

They need:

- A documented import contract.
- Deterministic outputs and migration behavior.
- No destructive graph edits.
- Packaging/cooking confidence.
- Error messages that identify the failed asset or stage.

Material Lab fit:

- Strong architecture and protected-master contract.
- Good per-asset bake save and parameter validation.
- Good backward intent: legacy Effect value, BN data, RAM, and legacy child arrays remain.
- Missing packaged-project, Fab install, engine-version, and platform validation.

### Indie users

They need:

- A five-minute first success.
- Useful defaults and examples.
- Minimal shader knowledge.
- Clear “edit → preview → bake → assign” steps.
- Low support burden when something fails.

Material Lab fit:

- The visual workflow is approachable.
- Native color picking and unified child rows are good decisions.
- There is no complete onboarding path, demo recipe, quick-start, or troubleshooting guide yet.
- Advanced controls need concise tooltips and presets to avoid an expert-only first impression.

## Necessities before a paid V1

### P0 — release blockers

1. Complete the listed Unreal verification matrix.
2. Verify global shaders on `PCD3D_SM6` after a clean editor restart.
3. Run recipe migration and mixed-order save/reopen tests.
4. Verify baking, RAM.A F0, texture settings, persistence, and rebake overwrite.
5. Validate a clean Fab-style plugin install with no project-local assumptions.
6. Validate cooking and a packaged project using baked results.
7. Build and verify the initial local undo/redo history for every recipe edit.
8. Add bake resolution selection: at least 1K, 2K, and 4K.
9. Add visible bake progress or a clear blocking-state indicator.
10. Add a quick-start workflow and one supplied editable example recipe.
11. Correct product metadata and remove the unimplemented runtime-controls claim.
12. Add support, documentation, changelog, and third-party asset/license information.

### P1 — strongly recommended for reviews and retention

- Reset-to-default per control and per inspector section.
- Duplicate recipe and “open baked output” shortcuts.
- Apply baked material to selected actors or expose a clear Content Browser action.
- Before/after or solo-layer preview.
- Per-layer mute/solo shortcuts.
- Bake destination and naming preview.
- Non-modal notifications for routine success; reserve dialogs for failures.
- Actionable shader/import/bake errors with asset paths.
- Search keywords and tooltips for advanced height and Peeling controls.
- Performance guidance for layer count, texture size, and VRAM.
- A first-run checklist: choose surface, add layer, mask, effect, save, bake.

### P2 — differentiating expansion, not launch necessity

- Runtime controller and bindings.
- Intermediate-layer cache.
- Batch baking and variant queues.
- User presets for layer/effect settings.
- More effects such as dirt, wetness, moss, oxidation, and dust.
- Custom preview mesh support.
- Optional 16-bit or higher-precision export workflows.

## Compositor audit

### Strengths

- Sequential GPU composition means cost is paid when the recipe changes, not every rendered frame.
- Ping-pong targets avoid a fixed shader-slot layer limit.
- One blend weight coherently transitions Base Color, roughness, AO, metallic, and F0.
- Normals use decoded tangent-space data and RNM/normalized handling.
- RAMH height is transient; baked RAM.A remains dielectric F0.
- Contact AO multiplies composed AO rather than replacing authored AO.
- Peeling is correctly modeled as a child that reveals the accumulated lower material.
- Bent Normal remains load-compatible without influencing new Peeling composition.
- The protected master is loaded and parameterized, not generated or rewritten.

### Risks

- Shader compilation and all recent visual features remain unverified in Unreal.
- Full-stack recomposition has no intermediate cache; large stacks may feel slow.
- Preview and bake share the selected 1K/2K/4K resolution; 4K edit responsiveness must be measured.
- Shader permutations accept SM5 feature level, but only the Win64 SM6 path is targeted for validation.
- “Unlimited layers” is structurally true, but edit time still grows with layers and children.
- Coat is a documented PBR texture approximation, not true multi-BSDF optical layering.

### Safe marketing language

Use:

- “No fixed recipe layer limit.”
- “GPU-composited when your recipe changes.”
- “Bakes to standard Base Color, Normal, and packed RAM textures.”
- “Editable nondestructive recipes.”
- “Physically coherent complete-surface blending.”

Avoid:

- “Zero-cost unlimited layers.”
- “True Substrate multi-layer baking.”
- “Runtime material compositor.”
- “All-platform compatible.”
- “Production ready” before packaging and migration validation.

## Baking audit

### What is already commercially valuable

- The recipe is saved before baking.
- Outputs are regenerated at a dedicated bake resolution.
- BC, Normal, RAM, and dedicated Height assets are created or updated.
- Compression, sRGB, mip generation, and material parameters are assigned.
- The material instance is reset to the protected master.
- Missing required master parameters are reported.
- Each output asset and the recipe are saved and checked.
- Baked references remain attached to the editable recipe.

### Production gaps

- Shared 1K/2K/4K preview and bake selection builds successfully but is not visually verified.
- GPU readback is synchronous and blocks through `FlushRenderingCommands` and `ReadPixels`.
- There is no progress, cancellation, duration estimate, or queued bake workflow.
- Every baked texture is forced to `NeverStream`, which can inflate memory use at scale.
- Output is stored as BGRA8; no higher-precision option exists.
- Destination naming is implicit and not previewed to the user.
- No complete bake automation suite or packaged-content validation is confirmed.
- No user-facing output summary links directly to the created assets.

For V1, resolution choice, visible progress, output reveal, and packaged validation provide more
buyer value than adding more compositor math.

## Compatibility audit

### Preserved compatibility

- Legacy `EMaterialLabLayerType::Effect` is retained.
- Legacy Masks and Effects migrate Masks first, then Effects.
- Legacy arrays are retained for one-way load migration.
- `_RAM` and opt-in `_RAMH` remain distinct.
- Baked RAM.A stays F0 and is never reinterpreted as height.
- Bent Normal data remains serialized/importable.
- New feature defaults are neutral or zero for old recipes.
- Editor and Runtime modules are separated.

### Unproven compatibility

- Only `MatLabEditor Win64 Development` on UE 5.8 is confirmed.
- Clean global-shader compilation remains pending.
- No macOS, Linux, Vulkan, or DirectX 11 result is documented.
- No UE 5.7 or future-version support policy is documented.
- No clean-install, packaged-project, or Fab review result is documented.
- Runtime recipe editing/controller behavior is not implemented.

### Descriptor issues

`MaterialLab.uplugin` currently says “runtime controls,” but those controls are not implemented.
The descriptor also lacks customer-facing support/documentation metadata and an explicit tested
platform/version policy.

Recommended initial support claim:

> Unreal Engine 5.8, Windows editor, DirectX 12 / SM6. Baked outputs are ordinary textures and a
> material instance. Other engine versions and platforms are not supported until tested.

## Usability audit

### Good decisions

- One ordered child hierarchy matches evaluation order.
- Drag/drop is the primary reordering interaction.
- Overflow menus reduce row clutter.
- One Add Child action avoids duplicated workflows.
- Unreal's native picker provides familiar RGB, HSV, hex, accept, and cancel behavior.
- Fixed exposure and consistent lighting presets improve visual comparison.
- Advanced controls live in selection-specific inspectors.
- Status text and explicit bake/import errors already exist.

### High-impact usability gaps

- Initial local recipe undo/redo builds successfully but is not verified in the editor.
- It is intentionally local to transient `WorkingLayers`, not Unreal's global asset transaction stack.
- Undo/redo shortcuts are present; other common shortcuts remain undocumented.
- Routine operations rely on modal message dialogs.
- Bake output location is not made obvious after success.
- Preview and bake share 1K/2K/4K selection with 2K default; the workflow awaits a UE check.
- Advanced height terminology has a learning curve without embedded guidance.
- No onboarding or goal-based presets exist.
- Preview quality status text is hardcoded as “High” and “SM6,” which can misrepresent live state.

### Recommended default workflow

1. Pick a surface.
2. Click “Create Material.”
3. Drag surfaces into the stack.
4. Add a Mask or Peeling child.
5. Adjust with live preview.
6. Save the editable recipe.
7. Choose bake resolution and destination.
8. Bake and reveal the material instance.
9. Apply it to the current selection.

Every step should be discoverable without reading external documentation.

## Competition

Fab search results on 2026-08-30 show a relatively small direct-tool category:

| Product or substitute | Public signal | Competitive pressure |
|---|---|---|
| Material Layering System - HillMLS | 5.0, 2 ratings | Direct layering alternative |
| Batch Material Maker | 5.0, 3 ratings | Workflow/automation alternative |
| Magic Map Material & Maker (M4) | 4.8, 160 ratings | Strong demand signal for material tooling |
| Material Shader Assistant | No rating shown | Higher-end workflow alternative |
| Material Layout Pro | 5.0, 1 rating | Material-editor productivity alternative |
| Unreal Material Layers/Substrate | Built into Unreal | Free, powerful, technically complex substitute |
| Substance 3D Painter/Designer | Established external tools | Strong authoring depth, separate workflow/cost |
| Premade material packs | Numerous and inexpensive | Faster for fixed looks, not reusable authoring |

Fab ratings are not unit-sales figures. They indicate category activity, not exact revenue.

### Competitive advantage

Material Lab should not compete on the number of included materials. It should compete on:

- Faster iteration than editing large Unreal material graphs.
- Lower final runtime cost than retaining a live multi-layer graph.
- A consistent three-map import and output contract.
- Nondestructive recipes that can be reopened and rebaked.
- Artist-friendly masks, height blending, and edge effects inside Unreal.

### Competitive weakness

- Native Unreal systems are free.
- Substance has deeper texture authoring.
- Material packs deliver instant visual quantity.
- An editor plugin earns poor reviews quickly if undo, packaging, or version support fails.
- A complex UI without tutorials can appear less valuable than a simpler master material.

## Positioning and marketing

### Recommended one-line pitch

> Build layered PBR materials visually in Unreal, keep the recipe editable, and bake the result to
> a lightweight standard material instance.

### Recommended headline features

1. Nondestructive ordered material recipes.
2. GPU live composition inside Unreal.
3. Complete BC/Normal/RAM/F0 surface blending.
4. Reusable ordered masks and effects.
5. Height blending, contact AO, border lift, and Peeling.
6. Standard baked textures and material instances.
7. No generated or destructive user shader-graph edits.

### Media needed for a Fab listing

- A 45–75 second overview video showing the full workflow.
- A side-by-side “live layered graph cost vs baked result” explanation.
- Close-ups of height blend, border lip, contact AO, and Peeling.
- A save/reopen/reorder/rebake proof clip.
- A clear output-assets screenshot.
- A compatibility card with exact tested UE/platform/RHI versions.
- A five-minute quick-start video.
- A limitations card that states coat approximation and editor-only composition.

### Pricing recommendation

Suggested launch range: **US$29–39**.

Suggested established price after validation, tutorials, and support proof: **US$39–59**.

Rationale:

- The compositor is more valuable than a material pack or small helper.
- The current validation/support risk argues against premium pricing at launch.
- Above US$59, buyers will expect broader engine support, polished undo/batch workflows,
  substantial examples, and proven updates.

A free limited demo or launch discount can reduce trust friction, but it should not create a second
compatibility path inside the plugin.

## Sales-number planning

Exact competitor sales are private. Fab exposes a publisher's own sales reports, while public ratings
cannot be converted reliably into purchases. The following are planning scenarios, not forecasts.

At US$39 and Fab's documented 88% publisher share:

| Units | Gross revenue | Approx. publisher share |
|---:|---:|---:|
| 100 | $3,900 | $3,432 |
| 250 | $9,750 | $8,580 |
| 500 | $19,500 | $17,160 |
| 1,000 | $39,000 | $34,320 |
| 2,500 | $97,500 | $85,800 |

These figures exclude refunds, taxes, discounts, currency effects, and support costs.

Reasonable goals for a niche UE editor plugin should be milestone-based:

- **Validation goal:** 25–50 external beta users complete a bake without support.
- **Launch goal:** first 100 paid units with low refund/support rates.
- **Product-market signal:** 250–500 units plus repeat positive reviews.
- **Strong outcome:** 1,000+ units, requiring sustained tutorials, updates, and compatibility work.

Do not use a private sales estimate as a launch promise. Track conversion, refund rate, support hours,
bake completion, and review themes after release.

## Recommended product sequence

1. Finish the current Unreal verification matrix.
2. Fix only confirmed compositor or migration failures.
3. Build and verify local undo/redo and shared 1K/2K/4K preview/bake resolution.
4. Productize baking: resolution, progress, reveal output, and apply action.
5. Validate clean install, packaging, cooking, and Fab submission structure.
6. Add quick-start docs, example recipe, troubleshooting, and accurate metadata.
7. Run a small external artist beta before adding more effects.
8. Use beta behavior to decide whether V1 needs presets, batch bake, or runtime controls.

## Go/no-go criteria

### Beta-ready when

- All 14 requested verification items pass.
- Undo protects recipe edits.
- A new user can create, save, bake, and find a result in under ten minutes.
- A failed shader/import/bake step gives an actionable message.

### Paid V1-ready when

- Clean Fab install and packaged-project checks pass.
- 1K/2K/4K baking is reliable.
- Save/reopen/reorder/rebake is regression-tested.
- Documentation and support metadata are complete.
- Marketing claims match implemented and tested behavior.
- External users complete the core workflow without developer assistance.

### Not ready to claim

- Runtime material authoring or runtime compositing.
- Cross-platform support.
- Broad UE-version compatibility.
- True optical multi-BSDF coat baking.
- Production readiness before the pending GPU and packaging checks.
