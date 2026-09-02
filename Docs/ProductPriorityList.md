# Material Lab — Product Priority List

Date: 2026-08-30

This list prioritizes the compositor and material-authoring tool. Prop workflows are a later phase.

## Current clarification

### Undo

Initial local undo/redo builds successfully on UE 5.8 and awaits an editor behavior check.

The history snapshots transient `WorkingLayers`, groups rapid interactive changes, tracks saved-state
dirtiness, refreshes the preview, and exposes toolbar plus Ctrl+Z/Ctrl+Y/Ctrl+Shift+Z actions.
It is local to the Material Lab workspace rather than Unreal's global asset transaction stack.

Undo should cover:

- Add, duplicate, delete, enable, disable, and reorder layers.
- Add, remove, replace, enable, disable, and reorder Mask/Effect children.
- Surface, mask, normal-detail, and effect assignment.
- Slider, numeric, color, composition, and inspector changes.
- One slider drag should create one undo step, not one step per mouse movement.

### Bake controls

Material Lab already bakes compositor outputs after material authoring.

Current bake result:

```text
Editable recipe
    ↓ GPU composition
T_<Name>_BC
T_<Name>_N
T_<Name>_RAM
T_<Name>_H
MI_<Name>
```

Preview and bake now share one 1K/2K/4K resolution with 2K default. It builds successfully, still chooses the output folder automatically, and awaits an editor behavior check.

“Bake controls” means productizing this existing final step:

- Shared preview/bake resolution: 1K, 2K, and 4K. Implemented; verification pending.
- Output destination.
- Output naming preview.
- Confirm overwrite when outputs already exist.
- Visible progress/blocking state.
- Clear success/failure summary.
- Reveal created assets in the Content Browser.
- Apply the baked material to selected actors when desired.

It does not mean replacing the live compositor. The recipe remains editable and can be rebaked.

## Priority 0 — verify the existing compositor

Do before adding new product behavior.

1. Verify all global shaders compile for `PCD3D_SM6`.
2. Verify Material, Fill, Mask, and Peeling output visually.
3. Verify mixed Mask/Effect order before and after save/reopen.
4. Verify legacy migration: Masks first, then Effects.
5. Verify Fill picker live update, accept, and cancel.
6. Verify Peeling normals, AO, Thickness, Lift, and Detail Strength.
7. Verify Bent Normal data has no output influence.
8. Verify height Contact AO multiplies into RAM.G.
9. Verify Border Lift creates visible RNM-composited lip normals.
10. Verify Hue, Saturation, and Value for Material and Fill layers.
11. Verify preview lighting presets under fixed exposure.
12. Verify baked RAM.A remains dielectric F0.
13. Verify texture settings, persistence, and rebake overwrite.
14. Run the existing automation tests after explicit permission.

## Priority 1 — safe material editing

### 1.1 Undo and redo

Build and verify the initial local recipe history before extending it.

Acceptance criteria:

- Ctrl+Z reverses the latest Material Lab edit.
- Ctrl+Y/Ctrl+Shift+Z restores it.
- Layer and child selection remains valid after undo/redo.
- Rapid slider changes are grouped into one local history step.
- Undo refreshes the preview and dirty state.
- Saved `UMaterialLabMaterial` assets remain consistent.

### 1.2 Reset controls

Status: Individual numeric reset is implemented in source; build/editor verification pending.

Numeric scrubbers reset through MMB or Backspace while hovered. Resets create one local undo
step, skip unchanged values, and use neutral/schema defaults. Peeling uses the selected effect
asset's authored defaults.

Numeric reset coverage includes:

- Material and Fill values.
- Layer and normal-derived adjustments.
- Mask settings.
- Peeling settings.
- Height and color settings.

Section-wide reset actions remain pending. Defaults must remain neutral for compatibility.

### 1.3 Dirty-state safety

Status: Implemented in source; build/editor verification pending.

Save/Discard/Cancel protection now runs before:

- New recipe.
- Open recipe.
- Closing the Material Lab tab.
- Replacing an unsaved working recipe.

## Priority 2 — production bake workflow

### 2.1 Bake settings

Add a compact bake dialog or inspector section:

- Shared preview/bake resolution: 1024, 2048 default, 4096. Implemented; verification pending.
- Destination folder.
- Output base name.
- Existing-output behavior: update or cancel.
- Estimated output names.

