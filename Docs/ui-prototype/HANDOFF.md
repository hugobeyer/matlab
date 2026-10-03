# Mixtormat UI styling — continuation handoff

## Purpose

Build a readable, modular HTML/CSS visual-contract prototype of Mixtormat that can later be translated into the existing Unreal Slate UI. It is not a replacement material editor.

Preserve Unreal behavior during eventual migration. Match its compact graphite styling rather than making contrast arbitrarily stronger. Use authored tokens, additive/multiply blending, gradients, and complete interaction states.

**Current scope: prototype styling only. Do not migrate the pending changes to Unreal yet.**

**Overlays are explicitly off-limits.** The user objected to earlier deviations from Unreal. Do not change their current layout, styling, or behavior in the next implementation.

## Provenance and validation

This handoff records the supplied conversation summary. Its implementation findings have not been independently re-audited while writing this document. The prototype directory and listed files exist.

Prior reported validation:
- JavaScript diagnostics reported no errors.
- No browser automation, commands, builds, or git ran.
- Browser rendering and interaction correctness remain unverified.
- Viewport imagery, thumbnails, scalar curve, and material processing are simulations.

Do not claim pixel matching or in-engine validation without an actual comparison.

## Files and responsibilities

Location: `Docs/ui-prototype/` within the Mixtormat project.

| File | Responsibility |
|---|---|
| `index.html` | Workspace structure and local references |
| `tokens.css` | Authored palette, spacing, typography, opacity, blend tokens |
| `components.css` | Workspace, layers, foldouts, cards, shared controls |
| `inspector-data.js` | Representative inspector schemas and demo values |
| `app.js` | Component factories, parameter state/history, token editor |
| `workspace-controls.js` | Splitters, movable windows, overlays, PNG integration |
| `popovers.css` | Shared context-menu and hover-help styling |
| `popovers.js` | Context actions and delayed hover/focus help |
| `README.md` | Usage, scope, Slate translation map |
| `icons/` | Copied plugin icon resources |
| `Icon128.png` | Copied plugin icon |

## Previously implemented, as reported

- Workspace toolbar, layers/library, simulated viewport, galleries, inspector.
- Representative Surface, Mask, Effect, Generator, ID, Output, States inspectors.
- Numeric draggers, dropdowns, toggles, segments, nested cards, driver popover.
- Editable UI Style popup, token reset, JSON export.
- Style popup moves by its title and resizes using a bottom-right grip.
- Arrow-key support; double-click title restores size and position.
- Draggable column dividers and viewport/gallery divider.
- Movable/resizable prototype workspace frame.
- Styled RMB menus, including token actions in UI Style.
- Styled delayed hover/focus help.
- Text selection disabled except in editable fields.
- All 57 PNG icons copied from `Resources/Icons`; matching controls use them.
- Layer gradients and foldout hairlines referenced from Unreal source.

Preserve these features while implementing the pending styling.

## Latest requirements — not yet implemented

1. Expose group-header height and font as editable tokens.
2. Slider fill must have two gradients, not one.
3. Fill color must use the accent color as an additive source.
4. Add missing fill hover and actual dragging states.
5. Add editable power or bias controls for gradient falloff; avoid bulging fades.
6. Foldout header must fade to its actual body color using additive percentage.
7. Additive contribution must reach zero at the foldout/body seam.
8. Do not touch overlays.

### Header roles must remain independent

Distinguish:
- Outer inspector foldout header.
- Non-collapsible Group Card header.
- Left-side layer-group row.

Do not merge these merely because some values happen to match.

Reported current tokens in `tokens.css`:
- `--foldout-height: 25px`
- `--foldout-title-size: 7px`
- `--foldout-title-tracking: 1.4px`
- Group Cards already have independent header height, padding, title size,
  weight, tracking, and opacity.

Reported remaining literal in `components.css`:

```css
.layer-row.group {
  font-size: 10px;
  font-weight: 400;
}
```

Reported live registry in `app.js`:
- Exposes foldout height, but not foldout title size/tracking.
- Exposes Group Card title controls.
- Lacks independent layer-group height/font controls.

Read the actual consumers before deciding which new tokens to add. Expose authored values only, with meaningful ranges and consistent reset/export/help support.

### Slider: two-gradient structure

Reported current prototype fill:

```css
.drag-fill {
  background: linear-gradient(
    rgb(var(--ground-rgb) / var(--fill-lift-opacity)),
    rgb(var(--ground-rgb) / var(--fill-shade-bottom))
  );
  mix-blend-mode: plus-lighter;
}
```

This is a single vertical additive gradient using the ground color. It does not satisfy the requested accent-based, two-gradient treatment.

`app.js` reportedly creates one `.drag-fill` span and updates its width/left position. Its pointer handlers update values/history but do not establish a dragging visual state.

Unreal `SMixtormatSlider.cpp` is the structural reference:
1. Vertical fill-body gradient.
2. Horizontal multiply/shade gradient with a midpoint.
3. Normal, hover, scrubbing, and disabled fill states.

