# Mixtormat UI visual contract

Open `index.html` directly in a modern desktop browser. No server, installation,
external font, network asset, or dependency is needed.

## Files

- `tokens.css`: readable authored palette, geometry, typography, opacity, and blend operations.
- `components.css`: shared component rules, state styling, and responsive shell.
- `inspector-data.js`: representative inspector hierarchy and demo values.
- `app.js`: component construction, parameter history, action routing, token editor.
- `index.html`: workspace structure; no inline styles or scripts.
- `workspace-controls.js`: splitters, floating-window reference, viewport overlays, PNG icons.
- `popovers.css` / `popovers.js`: shared right-click menu and hover/focus-help treatment.
- `icons/`: all 57 PNG icons copied from the plugin; `Icon128.png` is the plugin icon.

## Coverage

The workspace includes the document bar, layer tree/library, simulated material
viewport and channels, searchable galleries, selected-item header, inspector,
style drawer, bake dialog, compact driver popover, and component-state bench.

Select layer children to inspect Surface, Mask, Effect, Generator, Pattern ID,
Output, or States examples. Inspector schemas are representative; this is **not**
a complete mirror of every engine parameter or visibility branch.

- Drag numeric rows; Shift reduces sensitivity; arrows adjust; Enter/double-click edits.
- Backspace resets a numeric row. Card reset restores its numeric controls.
- Undo/Redo tracks demo numeric, dropdown, toggle, and segmented-control changes.
- Eyes and debug buttons demonstrate state; they do not evaluate a material.
- Only outer foldouts collapse. Cards and nested cards do not collapse.
- UI STYLE edits tokens; Export tokens writes a local JSON download.
- Drag the UI Style title to move the popup; drag its bottom-right grip to resize it.
- Its title/grip support arrow keys; double-click its title restores size and position.
- Right-click parameters, cards, layer rows, or UI Style tokens for styled context actions.
- Hover or keyboard-focus controls for styled help, including token ranges and defaults.
- Text selection is disabled outside editable fields. Context menus support arrows and Escape.
- SAVE is in-memory only; SAVE AS exports demo state. No automatic local storage.
- The scalar curve, viewport, and gallery thumbnails are labeled visual illustrations.
- NEW/LOAD explain the real integration boundary. Bake execution is disabled.
- Drag the thin column rails or the viewport/gallery rail to resize panels; arrows work too.
- Drag the title to move the prototype frame; resize with its bottom-right grip.
- Double-click the title to restore the full-window layout.
- OUTPUT / SCENE / CAMERA overlay tabs open source-aligned representative controls.
- Drag the preview-control title to move its overlay inside the viewport.
- UI STYLE → Plugin PNG icon sheet displays the complete copied icon set.

## Compositing contract

Palette roles use RGB channel data. Source-matched layer-state and foldout-gradient
endpoints come from `MixtormatPalette.h`; additive/multiply rules remain shared.

| Layer | Source | Operation |
| --- | --- | --- |
| Raised panel | Same base RGB at 5% opacity | Additive `plus-lighter` |
| Group header | Same base RGB, top opacity to body opacity | Additive vertical gradient |
| Group body | Same RGB, constant end opacity | Additive |
| Foldout header | Unreal HeaderTint → GroupSurround | Source alpha gradient |
| Layer rows | Unreal normal/hover/selected palette | Vertical gradients |
| Layer children | Unreal child tint endpoints | Horizontal alpha gradients |
| Control well | Black shade, independent top/bottom alpha | Multiply |
| Dragger fill | Base RGB, vertical alpha gradient | Additive |
| Well outline | Same RGB, subdued alpha | Top-to-bottom masked fade |

A 5% additive layer means adding **the source color at 5% alpha**, not adding
0.05 white to every RGB channel. Group Card header defaults to opacity 1;
the fade reaches .01 at the actual header/body seam, and the body holds .01.
No Group Card hairline, separate title strip, or stepped silhouette is present.
Foldouts retain their top lip/hairline; layer rows retain the existing normal/selected lip.
No inspector Group Card draws a hairline.

Browser blending operates in the browser's compositing color space. Slate may
need explicit per-channel math in its paint path. Match screenshots in-engine;
do not assume CSS gamma, font metrics, focus handling, or native select rendering
will be identical. The native select control is a semantic placeholder for a chip.

## Slate translation map

| Prototype role | Existing implementation target |
| --- | --- |
| Workspace | `SMixtormat` shell builders |
| Outer foldout | `SMixtormatInspectorGroup` |
| Non-collapsible Group Card | `SMixtormatInspectorCard` |
| Numeric row | `SMixtormatSlider` |
| Dropdown row / chip | `MixtormatRow::MakeDropdown` / `SMixtormatChip` |
| Boolean well | `SMixtormatToggle` |
| Segmented switch | `SMixtormatSegmentedControl` |
| Scalar curve | `SMixtormatScalarRamp` |
| Compact driver panel | `SMixtormatDriverPopover` + compact card layout |
| Gradient / blend layers | Shared `MixtormatGradient` painter and palette math |
| Authored values | `MixtormatDesignTokens`, `MixtormatPalette`, live theme registry |

Translate the visual layer, not the browser architecture. Preserve Unreal
callbacks, parameter wrappers, linked writes, live bindings, transactions/history,
preview actions, reset behavior, enable states, visibility conditions, legacy
paths, and expansion restoration. No plugin source was changed by this prototype.

## Validation

Prepared using file reads and static review only. Browser appearance and JavaScript
execution have not been verified. No shell, build, git, or package command was run.