Do not add automatic fallback destinations.

### 2.2 Bake operation feedback

Add:

- Disabled editing or visible busy state while baking.
- Current stage: Compose, Readback, Create Textures, Create Material, Save.
- Completion summary.
- Actionable errors with affected asset paths.

Cancellation is desirable if Unreal's supported operation boundaries make it safe. Do not leave
partial assets or claim cancellation once blocking GPU readback has started.

### 2.3 Bake result actions

Status: Implemented in source and built successfully; editor verification pending.

After success, provide:

- Reveal Outputs.
- Open Material Instance.
- Apply to Selected Actors.
- Re-bake using the same settings.

The editable recipe remains the source of truth.

### 2.4 Bake validation

Status: Validation plan includes the dedicated Height output; source builds successfully but the plan has not been run.

Verify:

- BC is sRGB with color compression.
- Normal is linear with normal compression.
- RAM is linear with masks compression.
- RAM.A contains F0.
- Height is a separate linear displacement texture.
- Mips are generated correctly.
- Output dimensions match the selected resolution.
- Existing outputs update without broken references.
- Failed saves report the exact asset.
- Baked instances work in a packaged project.

## Priority 3 — first-use usability

### 3.1 Guided empty state

Show a short workflow when no recipe is open:

1. Select a surface.
2. Create a Material Lab material.
3. Add layers, masks, and effects.
4. Save the recipe.
5. Bake the final material.

### 3.2 Compact inspector and embedded guidance

Implemented in source; build/editor verification pending:

- Smaller section titles and tighter section spacing.
- Compact custom scrub-control styling across numeric inspector fields.
- Separate lower scrolling area for the Mask Inspector.

Still add concise guidance:

Add concise tooltips for:

- Replace versus Coat.
- RAM versus RAMH.
- RAMH height, Mask Strength, Threshold, Softness, and base/blend bias.
- Contact AO and Border Normal derived from the shared height transition.
- Contact AO and Border Lift.
- Peeling Front, Width, Thickness, Lift, and Detail Strength.
- Normal Combine versus Override.

### Height and surface-data masking clarification

Implemented and built; editor/global-shader verification pending:

- Every complete-surface layer uses the current kernel regardless of legacy enum state.
- RAMH alpha supplies layer height; no-RAMH layers use scalar Layer Height.
- The ordered child mask is multiplied by Mask Strength inside the height kernel.
- Threshold, Softness, Base Height Bias, and Blend Height Bias shape one blend mask.
- Invert Base Height optionally flips only the accumulated lower height before comparison.
- That blend mask drives BC, Normal, Roughness, AO, Metallic, F0, and Height.
- Composited Height carries forward as the next layer's base height.
- Contact AO multiplies composed `RAM.G` and Border Lift adds RNM border normals.
- Final `RAM.A` remains dielectric F0; displacement Height remains separate.
- Compatibility source/reference controls remain serialized but are hidden.

Implemented and built; editor/global-shader runtime verification pending:

- Every complete-surface layer has independent BC, Roughness, AO, Metallic, F0, Normal, and Height weights.
- Each channel weight defaults to `1.0`, preserving existing recipes.
- A `0.0` channel weight preserves the accumulated channel underneath.
- Base Height at `0.0` emits neutral `0.5` when no lower layer exists.
- Height-driven Contact AO and Border Normal respect Height plus AO/Normal influence.
- Roughness/Metallic-only Fill uses zero weights for BC, AO, F0, Normal, and Height.

Implemented in source; build/editor/global-shader verification pending:

- The cavity-to-convex generated feature mask has an append-only one-minus toggle.
- Tiny inspector eyes preview curvature, height blending, Contact AO, Border Normal, and selected masks.
- Debug previews use a transient unlit material and a muted dark-red-to-cyan ramp.
- Only one eye is active at a time; clicking it again restores normal shaded preview.
- Debug state is transient, never serialized, and never passed to baking.
- The protected `M_MaterialLab_Substrate` graph remains unchanged.
- Hue Shift uses finer `0.01` scrub increments.
- Ordered masks expose Balance `0–2`, Contrast `0–10`, and signed Offset `-1–1`.
- Mask Offset defaults to `0` and is applied after contrast without changing existing recipes.
- HDRI previews add a dim `0.35` directional fill with a very soft `24°` source radius.
- HDRI fill softness is reapplied after lighting rotation; contact shadows remain disabled.
- Studio height fog uses a darker near-black blue response.