Prototype target:
- Accent-based additive vertical body gradient.
- Separate horizontal multiply/shade gradient.
- Explicit normal, hover, dragging, disabled behavior.
- Editable falloff controls where appropriate.
- Preserve label/value placement, fill extent, paired rows, and value semantics.

Use separate compositing layers when needed to express distinct blend modes. Keep the implementation readable rather than embedding large generated styles inline. Inspect existing code before choosing the smallest implementation.

Dragging state must clear on pointer up, pointer cancellation, and lost capture. Do not regress parameter history or leave a stuck active fill.

### Foldout: additive lift ending at the body

Reported current header CSS:

```css
background: linear-gradient(
  rgb(var(--header-tint-rgb) / var(--header-tint-opacity)),
  rgb(var(--group-ground-rgb))
);
```

The surrounding `.foldout` reportedly uses `--ground-rgb`, while the header bottom uses `--group-ground-rgb`.

Verify the actual body background first. The new header must finish at that exact body color, not a similar ground shade.

Model:

```text
header(t) = bodyColor + additiveSourceColor × contribution(t)
contribution(1) = 0
```

Keep the base body color and additive contribution distinct. The user wants a controllable additive percentage, not opaque interpolation between two unrelated colors.

### Falloff semantics

No shared editable power/bias mechanism was reported as implemented.

A minimal option is a shared sampled power curve, keeping authored controls separate from derived stops/gradient strings:

```text
opacity(t) = top + (bottom - top) × pow(t, exponent)
t ∈ [0, 1]
exponent > 0
```

For a decreasing fade:
- Exponent below 1 fades earlier.
- Exponent above 1 holds the top longer.
- Exponent 1 is linear.

Document the direction in token help. Do not reverse these semantics accidentally. Power is a suggested approach, not permission to introduce an unnecessary framework or duplicate fallback path.

## Visual and structural invariants

### Foldouts and cards

- Only outer foldouts collapse.
- Group Cards explicitly own their rows.
- Cards may nest through their content slot.
- Do not add a separate Subcard widget.
- Do not restore stepped/dented card tops.
- No separate colored title strip.
- No hairlines on inspector Group Cards or subgroup captions.
- Foldouts retain their hairlines.
- Layer-row lips are distinct from inspector subgroup styling.
- Preserve rounded corners.
- Titles remain single-line and clipped/ellipsized.

### Additive and multiply semantics

The user rejected substituting white/gray for the requested additive source.

```text
result = background + sourceColor × opacity
```

For “background itself at 5%”:

```text
result = background + background × 0.05
```

Not:

```text
result = background + white × 0.05
```

Slider fill specifically uses the accent color as its additive source.

Browser `plus-lighter` and multiply compositing provide a visual reference. Unreal may require explicit color math. Do not assume identical browser/Slate color-space behavior or clipping.

### Shared controls

- Numeric label and value stay inside the dragger.
- Fill remains behind both.
- Default stacked numeric gap is approximately 3px.
- Preserve paired rows.
- Dropdown labels align left using the dedicated dropdown-row builder.
- Do not globally alter `MixtormatRow::Make`.
- Eyes/debug controls may be leading or trailing.
- Do not move unrelated enable/reset/action controls into the leading slot.
- Preserve dropdown/toggle borders, dragger border roles, and existing fade treatment.
- Preserve the center divider's low opacity and shared well-border color role.

## Unreal reference locations

Project root:

`C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat`

Relevant source is under `Source/MixtormatEditor/Private/`:

- `Style/MixtormatDesignTokens.h`
- `Style/MixtormatPalette.h`
- `Style/MixtormatLiveTheme.cpp`
- `Style/MixtormatStyle.cpp`
- `UI/Controls/SMixtormatSlider.cpp`
- `UI/Primitives/MixtormatGradientPainter.h`
- `UI/Primitives/MixtormatGradientPainter.cpp`
- `UI/Containers/SMixtormatInspectorGroup.cpp`
- `UI/Containers/SMixtormatInspectorCard.h`
- `UI/Containers/SMixtormatInspectorCard.cpp`
- `UI/Layers/SMixtormatLayerRow.cpp`
- `UI/Layers/SMixtormatLayerGroupRow.cpp`
- `UI/Layers/SMixtormatLayerChildRow.cpp`
- `Widgets/SMixtormat_Preview.cpp`

Locate/read actual files before relying on APIs or implementing styling. These references are not a request to modify Unreal now.

### Prior Unreal structural work, as reported

- Approximately 80 caption sections migrated into cards.
- Contact Borders, Generator Composition, Legacy Treatment, ID Outputs converted.
- Composition split into Height Blend, Blending/Opacity, and Color.
- Shared dropdown builder introduced.
- Compact driver-card context preserved.
- Height Blend source-summary label removed.
- Missing first-row Height Blend gap fixed.
- Subgroup hairline path and live control removed.

Do not undo this work or reintroduce nested foldouts.

## Earlier audit context

A prior agent supplied a broad static audit of live styling. Treat it as leads to verify against the current source, not current proven defects or authorization for deletion.

