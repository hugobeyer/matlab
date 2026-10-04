# Mixtormat UI visual contract

Open `index.html` directly in a modern desktop browser. No server, installation,
external font, network asset, or dependency is needed.

## Files

- `tokens.css`: readable authored palette, geometry, typography, opacity, and blend operations.
- `components.css`: shared component rules, state styling, and responsive shell.
- `inspector-data.js`: representative inspector hierarchy and demo values.
- `app.js`: component construction, parameter history, action routing, token editor.
- `falloff.js`: samples the authored power curve into derived gradient stops.
- `index.html`: workspace structure; no inline styles or scripts.
- `workspace-controls.js`: splitters, floating-window reference, viewport overlays, PNG icons.
- `popovers.css` / `popovers.js`: shared right-click menu and hover/focus-help treatment.
- `icons/`: all 57 PNG icons copied from the plugin; `Icon128.png` is the plugin icon.
- `token-autosave.js`: optional Connect-autosave file bridge for token overrides.
- `mixtormat-prototype-tokens*.json`: exported token snapshots. `tokens.css` holds the
  authored defaults and is the reset source.

## Token autosave

In UI Style, click **Connect autosave** and choose an existing token export or a new
JSON file. Existing overrides load on connection; edits and resets save after a
500 ms pause. Normal exports remain available. Disconnect flushes pending edits.

This requires a browser/context supporting `showSaveFilePicker` (typically desktop
Chrome/Edge in a secure context). Unsupported browsers show a disabled action.
File permission is granted through the browser picker. Reconnect after each reload;
no handle or token data is kept in browser storage. `tokens.css` stays unchanged and
remains the source of reset defaults. Invalid known values reject the entire import;
unknown token keys are preserved in the selected JSON. Wait for **Saved** before closing.

## Fonts

UI Style → Typography provides the default font, Roboto, and Inter. Reset and JSON
export include `--font-family`. Font selection applies to the entire prototype.

`fonts.css` loads bundled upright variable TTFs from `fonts/`; no CDN is used.
Sources: Google Fonts repository, `ofl/inter/Inter[opsz,wght].ttf` and
`ofl/roboto/Roboto[wdth,wght].ttf`. Each family's OFL license is included alongside it.
For Unreal migration, use static weight instances for predictable Slate results;
these variable files have not been validated in Unreal. No Unreal files were changed.

## Coverage

The workspace includes the document bar, layer tree/library, simulated material
viewport and channels, searchable galleries, selected-item header, inspector,
style drawer, bake dialog, compact driver popover, and component-state bench.

Select layer children to inspect Surface, Mask, Effect, Generator, Pattern ID,
Output, or States examples. Inspector schemas are representative; this is **not**
a complete mirror of every engine parameter or visibility branch.

- Drag numeric rows; Shift reduces sensitivity; arrows adjust; Enter/double-click edits.
- Backspace resets a numeric row. Right-click a Group Card or foldout to reset its controls.
- Undo/Redo tracks demo numeric, dropdown, toggle, and segmented-control changes.
- Eyes and debug buttons demonstrate state; they do not evaluate a material.
  Layer and card visibility use small squircles rather than eye glyphs.
- Inspector foldouts collapse. Layer groups and layers collapse too: groups on a
  single click of the header or title, layers on double-click or the chevron.
  Cards and nested cards do not collapse.
- Dropdowns are styled buttons backed by the native `select`; the popup reuses the
  right-click menu surface with checkmarks, type-to-find, and arrow navigation.
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
  Column resizing does not change viewport overlay sizing.
- Layer tree: **Layer**, **Group**, and **Fill Layer** buttons sit flush at the
  bottom of the layer column. Rows align on shared eye, icon, label, source,
  badge, and chevron columns with drawn hierarchy connectors.
- Drag the title to move the prototype frame; resize with its bottom-right grip.
- Double-click the title to restore the full-window layout.
- Viewport overlays are fixed edge clusters mirroring `BuildPreviewPanel`: render
  (top-left), comparison (top-center), scene (bottom-left), camera (bottom-center),
  output resolution (bottom-right), plus light and mesh icon rails on the sides.
  They have no title, no drag, and no background plate.
- UI STYLE → Plugin PNG icon sheet displays the complete copied icon set.

## Compositing contract

Palette roles use RGB channel data. Source-matched layer-state and foldout-gradient
endpoints come from `MixtormatPalette.h`; additive/multiply rules remain shared.

| Layer | Source | Operation |
| --- | --- | --- |
| Raised panel | Same base RGB at the authored lift opacity | Additive `plus-lighter` |
| Group header | Same base RGB, top opacity to body opacity | Additive vertical gradient |
| Group body | Same RGB, holds the body opacity | Additive |
| Foldout header | Unreal HeaderTint → GroupSurround | Source alpha gradient |
| Layer rows | Unreal normal/hover/selected palette | Vertical gradients |
| Layer children | Unreal child tint endpoints | Horizontal alpha gradients |
| Control well | Black shade, independent top/bottom alpha | Multiply |
| Dragger fill | Base RGB, vertical alpha gradient | Additive |
| Well outline | Same RGB, subdued alpha | Top-to-bottom masked fade |

An additive layer means adding **the source color at its authored alpha**, not adding
that amount of white to every RGB channel. `--card-gradient-reach` extends the card
fade past the header seam and mirrors its tail upward from the bottom without
changing layout. No Group Card hairline, separate title strip, or stepped
silhouette is present.
Columns, Group Cards, and foldout bodies carry a radial vignette; it darkens by
shade color only and uses no additive pass.
Foldouts retain their top lip/hairline; layer rows retain the existing normal/selected lip.
No inspector Group Card draws a hairline.

Browser blending operates in the browser's compositing color space. Slate may
need explicit per-channel math in its paint path. Match screenshots in-engine;
do not assume CSS gamma, font metrics, focus handling, or native select rendering
will be identical. The hidden `select` is a semantic value holder only; its popup
is the shared menu surface, so the chip's visuals are prototype-authored.

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