Implemented and built; editor/import verification pending:

- `_RAM` surfaces without authored height derive it from normals during import/reimport.
- Reconstruction uses editor-only FFT Poisson integration and creates importer-owned RAMH data.
- Authored `_RAMH` always wins; no runtime FFT or user-facing generation controls.
- See `NormalDerivedHeightPlan.md`.

### 3.3 Preview tools

Implemented and built; editor verification pending:

- Softer wheel zoom.
- Preview-only 20°–90° FOV slider overlaid inside the viewport.
- Preview-only Solo Layer toggle; baking still uses the complete stack.

Implemented and built; editor verification pending:

- Preview-only Before/After composition toggle.
- Before shows the base layer; After shows the complete stack.
- Baking still uses the complete stack.

Implemented and built; editor verification pending:

- Temporarily bypass the selected child in preview only.
- Preview-only Displacement toggle drives the authored `ML_UseHeight` master parameter.
- Amount appears beside it while enabled and drives `ML_HeightAmount` from 0–4.
- Recipe data, undo state, saved enabled state, and baking remain unchanged.

Still add:

- Reset camera and lighting rotation.

### 3.4 Shortcuts

Support and document:

- Save.
- Save As.
- Undo/redo.
- Duplicate layer.
- Delete selected layer/child.
- Frame/reset preview.
- Bake.

## Priority 4 — release compatibility and trust

1. Validate a clean plugin install.
2. Validate Fab package structure.
3. Validate cooking and packaged use of baked materials.
4. Document exact UE, platform, RHI, and shader-model support.
5. Correct the plugin description until runtime controls exist.
6. Add quick-start, troubleshooting, changelog, and support links.
7. Document limitations such as approximate Coat behavior.
8. Confirm all bundled third-party assets have distributable licenses.
9. Test migration from every shipped recipe schema.
10. Run an external artist beta before calling the plugin production ready.

## Priority 5 — workflow acceleration

Do after the core workflow is safe and validated.

- Save and reuse layer presets.
- Save Peeling and height-setting presets.
- Batch bake recipes.
- Duplicate material variants.
- Intermediate-layer compositor caching.
- User-selectable interactive preview resolution.
- More effects driven by confirmed user demand.

## Priority 6 — props workflow

Props are a separate later product phase built on top of reliable baked materials.

Suggested order:

### 6.1 Apply materials to props

- Apply baked material to selected actors or mesh slots.
- Choose the target material slot explicitly.
- Preserve existing assignments unless the user confirms replacement.
- Support undo through Unreal transactions.

### 6.2 Per-prop material instances

- Create child instances for prop-specific scalar/vector overrides.
- Keep baked shared textures reusable.
- Avoid duplicating textures for simple prop variation.

### 6.3 Prop masks and local variation

Later options may include:

- Vertex color masks.
- World-space or object-space masks.
- Curvature/AO baked from a prop.
- Position, orientation, and bounding-box masks.
- Decal or damage masks.

These should not be added to the current material recipe contract prematurely.

### 6.4 Batch prop workflow

After single-prop behavior is proven:

- Process multiple selected props.
- Assign by material slot.
- Create variants with predictable naming.
- Report every changed actor and asset.
- Provide undo and a dry-run summary.

## Recommended implementation order

```text
Existing compositor verification
    ↓
Undo / redo and reset safety
    ↓
Bake resolution, destination, progress, and output actions
    ↓
Onboarding, tooltips, preview comparison, and shortcuts
    ↓
Clean-install, package, cook, and Fab validation
    ↓
External artist beta
    ↓
Workflow acceleration
    ↓
Prop application and prop-specific variation
```

## Immediate next implementation task

Build and verify numeric reset interactions and Save/Discard/Cancel dirty-state safety.

Then implement the production bake settings and output workflow.

Reason:

- MMB and hovered Backspace reset must remain conflict-free with typing and scrubbing.
- New, Open, and tab close must never lose unsaved recipes silently.
- These safety controls should be proven before expanding the bake workflow.