Reported categories:
- Registered controls without consumers: `RowTextInset`, `DragRangeDistance`,
  `GroupCardTitleDropDepth`.
- Frozen `GroupCardTitleWidthRatio` registry range (minimum equals maximum).
- Gallery tile range/category mismatch around `MaskBarTileSize`.
- Used but unregistered palette state colors: slider active/disabled, badge hover,
  menu top, segment states, multiply-gradient colors, header hover accent.
- Host foreground overrides bypassing Mixtormat palette roles.
- Duplicate local well styling in Toggle/Chip and layer/group paint cascades.
- Unused badge style registration and duplicated hairline implementations.
- Literal dimensions/opacities with possible existing token equivalents.
- Style panel layout/style values not themselves tokenized.

Important cautions:
- Zero references must be confirmed before removing a token or role.
- Compatibility comments do not independently authorize removal.
- Equal literals do not prove equivalent semantic roles.
- Widening a frozen range changes layout behavior and requires intent checking.
- Registering colors can alter rendering for saved overrides.
- Keep this broader audit separate from the pending prototype task.

## Known pitfalls

### Markup corruption

Earlier saved HTML reportedly contained extra/malformed closing tags. This broke a stylesheet URL and several DOM IDs, including select/input structure. Targeted repairs were made.

Prefer localized edits. Inspect relevant saved markup; HTML diagnostics alone did not reliably catch these defects.

### Local SVG references

Local SVG `<use href="#…">` references reportedly triggered `file://` security errors. They were replaced with inline paths, then plugin PNG icons. Do not reintroduce local fragment-reference loading.

### Invisible hairline

A foldout hairline previously used background RGB and was effectively invisible. It now uses Unreal's actual hairline role. Preserve that distinction.

### Token semantics

- Do not expose derived gradient values as live knobs.
- Do not add dead controls or duplicate compatibility layers.
- Audit palette accessor transforms before changing picker/reset/export semantics.
- Prototype schemas are representative, not exhaustive engine parameter/default data.
- Avoid whole-file rewrites for local changes.

## Recommended execution checklist

1. Read this file, README, prototype tokens/styles, and relevant JS consumers.
2. Read Unreal slider/header source for structural reference.
3. Map foldout, Group Card, and layer-group header token consumers.
4. Expose missing independent height/font controls.
5. Add a small shared falloff mechanism with documented semantics.
6. Implement accent-additive body plus horizontal multiply/shade fill gradients.
7. Add actual hover/dragging states with pointer cleanup.
8. Make foldout additive lift reach zero over its actual body color.
9. Add meaningful registry ranges, reset, export, and hover-help coverage.
10. Keep overlays untouched.
11. Run static diagnostics and inspect edited markup.
12. Request screenshot/reload feedback for visual comparison if browser checks are unavailable.

## Working constraints

- No git commands without explicit consent.
- No shell, builds, installs, package/process/migration commands without consent.
- “Check” or “verify” does not authorize commands.
- No new dependencies or lockfile edits without consent.
- Ask before destructive changes, removals, or broad behavior changes.
- Use file reads/search/diagnostics for static validation.
- Preserve architecture, APIs, names, folders, and compatibility paths.
- Keep tokens, CSS, schema data, and JS separate and readable.
- No large inline HTML/CSS/JS blocks.
- No unrelated refactors.
- Keep user-facing updates short and scannable.

## Copy-ready continuation prompt

```text
Continue the Mixtormat HTML/CSS visual-contract prototype in
Docs/ui-prototype/. Read HANDOFF.md and the current files first.

Implement only these pending styling requirements:
1. Expose independent group-header height/font controls, distinguishing
   foldouts, Group Cards, and left-side layer-group rows.
2. Restore the slider's two-gradient treatment: accent-color additive body
   gradient plus a separate horizontal multiply/shade gradient.
3. Add fill hover and actual dragging states, including pointer cancellation
   and lost-capture cleanup.
4. Add shared editable power or bias falloff controls with documented semantics.
5. Make foldout header lift an additive-color percentage over its actual body
   color, reaching zero contribution at the body seam.

DO NOT TOUCH THE OVERLAYS.

Preserve cards without notches/hairlines, rounded corners, paired rows, internal
numeric labels/values, context menus, hover help, draggable/resizable Style popup,
splitters, PNG icons, parameter history, reset, and token export.

Keep authored tokens, component CSS, schema data, and JavaScript separate and
readable. Do not expose derived values as controls. Match Unreal rather than
arbitrarily strengthening contrast. Additive means sourceColor times opacity
added to the underlying body; do not substitute white/gray for the source.

Read Unreal source as reference, especially SMixtormatSlider.cpp and the
inspector group/card painters. Do not change Unreal implementation yet.

No commands, git, builds, dependencies, removals, or destructive changes without
consent. Use localized edits and static diagnostics. Inspect edited HTML for
malformed closing tags. Treat earlier audit claims as leads to verify, not facts
or authorization to delete compatibility tokens.

Report implemented changes briefly. Distinguish static checks from browser or
in-engine validation; do not claim pixel matching without a visual comparison.
```
